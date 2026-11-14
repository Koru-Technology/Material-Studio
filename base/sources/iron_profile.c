// Sampling profiler for Apple platforms, off unless the env IRON_PROFILE is set (to a file path, or to 1 for
// <HOME>/Documents/profile.txt). A thread samples the main thread's call stack every millisecond (frame-pointer walk while
// it is suspended) for IRON_PROFILE_SECONDS (default 120) or until exit, then writes the samples as text:
//   image <load address hex> <path>          the main executable, for offline symbolication with atos and the dSYM
//   sym <pc hex> <library> <symbol>         names of addresses outside the main executable, from dladdr
//   mark <time us> <name>                   iron_profile_mark() calls, to cut the samples into phases
//   S <time us> <pc hex> <pc hex> ...       one sample, innermost frame first
// paint/tools/profile_report.py turns the file into a report.

#include "iron_profile.h"

#ifdef __APPLE__

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PROFILE_DEPTH 96
#define PROFILE_WORDS (48 * 1024 * 1024 / 8) // 48 MB of samples, about 4 minutes of deep stacks
#define PROFILE_MARKS 4096

#if defined(IRON_IOS) || TARGET_OS_IPHONE
#define PROFILE_PC_MASK 0x0000007FFFFFFFFFULL // Strips pointer authentication bits of return addresses
#else
#define PROFILE_PC_MASK 0x00007FFFFFFFFFFFULL
#endif

static bool             profile_on = false;
static mach_port_t      profile_main_thread;
static uintptr_t        profile_stack_lo;
static uintptr_t        profile_stack_hi;
static uint64_t        *profile_words;
static size_t           profile_nwords;
static atomic_bool      profile_stop_flag;
static atomic_bool      profile_stopped;
static pthread_t        profile_thread;
static double           profile_seconds = 120;
static uint64_t         profile_t0;
static mach_timebase_info_data_t profile_timebase;
static char             profile_path[1024];

static uint64_t profile_marks_t[PROFILE_MARKS];
static char     profile_marks_name[PROFILE_MARKS][96];
static atomic_int profile_nmarks;

static uint64_t profile_now_us(void) {
	uint64_t t = mach_absolute_time() - profile_t0;
	return t * profile_timebase.numer / profile_timebase.denom / 1000;
}

static bool profile_stack_ok(uintptr_t fp) {
	return fp >= profile_stack_lo && fp + 16 <= profile_stack_hi && (fp & 7) == 0;
}

// Runs while the main thread is suspended: no allocation, no locks
static void profile_sample(void) {
	if (profile_nwords + 2 + PROFILE_DEPTH > PROFILE_WORDS) {
		return;
	}
#if defined(__arm64__) || defined(__aarch64__)
	arm_thread_state64_t   state;
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
	if (thread_get_state(profile_main_thread, ARM_THREAD_STATE64, (thread_state_t)&state, &count) != KERN_SUCCESS) {
		return;
	}
	uintptr_t pc = (uintptr_t)arm_thread_state64_get_pc(state);
	uintptr_t lr = (uintptr_t)arm_thread_state64_get_lr(state);
	uintptr_t fp = (uintptr_t)arm_thread_state64_get_fp(state);
#else
	x86_thread_state64_t   state;
	mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
	if (thread_get_state(profile_main_thread, x86_THREAD_STATE64, (thread_state_t)&state, &count) != KERN_SUCCESS) {
		return;
	}
	uintptr_t pc = state.__rip;
	uintptr_t lr = 0;
	uintptr_t fp = state.__rbp;
#endif
	size_t    head  = profile_nwords;
	uint64_t *out   = profile_words + head + 1;
	int       depth = 0;
	out[depth++]    = pc & PROFILE_PC_MASK;
	uintptr_t first = profile_stack_ok(fp) ? (((uintptr_t *)fp)[1] & PROFILE_PC_MASK) : 0;
	if (lr != 0 && (lr & PROFILE_PC_MASK) != first) { // A leaf without a frame of its own: its caller is only in lr
		out[depth++] = lr & PROFILE_PC_MASK;
	}
	while (depth < PROFILE_DEPTH && profile_stack_ok(fp)) {
		uintptr_t *frame = (uintptr_t *)fp;
		uintptr_t  ret   = frame[1] & PROFILE_PC_MASK;
		uintptr_t  next  = frame[0];
		if (ret == 0) {
			break;
		}
		out[depth++] = ret;
		if (next <= fp) {
			break;
		}
		fp = next;
	}
	profile_words[head] = (profile_now_us() << 8) | (uint64_t)depth;
	profile_nwords      = head + 1 + depth;
}

static void profile_write(void);

static void *profile_run(void *_) {
	uint64_t end = (uint64_t)(profile_seconds * 1e6);
	while (!atomic_load(&profile_stop_flag) && profile_now_us() < end) {
		if (thread_suspend(profile_main_thread) == KERN_SUCCESS) {
			profile_sample();
			thread_resume(profile_main_thread);
		}
		usleep(1000);
	}
	atomic_store(&profile_stopped, true);
	profile_write(); // Here, not at exit: an app killed from outside never reaches exit
	return NULL;
}

// Unique non-main pcs get a dladdr name
typedef struct {
	uint64_t pc;
	bool     used;
} profile_slot_t;

static void profile_write(void) {
	FILE *f = fopen(profile_path, "wb");
	if (f == NULL) {
		fprintf(stderr, "[profile] could not write %s\n", profile_path);
		return;
	}
	const struct mach_header *mh = _dyld_get_image_header(0);
	fprintf(f, "# iron_profile 1\n");
	fprintf(f, "image %llx %s\n", (unsigned long long)(uintptr_t)mh, _dyld_get_image_name(0));

	Dl_info main_info;
	dladdr(mh, &main_info);
	size_t          cap   = 1 << 18;
	profile_slot_t *slots = calloc(cap, sizeof(profile_slot_t));
	for (size_t i = 0; i < profile_nwords;) {
		int depth = (int)(profile_words[i] & 0xff);
		for (int d = 0; d < depth; ++d) {
			uint64_t pc = profile_words[i + 1 + d];
			Dl_info  info;
			if (dladdr((void *)(uintptr_t)pc, &info) == 0 || info.dli_fbase == main_info.dli_fbase) {
				continue;
			}
			size_t h = (size_t)((pc * 0x9E3779B97F4A7C15ULL) >> 46) & (cap - 1);
			while (slots[h].used && slots[h].pc != pc) {
				h = (h + 1) & (cap - 1);
			}
			if (!slots[h].used) {
				slots[h].used   = true;
				slots[h].pc     = pc;
				const char *lib = info.dli_fname != NULL ? strrchr(info.dli_fname, '/') : NULL;
				fprintf(f, "sym %llx %s %s\n", (unsigned long long)pc, lib != NULL ? lib + 1 : "?", info.dli_sname != NULL ? info.dli_sname : "?");
			}
		}
		i += 1 + depth;
	}
	free(slots);

	int nmarks = atomic_load(&profile_nmarks);
	for (int i = 0; i < nmarks && i < PROFILE_MARKS; ++i) {
		fprintf(f, "mark %llu %s\n", (unsigned long long)profile_marks_t[i], profile_marks_name[i]);
	}
	size_t samples = 0;
	for (size_t i = 0; i < profile_nwords;) {
		int depth = (int)(profile_words[i] & 0xff);
		fprintf(f, "S %llu", (unsigned long long)(profile_words[i] >> 8));
		for (int d = 0; d < depth; ++d) {
			fprintf(f, " %llx", (unsigned long long)profile_words[i + 1 + d]);
		}
		fputc('\n', f);
		i += 1 + depth;
		samples++;
	}
	fclose(f);
	fprintf(stderr, "[profile] %zu samples written to %s\n", samples, profile_path);
}

void iron_profile_stop(void) {
	if (!profile_on) {
		return;
	}
	profile_on = false;
	atomic_store(&profile_stop_flag, true);
	pthread_join(profile_thread, NULL); // The thread writes the file as it ends
}

void iron_profile_mark(const char *name) {
	if (!profile_on) {
		return;
	}
	int i = atomic_fetch_add(&profile_nmarks, 1);
	if (i >= PROFILE_MARKS) {
		return;
	}
	profile_marks_t[i] = profile_now_us();
	snprintf(profile_marks_name[i], sizeof(profile_marks_name[i]), "%s", name);
}

bool iron_profile_enabled(void) {
	return profile_on;
}

static void profile_at_exit(void) {
	iron_profile_stop();
}

// Before main, on the main thread
__attribute__((constructor)) static void profile_start(void) {
	const char *env = getenv("IRON_PROFILE");
	if (env == NULL || env[0] == 0) {
		return;
	}
	if (strchr(env, '/') != NULL) {
		snprintf(profile_path, sizeof(profile_path), "%s", env);
	}
	else {
		const char *home = getenv("HOME");
		snprintf(profile_path, sizeof(profile_path), "%s/Documents/profile.txt", home != NULL ? home : ".");
	}
	const char *secs = getenv("IRON_PROFILE_SECONDS");
	if (secs != NULL) {
		profile_seconds = atof(secs);
	}
	profile_words = mmap(NULL, PROFILE_WORDS * sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (profile_words == MAP_FAILED) {
		return;
	}
	mach_timebase_info(&profile_timebase);
	profile_t0          = mach_absolute_time();
	profile_main_thread = mach_thread_self();
	pthread_t self      = pthread_self();
	profile_stack_hi    = (uintptr_t)pthread_get_stackaddr_np(self);
	profile_stack_lo    = profile_stack_hi - pthread_get_stacksize_np(self);
	profile_on          = true;
	pthread_create(&profile_thread, NULL, profile_run, NULL);
	atexit(profile_at_exit);
	fprintf(stderr, "[profile] sampling the main thread for %.0f s into %s\n", profile_seconds, profile_path);
}

#else

void iron_profile_mark(const char *name) {}
void iron_profile_stop(void) {}
bool iron_profile_enabled(void) {
	return false;
}

#endif
