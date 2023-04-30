#include "csapp.h"
#include <stdio.h>
#include <stdlib.h>

/*Cache Implementation: doubly linked list with each block containing data on
URI and URI data With LRU structure of most recent: head of cache & least
recent: tail of cache*/
typedef struct block_elem {
    char *key;
    char *data;
    size_t refCount;

    size_t blockSize;
    struct block_elem *next;
    struct block_elem *prev;

} block_t;

typedef struct cache_blocks {
    block_t *tail;
    block_t *head;
    size_t size;
    size_t numBlock;
} cache_t;

/*init_cache: initialize an empty cache with a size of 0*/
cache_t *init_cache(void);

/*find_key: searches through cache to see if URI data is still in cache*/
block_t *find_key(const char *uri, cache_t *cache);

/*insert_block: inserts a newly malloced block to the head of the cache*/
void insert_block(cache_t *cache, size_t size, char *key, char *data);

/*remove_block: removes the block from the tail of the cache(least recently used
 * block)*/
block_t *remove_block(cache_t *cache);

/*update_LRU: If looking through cache for URI and finds one then moves that
 * block to the head(most recently used block)*/
void update_LRU(cache_t *cache, block_t *block);
