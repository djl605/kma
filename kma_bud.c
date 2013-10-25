/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the buddy algorithm
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
#ifdef KMA_BUD
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

  typedef struct
  {
    kma_page_t* thisPage;
    kma_page_t* nextPage;
  } page_header_t;

  typedef struct
  {
    int size;
    bool used;
  } block_header_t;

/************Global Variables*********************************************/
static kma_page_t* firstPage = NULL;
/************Function Prototypes******************************************/
inline bool TopOfPage(block_header_t*);
inline void InitPage(kma_page_t*);
inline block_header_t* NextPage(block_header_t*);
inline int NextPowerOfTwo(int);

block_header_t* Buddy(block_header_t*, int);
block_header_t* TraverseBlock(int, int, block_header_t*, bool);
block_header_t* FindAvailableBlock(int);
void* SplitBlock(block_header_t*, int);
/************External Declaration*****************************************/

/**************Implementation***********************************************/

inline bool TopOfPage(block_header_t* block)
{
  return (((size_t)block) % PAGESIZE) == 0;
}

inline int NextPowerOfTwo(int n)
{
  int i = 1;
  while(i < n)
    i <<= 1;

  return i;
}


block_header_t* Buddy(block_header_t* block, int size)
{
  if(size >= PAGESIZE)
    return NULL;
  unsigned shift = 0;
  while(size > 0)
  {
    size >>= 1;
    ++shift;
  }
  return (block_header_t*)((size_t)block ^ (1 << (shift - 1)));
}

inline block_header_t* NextPage(block_header_t* pageHead)
{
  kma_page_t* nextPage = ((page_header_t*)((size_t)pageHead + sizeof(*pageHead)))->nextPage;
  if(nextPage == NULL)
    return NULL;
  return (block_header_t*)(nextPage->ptr);
}

block_header_t* FindAvailableBlock(int needSize)
{
  block_header_t* page = (block_header_t*)(firstPage->ptr);

  //This should never happen, but just in case
  if(page == NULL)
    return NULL;
  
  //prevBlock so we can add to the linked list if we need to alloc a new page.
  block_header_t* prevPage;

  while(page != NULL)
  {
    prevPage = page;
    block_header_t* available = TraverseBlock(needSize, PAGESIZE, page, FALSE);
    if(available != NULL)
      return available;
    page = NextPage(page);
  }

  // We have searched through all of the pages and there are no available blocks that are big enough.
  // Allocate a new page.
  kma_page_t* newPage = get_page();
  InitPage(newPage);
  ((page_header_t*)((size_t)prevPage + sizeof(*prevPage)))->nextPage = newPage;
  return (block_header_t*)(newPage->ptr);
}


block_header_t* TraverseBlock(int needSize, int inspectingSize, block_header_t* head, bool checkBuddy)
{
  int effectiveSize = needSize;
  if(TopOfPage(head))
    effectiveSize += sizeof(page_header_t);
  effectiveSize = NextPowerOfTwo(effectiveSize);

  if(inspectingSize < effectiveSize)
    return NULL;

  // This should never happen, but just in case
  if(inspectingSize < head->size)
    return NULL;

  if(inspectingSize > head->size)
  {
    block_header_t* available = TraverseBlock(needSize, inspectingSize >> 1, head, TRUE);
    if(available != NULL)
      return available;

    // To prevent infinite loops
    if(!checkBuddy)
      return NULL;
    return TraverseBlock(needSize, inspectingSize, Buddy(head, inspectingSize), FALSE);
  }

  // inspectingSize == head->size
  if(!head->used)
    return head;
  return NULL;

}

void* SplitBlock(block_header_t* bigBlock, int newSize)
{
  int effectiveSize;
  bool top = TopOfPage(bigBlock);
  if(top)
    effectiveSize = NextPowerOfTwo(newSize + sizeof(page_header_t));
  else
    effectiveSize = NextPowerOfTwo(newSize);

  int blockSize = bigBlock->size;
  while(blockSize > effectiveSize)
  {
    blockSize >>= 1;
    block_header_t* buddy = Buddy(bigBlock, blockSize);
    buddy->size = blockSize;
    buddy->used = FALSE;
  }
  bigBlock->size = blockSize;
  bigBlock->used = TRUE;

  return (void*)((size_t)bigBlock + sizeof(block_header_t) + (top ? sizeof(page_header_t) : 0));
}

inline void InitPage(kma_page_t* page)
{
  block_header_t* block = (block_header_t*)(page->ptr);
  block->size = PAGESIZE;
  block->used = FALSE;
  page_header_t* pageHeader = (page_header_t*)((size_t)block + sizeof(*block));
  pageHeader->thisPage = page;
  pageHeader->nextPage = NULL;
}

void*
kma_malloc(kma_size_t size)
{
  if(size > PAGESIZE - (sizeof(block_header_t) + sizeof(page_header_t)))
    return NULL;

  size += sizeof(block_header_t);

  if(firstPage == NULL)
  {
    firstPage = get_page();
    InitPage(firstPage);
  }

  
  return SplitBlock(FindAvailableBlock(size), size);
  

}

void 
kma_free(void* ptr, kma_size_t size)
{
  ;
}

#endif // KMA_BUD
