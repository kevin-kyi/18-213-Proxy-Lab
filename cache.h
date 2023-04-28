#include "csapp.h"
#include <stdio.h>
#include <stdlib.h>

/*Cache Implementation: doubly linked list with each block containing data on
URI and URI data With LRU structure of most recent: head of cache & least
recent: tail of cache*/
typedef struct block_elem {
    const char *key;
    const char *data;
    size_t refCount;
    size_t lru;

    size_t blockSize;
    struct block_elem *next;
    struct block_elem *prev;

} block_t;

typedef struct cache_blocks {
    size_t lruAge;
    block_t *head;
    size_t size;
} cache_t;

/*init_cache: initialize an empty cache with a size of 0*/
cache_t *init_cache(void);

/*find_key: searches through cache to see if URI data is still in cache*/
block_t *find_key(const char *uri, cache_t *cache);

/*insert_block: inserts a newly malloced block to the head of the cache*/
void insert_block(cache_t *cache, size_t size, const char *key,
                  const char *data);

/*remove_block: removes the block from the tail of the cache(least recently used
 * block)*/
void remove_block(cache_t *cache, size_t size);

/*update_LRU: If looking through cache for URI and finds one then moves that
 * block to the head(most recently used block)*/
void update_LRU(cache_t *cache, block_t *block);

block_t *find_LRU(cache_t *cache);