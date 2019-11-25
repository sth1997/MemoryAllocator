#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "solve.h"
#include "utils.h"

#define WORD sizeof(void*)
#define DWORD (WORD << 1)

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p)          (*(uintptr_t*)(p))
#define SET(p,val)      (*(uintptr_t*)(p) = (val))

#define BLOCK_SIZE(p)     (GET(p) & ~(DWORD - 1))

#define HEAD(bp)        ((char*)(bp) - WORD)
#define TAIL(bp)        ((char*)(bp) + BLOCK_SIZE(HEAD(bp)) - DWORD)

#define NEXT_BLOCK(bp) ((char*)(bp) + BLOCK_SIZE(((char *)(bp) - WORD)))
#define PREV_BLOCK(bp) ((char*)(bp) - BLOCK_SIZE(((char *)(bp) - DWORD)))

#define SEGREGATED_NUM 17
void* segregated_list[SEGREGATED_NUM];

#define SBRK_SIZE (1 << 10)

int my_init(void)
{
    void* heap_listp = NULL;
	if ((heap_listp = mem_sbrk(4*WORD)) == (void *)-1)
		return -1;
	SET(heap_listp, 0);
	SET(heap_listp + (1 * WORD), PACK(DWORD, 1));
	SET(heap_listp + (2 * WORD), PACK(DWORD, 1));
	SET(heap_listp + (3 * WORD), PACK(0, 1));

	int i;
	for(i = 0; i < SEGREGATED_NUM; i++)
	{
		segregated_list[i] = NULL;
	}

	return 0;
}

int get_segregated_index(size_t size)
{
	int index;
	if(size <= 128)
	{
		index = (size >> 4) - 1;
		return index;
	}
	else if (size < 256)
		return 8;
	else if (size < 512)
		return 9;
	else if (size < 1024)
		return 10;
	else if (size < 2048)
		return 11;
	else if (size < 4096)
		return 12;
	else if (size < 8192)
		return 13;
	else if (size < 16384)
		return 14;
	else if (size < 32768)
		return 15;
	else
		return 16;
}

void remove_free_block(void *bp)
{
	size_t size = BLOCK_SIZE(HEAD(bp));
    uintptr_t *prev = *(void**)(bp);
    uintptr_t *next = *(void**)((char*)bp + WORD);

	if(!prev && !next)
	{
		segregated_list[get_segregated_index(size)] = NULL;
	}
	else if(!prev && next)
	{
		SET(next,0);
		segregated_list[get_segregated_index(size)] = next;
	}
	else if(prev && !next)
	{
		SET((char *)prev + WORD,0);
	}
	else if(prev && next)
	{
		SET((char *)prev + WORD, (uintptr_t) next);
		SET(next, (uintptr_t) prev);
	}
}

void add_to_free_list(void *bp)
{
	size_t size = GET(HEAD(bp));
	
	int i = get_segregated_index(size);

	if(segregated_list[i] == NULL)
	{
		segregated_list[i] = bp;
		SET(bp,0);
		SET((char *)bp + WORD,0);
	}
	else
	{
		SET((char *)bp + WORD, (uintptr_t) segregated_list[i]);
		SET(bp, 0);
		SET(segregated_list[i], (uintptr_t) bp);
       	segregated_list[i] = bp;
	}
}

void* coalesce(void *bp)
{
	size_t prev_alloc = GET(TAIL(PREV_BLOCK(bp))) & 1;
	size_t next_alloc = GET(HEAD(NEXT_BLOCK(bp))) & 1;
	size_t size = BLOCK_SIZE(HEAD(bp));

	if (prev_alloc && next_alloc) {
		add_to_free_list(bp);
		return bp;
	}
	else if (prev_alloc && !next_alloc) {
		size += BLOCK_SIZE(HEAD(NEXT_BLOCK(bp)));

		remove_free_block(NEXT_BLOCK(bp));
		SET(HEAD(bp), PACK(size, 0));
		SET(TAIL(bp), PACK(size, 0));
		add_to_free_list(bp);
		return bp;
	}
	else if (!prev_alloc && next_alloc) {
		size += BLOCK_SIZE(HEAD(PREV_BLOCK(bp)));

		remove_free_block(PREV_BLOCK(bp));
		SET(TAIL(bp), PACK(size, 0));
		SET(HEAD(PREV_BLOCK(bp)), PACK(size, 0));
		add_to_free_list(PREV_BLOCK(bp));
		return (PREV_BLOCK(bp));
	}
	else {
		size += BLOCK_SIZE(HEAD(PREV_BLOCK(bp)))+BLOCK_SIZE(TAIL(NEXT_BLOCK(bp)));
		remove_free_block(PREV_BLOCK(bp));
		remove_free_block(NEXT_BLOCK(bp));
		SET(HEAD(PREV_BLOCK(bp)), PACK(size,0));
		SET(TAIL(NEXT_BLOCK(bp)), PACK(size,0));
		add_to_free_list(PREV_BLOCK(bp));
		return (PREV_BLOCK(bp));
	}
}

void *extend_heap(size_t size)
{
	char *bp;
	if ( (bp = mem_sbrk(size)) == (void *)-1 )
		return NULL;

	SET(HEAD(bp), PACK(size, 0));
	SET(TAIL(bp), PACK(size, 0));
	SET(HEAD(NEXT_BLOCK(bp)), PACK(0, 1));

	// TODO : Coalesce the prev block(if not allocated)
	return bp;
}

void* find_fit(size_t aligned_size, void* free_listp)
{
	//check if your free list is empty
	if(free_listp == NULL)
		return NULL;
	
    while (free_listp != NULL)
	{
        if(aligned_size <= BLOCK_SIZE(HEAD(free_listp)))
		    return free_listp;
        free_listp = *(void**)((char*)free_listp + WORD);
    }
	return NULL;
}

void* find_segregated_best_fit(size_t aligned_size)
{
	int segregated_index = get_segregated_index(aligned_size);

	int i;
	for(i = segregated_index; i < SEGREGATED_NUM; i++)
	{
		void * free_segregated_list = segregated_list[i];
		void * free_blk = find_fit(aligned_size,free_segregated_list);

		if(free_blk != NULL)
			return free_blk;
		else	
			continue;
	}

	//no free block
	return NULL;
}

void set_allocated(void* bp, size_t aligned_size)
{
	size_t block_size = BLOCK_SIZE(HEAD(bp));
	if((block_size-aligned_size) >= 4 * WORD)	//we need 4*WORD to hold metadata for one block which is not allocated
	{
        //allocated block
		SET(HEAD(bp),PACK(aligned_size,1));
		SET(TAIL(bp),PACK(aligned_size,1));

		SET(HEAD(bp+aligned_size),PACK(block_size-aligned_size,0));
		SET(TAIL(bp+aligned_size),PACK(block_size-aligned_size,0));
		add_to_free_list(bp+aligned_size);
	}
	else
	{
		SET(HEAD(bp), PACK(block_size, 1));
		SET(TAIL(bp), PACK(block_size, 1));
	}
}

void my_free(void *bp)
{
	if(bp == NULL){
		return;
	}

	size_t size = BLOCK_SIZE(HEAD(bp));
	SET(HEAD(bp), PACK(size,0));
	SET(TAIL(bp), PACK(size,0));
	coalesce(bp);
}

void *my_malloc(size_t size)
{
    size_t aligned_size;
    char * bp;

    if (size == 0)
        return NULL;

    aligned_size = DWORD * ((size + DWORD - 1)/ DWORD + 1);

    if ((bp = find_segregated_best_fit(aligned_size)) != NULL) {
    	remove_free_block(bp);
        set_allocated(bp, aligned_size);
        return bp;
    }

    //No fitted block found.
    size_t extendsize = aligned_size >= SBRK_SIZE ? aligned_size : SBRK_SIZE;
    if ((bp = extend_heap(extendsize)) == NULL)
    {
        return NULL;
    }
    set_allocated(bp, aligned_size);
    return bp;
}

void *my_realloc(void *ptr, size_t size)
{
	//realloc(ptr, 0) = free(ptr)
	if(size == 0){
		my_free(ptr);
		return NULL;
	}
	//realloc(NULL, size) = malloc(size)
	if (ptr == NULL)
		return (my_malloc(size));

	size_t aligned_size;
	aligned_size = DWORD * ((size + DWORD - 1)/ DWORD + 1);

	if(BLOCK_SIZE(HEAD(ptr)) >= aligned_size)
	{
		set_allocated(ptr, aligned_size);
		return ptr;	
	}

	if(!(GET(HEAD(NEXT_BLOCK(ptr))) & 1))
	{
		size_t merge_size = BLOCK_SIZE(HEAD(ptr)) + BLOCK_SIZE(HEAD(NEXT_BLOCK(ptr)));
		if(merge_size - 4 * WORD >= aligned_size - 2 * WORD)
		{
			remove_free_block(NEXT_BLOCK(ptr));
			SET(HEAD(ptr), PACK(merge_size, 1));
			set_allocated(ptr,aligned_size);
			return ptr;			
		}
	}
	void *oldptr = ptr;
	void *newptr;
	size_t copySize;

	newptr = my_malloc(size);
	if (newptr == NULL)
		return NULL;

	/* Copy the old data. */
	copySize = BLOCK_SIZE(HEAD(oldptr)) - 2 * WORD;
	if (size < copySize)
		copySize = size;
	memcpy(newptr, oldptr, copySize);
	my_free(oldptr);
	return newptr;
}