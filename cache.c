/*Cache implementation for URI's given in proxy.c implementation*/

/*standard lib's used*/
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

//#include <cache.h>
#include <csapp.h>

// INIT variables for cache params:
#define HOSTLEN 256
#define SERVLEN 8
#define MAX_OBJECT_SIZE (100 * 1024)
#define MAX_CACHE_SIZE (1024 * 1024)

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

cache_t *init_cache(void);
block_t *find_key(const char *uri, cache_t *cache);
void insert_block(cache_t *cache, size_t size, const char *key,
                  const char *data);
void remove_block(cache_t *cache);
void update_LRU(cache_t *cache, block_t *block);
block_t *find_LRU(cache_t *cache);

// IMPORTANT NOTE: LRU LOGIC: head of list is the most recent and the end of the
// list is the least recent used

// initialize space for the main cache
cache_t *init_cache(void) {
    cache_t *cache = Malloc(sizeof(cache_t));
    // safety init cache's head and tail to NULL
    cache->head = NULL;
    cache->size = 0;
    cache->lruAge = 0;
    return cache;
}

/*find_key: returns a block if key is present in cache if not returns NULL*/
block_t *find_key(const char *uri, cache_t *cache) {
    block_t *currBlock = cache->head;
    if (currBlock == NULL)
        return NULL;

    for (currBlock = cache->head; currBlock != NULL;
         currBlock = currBlock->next) {
        if (strcmp(uri, currBlock->key) == 0) {
            update_LRU(cache, currBlock);
            return currBlock;
        }
    }

    return NULL;
}

/*insert_block: insert new URI at the front and if there is not enough size to
 * insert(remove the last block)*/
void insert_block(cache_t *cache, size_t size, const char *key,
                  const char *data) {
    if (cache == NULL || cache->head == NULL)
        return;
    if (find_key(key, cache) != NULL)
        return;

    size_t freeSpace = MAX_CACHE_SIZE - cache->size;
    if (size > MAX_CACHE_SIZE) {
        printf("Size of URI larger than MAX_CACHE_SIZE");
        return;
    }

    while (size > freeSpace) {
        remove_block(cache);
    }
    // create new block and insert it into cache
    block_t *new_block;
    new_block = Malloc(sizeof(block_t));

    new_block->data = data;
    new_block->key = key;
    new_block->blockSize = size;
    new_block->next = cache->head;
    new_block->prev = NULL;
    new_block->refCount = 0;

    // update cache
    cache->head = new_block;
    cache->size = cache->size + size;
    update_LRU(cache, new_block);
}

void remove_block(cache_t *cache) {
    if (cache == NULL || cache->head == NULL)
        return;

    block_t *lru = find_LRU(cache);
    block_t *pBlock = lru->prev;
    block_t *nBlock = lru->next;

    if (pBlock == NULL) {
        nBlock = cache->head;
        nBlock->prev = NULL;
        cache->head = nBlock;
    } else if (nBlock == NULL) {
        pBlock->next = NULL;
    } else {
        pBlock->next = nBlock;
        nBlock->prev = pBlock;
    }

    cache->size = cache->size - lru->blockSize;

    Free((void *)lru->key);
    Free((void *)lru->data);
    Free(lru);
}

void update_LRU(cache_t *cache, block_t *block) {
    block->lru = cache->lruAge;
    cache->lruAge++;
    // update lru of all other blocks
    block_t *currBlock = cache->head;
    while (currBlock != NULL) {
        currBlock->lru = cache->lruAge;

        currBlock = currBlock->next;
    }
}

block_t *find_LRU(cache_t *cache) {
    block_t *currBlock = cache->head;
    block_t *lruBlock = cache->head;
    size_t smallestLru = lruBlock->lru;

    while (currBlock != NULL) {
        if (currBlock->lru < smallestLru) {
            smallestLru = currBlock->lru;
            lruBlock = currBlock;
        }
        currBlock = currBlock->next;
    }

    return lruBlock;
}