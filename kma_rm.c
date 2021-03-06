/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the resource map algorithm
 *    Author: Stefan Birrer
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    Revision 1.2  2009/10/31 21:28:52  jot836
 *    This is the current version of KMA project 3.
 *    It includes:
 *    - the most up-to-date handout (F'09)
 *    - updated skeleton including
 *        file-driven test harness,
 *        trace generator script,
 *        support for evaluating efficiency of algorithm (wasted memory),
 *        gnuplot support for plotting allocation and waste,
 *        set of traces for all students to use (including a makefile and README of the settings),
 *    - different version of the testsuite for use on the submission site, including:
 *        scoreboard Python scripts, which posts the top 5 scores on the course webpage
 *
 *    Revision 1.1  2005/10/24 16:07:09  sbirrer
 *    - skeleton
 *
 *    Revision 1.2  2004/11/05 15:45:56  sbirrer
 *    - added size as a parameter to kma_free
 *
 *    Revision 1.1  2004/11/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *
 ***************************************************************************/
#ifdef KMA_RM
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>

/************Private include**********************************************/
#include "kma_page.h"
#include "kma.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/*
Each block of memory is specified by a blockT struct.
These structs are managed in a linked list.
Each block of allocated memory begings w/ a corresponding blockT object.*/  

typedef struct blockT
{
  struct blockT* prev;
  struct blockT* next;
  bool used;
} block_t;

/************Global Variables*********************************************/
static kma_page_t* firstPage = NULL;
/************Function Prototypes******************************************/

/***********************************************************************
 *  Title: Calculate Block Size
 * ---------------------------------------------------------------------
 *    Purpose: Calculates the amount of usable memory in a block
 *    Input: a block
 *    Output: The amount of usable memory in the given block
 ***********************************************************************/
int CalcBlockSize(block_t*);

/***********************************************************************
 *  Title: Same Page
 * ---------------------------------------------------------------------
 *    Purpose: Determines whether two blocks are in the same page
 *    Input: two blocks
 *    Output: TRUE if the blocks are in the same page, FALSE otherwise
 ***********************************************************************/
bool SamePage(block_t*, block_t*);

/************External Declaration*****************************************/

/**************Implementation***********************************************/

inline bool SamePage(block_t* b1, block_t* b2)
{
  return BASEADDR(b1) == BASEADDR(b2);
}

int CalcBlockSize(block_t* block)
{
  if(block == NULL)
    return -1;

 /*Useable memory = start address of next block - start address of current block-size of header*/
  if(block->next != NULL && SamePage(block, block->next))
    return (int)((size_t)(block->next) - (size_t)block - sizeof(*block));

/*In the case where you are the last block on the page...*/
/*Useable memomory= address of end of page - start address of current block -
size of header */

  void* endPage = (void*)((size_t)BASEADDR(block) + PAGESIZE);
  return (int)((size_t)endPage - (size_t)block - sizeof(*block));
}

void*
kma_malloc(kma_size_t size)
{
/*This function will scan the LL, looking for the first available free block.
(first fit). If no free block exsists, it will allocate a new page if necessary. */


/* Each page begins w/ a pointer to a corresponding kma_page_t struct*/
/* Each block begins w/ a corresponding block_t struct*/
/* Therefore the total useable memory on any page = size of page - size of (block_t) - sizeof(kma_page_t*)*/ 

/*A request for more memory than can be supplied by a single page is invalid*/
  if(size > PAGESIZE - sizeof(block_t) - sizeof(kma_page_t*))
    return NULL;

/*If this is the first page we are allocating, we must initalize a first page and a LL to keep track of all subsequent blocks.*/
  if(firstPage == NULL)
  {
   /*get the first page*/
    firstPage = get_page();
   /*set a ptr to the corresponding kma_page_t struct at the top of the page*/
    *((kma_page_t**)firstPage->ptr) = firstPage;
   /*create a new block which also provides an entry into the LL*/
    block_t* head = (block_t*)((size_t)firstPage->ptr + sizeof(kma_page_t*));
    head->prev = NULL;
    head->next = NULL;
    head->used = FALSE;
  }

/*Search the LL for a free block*/
  block_t* block = (block_t*)((size_t)firstPage->ptr + sizeof(kma_page_t*));
  while(block->used || CalcBlockSize(block) < size)
  {
    /*If you reach the end of the LL, there is no free block to be found*/
    /*In this case you must allocate a new page.*/
    if(block->next == NULL)
    {
      kma_page_t* nextPage = get_page();
      *((kma_page_t**)nextPage->ptr) = nextPage;
      block_t* pageHead = (block_t*)((size_t)nextPage->ptr + sizeof(kma_page_t*));
      block->next = pageHead;
      pageHead->prev = block;
      pageHead->next = NULL;
      pageHead->used = FALSE;
    }
    block = block->next;
  }

  /*Once you escape the loop, you are gauranteed to have found a free block*/
  /*If there wasn't a free block initally it was created when a new page was allocated*/

/*fill the free block*/
  block->used = TRUE;

/*the block which starts after the block you are filling begins at address, 
address= start address of current block+size of header+ size of memory request*/
  block_t* newNext = (block_t*)((size_t)block + sizeof(*block) + size);

/*Make sure you have enough room for your header.*/
  int availableSpace;
  if(block->next != NULL && SamePage(block, block->next))
    availableSpace = (int)((size_t)(block->next) - (size_t)newNext);
  else
    availableSpace = (int)((size_t)BASEADDR(block) + PAGESIZE - (size_t)newNext);
  
  if(availableSpace > sizeof(block_t))
  {
    newNext->prev = block;
    newNext->next = block->next;
    newNext->used = FALSE;
    block->next = newNext;
    if(newNext->next != NULL)
      newNext->next->prev = newNext;
  }


  return (void*)((size_t)block + sizeof(*block));
}

void
kma_free(void* ptr, kma_size_t size)
{

/*This function frees a block of memory and performs coalescing if necessary*/

/*There are 4 basic cases for coalescing:
1: Free-Free: the blocks before and after the current block are free
	-Coalesece all 3 blocks into 1 block
2: Free-Used: the block before the current block is free, after is used
	-Coalesce the top 2 blocks into 1 block
3: Used-Free: the block before the current block is used, after is free
	-Coalesce the bottom 2 blocks into 1 block
4: Used-Used: the block before and after the current block are used.
	-Cannot Coalesce
*/

/*We cannot coalesce between pages, so we must test for these cases as well"
1: Block at top of page: current block can only coalesce with block after it.

2: Block at bottom of page: current block can only coalesce with block before it.*/ 

  if(ptr == NULL)
    return;

  /*set curBlock to point to the top of the block*/
  /*top of block = start of useable memory - size of header*/

  block_t* curBlock = (block_t*)((size_t)ptr - sizeof(block_t));


  if((curBlock->prev != NULL) && 
     SamePage(curBlock, curBlock->prev) && 
     (!curBlock->prev->used))

  {

    if((curBlock->next != NULL) &&
       SamePage(curBlock, curBlock->next) &&
       (!curBlock->next->used))

  //Free-Free Case
    {
      curBlock->prev->next = curBlock->next->next;
      if(curBlock->next->next != NULL)
        curBlock->next->next->prev = curBlock->prev;
    }
  //Free-Used Case
    else
    {
      curBlock->prev->next = curBlock->next;
      if(curBlock->next != NULL)
        curBlock->next->prev = curBlock->prev;
    }
  }
  else
  {
    curBlock->used = FALSE;
    if((curBlock->next != NULL) && 
       SamePage(curBlock, curBlock->next) &&
       (!curBlock->next->used))
   //Used-Free Case
    {
      curBlock->next = curBlock->next->next;
      if(curBlock->next != NULL)
        curBlock->next->prev = curBlock;
    }
  }

   //Used-Used Case: cannot coalesce 
 
  // There will always be a block_t struct at the beginning of any
  // page.
  block_t* base = (block_t*)((size_t)BASEADDR(curBlock) + sizeof(kma_page_t*));
  
  if(CalcBlockSize(base) >= PAGESIZE - sizeof(*base) - sizeof(kma_page_t*))
  {
    if(base == (block_t*)((size_t)firstPage->ptr + sizeof(kma_page_t*)))
    {
      if(base->next == NULL)
        firstPage = NULL;
      else
        firstPage = *((kma_page_t**)BASEADDR(base->next));
    }
    
    if(base->prev != NULL)
      base->prev->next = base->next;

    if(base->next != NULL)
      base->next->prev = base->prev;
    
    free_page(*((kma_page_t**)BASEADDR(curBlock)));
  }
}

#endif // KMA_RM
