/*Cache implementation for URI's given in proxy.c implementation*/

/*standard lib's used*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// INIT variables for cache params:
#define HOSTLEN 256
#define SERVLEN 8
#define MAX_OBJECT_SIZE (100 * 1024)
#define MAX_CACHE_SIZE (1024 * 1024)

struct block_elem {
    char *key;
    char *data;

    size_t blockSize;
    struct block_elem *next;
    struct block_elem *prev;

} block_t;

struct cache_blocks {
    block_t *head;
    block_t *tail;
    size_t size;
} cache_t;

// NEED TO IMPLEMENT:
// insert_block, remove_block, updateLRU

void insert_block(cache_t *cache, size_t size, char *key,
                  char *data) void remove_block(cache_t *cache);
void update_LRU(block_t *block)

    // IMPORTANT NOTE: LRU LOGIC: head of list is the most recent and the end of
    // the list is the least recent used

    // initialize space for the main cache
    cache_t *init_cache(void) {
    cache_t *cache = Malloc(sizeof(cache_t));
    // safety init cache's head and tail to NULL
    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;
    return cache;
}

/*find_key: returns a block if key is present in cache if not returns NULL*/
block_t find_key(const char *uri, cache_t *cache) {
    block_t *currBlock;

    for (currBlock = cache->head; currBlock != cache->tail;
         currBlock = currBlock->next) {
        if (strcmp(uri, currBlock->key) == 0) {
            update_LRU(currBlock);
            return currBlock;
        }
    }

    return NULL;
}

/*insert_block: insert new URI at the front and if there is not enough size to
 * insert(remove the last block)*/
void insert_block(cache_t *cache, size_t size, char *key, char *data) {
    if (cache == NULL)
        return;

    block_t *new_block;
    new_block = Malloc(sizeof(block_t));
    char *b_key, *b_data;
    b_key = Malloc(strlen(key) + 1);
    b_data = Malloc(strlen(data) + 1);

    if (cache->size == 0) {
        cache->tail = new_block;
    }

    size_t freeSpace = MAX_CACHE_SIZE - cache->size;
    if (size > freeSpace) {
        printf("Size of URI larger than MAX_CACHE_SIZE");
        return;
    }

    while (freeSpace < size) {
        // remove block and reset tail to prev
        cache->size = cache->size - cache->tail->blockSize;
        remove_block(cache, cache->tail);
        cache->tail = cache->tail->prev;
        freeSpace = MAX_CACHE_SIZE - cache->size;
    }
    // create new block and insert it into cache
    // block_t *new_block;
    new_block->data = b_data;
    new_block->key = b_key;
    new_block->blockSize = size;
    new_block->next = cache->head;
    new_block->prev = NULL;

    // update cache
    cache->head = newBlock;
    cache->size = cache->size + size;
}

void remove_block(cache_t *cache) {
    block_t *rBlock = cache->tail;
    if (rBlock == NULL || cache == NULL)
        return;
    block_t *prevBlock = rBlock->prev;

    size_t blockSize = rBlock->size;
    cache->size = cache->size - blockSize;
    cache->tail = prevBlock;

    free(rBlock->key);
    free(rBlock->data);
    free(rBlock);
}

void update_LRU(cache_t *cache, block_t *block) {
    // change the pointers for the prev/nextblocks
    block_t *nBlock = block->next;
    block_t *pBlock = block->prev;

    pBlock->next = nBlock;
    nBlock->prev = pBlock;

    // changing block pointers and cache head
    block->next = cache->head;
    block->prev = NULL;
    cache->head = block;
}