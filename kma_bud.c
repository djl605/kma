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

 typedef struct block_header_t
 {
   // info includes bits telling the size of a block and whether it is used
   unsigned short info;
 } block_header_t;

 typedef struct block_t
 {
   block_header_t* block;
   size_t size;
   struct block_t* next;
   struct block_t* prev;
   bool structUsed;
 } block_t;

 typedef struct page_t
 {
   kma_page_t* page;
   struct page_t* next;
   struct page_t* prev;
   bool structUsed;
 } page_t;

 typedef struct bookkeeping_header_t
 {
   int numHeaders;
   kma_page_t* thisPage;
   
   // These are only used in the first book-keeping page, and point
   // to the beginning of the linked lists. In other book-keeping pages,
   // they are set to NULL.
   block_t* firstBlock;
   block_t* lastBlock;
   page_t* firstPage;
   page_t* lastPage;
 } bookkeeping_header_t;



/************Global Variables*********************************************/
static kma_page_t* bookkeepingPage = NULL;
//static int count = 0;
/************Function Prototypes******************************************/
inline int NextPowerOfTwo(int);
inline bool Used(block_header_t*, size_t);

block_header_t* Buddy(block_header_t*, int);
bookkeeping_header_t* InitializePageKeeper(kma_page_t*);
bookkeeping_header_t* InitializeBlockKeeper(kma_page_t*);
block_t* AddAllocedPage(void);
void RemoveAllocedPage(block_header_t*);
block_t* Split(block_t*, int);
block_header_t* RemoveBlockFromList(block_t*);
block_header_t* RemoveBlockHeaderFromList(block_header_t*);
void AddBlockToList(block_header_t*, int);
/************External Declaration*****************************************/

/**************Implementation***********************************************/

inline int NextPowerOfTwo(int n)
{
  int i = 1;
  while(i < n)
    i <<= 1;

  return i;
}

inline bool Used(block_header_t* block, size_t size)
{
  return (block->info & (PAGESIZE << 1)) || (block->info < size);
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


bookkeeping_header_t* InitializePageKeeper(kma_page_t* page)
{
  bookkeeping_header_t* header = (bookkeeping_header_t*)(page->ptr);
  header->numHeaders = 0;
  header->thisPage = page;
 
  page_t* i;
  for(i = (page_t*)((size_t)header + sizeof(*header)); 
      i != NULL; 
      i = i->next)
  {
    i->structUsed = FALSE;
    page_t* next = (page_t*)((size_t)i + sizeof(page_t));
    if(BASEADDR((size_t)next + sizeof(page_t)) == BASEADDR(i))
    {
      i->next = next;
      i->next->prev = i;
    }
    else
      i->next = NULL;
  }

  return header;
}


bookkeeping_header_t* InitializeBlockKeeper(kma_page_t* page)
{
  bookkeeping_header_t* header = (bookkeeping_header_t*)(page->ptr);
  header->numHeaders = 0;
  header->thisPage = page;
  
  block_t* i;
  for(i = (block_t*)((size_t)header + sizeof(*header)); 
      i != NULL; 
      i = i->next)
  {
    i->structUsed = FALSE;
    block_t* next = (block_t*)((size_t)i + sizeof(block_t));
    if(BASEADDR((size_t)next + sizeof(block_t)) == BASEADDR(i))
    {
      i->next = next;
      i->next->prev = i;
    }
    else
      i->next = NULL;
  }
  return header;
}

block_t* AddAllocedPage(void)
{
  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);
  kma_page_t* newPage = get_page();
  bookkeeping_header_t* thisBookkeepingPage;
  if(header->lastPage->next == NULL)
  {
    kma_page_t* newPageKeeper = get_page();
    thisBookkeepingPage = InitializePageKeeper(newPageKeeper);
    page_t* firstPageStruct = (page_t*)((size_t)thisBookkeepingPage + sizeof(bookkeeping_header_t));
    firstPageStruct->prev = header->lastPage;
    header->lastPage->next = firstPageStruct;
  }
  else
    thisBookkeepingPage = (bookkeeping_header_t*)BASEADDR(header->lastPage->next);

  header->lastPage->next->page = newPage;
  header->lastPage->next->structUsed = TRUE;
  header->lastPage = header->lastPage->next;
  thisBookkeepingPage->numHeaders++;


  block_header_t* newBlock = (block_header_t*)(newPage->ptr);
  newBlock->info = PAGESIZE;
  if(header->lastBlock == NULL)
  {
    kma_page_t* newBlockKeeper = get_page();
    thisBookkeepingPage = InitializeBlockKeeper(newBlockKeeper);
    block_t* firstBlockStruct = (block_t*)((size_t)thisBookkeepingPage + sizeof(bookkeeping_header_t));
    firstBlockStruct->prev = NULL;
    firstBlockStruct->block = newBlock;
    firstBlockStruct->size = PAGESIZE;
    firstBlockStruct->structUsed = TRUE;
    header->firstBlock = firstBlockStruct;
    header->lastBlock = firstBlockStruct;
    thisBookkeepingPage->numHeaders++;
    return header->lastBlock;
  }
  if(header->lastBlock->next == NULL)
  {
    kma_page_t* newBlockKeeper = get_page();
    thisBookkeepingPage = InitializeBlockKeeper(newBlockKeeper);
    block_t* firstBlockStruct = (block_t*)((size_t)thisBookkeepingPage + sizeof(bookkeeping_header_t));
    firstBlockStruct->prev = header->lastBlock;
    header->lastBlock->next = firstBlockStruct;
  }
  else
    thisBookkeepingPage = (bookkeeping_header_t*)BASEADDR(header->lastBlock->next);

  header->lastBlock->next->block = newBlock;
  header->lastBlock->next->size = PAGESIZE;
  header->lastBlock->next->structUsed = TRUE;
  header->lastBlock = header->lastBlock->next;
  thisBookkeepingPage->numHeaders++;

  return header->lastBlock;
}

void RemoveAllocedPage(block_header_t* block)
{
  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);
  page_t* i;
  for(i = header->firstPage; i->page->ptr != (void*)block; i = i->next){}

  free_page(i->page);

  if(i->prev == NULL)
  {
    if(i->next != NULL && i->next->structUsed)
      header->firstPage = i->next;
    else
      header->firstPage = NULL;
  }
  else
    i->prev->next = i->next;

  if(i->next == NULL || !(i->next->structUsed))
    header->lastPage = i->prev;
  if(i->next != NULL)
    i->next->prev = i->prev;

  i->structUsed = FALSE;
  if(header->lastPage != NULL)
  {
    i->next = header->lastPage->next;
    i->prev = header->lastPage;
    if(i->next != NULL)
      i->next->prev = i;
    header->lastPage->next = i;
  }

  bookkeeping_header_t* thisBookkeepingPage = (bookkeeping_header_t*)BASEADDR(i);
  thisBookkeepingPage->numHeaders--;

  if(thisBookkeepingPage->numHeaders == 0)
  {
    for(i = header->lastPage; i != NULL; i = i->next)
    {
      if(BASEADDR(i) == (void*)thisBookkeepingPage)
      {
        i->prev->next = i->next;
        if(i->next != NULL)
          i->next->prev = i->prev;
      }
    }

    if(thisBookkeepingPage->thisPage == bookkeepingPage)
    {
      if(thisBookkeepingPage->firstPage != NULL)
      {
        bookkeepingPage = ((bookkeeping_header_t*)BASEADDR(thisBookkeepingPage->firstPage))->thisPage;
        bookkeeping_header_t* newFirstPage = (bookkeeping_header_t*)(bookkeepingPage->ptr);
        newFirstPage->firstBlock = thisBookkeepingPage->firstBlock;
        newFirstPage->lastBlock = thisBookkeepingPage->lastBlock;
        newFirstPage->firstPage = thisBookkeepingPage->firstPage;
        newFirstPage->lastPage = thisBookkeepingPage->lastPage;
      }
      else
        bookkeepingPage = NULL;
    }
    free_page(thisBookkeepingPage->thisPage);
  }
}

void AddBlockToList(block_header_t* block, int size)
{
  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);
  if(header->lastBlock == NULL)
  {
    kma_page_t* newBlockKeeper = get_page();
    bookkeeping_header_t* thisBlockKeeper = InitializeBlockKeeper(newBlockKeeper);
    block_t* firstBlock = (block_t*)((size_t)thisBlockKeeper + sizeof(*thisBlockKeeper));
    firstBlock->block = block;
    firstBlock->size = block->info;
    firstBlock->prev = NULL;
    firstBlock->structUsed = TRUE;
    header->firstBlock = firstBlock;
    header->lastBlock = firstBlock;
    thisBlockKeeper->numHeaders = 1;
  }
  else if(header->lastBlock->next == NULL)
  {
    kma_page_t* newBlockKeeper = get_page();
    bookkeeping_header_t* thisBlockKeeper = InitializeBlockKeeper(newBlockKeeper);
    block_t* firstBlock = (block_t*)((size_t)thisBlockKeeper + sizeof(*thisBlockKeeper));
    firstBlock->block = block;
    firstBlock->size = block->info;
    firstBlock->structUsed = TRUE;
    firstBlock->prev = header->lastBlock;
    header->lastBlock->next = firstBlock;
    header->lastBlock = firstBlock;
    thisBlockKeeper->numHeaders = 1;
  }
  else
  {
    block_t* nextBlock = header->lastBlock->next;
    nextBlock->block = block;
    nextBlock->size = block->info;
    nextBlock->structUsed = TRUE;
    header->lastBlock = nextBlock;
    ((bookkeeping_header_t*)BASEADDR(nextBlock))->numHeaders++;
  }
}


block_t* Split(block_t* block, int size)
{
  while(block->size > size)
  {
    block->size >>= 1;
    block_header_t* buddy = Buddy(block->block, block->size);
    buddy->info = block->size;
    AddBlockToList(buddy, block->size);
  }
  block->block->info = block->size;

  return block;
}

block_header_t* RemoveBlockHeaderFromList(block_header_t* block)
{
  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);
  block_t* i;
  for(i = header->firstBlock; i->block != block; i = i->next){}
  return RemoveBlockFromList(i);
}

block_header_t* RemoveBlockFromList(block_t* block)
{
  block_header_t* ret = block->block;
  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);

  if(block->prev == NULL)
  {
    if(block->next != NULL && block->next->structUsed)
      header->firstBlock = block->next;
    else
      header->firstBlock = NULL;
  }
  else
    block->prev->next = block->next;

  if(block->next == NULL || !(block->next->structUsed))
    header->lastBlock = block->prev;
  if(block->next != NULL)
    block->next->prev = block->prev;

  block->structUsed = FALSE;
  if(header->lastBlock != NULL)
  {
    block->next = header->lastBlock->next;
    block->prev = header->lastBlock;
    if(block->next != NULL)
      block->next->prev = block;
    header->lastBlock->next = block;
  }

  bookkeeping_header_t* thisBookkeepingPage = (bookkeeping_header_t*)BASEADDR(block);
  thisBookkeepingPage->numHeaders--;

  if(thisBookkeepingPage->numHeaders == 0)
  {
    block_t* i;
    for(i = header->lastBlock; i != NULL; i = i->next)
    {
      if(BASEADDR(i) == (void*)thisBookkeepingPage)
      {
        i->prev->next = i->next;
        if(i->next != NULL)
          i->next->prev = i->prev;
      }
    }
    free_page(thisBookkeepingPage->thisPage);
  }
  return ret;
}


void*
kma_malloc(kma_size_t size)
{
  
  size = NextPowerOfTwo(size + sizeof(block_header_t));
  if(size > PAGESIZE)
    return NULL;

  if(bookkeepingPage == NULL)
  {
    bookkeepingPage = get_page();
    bookkeeping_header_t* header = InitializePageKeeper(bookkeepingPage);

    kma_page_t* newAllocedPage = get_page();
    page_t* firstPage = (page_t*)((size_t)header + sizeof(*header));
    firstPage->page = newAllocedPage;
    firstPage->prev = NULL;
    firstPage->structUsed = TRUE;
    header->firstPage = firstPage;
    header->lastPage = firstPage;
    header->numHeaders = 1;

    kma_page_t* blockBookkeepingPage = get_page();
    bookkeeping_header_t* blockPage = InitializeBlockKeeper(blockBookkeepingPage);
    block_t* firstBlock = (block_t*)((size_t)blockPage + sizeof(*blockPage));
    firstBlock->block = (block_header_t*)(newAllocedPage->ptr);
    firstBlock->block->info = PAGESIZE;
    firstBlock->size = PAGESIZE;
    firstBlock->prev = NULL;
    firstBlock->structUsed = TRUE;
    header->firstBlock = firstBlock;
    header->lastBlock = firstBlock;
    blockPage->numHeaders = 1;
  }


  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);

  block_t* i;
  block_t* minBlock = NULL;
  for(i = header->firstBlock; i != NULL && i->structUsed; i = i->next)
  {
    if(i->size == size)
    {
      minBlock = i;
      break;
    }
    if(i->size > size && (minBlock == NULL || (i->size < minBlock->size)))
      minBlock = i;
  }
  if(minBlock == NULL)
    minBlock = AddAllocedPage();

  minBlock = Split(minBlock, size);
  minBlock->block->info |= (PAGESIZE << 1);

  block_header_t* ret = RemoveBlockFromList(minBlock);
  
 
 
  
  return (void*)((size_t)ret + sizeof(block_header_t));

}

void 
kma_free(void* ptr, kma_size_t size)
{
  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);
  block_header_t* blockHeader = (block_header_t*)((size_t)ptr - sizeof(block_header_t));
  blockHeader->info &= ~(PAGESIZE << 1);
  block_header_t* buddy = Buddy(blockHeader, blockHeader->info);
  bool coalesced = FALSE;
  while(buddy != NULL && !Used(buddy, blockHeader->info))
  {
    if(!coalesced)
    {
      block_t* i;
      for(i = header->firstBlock; i->block != buddy; i = i->next){}
      i->size <<= 1;
      i->block = (((size_t)blockHeader < (size_t)buddy) ? blockHeader : buddy);
      i->block->info <<= 1;
      blockHeader = i->block;
      buddy = Buddy(blockHeader, blockHeader->info);
      coalesced = TRUE;
    }
    else
    {
      block_t* i;
      for(i = header->firstBlock; i->block != blockHeader; i = i->next){}
      block_t* j;
      for(j = header->firstBlock; j->block != buddy; j = j->next){}
      block_t* lowBuddy = (((size_t)(i->block) < (size_t)(j->block)) ? i : j);
      block_t* highBuddy = lowBuddy == i ? j : i;
      RemoveBlockFromList(highBuddy);
      lowBuddy->size <<= 1;
      lowBuddy->block->info <<= 1;
      blockHeader = lowBuddy->block;
      buddy = Buddy(blockHeader, blockHeader->info);
    }
  }

  if(!coalesced)
    AddBlockToList(blockHeader, blockHeader->info);

  if(buddy == NULL)
  {
    RemoveBlockHeaderFromList(blockHeader);
    RemoveAllocedPage(blockHeader);
  }

}

#endif // KMA_BUD
