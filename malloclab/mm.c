/*
 * mm.c - segregated free list implentated allocation (2021/11/23 19:40)
 * Score: 44.0 (correctness) + 52.7 (performance) = 96.7
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

// Sizes
#define WSIZE 4 // word size
#define DSIZE 8 // double word size
#define MSIZE 16 // minimum free block size = 24
#define MPAYLOAD 8 // minimum payload size
#define CHUNKSIZE 1 << 6 // default size for expanding the heap

// MACROs for mm.c
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define MIN(x ,y) ((x) < (y)? (x) : (y))

#define PACK(size, alloc)  ((size) | (alloc))

#define GET(p)       (*(unsigned int *)(p))
#define PUT(p, val)  (*(unsigned int *)(p) = (val))
#define SET(p, bp)   (*(unsigned int *)(p) = (unsigned int)(bp))

#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp)        ((char *)(bp) - WSIZE)
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define PP(bp)        ((char *)(bp) + WSIZE)
#define SP(bp)        ((char *)(bp))

#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(((char*)(bp) - WSIZE)))
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))
#define SUCC(bp)   (*(char **)((char *)(bp))) 
#define PRED(bp)   (*(char **)((char *)(bp) + WSIZE))

// global variables
static char *heap_listp = 0; // pointer to the 1st block

// segregated free list. seglistk means k-th seglist. 
// from 0~7, 32B interval
static void *seglist0; // 1~31
static void *seglist1; // 32~63
static void *seglist2; // 64~95
static void *seglist3; // 96~127
static void *seglist4; // 128~159
static void *seglist5; // 160~191
static void *seglist6; // 192~223
static void *seglist7; // 224~255
// from 8~24, power of 2 interval
static void *seglist8; // 256B ~
static void *seglist9; // 512B ~
static void *seglist10; // 1KB ~
static void *seglist11; // 2KB ~
static void *seglist12; // 4KB ~
static void *seglist13; 
static void *seglist14;
static void *seglist15;
static void *seglist16;
static void *seglist17;
static void *seglist18;
static void *seglist19;
static void *seglist20; // 1MB ~
static void *seglist21;
static void *seglist22;
static void *seglist23; // 8MB ~ inf

// helper functions
static void *extend_heap(size_t size);
static void *realloc_place(void *bp, size_t adjsize);
static void *place(void *bp, size_t adjsize);
static void *find_fit(size_t adjsize);
static void *coalesce(void *bp);
static void remove_node(void *bp);
static void add_node(void *bp);
static int find_index(size_t size);
static void seglist_init(void);
static void **find_list(int i);

/*
 * mm_init - creates a heap with an intial free block
 */
int mm_init(void)
{
    //printf("\n Entering Init: \n");
    // intialize the list first. 
    seglist_init();

    /* create a free block. sbrk returns the pointer to the original top of the heap */
    if ((heap_listp = mem_sbrk(2*DSIZE)) == (void *)-1) return -1;
    PUT(heap_listp, 0);                             /* Alignment Padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));    /* Prologue header */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));    /* Prologue footer */
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));        /* Epilogue header */

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    //printf("Init: extending the heap.\n");
    if (extend_heap(CHUNKSIZE) == NULL) return -1; 
    return 0;
}

/*
 * mm_malloc - Allocate a block using implicit allocations
 */
void *mm_malloc(size_t size)
{
    //printf("\n Entering Malloc: \n");
    //printf("Malloc: now allocating size (%d)\n", size);
    size_t adjsize;
    char *bp;

    if (heap_listp == 0) mm_init(); // initialize the heap by calling mm_init
    if (size == 0) return NULL; // ignore 0B requests
    if (size <= MPAYLOAD) adjsize = MSIZE; // 8B is the minimum payload, and block is 16B total. 
    else adjsize = ALIGN(DSIZE + size); // if larger than 8B, then just align the size + 8B. 
    //printf("Malloc: adjusted size is %d\n", adjsize); 

    // If no fit found, get more memory and place the block
    if ((bp = find_fit(adjsize)) == NULL) 
    {
        //printf("Malloc: extending the heap.\n");
        if ((bp = extend_heap(adjsize)) == NULL) return NULL; // cannot extend heap
    }
    //printf("Malloc: find_fit suggestes %p\n", bp); 
    return place(bp, adjsize); // allocate by placing the block
    //printf("Malloc: allocated at %p\n", bp); 
    //return bp;
}

/*
 * mm_free - Freeing a block
 */
void mm_free(void *ptr)
{
    //printf("\n Entering Free: \n");
    if (ptr == 0) return; // do nothing

    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0)); // set header
    PUT(FTRP(ptr), PACK(size, 0)); // and footer bits to zero
    
    add_node(ptr); // after freeing, add the block to the appropriate seglist
    coalesce(ptr); // if necessary, coalesce it. 
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    //printf("\n Entering Realloc: \n");
    void *oldptr = ptr;
    void *newptr;
    size_t newsize;
    size_t oldsize;
    
    // when size is 0 then same as ptr. 
    if (size == 0) 
    {
        //printf("Realloc: size = 0, so free\n");
        mm_free(ptr);
        return NULL;
    }
    // when ptr is NULL, then same with mm_malloc
    else if (ptr == NULL) 
    {
        //printf("Realloc: ptr = 0, so mm_malloc\n");
        return mm_malloc(size);
    }
    // when ptr is not NULL, then use my big brain :)
    oldsize = GET_SIZE(HDRP(ptr)); // originally allocated size
    if (size <= MPAYLOAD) newsize = MSIZE;
    else newsize = ALIGN(size + DSIZE); // align the new size. 

    // newsize is same or smaller than the original
    if (oldsize == newsize) return oldptr; // keep the block still. 
    if (oldsize > newsize) // when newsize is smaller, re-place the block and split if necessary.
    {
        //printf("Realloc: newsize is smaller\n");
        return realloc_place(oldptr, newsize);
    }
    else // when the newsize > oldsize
    {
        //printf("Realloc: newsize is larger\n");
        // if any next block is empty, try using that space first!
        void *next = NEXT_BLKP(oldptr);
        if (GET_SIZE(HDRP(next)) == 0) // check whether next is an epilogue 
        {
            // if next block is an epilogue, then extend the heap.
            size_t extendsize = MAX((newsize - oldsize), 32);
            if ((extend_heap(extendsize)) == NULL) return NULL;
            oldsize += extendsize;
            remove_node(next);
            PUT(HDRP(oldptr), PACK(oldsize, 1));
            PUT(FTRP(oldptr), PACK(oldsize, 1));
            return realloc_place(oldptr, newsize);
        }
        if (GET_ALLOC(HDRP(next)) == 0)
        {
            // if next block is a free block, combine it and check whether sum of size is sufficient.  
            //printf("Realloc: next block is available\n");
            size_t freesize = GET_SIZE(HDRP(next));
            oldsize += freesize;
            // if the size + next size <= newsize, then use the next block!
            if (newsize <= oldsize)
            {
                //printf("Realloc: using the next block\n");
                remove_node(next); // first capture the free list. 
                PUT(HDRP(oldptr), PACK(oldsize, 1));
                PUT(FTRP(oldptr), PACK(oldsize, 1));
                return realloc_place(oldptr, newsize);
            }
        }
    }
    // when all cases fail, then allocate to a whole new place.
    if ((newptr = mm_malloc(size)) == NULL) return NULL;
    // copy the data as data is located at newptr.
    memcpy(newptr, oldptr, oldsize);
    // free the old block. (Adios!)
    mm_free(oldptr);
    return newptr;
}

//////////////////////////////* HELPER FUNCTIONS *///////////////////////////////
/* find_fit - Find any free blocks to fit memory to be allocated  */
static void *find_fit(size_t adjsize)
{
    //printf("\n Entering Find Fit: \n");
    int seg_index = find_index(adjsize); // first find the appropriate size. 
    int i = seg_index;
    //printf("Find Fit: size %d belongs to seglist[%d]\n", adjsize, i); 
    void **listp = find_list(seg_index);
    void *bp = *listp;
    // first, find the appropriate size in the free list. 
    while ((bp != NULL) && (adjsize > GET_SIZE(HDRP(bp))))
    {
        bp = SUCC(bp);
    }
    if (bp != NULL) return bp; // if found a appropriate size. 
    // if not, move to the larger sized segregated free list. 
    //printf("Find Fit: not available in the seglist[%d]\n", seg_index);
    while (i < 23)
    {
        i++; // traverse from seg_index + 1 to 23. 
        //printf("Find Fit: Next index is now %d\n", i);   
        listp = find_list(i);
        if (*listp != NULL) 
        {
            bp = *listp;
            //printf("Find Fit: block size %d bytes in seglist[%d] \n", GET_SIZE(HDRP(bp)), i);
            return bp; // just get the smallest available. 
        }
    }
    // when no such appropriate block in all segregated free list:
    //printf("Find Fit: failed to find any blocks, prepare for extend heap.\n");
    return NULL;
}

/* realloc_place & place - Place the block to allocate, split if necessary */
static void *realloc_place(void *bp, size_t adjsize)
{
    //printf("\n Entering Realloc Place: \n");
    void *new_bp;
    //printf("R_Place: size for allocation is %d bytes\n", adjsize);
    size_t csize = GET_SIZE(HDRP(bp)); // csize = size of the free block
    //printf("R_Place: size of available block is %d bytes\n", csize);
    if ((csize - adjsize) >= 32) // split the block if remainder >= 24B
    {
        if (adjsize >= 32)
        {
            PUT(HDRP(bp), PACK(csize - adjsize, 0)); 
            PUT(FTRP(bp), PACK(csize - adjsize, 0));
            add_node(bp);
            new_bp = NEXT_BLKP(bp);
            PUT(HDRP(new_bp), PACK(adjsize, 1));
            PUT(FTRP(new_bp), PACK(adjsize, 1));
            return new_bp;     
        }
        //printf("R_Place: splitting the block\n");
        PUT(HDRP(bp), PACK(adjsize, 1)); 
        PUT(FTRP(bp), PACK(adjsize, 1));
        // split the block
        new_bp = NEXT_BLKP(bp);
        PUT(HDRP(new_bp), PACK(csize - adjsize, 0)); 
        PUT(FTRP(new_bp), PACK(csize - adjsize, 0));
        // add the split free block to the seglist. 
        add_node(new_bp);
        return bp;
    }
    else
    {
        //printf("R_Place: no split needed.\n");
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(HDRP(bp), PACK(csize, 1));
        return bp;
    }
}

/* place - after find_fit, allocate the free block. */
static void* place(void *bp, size_t adjsize)
{
    //printf("\n Entering Place: \n");
    void *new_bp;
    //printf("Place: size for allocation is %d bytes\n", adjsize);
    size_t csize = GET_SIZE(FTRP(bp)); // csize = size of the free block
    //printf("Place: size of available block is %d bytes\n", csize);

    if ((csize - adjsize) >= MSIZE) // split the block if remainder >= 24B
    {
        remove_node(bp); // first, remove the node from its segregated free list. 
        // allocating at the rear side of the free block can inprove the utilization as coalescing is more likely.
        if (adjsize >= 32)
        {
            PUT(HDRP(bp), PACK(csize - adjsize, 0)); 
            PUT(FTRP(bp), PACK(csize - adjsize, 0));
            add_node(bp);
            new_bp = NEXT_BLKP(bp);
            PUT(HDRP(new_bp), PACK(adjsize, 1));
            PUT(FTRP(new_bp), PACK(adjsize, 1));
            return new_bp;
            
        }
        PUT(HDRP(bp), PACK(adjsize, 1)); 
        PUT(FTRP(bp), PACK(adjsize, 1));

        new_bp = NEXT_BLKP(bp);
        PUT(HDRP(new_bp), PACK(csize - adjsize, 0)); 
        PUT(FTRP(new_bp), PACK(csize - adjsize, 0));
        //printf("Place: adding split remainder with size (%d) at %p\n", GET_SIZE(HDRP(new_bp)), new_bp);
        add_node(new_bp);
        return bp;
    }
    else
    {
        //printf("Place: no split needed.\n");
        remove_node(bp);
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(HDRP(bp), PACK(csize, 1));
        return bp;
    }
}

/* extend_heap - extend the heap size by increasing brk pointer value with sbrk */
static void *extend_heap(size_t size)
{
    //printf("\n Entering Extend Heap: \n");
    void *bp;
    size_t adjsize = ALIGN(size); // make sure to align in DSIZE
    if ((bp = mem_sbrk(adjsize)) == (void*)-1) return NULL; // failed extending the heap. 
    //printf("Extend Heap: extended %d bytes.\n", adjsize);
    PUT(HDRP(bp), PACK(adjsize, 0));
    PUT(FTRP(bp), PACK(adjsize, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    add_node(bp); // add extended area (a free block) in to a DLL

    // merge with the extend block with prev free block if any, and return merged bp.
    return coalesce(bp);
}

/* coalesce - merge the free adjacent blocks if any exists. */
static void *coalesce(void *bp)
{
    //printf("\n Entering Coalesce: \n");
    void *prev_blkp = PREV_BLKP(bp); // pointer to the previous block.
    size_t prev_alloc = ((prev_blkp == bp) ? 1 : GET_ALLOC(HDRP(prev_blkp)));
    // intially, when prev block is not there, PREV_BLKP returns the bp, which is stupid. 
    // must check whether the prev_alloc is not same with bp. 

    //printf("Coalesce: previous block is at %p\n", PREV_BLKP(bp));
    //printf("Coalesce: previous block size is %d bytes\n", GET_SIZE(HDRP(PREV_BLKP(bp))));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

    //printf("Coalesce: next block is at %p\n", NEXT_BLKP(bp));
    //printf("Coalesce: next block size is %d bytes\n", GET_SIZE(HDRP(NEXT_BLKP(bp))));
    size_t size = GET_SIZE(HDRP(bp));

    //printf("Coalesce: a block to be merged is at %p\n", bp);
    //printf("Coalesce: a block to be merged size is %d bytes \n", size);

    if (prev_alloc && next_alloc) 
    {
        //printf("Coalesce: no merging\n");
        return bp;
    } // both prev and next allocated, stop.
    else if (prev_alloc && !next_alloc) // next block free
    {
        //printf("Coalesce: merge with next block\n");
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // add the size, updating the block size
        //printf("Coalesce: merged size is %d bytes\n", size);
        remove_node(bp);
        remove_node(NEXT_BLKP(bp)); // connect the pointers of doubly linked free list
        
        PUT(HDRP(bp), PACK(size, 0)); // update header 
        PUT(FTRP(bp), PACK(size, 0)); // update footer

        add_node(bp); // now add the merged block
    }
    else if (!prev_alloc && next_alloc) // prev block free
    {
        //printf("Coalesce: merge with prev block\n");
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        //printf("Coalesce: merged size is %d bytes\n", size);
        remove_node(bp);
	    remove_node(PREV_BLKP(bp)); // connect the pointers of doubly linked free list
        
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp); // move the bp to the prev block's pointer

        add_node(bp);
    }
    else // both prev and next empty
    {
        //printf("Coalesce: merge with both block\n");
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); // add both sizes of prev and next
        //printf("Coalesce: merged size is %d bytes\n", size);
        remove_node(bp);
        remove_node(NEXT_BLKP(bp)); // connect the pointers of doubly linked free list
        remove_node(PREV_BLKP(bp)); // connect the pointers of doubly linked free list
        
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); 
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp); // move the bp to the prev block's pointer

        add_node(bp);
    }
    //printf("Coalesce: merged block is at %p\n", bp);
    return bp; // return the pointer of merged free block
}

/* remove_node - removes the node from the segregated free list */
static void remove_node(void *bp)
{
    //printf("\n Entering Remove Node: \n");
    size_t size = GET_SIZE(HDRP(bp)); // get size of the free block to be removed. 
    int seg_index = find_index(size); // find which seglist to put.
    void **listp = find_list(seg_index);
    //printf("Remove Node: removing a node from the %d list\n", seg_index);

    if ((SUCC(bp) == NULL) && (PRED(bp) == NULL)) // last single free block
    {
        *listp = NULL; // no free blocks now.
        return; 
    }
    if ((SUCC(bp) == NULL) && (PRED(bp) != NULL)) // bp is the tail node; no succ block
    {
        SET(SP(PRED(bp)), NULL);
        return;
    }
    if ((SUCC(bp) != NULL) && (PRED(bp) == NULL)) // bp is the head node; no pred block
    {
        SET(PP(SUCC(bp)), NULL);
        *listp = SUCC(bp);
        return;
    }
    else // otherwise
    {
        SET(SP(PRED(bp)), SUCC(bp)); // pred's succ is now bp's succ
        SET(PP(SUCC(bp)), PRED(bp)); // succ's pred is now bp's pred
    }
    return;
}

/* add_node - inserts a node into segregated free list corresponding to the size. */
static void add_node(void *bp)
{
    //printf("\n Entering Add Node: \n");
    size_t size = GET_SIZE(HDRP(bp));
    int seg_index = find_index(size); // find which seglist to put.
    void** listp = find_list(seg_index);
    //printf("Add Node: adding the node to the seglist[%d] \n", seg_index);
    void *walk = *listp;
    void *here = NULL;
    if (*listp == NULL) // nothing in the seglist's DLL
    {
        SET(SP(bp), NULL);
        SET(PP(bp), NULL); // Alone in the DLL...
        *listp = bp; // bp is the new head
        return;
    }
    if (*listp != NULL) // something is in the seglist's DLL
    {
        while (size > GET_SIZE(HDRP(walk)))
        {
            here = walk; // just before the walk that is larger than size
            walk = SUCC(walk); // traverse if walk's size is same smaller than the size. 
            if (walk == NULL) break; // reached the end. 
        }
    }
    // now add node to here and connect. note that walk = SUCC(here)
    if (here != NULL)
    {
        if (walk != NULL)
        {
            //printf("Add Node: Adding node between two nodes \n");
            SET(SP(here), bp);
            SET(PP(bp), here);
            SET(SP(bp), walk);
            SET(PP(walk), bp);
        }
        else // walk is NULl, largest of the DLL
        {
            //printf("Add Node: Adding node at the last \n");
            SET(SP(here), bp);
            SET(PP(bp), here);
            SET(SP(bp), NULL);
        }
    }
    else // smallest of the DLL
    {
        SET(PP(bp), NULL);
        SET(SP(bp), walk);
        SET(PP(walk), bp);
        *listp = bp; // bp is the new head
    }
    return;
}

/* find_index - get the proper index of segregated free list according to its size */
static int find_index(size_t size)
{
    // under 256B, seglist is divided into 32B interval
    int k = ((int)size) >> 5;
    if (k < 8) return k;

    // after 256B, seglist interval is powers of 2. 
    size_t search = size;
    int seg_index = 8;
    do {
        if (search >= 256)
        {
            search = search >> 1;
            seg_index++;
        }
        else
        {
            break;
        }
    } while (seg_index < 23); 
    
    return seg_index;
}

/* seglist_init - initialize the segregated free lists. */
static void seglist_init(void)
{
    void **p;
    int seg_index = 0;
    while (seg_index < 24)
    {
        p = find_list(seg_index);
        *p = NULL;
        seg_index++;
    }
}

/* find_list - find the corresponding seglist from find_index */
static void **find_list(int i)
{
    void** p;
    switch (i) {
    case 0:
        p = &seglist0;
        break;
    case 1:
        p = &seglist1;
        break;
    case 2:
        p = &seglist2;
        break;
    case 3:
        p = &seglist3;
        break;
    case 4:
        p = &seglist4;
        break;
    case 5:
        p = &seglist5;
        break;
    case 6:
        p = &seglist6;
        break;
    case 7:
        p = &seglist7;
        break;
    case 8:
        p = &seglist8;
        break;
    case 9:
        p = &seglist9;
        break;
    case 10:
        p = &seglist10;
        break;
    case 11:
        p = &seglist11;
        break;
    case 12:
        p = &seglist12;
        break;
    case 13:
        p = &seglist13;
        break;
    case 14:
        p = &seglist14;
        break;
    case 15:
        p = &seglist15;
        break;
    case 16:
        p = &seglist16;
        break;
    case 17:
        p = &seglist17;
        break;
    case 18:
        p = &seglist18;
        break;
    case 19:
        p = &seglist19;
        break;
    case 20:
        p = &seglist20;
        break;
    case 21:
        p = &seglist21;
        break;
    case 22:
        p = &seglist22;
        break;
    case 23:
        p = &seglist23;
        break;
    default:
        p = NULL;
    }
    return p;
}

