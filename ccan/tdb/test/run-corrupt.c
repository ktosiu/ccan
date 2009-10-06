#define _XOPEN_SOURCE 500
#include "tdb/tdb.h"
#include "tdb/io.c"
#include "tdb/tdb.c"
#include "tdb/lock.c"
#include "tdb/freelist.c"
#include "tdb/traverse.c"
#include "tdb/transaction.c"
#include "tdb/error.c"
#include "tdb/open.c"
#include "tdb/check.c"
#include "tap/tap.h"
#include <stdlib.h>
#include <err.h>

static int check(TDB_DATA key, TDB_DATA data, void *private)
{
	unsigned int *sizes = private;

	if (key.dsize > strlen("hello"))
		return -1;
	if (memcmp(key.dptr, "hello", key.dsize) != 0)
		return -1;

	if (data.dsize != strlen("world"))
		return -1;
	if (memcmp(data.dptr, "world", data.dsize) != 0)
		return -1;

	sizes[0] += key.dsize;
	sizes[1] += data.dsize;
	return 0;
}

int main(int argc, char *argv[])
{
	struct tdb_context *tdb;
	TDB_DATA key, data;
	unsigned int i, verifiable, corrupt, sizes[2], dsize, ksize;

	plan_tests(2);
	tdb = tdb_open("/tmp/test6.tdb", 2, TDB_CLEAR_IF_FIRST,
		       O_CREAT|O_TRUNC|O_RDWR, 0600);

	if (!tdb)
		abort();
	ok1(tdb_check(tdb, NULL, NULL) == 0);
	
	key.dptr = (void *)"hello";
	data.dsize = strlen("world");
	data.dptr = (void *)"world";

	/* Key and data size respectively. */
	dsize = ksize = 0;

	/* 5 keys in hash size 2 means we'll have multichains. */
	for (key.dsize = 1; key.dsize <= 5; key.dsize++) {
		ksize += key.dsize;
		dsize += data.dsize;
		if (tdb_store(tdb, key, data, TDB_INSERT) != 0)
			abort();
	}

	/* This is how many bytes we expect to be verifiable. */
	/* From the file header. */
	verifiable = strlen(TDB_MAGIC_FOOD) + 1
		+ 2 * sizeof(uint32_t) + 2 * sizeof(tdb_off_t);
	/* From the free list chain and hash chains. */
	verifiable += 3 * sizeof(tdb_off_t);
	/* From the record headers & tailer */
	verifiable += 5 * (sizeof(struct list_struct) + sizeof(uint32_t));
	/* The free block: we ignore datalen, keylen, full_hash. */
	verifiable += sizeof(struct list_struct) - 3*sizeof(uint32_t) +
		sizeof(uint32_t);
	/* Our check function verifies the key and data. */
	verifiable += ksize + dsize;

	/* Flip one bit at a time, make sure it detects verifiable bytes. */
	for (i = 0, corrupt = 0; i < tdb->map_size * CHAR_BIT; i++) {
		bit_flip(tdb->map_ptr, i);
		memset(sizes, 0, sizeof(sizes));
		if (tdb_check(tdb, check, sizes) != 0)
			corrupt++;
		else if (sizes[0] != ksize || sizes[1] != dsize)
			corrupt++;
		bit_flip(tdb->map_ptr, i);
	}
	ok(corrupt == verifiable * CHAR_BIT, "corrupt %u should be %u",
	   corrupt, verifiable * CHAR_BIT);
	tdb_close(tdb);

	return exit_status();
}