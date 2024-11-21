/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* size of data? */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
  return 0;
}

void* find_fit(size_t size) {
  size_t *header = mem_heap_lo();
  while (header < (size_t*) mem_heap_hi()) {
    // if this space isn't allocated and size is smaller or equal to open space
    if (!(*header & 1) && *header >= size) {
      return header;
    }
    //move header forward the size of this space
    header = (size_t*) header + (*header & ~1L);
  }

  return NULL;
}

void* coallesce(size_t *bp) {

  size_t *next, *prev;
  int next_alloc, prev_alloc;

  //deal with edge cases:
  if ((size_t *) mem_heap_lo() < bp) {
    prev = bp - (*(size_t *)bp - SIZE_T_SIZE & ~1L);
  } else {
    prev_alloc = 1;
  }

  if ((size_t *) mem_heap_hi() > bp) {
    next = bp - (*(size_t *)bp - SIZE_T_SIZE & ~1L);
  } else {
    next_alloc = 1;
  }

  return next;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void* mm_malloc(size_t size)
{
  int blksize = ALIGN(size + SIZE_T_SIZE);
  size_t *header = find_fit(blksize);
  if (header) {
    //split block if possible
    *(size_t*)((char*)header + blksize) = *header - blksize;
  } else {
    header = mem_sbrk(blksize);
  }

  *header = blksize | 1;
  return (char*) header + SIZE_T_SIZE;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
  size_t *header = (size_t*) ptr - SIZE_T_SIZE;
  *header = *header & ~1L;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void* mm_realloc(void *ptr, size_t size)
{
  size_t *header = (size_t *)((char *)ptr - SIZE_T_SIZE);
  size_t oldsize = *header & ~1L,
         newsize = ALIGN(size + SIZE_T_SIZE);
  void *newptr;

  if (newsize <= oldsize) {
    return ptr;
  } else {
    newptr = mm_malloc(size);
    memcpy(newptr, ptr, oldsize - SIZE_T_SIZE);
    mm_free(ptr);
    return newptr;
  }
}
