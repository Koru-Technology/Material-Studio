#include <Foundation/Foundation.h>
#include <iron_thread.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

void iron_mutex_init(iron_mutex_t *mutex) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&mutex->impl.mutex, &attr);
}

void iron_mutex_destroy(iron_mutex_t *mutex) {
	pthread_mutex_destroy(&mutex->impl.mutex);
}

bool iron_mutex_try_to_lock(iron_mutex_t *mutex) {
	return pthread_mutex_trylock(&mutex->impl.mutex) == 0;
}

void iron_mutex_lock(iron_mutex_t *mutex) {
	pthread_mutex_lock(&mutex->impl.mutex);
}

void iron_mutex_unlock(iron_mutex_t *mutex) {
	pthread_mutex_unlock(&mutex->impl.mutex);
}

void iron_thread_set_name(const char *name) {
	pthread_setname_np(name);
}

static void *ThreadProc(void *arg) {
	@autoreleasepool {
		iron_thread_t *t = (iron_thread_t *)arg;
		t->impl.thread(t->impl.param);
		pthread_exit(NULL);
		return NULL;
	}
}

void iron_thread_init(iron_thread_t *t, void (*thread)(void *param), void *param) {
	t->impl.param  = param;
	t->impl.thread = thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	// As the main thread's: shader translation (SPIRV-Cross) recurses deep on big shaders, past the 512 KB default of
	// secondary threads. Only the pages touched are committed.
	pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
	struct sched_param sp;
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = 0;
	pthread_attr_setschedparam(&attr, &sp);
	pthread_create(&t->impl.pthread, &attr, &ThreadProc, t);
	pthread_attr_destroy(&attr);
}

void iron_thread_wait_and_destroy(iron_thread_t *thread) {
	int ret;
	do {
		ret = pthread_join(thread->impl.pthread, NULL);
	} while (ret != 0);
}

bool iron_thread_try_to_destroy(iron_thread_t *thread) {
	return pthread_join(thread->impl.pthread, NULL) == 0;
}

void iron_threads_init(void) {}

void iron_threads_quit(void) {}

int iron_hardware_threads(void) {
	return (int)[[NSProcessInfo processInfo] processorCount];
}
