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

  if(block->next != NULL && SamePage(block, block->next))
    return (void*)(block->next) - (void*)block - sizeof(*block);

  void* endPage = BASEADDR(block) + PAGESIZE;
  return endPage - (void*)block - sizeof(*block);
}

void*
kma_malloc(kma_size_t size)
{
  if(size > PAGESIZE - sizeof(block_t) - sizeof(kma_page_t*))
    return NULL;
  if(firstPage == NULL)
  {
    firstPage = get_page();
    *((kma_page_t**)firstPage->ptr) = firstPage;
    block_t* head = (block_t*)(firstPage->ptr + sizeof(kma_page_t*));
    head->prev = NULL;
    head->next = NULL;
    head->used = FALSE;
  }


  block_t* block = (block_t*)(firstPage->ptr + sizeof(kma_page_t*));
  while(block->used || CalcBlockSize(block) < size)
  {
    if(block->next == NULL)
    {
      kma_page_t* nextPage = get_page();
      *((kma_page_t**)nextPage->ptr) = nextPage;
      block_t* pageHead = (block_t*)(nextPage->ptr + sizeof(kma_page_t*));
      block->next = pageHead;
      pageHead->prev = block;
      pageHead->next = NULL;
      pageHead->used = FALSE;
    }
    block = block->next;
  }
  block->used = TRUE;

  block_t* newNext = (block_t*)((void*)block + sizeof(*block) + size);

  int availableSpace;
  if(block->next != NULL && SamePage(block, block->next))
    availableSpace = (void*)(block->next) - (void*)newNext;
  else
    availableSpace = BASEADDR(block) + PAGESIZE - (void*)newNext;
  
  if(availableSpace > sizeof(block_t))
  {
    newNext->prev = block;
    newNext->next = block->next;
    newNext->used = FALSE;
    block->next = newNext;
    if(newNext->next != NULL)
      newNext->next->prev = newNext;
  }


  return (void*)block + sizeof(*block);
}

void
kma_free(void* ptr, kma_size_t size)
{
  if(ptr == NULL)
    return;

  block_t* curBlock = (block_t*)(ptr - sizeof(block_t));

  if((curBlock->prev != NULL) && 
     SamePage(curBlock, curBlock->prev) && 
     (!curBlock->prev->used))

  {

    if((curBlock->next != NULL) &&
       SamePage(curBlock, curBlock->next) &&
       (!curBlock->next->used))

    {
      curBlock->prev->next = curBlock->next->next;
      if(curBlock->next->next != NULL)
        curBlock->next->next->prev = curBlock->prev;
    }
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
    {
      curBlock->next = curBlock->next->next;
      if(curBlock->next != NULL)
        curBlock->next->prev = curBlock;
    }
  }

  // There will always be a block_t struct at the beginning of any
  // page.
  block_t* base = (block_t*)(BASEADDR(curBlock) + sizeof(kma_page_t*));
  
  if(CalcBlockSize(base) >= PAGESIZE - sizeof(*base) - sizeof(kma_page_t*))
  {
    if(base == (block_t*)(firstPage->ptr + sizeof(kma_page_t*)))
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
