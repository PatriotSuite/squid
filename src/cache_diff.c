/*
 * $Id: cache_diff.c,v 1.8 1998/05/24 03:41:06 wessels Exp $
 *
 * AUTHOR: Alex Rousskov
 *
 * SQUID Internet Object Cache  http://squid.nlanr.net/Squid/
 * --------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from the
 *  Internet community.  Development is led by Duane Wessels of the
 *  National Laboratory for Applied Network Research and funded by
 *  the National Science Foundation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *  
 */

/*
 * Computes the difference between the contents of two caches
 * using swap logs
 * Reports the percentage of common files and other stats
 */

#include "squid.h"

typedef struct {
    const char *name;
    hash_table *hash;
    int count;			/* #currently cached entries */
    int scanned_count;		/* #scanned entries */
    int bad_add_count;		/* #duplicate adds */
    int bad_del_count;		/* #dels with no prior add */
} CacheIndex;


typedef struct _CacheEntry {
    const cache_key *key;
    struct _CacheEntry *next;
    /* storeSwapLogData s; */
    unsigned char key_arr[MD5_DIGEST_CHARS];
} CacheEntry;


/* copied from url.c */
const char *RequestMethodStr[] =
{
    "NONE",
    "GET",
    "POST",
    "PUT",
    "HEAD",
    "CONNECT",
    "TRACE",
    "PURGE"
};


static int cacheIndexScan(CacheIndex * idx, const char *fname, FILE * file);


static CacheEntry *
cacheEntryCreate(const storeSwapLogData * s)
{
    CacheEntry *e = xcalloc(1, sizeof(CacheEntry));
    assert(s);
    /* e->s = *s; */
    xmemcpy(e->key_arr, s->key, MD5_DIGEST_CHARS);
    e->key = &e->key_arr[0];
    return e;
}

static void
cacheEntryDestroy(CacheEntry * e)
{
    assert(e);
    xfree(e);
}

static CacheIndex *
cacheIndexCreate(const char *name)
{
    CacheIndex *idx;
    if (!name || !strlen(name))
	return NULL;

    idx = xcalloc(1, sizeof(CacheIndex));
    idx->name = name;
    idx->hash = hash_create(storeKeyHashCmp, 2e6, storeKeyHashHash);

    return idx;
}

static void
cacheIndexDestroy(CacheIndex * idx)
{
    hash_link *hashr = NULL;
    if (idx) {
	/* destroy hash list contents */
	hash_first(idx->hash);
	while (hashr = hash_next(idx->hash)) {
	    hash_remove_link(idx->hash, hashr);
	    cacheEntryDestroy((CacheEntry *) hashr);
	}
	/* destroy the hash table itself */
	hashFreeMemory(idx->hash);
	xfree(idx);
    }
}

static int
cacheIndexAddLog(CacheIndex * idx, const char *fname)
{
    FILE *file;
    int scanned_count = 0;
    assert(idx);
    assert(fname && strlen(fname));

    file = fopen(fname, "r");
    if (!file) {
	fprintf(stderr, "cannot open %s: %s\n", fname, strerror(errno));
	return 0;
    }
    scanned_count = cacheIndexScan(idx, fname, file);

    fclose(file);
    return scanned_count;
}

static void
cacheIndexInitReport(CacheIndex * idx)
{
    assert(idx);
    fprintf(stderr, "%s: bad swap_add:  %d\n",
	idx->name, idx->bad_add_count);
    fprintf(stderr, "%s: bad swap_del:  %d\n",
	idx->name, idx->bad_del_count);
    fprintf(stderr, "%s: scanned lines: %d\n",
	idx->name, idx->scanned_count);
}

static int
cacheIndexScan(CacheIndex * idx, const char *fname, FILE * file)
{
    int count = 0;
    storeSwapLogData s;
    fprintf(stderr, "%s scanning\n", fname);
    while (fread(&s, sizeof(s), 1, file) == 1) {
	count++;
	idx->scanned_count++;
	/* if (s.op <= SWAP_LOG_NOP || s.op >= SWAP_LOG_MAX)
	 * continue; */
	if (s.op == SWAP_LOG_ADD) {
	    CacheEntry *olde = (CacheEntry *) hash_lookup(idx->hash, s.key);
	    if (olde) {
		idx->bad_add_count++;
	    } else {
		CacheEntry *e = cacheEntryCreate(&s);
		hash_join(idx->hash, (hash_link *) e);
		idx->count++;
	    }
	} else if (s.op == SWAP_LOG_DEL) {
	    CacheEntry *olde = (CacheEntry *) hash_lookup(idx->hash, s.key);
	    if (!olde)
		idx->bad_del_count++;
	    else {
		assert(idx->count);
		hash_remove_link(idx->hash, (hash_link *) olde);
		cacheEntryDestroy(olde);
		idx->count--;
	    }
	} else {
	    fprintf(stderr, "%s:%d: unknown swap log action\n", fname, count);
	    exit(-3);
	}
    }
    fprintf(stderr, "%s:%d: scanned (size: %d bytes)\n",
	fname, count, (int) (count * sizeof(CacheEntry)));
    return count;
}

static void
cacheIndexCmpReport(CacheIndex * idx, int shared_count)
{
    assert(idx && shared_count <= idx->count);

    printf("%s:\t %7d = %7d + %7d (%7.2f%% + %7.2f%%)\n",
	idx->name,
	idx->count,
	idx->count - shared_count,
	shared_count,
	xpercent(idx->count - shared_count, idx->count),
	xpercent(shared_count, idx->count));
}

static void
cacheIndexCmp(CacheIndex * idx1, CacheIndex * idx2)
{
    int shared_count = 0;
    int hashed_count = 0;
    hash_link *hashr = NULL;
    CacheIndex *small_idx = idx1;
    CacheIndex *large_idx = idx2;
    assert(idx1 && idx2);

    /* check our guess */
    if (idx1->count > idx2->count) {
	small_idx = idx2;
	large_idx = idx1;
    }
    /* find shared_count */
    hash_first(small_idx->hash);
    for (hashr = hash_next(small_idx->hash)) {
	hashed_count++;
	if (hash_lookup(large_idx->hash, hashr->key))
	    shared_count++;
    }
    assert(hashed_count == small_idx->count);

    cacheIndexCmpReport(idx1, shared_count);
    cacheIndexCmpReport(idx2, shared_count);
}


static int
usage(const char *prg_name)
{
    fprintf(stderr, "usage: %s <label1>: <swap_state>... <label2>: <swap_state>...\n",
	prg_name);
    return -1;
}

int
main(int argc, char *argv[])
{
    CacheIndex *CacheIdx[2];
    CacheIndex *idx = NULL;
    int idxCount = 0;
    int i;

    if (argc < 5)
	return usage(argv[0]);

    for (i = 1; i < argc; ++i) {
	const int len = strlen(argv[i]);
	if (!len)
	    return usage(argv[0]);
	if (argv[i][len - 1] == ':') {
	    idxCount++;
	    if (len < 2 || idxCount > 2)
		return usage(argv[0]);
	    idx = cacheIndexCreate(argv[i]);
	    CacheIdx[idxCount - 1] = idx;
	} else {
	    if (!idx)
		return usage(argv[0]);
	    cacheIndexAddLog(idx, argv[i]);
	}
    }

    if (idxCount != 2)
	return usage(argv[0]);

    cacheIndexInitReport(CacheIdx[0]);
    cacheIndexInitReport(CacheIdx[1]);

    cacheIndexCmp(CacheIdx[0], CacheIdx[1]);

    cacheIndexDestroy(CacheIdx[0]);
    cacheIndexDestroy(CacheIdx[1]);

    return 1;
}
