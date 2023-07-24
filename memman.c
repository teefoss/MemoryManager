#include "memman.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/*
 TODO:
 - Defragment?
 */

//#define MM_DEBUG

#ifdef MM_VERBOSE
    #include <stdio.h>
    #define MM_LOG(...) printf(__VA_ARGS__)
#else
    #define MM_LOG(...)
#endif

#ifdef MM_DEBUG
    #include <stdio.h>
    #define MM_DEBUG_LOG(...) printf(__VA_ARGS__)
    #define MM_ERROR(...) fprintf(stderr, __VA_ARGS__)
#else
    #define MM_DEBUG_LOG(...)
    #define MM_ERROR(...)
#endif



#define MM_ID 0xBEEF
#define NULL_LINK 0xFFFFFFFF

#define U32_MAX 0xFFFFFFFF
#define MEM_MAX (size_t)U32_MAX
#define MEM_MIN (size_t)256

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef struct {
    u16 id;         // Internal check.
    u8  is_free;
    u8  unused;     // (Padding)
    u32 size;       // Block size, not including this struct.
    u32 prev;       // Here we use byte offsets into `memory` rather than...
    u32 next;       // ... pointers since `memory` may move after reallocation.
} Block;


static u32 realloc_increment;

// Current size of `memory`.
static u32 allocated;

static int rover;

static void * memory;



// -----------------------------------------------------------------------------
// PRIVATE
// -----------------------------------------------------------------------------



// Return Block at byte offset in `memory`
static Block * GetBlock(u32 offset)
{
    if ( offset == NULL_LINK )
        return NULL;

    return memory + offset;
}



// Returns `block`'s byte offset within `memory`.
static u32 GetOffset(const Block * block)
{
    return (u32)((void *)block - memory);
}



#ifdef MM_DEBUG
// For debugging: print the contents of memory followed each
// block's information.
static void PrintMemory(void)
{
    for ( u32 i = 0; i < allocated; i++ )
    {
        char c = ((char *)memory)[i];

        if ( i % 4 == 0 ) {
            u32 u = *(u32 *)(memory + i);
            printf("addr %2d: %d (u32: %d)\n", i, c, u);
        } else {
            printf("addr %2d: %d\n", i, c);
        }
    }

    int block_num = 0;
    Block * block = GetBlock(0);

    do {
        block_num++;
        printf("\nblock %d:\n", block_num);
        printf(" - offset: %d\n", GetOffset(block));
        printf(" - is free: %s\n", block->is_free ? "yes" : "no");
        printf(" - size: %d\n", block->size);
        printf(" - prev: %d\n", block->prev);
        printf(" - next: %d\n", block->next);
        printf(" - id: %X\n", block->id);
    } while ( (block = GetBlock(block->next)) );
}
#endif



/**
 *  Get the first free block that's at least `size` bytes big.
 */
static Block * FindFreeBlock(u32 size)
{
    u32 start = rover;
    Block * block;

    MM_DEBUG_LOG("Finding free block, rover starting at %d\n", rover);

    while ( 1 ) {
        block = GetBlock(rover);

#ifdef MM_DEBUG
        if ( block == NULL ) {
            MM_DEBUG_LOG("%s: rover got a NULL block at %d!\n", __func__, rover);
        }
#endif

        if ( block->is_free && block->size >= size ) {
            return block;
        }

        rover = block->next;
        if ( rover == start ) {
            MM_DEBUG_LOG("%s: no free blocks\n", __func__);
            return NULL; // Went all the way around, no free blocks.
        }

        if ( rover == NULL_LINK ) {
            rover = 0;
        }

        MM_DEBUG_LOG("moving rover to %d\n", rover);
    }

//    Block * block = GetBlock(0);
//
//    do {
//        if ( block->is_free && block->size >= size ) {
//            return block;
//        }
//    } while ( (block = GetBlock(block->next)) );
//
//    return NULL;
}



/**
 *  Get a free block, reallocating `memory` if necessary.
 */
static Block * GetFreeBlock(u32 size)
{
    Block * block = FindFreeBlock(size);

    if ( block == NULL ) { // Try to reallocate `memory`
        do {
            u32 old_size = allocated;
            allocated += realloc_increment;
            MM_DEBUG_LOG("reallocating to %d\n", allocated);

            void * temp = realloc(memory, allocated);
            if ( temp == NULL ) {
                MM_ERROR("%s: could not reallocate memory\n", __func__);
                return NULL;
            }

            memory = temp;

            // Create a new block starting at the new memory.

            Block * new_block = GetBlock(old_size);
            new_block->id = MM_ID;
            new_block->is_free = true;
            new_block->size = allocated - old_size - sizeof(Block);

            // Get the last block and update everybody's links.
            Block * b = GetBlock(0);
            while ( b->next != NULL_LINK )
                b = GetBlock(b->next);
            b->next = GetOffset(new_block);
            new_block->prev = GetOffset(b);
            new_block->next = NULL_LINK;

        } while ( (block = FindFreeBlock(size)) == NULL );
    }

    return block;
}



static void TrySplitBlock(Block * block, u32 requested_size)
{
    u32 size_needed = requested_size + sizeof(Block);
    u32 excess = block->size - requested_size;

    MM_DEBUG_LOG("Free block (%d bytes) is bigger than requested (%d) "
                 "by %d bytes\n", block->size, requested_size, excess);

    // Big enough for a header and user data?
    if ( excess > sizeof(Block) ) {
        MM_DEBUG_LOG("Splitting block...\n");

        u32 block_offset = GetOffset(block);

        // Create a new block.
        Block * split = GetBlock(block_offset + size_needed);
        split->id = MM_ID;
        split->is_free = true;
        split->size = excess - sizeof(Block);
        split->prev = block_offset;
        split->next = block->next;

        // Update block's forward link.
        block->next = GetOffset(split);
        block->size = requested_size;
    }
#ifdef MM_DEBUG
    else {
        MM_DEBUG_LOG("Block could not be split (too small)\n");
    }
#endif
}



/**
 *  Try to merge `block` with blocks on either side, if they are free.
 */
static void TryMergeBlock(Block * block)
{
    Block * prev = GetBlock(block->prev);
    Block * next = GetBlock(block->next);

    bool merge_prev = prev != NULL && prev->is_free;
    bool merge_next = next != NULL && next->is_free;

    if ( merge_prev && merge_next ) {
        MM_DEBUG_LOG("merging free block with prev and next\n");
        prev->size += block->size + sizeof(Block);
        prev->size += next->size + sizeof(Block);
        prev->next = next->next;
    } else if ( merge_prev ) {
        MM_DEBUG_LOG("merging free block with prev\n");
        prev->size += block->size + sizeof(Block);
        prev->next = block->next;
    } else if ( merge_next ) {
        MM_DEBUG_LOG("merging free block with next\n");
        block->size += next->size + sizeof(Block);
        block->next = next->next;
    }
}


static void FreeMemory(void)
{
    free(memory);
}



// -----------------------------------------------------------------------------
// PUBLIC
// -----------------------------------------------------------------------------



/**
 *  Allocate `memory` and setup initial block.
 */
bool MM_Init(size_t size)
{
    if ( allocated != 0 ) {
        fprintf(stderr, "Error: %s has already been called!\n", __func__);
        return false;
    }

    // Validate `size`

    if ( size > MEM_MAX || size < MEM_MIN ) {
        fprintf(stderr, "%s error: size should be in range %zu-%zu bytes\n",
                __func__, MEM_MIN, MEM_MAX);
        return false;
    }

    // Allocate `memory`

    allocated = (u32)size;
    realloc_increment = (u32)size;

    memory = malloc(allocated);
    if ( memory == NULL ) {
        fprintf(stderr, "%s: Not enough memory to allocate %zu bytes\n",
                __func__, size);
        return false;
    }

    atexit(FreeMemory);

    // Setup initial block.
    // `memory` initially starts off as one big free block.

    Block * block = memory;
    block->id = MM_ID;
    block->is_free = true;
    block->size = realloc_increment - sizeof(Block);
    block->prev = NULL_LINK;
    block->next = NULL_LINK;

    return true;
}



void * MM_malloc(size_t size)
{
    if ( size > allocated - sizeof(Block) || size == 0 ) {
        return NULL;
    }

    u32 requested_size = (u32)size;

    Block * block = GetFreeBlock(requested_size);
    if ( block == NULL ) {
        return NULL;
    }

    if ( block->size > requested_size ) {
        TrySplitBlock(block, requested_size);
    }

    block->is_free = false;
    block->id = MM_ID;

#ifdef MM_DEBUG
    PrintMemory();
#endif

    return (void *)block + sizeof(Block);
}



void MM_free(void * mem)
{
    if ( mem == NULL ) {
        return;
    }

    Block * block = (Block *)mem - 1;

    if ( block->id != MM_ID ) {
        MM_LOG("%s: memory was not previously allocated "
               "or a buffer overrun has corrupted it!",
               __func__);
        return;
    }

    if ( block->is_free ) {
        MM_LOG("%s: memory is already free!", __func__);
        return;
    }

    block->is_free = true;
    TryMergeBlock(block);

#ifdef MM_DEBUG
    PrintMemory();
#endif
}



void * MM_calloc(size_t count, size_t size)
{
    size_t bytes = count * size;
    void * mem = MM_malloc(bytes);

    if ( mem != NULL ) {
        memset(mem, 0, bytes);
    }

    return mem;
}



void * MM_realloc(void * mem, size_t size)
{
    void * new_mem = MM_malloc(size);
    if ( new_mem ) {
        Block * block = (Block *)mem - 1;
        memmove(new_mem + sizeof(Block),
                mem + sizeof(Block), block->size);

        MM_free(mem);
    }

    return new_mem;
}
