/*Cache implementation for URI's given in proxy.c implementation*/

/*standard lib's used*/
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// #include <cache.h>

// INIT variables for cache params:
#define HOSTLEN 256
#define SERVLEN 8
#define MAX_OBJECT_SIZE (100 * 1024)
#define MAX_CACHE_SIZE (1024 * 1024)

// IMPORTANT NOTE: LRU LOGIC: head of list is the most recent and the end of the
// list is the least recent used
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

cache_t *init_cache(void);
block_t *find_key(const char *uri, cache_t *cache);
void insert_block(cache_t *cache, size_t size, char *key, char *data);
block_t *remove_block(cache_t *cache);
void update_LRU(cache_t *cache, block_t *block);

// initialize space for the main cache
cache_t *init_cache(void) {
    cache_t *cache = malloc(sizeof(cache_t));
    // safety init cache's head and tail to NULL
    if (cache == NULL) {
        printf("Error init cache");
        exit(1);
    }
    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;
    cache->numBlock = 0;
    return cache;
}

/*find_key: returns a block if key is present in cache if not returns NULL*/
block_t *find_key(const char *uri, cache_t *cache) {

    block_t *currBlock;

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
void insert_block(cache_t *cache, size_t size, char *key, char *data) {
    if (find_key(key, cache) != NULL || size > MAX_OBJECT_SIZE)
        return;

    block_t *new_block;
    new_block = malloc(sizeof(block_t));
    if (new_block == NULL) {
        printf("Error creating block");
        exit(1);
    }
    // create new block and insert it into cache
    new_block->data = data;
    new_block->key = key;
    new_block->blockSize = size;
    new_block->prev = NULL;
    new_block->refCount = 1;

    size_t freeSpace = MAX_CACHE_SIZE - cache->size;

    while (freeSpace < size) {
        // remove block and reset tail to prev
        // cache->size = cache->size - cache->tail->blockSize;
        block_t *rBlock = remove_block(cache);
        if (rBlock->refCount == 0) {
            free(rBlock->key);
            free(rBlock->data);
            free(rBlock);
        }
        freeSpace = MAX_CACHE_SIZE - cache->size;
    }

    if (cache->numBlock == 0) {
        cache->tail = new_block;
        new_block->next = NULL;
    } else {
        new_block->next = cache->head;
        cache->head->prev = new_block;
    }

    // update cache
    cache->head = new_block;
    cache->size = cache->size + size;
    cache->numBlock++;
    update_LRU(cache, new_block);
}

block_t *remove_block(cache_t *cache) {
    block_t *rBlock = cache->tail;
    if (cache == NULL || cache->numBlock == 0)
        return NULL;

    if (cache->numBlock == 1) {
        cache->head = NULL;
        cache->tail = NULL;
    } else {
        cache->tail = rBlock->prev;
        cache->tail->next = NULL;
    }
    rBlock->next = NULL;
    rBlock->prev = NULL;
    cache->size = cache->size - rBlock->blockSize;
    cache->numBlock--;
    return rBlock;
}

void update_LRU(cache_t *cache, block_t *block) {
    // change the pointers for the prev/nextblocks
    if (cache == NULL || block == NULL || block == cache->head)
        return;

    if (block == cache->tail) {
        cache->tail = block->prev;
        cache->tail->next = NULL;
    } else {
        block->prev->next = block->next;
        block->next->prev = block->prev;
    }

    // changing block pointers and cache head
    block->prev = NULL;
    block->next = cache->head;
    cache->head->prev = block;
    cache->head = block;
}
