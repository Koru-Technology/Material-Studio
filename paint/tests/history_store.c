#include "global.h"

// Blob store (history_store.c): put, get, refs, dedup, the journal (spill, trim, reads back, torn tail recovery,
// compaction) and stats. Prints the flush time of a large dirty set (iOS resign-active has about 5 s).

int fails  = 0;
int checks = 0;

void check(int ok, char *name) {
	checks = checks + 1;
	if (!ok) {
		fails = fails + 1;
	}
	printf("[history_store] %s %s", ok ? "ok  " : "FAIL", name);
}

void basics() {
	int a = hstore_test_put(1, 1, 65536, 0); // Compresses
	int b = hstore_test_put(1, 2, 65536, 1); // Noise
	int c = hstore_test_put(3, 3, 10, 0);    // Stored raw
	check(hstore_test_stat(0) == 3.0, "three blobs");
	check(hstore_test_get(a, 1, 65536, 0) == 1 && hstore_test_get(b, 2, 65536, 1) == 1 && hstore_test_get(c, 3, 10, 0) == 1, "blobs read back");
	float ram = hstore_test_stat(1);
	check(ram < 64.0 + 66.0, string("the smooth blob is compressed (%f KB in RAM)", ram));
	check(hstore_test_stat(3) == ram && hstore_test_stat(2) == 0.0, "all dirty, nothing journaled");

	int a2 = hstore_test_put(1, 1, 65536, 0);
	check(hstore_test_stat(0) == 3.0 && hstore_test_stat(1) == ram, "the same bytes are stored once");
	hstore_test_unref(a);
	hstore_test_trim(0);
	check(hstore_test_has(a2), "a blob stays while referenced");
	hstore_test_unref(a2);
	hstore_test_trim(0);
	check(!hstore_test_has(a) && hstore_test_stat(0) == 2.0, "an unreferenced blob goes at trim");
	hstore_test_ref(c);
	hstore_test_unref(c);
	hstore_test_trim(0);
	check(hstore_test_has(c), "ref and unref balance");

	hstore_drop_tier(1);
	check(hstore_test_stat(1) == 0.0 && hstore_test_stat(3) == 0.0 && hstore_test_stat(2) > 64.0, "T1 dropped: all in the journal");
	float reads = hstore_test_stat(8);
	check(hstore_test_get(b, 2, 65536, 1) == 1 && hstore_test_get(c, 3, 10, 0) == 1, "blobs read back from the journal");
	check(hstore_test_stat(8) == reads + 2.0, "two chunks read from the journal");
	check(hstore_test_get(b, 2, 65536, 1) == 1 && hstore_test_stat(8) == reads + 2.0, "a blob read from the journal is cached again");
	hstore_test_unref(b);
	hstore_test_trim(0);
	check(hstore_test_stat(4) > 64.0, "a journaled blob that goes leaves dead bytes");
}

void recovery() {
	hstore_test_reset();
	char *uuid = project_uuid_get();
	check(!hstore_journal_pending(uuid), "no journal after reset");
	int x = hstore_test_put(2, 10, 4096, 1);
	hstore_test_put(2, 11, 4096, 1);
	hstore_test_flush();
	hstore_journal_mark_saved();
	check(!hstore_journal_pending(uuid), "nothing pending after a save");
	int p = hstore_test_put(2, 12, 4096, 1);
	int q = hstore_test_put(5, 13, 300, 0);
	int r = hstore_test_put(2, 14, 4096, 1);
	hstore_test_flush();
	check(hstore_journal_pending(uuid), "blobs journaled after the save are pending");

	hstore_test_crash();
	hstore_test_tear(100); // The last chunk is torn
	check(hstore_journal_pending(uuid), "still pending with a torn tail");
	int n = hstore_recover();
	check(n == 2, string("two whole chunks after the save recovered (%d)", n));
	check(hstore_test_get(p, 12, 4096, 1) == 1 && hstore_test_get(q, 13, 300, 0) == 1, "recovered blobs read back");
	check(!hstore_test_has(r), "the torn chunk is gone");
	check(!hstore_test_has(x), "chunks before the save are not recovered");
	hstore_test_trim(0);
	check(hstore_test_has(p) && hstore_test_has(q), "recovered blobs stay until the recovery ends");
	hstore_test_ref(p);
	hstore_recover_end();
	hstore_test_trim(0);
	check(hstore_test_has(p) && !hstore_test_has(q), "after it only referenced ones stay");

	int s = hstore_test_put(2, 15, 4096, 1);
	hstore_test_flush();
	hstore_test_crash();
	n = hstore_recover();
	check(n == 3, string("the session appends after the recovered chunks (%d)", n));
	check(hstore_test_get(s, 15, 4096, 1) == 1 && hstore_test_get(p, 12, 4096, 1) == 1, "appended blob recovered too");
	hstore_recover_end();
	hstore_journal_mark_saved();
	check(!hstore_journal_pending(uuid), "a save ends the pending state");
}

// Kind 2 blobs list their children (a test enumerator), kind 1 are leaves
int parent_of(int a, int b) {
	hstore_test_list_clear();
	hstore_test_list_add(a);
	if (b >= 0) {
		hstore_test_list_add(b);
	}
	return hstore_test_put_list(2);
}

void children() {
	hstore_test_reset();
	hstore_test_children(2, 1);
	int t1 = hstore_test_put(1, 20, 4096, 1);
	int t2 = hstore_test_put(1, 21, 4096, 1);
	int t3 = hstore_test_put(1, 22, 4096, 1);
	int p  = parent_of(t1, t2);
	check(hstore_test_refs(t1) == 2 && hstore_test_refs(t2) == 2 && hstore_test_refs(t3) == 1, "a new parent refs its children");
	int p2 = parent_of(t1, t2);
	check(p2 != p && hstore_test_refs(p) == 2 && hstore_test_refs(t1) == 2, "a dedup hit refs the parent, not its children again");
	hstore_test_unref(t1);
	hstore_test_unref(t2);
	hstore_test_unref(t3);
	hstore_test_trim(0);
	check(hstore_test_has(t1) && hstore_test_has(t2) && !hstore_test_has(t3), "children held by a parent stay");
	int g = parent_of(p, t1);
	check(hstore_test_refs(p) == 3 && hstore_test_refs(t1) == 2, "a grandparent refs its children");
	hstore_test_unref(p);
	hstore_test_unref(p);
	hstore_test_trim(0);
	check(hstore_test_has(p) && hstore_test_refs(p) == 1, "a parent held by a grandparent stays");
	hstore_test_unref(g);
	hstore_test_trim(0);
	check(!hstore_test_has(g) && !hstore_test_has(p) && !hstore_test_has(t1) && !hstore_test_has(t2), "removal cascades to grandchildren");
	check(hstore_test_stat(0) == 0.0, string("nothing left (%f blobs)", hstore_test_stat(0)));

	// A parent only in the journal waits for a trim that may read
	int a = hstore_test_put(1, 23, 4096, 1);
	int b = hstore_test_put(1, 24, 4096, 1);
	int q = parent_of(a, b);
	hstore_test_unref(a);
	hstore_test_unref(b);
	hstore_drop_tier(1);
	hstore_test_unref(q);
	float reads = hstore_test_stat(8);
	hstore_test_trim(1);
	check(hstore_test_has(q) && hstore_test_has(a) && hstore_test_refs(a) == 1, "memory warning trim defers a parent only on disk");
	check(hstore_test_stat(8) == reads, "memory warning trim reads nothing");
	hstore_test_trim(0);
	check(!hstore_test_has(q) && !hstore_test_has(a) && !hstore_test_has(b), "the next trim removes it and its children");

	// Refs rebuilt from roots after a recovery
	hstore_test_reset();
	int c  = hstore_test_put(1, 25, 4096, 1);
	int d  = hstore_test_put(1, 26, 4096, 1);
	int e  = hstore_test_put(1, 27, 4096, 1);
	int f  = hstore_test_put(1, 28, 4096, 1);
	int r  = parent_of(c, d);
	int s  = parent_of(r, e);
	int s2 = parent_of(r, c);
	hstore_test_flush();
	hstore_test_crash();
	int n = hstore_recover();
	check(n == 7, string("recovered (%d)", n));
	check(hstore_test_refs(r) == 0 && hstore_test_refs(c) == 0, "recovered blobs have no refs");
	hstore_test_list_clear();
	hstore_test_list_add(s);
	hstore_test_list_add(s2);
	hstore_test_list_add(s);
	hstore_test_rebuild();
	check(hstore_test_refs(s) == 2 && hstore_test_refs(s2) == 1, "a root gets a ref per listing");
	check(hstore_test_refs(r) == 0, "children of blobs only on disk are not read yet");
	check(hstore_test_refs(f) == 0, "unreachable blobs have none");
	hstore_test_trim(0);
	check(hstore_test_refs(r) == 2 && hstore_test_refs(c) == 2 && hstore_test_refs(d) == 1 && hstore_test_refs(e) == 1, "the next trim reads them: a blob gets a ref per parent");
	check(!hstore_test_has(f) && hstore_test_has(c) && hstore_test_has(r), "unreachable blobs go at the next trim");
	hstore_test_unref(s);
	hstore_test_unref(s);
	hstore_test_unref(s2);
	hstore_test_trim(0);
	check(hstore_test_stat(0) == 0.0, "rebuilt refs cascade like put ones");
	hstore_test_children(2, 0);
}

void budgets() {
	config_t *cfg = script_get_config();
	hstore_test_reset();
	cfg->history_ram_mb = 16;
	hstore_test_trim(0); // Reads the budget
	int first = -1;
	for (int i = 0; i < 400; i = i + 1) {
		int id = hstore_test_put(1, 100 + i, 262144, 1);
		if (i == 0) {
			first = id;
		}
	}
	float ram = hstore_test_stat(1);
	check(ram <= 16.0 * 1024.0, string("RAM within its budget (%f KB)", ram));
	check(hstore_test_stat(2) > 50.0 * 1024.0, "the rest spilled to the journal");
	int ok = 1;
	for (int i = 0; i < 400; i = i + 7) {
		if (hstore_test_get(first + i, 100 + i, 262144, 1) != 1) {
			ok = 0;
		}
	}
	check(ok, "spilled blobs read back");

	for (int i = 0; i < 400; i = i + 1) {
		if (i % 3 != 0) {
			hstore_test_unref(first + i);
		}
	}
	float disk = hstore_test_stat(2);
	hstore_test_trim(0);
	float after = hstore_test_stat(2);
	check(after < disk / 2.0, string("a mostly dead journal is compacted (%f to %f KB)", disk, after));
	ok = 1;
	for (int i = 0; i < 400; i = i + 3) {
		if (hstore_test_get(first + i, 100 + i, 262144, 1) != 1) {
			ok = 0;
		}
	}
	check(ok, "blobs read back after compaction");
	check(!hstore_test_has(first + 1), "unreferenced blobs are gone after compaction");

	// Flush cost of a large dirty set (resign-active)
	cfg->history_ram_mb = 1024;
	hstore_test_reset();
	hstore_test_trim(0);
	for (int i = 0; i < 800; i = i + 1) {
		hstore_test_put(1, 1000 + i, 262144, 1);
	}
	float dirty = hstore_test_stat(3);
	hstore_test_flush();
	printf("[history_store] flush of %f MB of noise: %f ms", dirty / 1024.0, hstore_test_stat(7));
	for (int i = 0; i < 800; i = i + 1) {
		hstore_test_put(1, 5000 + i, 262144, 0);
	}
	dirty = hstore_test_stat(3);
	hstore_test_flush();
	printf("[history_store] flush of %f MB of smooth tiles (200 MB raw): %f ms", dirty / 1024.0, hstore_test_stat(7));
	check(hstore_test_stat(3) == 0.0, "flush journals every dirty blob");
	check(hstore_test_stat(5) == 1024.0 * 1024.0, "RAM budget from history_ram_mb");
}

void main() {
	config_t *cfg    = script_get_config();
	int       ram_mb = cfg->history_ram_mb;
	hstore_test_reset();
	basics();
	recovery();
	children();
	budgets();
	char *path = hstore_test_journal_path();
	hstore_test_reset();
	check(!iron_file_exists(path), "reset deletes the journal");
	cfg->history_ram_mb = ram_mb;
	hstore_test_trim(0);
	printf("[history_store] %d checks, %d failures", checks, fails);
	printf(fails == 0 ? "HISTORY_STORE PASS" : "HISTORY_STORE FAIL");
	script_quit();
}
