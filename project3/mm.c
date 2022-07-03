/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your information in the following struct.
 ********************************************************/
team_t team = {
    /* Your student ID */
    "20181654",
    /* Your full name*/
    "SangHun Oh",
    /* Your email address */
    "ohsh0925@sogang.ac.kr",
};

//#define SUBMIT

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)


#define WSIZE 4 /* Word and header/footer size (bytes) */
#define DSIZE 8 /* Double word size (bytes) */
#define CHUNKSIZE (1<<12) /* Extend heap by this amount (bytes) */

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

#define GET_PREV_FREE_BLKP(bp) ((char *)(bp))
#define GET_NEXT_FREE_BLKP(bp) ((char *)(bp) + 4)

#define PREV_FREE_BLK(bp) (*(char**)(bp))
#define NEXT_FREE_BLK(bp) (*(char**)(GET_NEXT_FREE_BLKP(bp)))

#define SET_BLKP(p, ptr) (*(unsigned int*)(p) = (unsigned int)(ptr))

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* always points to the prologue block, which is an 8-byte allocated block consisting of only a header and a footer */
static char *heap_listp = NULL;
static char *first_freeblk = NULL;

static void *coalesce(void *);
static void *extend_heap(size_t);
static void place(void *, size_t);
static void *find_fit(size_t);
static void insert_freeblk(void *);
static void remove_freeblk(void *);


#ifdef SUBMIT
static void mm_check();

static void checkblock(void *bp)
{
	if ((unsigned int )bp % DSIZE)
		printf("Error: %p is not doubleword aligned\n", bp);
	if (GET_SIZE(HDRP(bp)) != GET_SIZE(FTRP(bp)) || GET_ALLOC(HDRP(bp)) != GET_ALLOC(FTRP(bp)))
		printf("Error: header does not match footer\n");
}

static void mm_check()
{
	void *bp;

	printf("heap listp: %p\n", heap_listp);

	if (GET_SIZE(HDRP(heap_listp)) != DSIZE || !GET_ALLOC(HDRP(heap_listp)))
		printf("Bad prologue header\n");
	checkblock(heap_listp);

	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		checkblock(bp);
	}

	if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp)))
		printf("Bad epilogue header\n");
}
#endif

static void insert_freeblk(void *bp)
{
	SET_BLKP(GET_NEXT_FREE_BLKP(bp), first_freeblk);
	SET_BLKP(GET_PREV_FREE_BLKP(first_freeblk), bp);
	SET_BLKP(GET_PREV_FREE_BLKP(bp), NULL);
	first_freeblk = bp;
}

static void remove_freeblk(void *bp)
{
	if (!PREV_FREE_BLK(bp)) {
		first_freeblk = NEXT_FREE_BLK(bp);	
//		SET_BLKP(GET_PREV_FREE_BLKP(NEXT_FREE_BLK(bp)), PREV_FREE_BLK(bp));
		SET_BLKP(GET_PREV_FREE_BLKP(NEXT_FREE_BLK(bp)), NULL);
	}
	else {
		SET_BLKP(GET_NEXT_FREE_BLKP(PREV_FREE_BLK(bp)), NEXT_FREE_BLK(bp));
		SET_BLKP(GET_PREV_FREE_BLKP(NEXT_FREE_BLK(bp)), PREV_FREE_BLK(bp));
	}
	return;
}

static void *coalesce(void* bp)
{
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));

	if (prev_alloc && !next_alloc) {	/* Case 2 */
		remove_freeblk(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	else if (!prev_alloc && next_alloc) {	/* Case 3 */
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		remove_freeblk(PREV_BLKP(bp));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	else if (!prev_alloc && !next_alloc) {	/* Case 4 */
		remove_freeblk(PREV_BLKP(bp));
		remove_freeblk(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(NEXT_BLKP(bp))) + GET_SIZE(HDRP(PREV_BLKP(bp)));
		bp = PREV_BLKP(bp);
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	insert_freeblk(bp);

	return bp;
}

static void *extend_heap(size_t words)
{
	char *bp;
	size_t size;

	/* Allocate an even number of words to maintain alignment */
	size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
	size = ALIGN(words) * WSIZE;
	if ((long)(bp = mem_sbrk(size)) == -1)
		return NULL;

	/* Initialize free block header/footer and the epilogue header */
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
/*
	if (!epilogue_header || !PREV_FREE_BLK(epilogue_header) || first_freeblk == epilogue_header) {
		first_freeblk = epilogue_header = NEXT_BLKP(bp);
	}
	else {
		prev = PREV_FREE_BLK(epilogue_header);
		epilogue_header = NEXT_BLKP(bp);
	
		SET_BLKP(GET_NEXT_FREE_BLKP(prev), epilogue_header);
		SET_BLKP(GET_NEXT_FREE_BLKP(bp), epilogue_header);
	}
//		first_freeblk = epilogue_header = NEXT_BLKP(bp);
*/	

	/* Coalesce if the previous block was free */
	return coalesce(bp);
}

static void place(void *bp, size_t asize)
{
	size_t csize = GET_SIZE(HDRP(bp));
	size_t frag = csize - asize;

		remove_freeblk(bp);
	if (frag <= 2 * DSIZE) {
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}
	else {
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		PUT(HDRP(NEXT_BLKP(bp)), PACK(frag, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(frag, 0));
		coalesce(NEXT_BLKP(bp));
	}
}

static void *find_fit(size_t asize) 
{
	void *bp;
	for (bp = first_freeblk; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE_BLK(bp)) {
		if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
			return bp;
		}
	}

	return NULL;
}


/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
	heap_listp = NULL;
	first_freeblk = NULL;

	if ((heap_listp = mem_sbrk(8 * WSIZE)) == (void *) - 1)
		return -1;
	PUT(heap_listp, 0);								/* Alignment padding */
	PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));	/* Prologue header */
	PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));	/* Prologue footer */
	PUT(heap_listp + (3 * WSIZE), PACK(0, 1));		/* Epilogue header */
	first_freeblk = heap_listp + (2 * WSIZE);
	
	/* Extend the empty heap with a free block of CHUNKISZE byte */
	if (extend_heap(4) == NULL) 
		return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
#ifdef SUBMIT
	checkheap();
#endif

	size_t asize;
	size_t extendsize;
	char *bp;

	/* Ignore spurious requests */
	if (size == 0)
		return NULL;

	/* Adjust block size to include overhead and alignment reqs */
	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

	/* Search the free list for a fit */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	/* No fit found. Get more memory and place the block */
	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;
	place(bp, asize);
	
	return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
	
	size_t size = GET_SIZE(HDRP(bp));
#ifdef SUBMIT
	checkheap();
#endif

	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
	size_t oldsize;
	size_t new_asize;

	if (!ptr) {		/* if ptr is NULL, same as mm_malloc(size) */
		return mm_malloc(size);
	}
	
	if (!size) {	/* if size is 0, same as mm_free() */
		mm_free(ptr);
		return NULL;
	}

	/* ptr has not been returned by mm_malloc or mm_realloc, return NULL */
	if (!GET_ALLOC(HDRP(ptr)) || !GET_ALLOC(FTRP(ptr))) {	
		return NULL;
	}

	if (size <= DSIZE)	/* if size is smaller than double word size */
		new_asize = 2 * DSIZE;
	else
		new_asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

	oldsize = GET_SIZE(HDRP(ptr));
	if (new_asize <= oldsize)
		return ptr;
	else if (!GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && \
			new_asize <= GET_SIZE(HDRP(NEXT_BLKP(ptr))) +  oldsize) {
		size_t oldnextsize = GET_SIZE(HDRP(NEXT_BLKP(ptr)));

		remove_freeblk(NEXT_BLKP(ptr));

		PUT(FTRP(ptr), PACK(0, 0));
		PUT(HDRP(ptr), PACK(oldsize + oldnextsize, 1));
		PUT(FTRP(ptr), PACK(oldsize + oldnextsize, 1));

//		PUT(HDRP(NEXT_BLKP(ptr)), PACK(oldsize + oldnextsize - new_asize, 0));
//		PUT(FTRP(NEXT_BLKP(ptr)), PACK(oldsize + oldnextsize - new_asize, 0));
//		coalesce(NEXT_BLKP(ptr));

		return ptr;
	}
	else {
		void *newptr;

		newptr = mm_malloc(new_asize);
		if (newptr == NULL)
		  return NULL;
		place(newptr, new_asize);
		memcpy(newptr, ptr, new_asize);
		mm_free(ptr);

		return newptr;
	}
}
