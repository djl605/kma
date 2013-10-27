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
   size_t info;
 } block_header_t;

 typedef struct block_t
 {
   block_header_t* block;
   size_t size;
   struct block_t* next;
   struct block_t* prev;
 } block_t;

 typedef struct page_t
 {
   kma_page_t* page;
   struct page_t* next;
   struct page_t* prev;
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
   void* nextAvailable;
 } bookkeeping_header_t;



/************Global Variables*********************************************/
static kma_page_t* bookkeepingPage = NULL;
/************Function Prototypes******************************************/
inline int NextPowerOfTwo(int);
inline bool Used(block_header_t*);

block_header_t* Buddy(block_header_t*, int);
void CreateBookkeepingPage(void);
/************External Declaration*****************************************/

/**************Implementation***********************************************/

inline int NextPowerOfTwo(int n)
{
  int i = 1;
  while(i < n)
    i <<= 1;

  return i;
}

inline bool Used(block_header_t* block)
{
  return (block->info & (PAGESIZE << 1));
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

void CreateBookkeepingPage(void)
{
  kma_page_t* newPage = get_page();
  bookkeeping_header_t* newBookkeepingHeader = (bookkeeping_header_t*)(newPage->ptr);
  newBookkeepingHeader->numHeaders = 0;
  newBookkeepingHeader->thisPage = newPage;
  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);
  header->nextAvailable = (void*)((size_t)newBookkeepingHeader + sizeof(bookkeeping_header_t));
}


void*
kma_malloc(kma_size_t size)
{
  size = NextPowerOfTwo(size + sizeof(block_header_t));
  if(size > PAGESIZE)
    return NULL;

  if (bookkeepingPage == NULL)
  {
    bookkeepingPage = get_page();
    bookkeeping_header_t* page = (bookkeeping_header_t*)(bookkeepingPage->ptr);
    page->numHeaders = 1;
    page->thisPage = bookkeepingPage;
    
    block_t* firstBlockStruct = (block_t*)((size_t)page + sizeof(*page));
    page_t* firstPageStruct = (page_t*)((size_t)firstBlockStruct + sizeof(*firstBlockStruct));
    page->firstBlock = firstBlockStruct;
    page->lastBlock = firstBlockStruct;
    page->firstPage = firstPageStruct;
    page->lastPage = firstPageStruct;
    page->nextAvailable = (void*)((size_t)firstPageStruct + sizeof(page_t));

    firstPageStruct->page = get_page();
    firstPageStruct->next = NULL;
    firstPageStruct->prev = NULL;

    firstBlockStruct->block = (block_header_t*)(firstPageStruct->page->ptr);
    firstBlockStruct->size = PAGESIZE;
    firstBlockStruct->next = NULL;
    firstBlockStruct->prev = NULL;

    block_header_t* firstBlock = firstBlockStruct->block;
    firstBlock->info = PAGESIZE;
  }

  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);
  
  block_t* minBlock = NULL;
  block_t* i;
  for(i = header->firstBlock; i != NULL; i = i->next)
  {
    if(i->size == size)
    {
      minBlock = i;
      break;
    }

    if(i->size > size &&
      (minBlock == NULL || i->size < minBlock->size))
    {
      minBlock = i;
    }
  }

  if(minBlock == NULL)
  {
    kma_page_t* newPage = get_page();
    block_header_t* block = (block_header_t*)(newPage->ptr);
    block->info = PAGESIZE;

    void* nextAvailable = (void*)((size_t)(header->nextAvailable) + sizeof(page_t)) - 1;
    if(BASEADDR(nextAvailable) != BASEADDR(header->lastPage) ||
       BASEADDR(header->nextAvailable) != BASEADDR(header->lastBlock))
    {
      CreateBookkeepingPage();
    }
    page_t* nextPageStruct = (page_t*)(header->nextAvailable);
    nextPageStruct->page = newPage;
    nextPageStruct->next = NULL;
    nextPageStruct->prev = header->lastPage;
    header->lastPage->next = nextPageStruct;
    header->lastPage = nextPageStruct;
    header->nextAvailable = (void*)((size_t)nextAvailable + 1);
    ((bookkeeping_header_t*)BASEADDR(nextPageStruct))->numHeaders++;

    nextAvailable = (void*)((size_t)(header->nextAvailable) + sizeof(block_t)) - 1;
    if(BASEADDR(nextAvailable) != BASEADDR(header->lastPage) ||
       BASEADDR(nextAvailable) != BASEADDR(header->lastBlock))
    {
      CreateBookkeepingPage();
    }
    block_t* nextBlockStruct = (block_t*)(header->nextAvailable);
    nextBlockStruct->block = (block_header_t*)(newPage->ptr);
    nextBlockStruct->block->info = PAGESIZE;
    nextBlockStruct->size = PAGESIZE;
    nextBlockStruct->next = NULL;
    nextBlockStruct->prev = header->lastBlock;
    header->lastBlock->next = nextBlockStruct;
    header->lastBlock = nextBlockStruct;
    header->nextAvailable = (void*)((size_t)nextAvailable + 1);
    ((bookkeeping_header_t*)BASEADDR(nextBlockStruct))->numHeaders++;

    minBlock = nextBlockStruct;
  }

  while(minBlock->size > size)
  {
    minBlock->size >>= 1;
    block_header_t* buddy = Buddy(minBlock->block, minBlock->size);
    buddy->info = minBlock->size;
    
    void* nextAvailable = (void*)((size_t)(header->nextAvailable) + sizeof(block_t)) - 1;
    if(BASEADDR(nextAvailable) != BASEADDR(header->lastPage) ||
       BASEADDR(nextAvailable) != BASEADDR(header->lastBlock))
    {
      CreateBookkeepingPage();
    }

    block_t* nextBlockStruct = (block_t*)(header->nextAvailable);
    nextBlockStruct->block = buddy;
    nextBlockStruct->size = minBlock->size;
    nextBlockStruct->next = NULL;
    nextBlockStruct->prev = header->lastBlock;
    header->lastBlock->next = nextBlockStruct;
    header->lastBlock = nextBlockStruct;
    ((bookkeeping_header_t*)BASEADDR(nextBlockStruct))->numHeaders++;
  }



  minBlock->block->info = minBlock->size | (PAGESIZE << 1);
  if(minBlock->prev != NULL)
    minBlock->prev->next = minBlock->next;
  if(minBlock->next != NULL)
    minBlock->next->prev = minBlock->prev;

  bookkeeping_header_t* thisPage = (bookkeeping_header_t*)BASEADDR(minBlock);
  thisPage->numHeaders--;
  if(thisPage->numHeaders == 0)
  {
    if(thisPage == header)
    {
      bookkeeping_header_t* newFirstPage = (bookkeeping_header_t*)BASEADDR(header->firstBlock);
      newFirstPage->firstBlock = header->firstBlock;
      newFirstPage->lastBlock = header->lastBlock;
      newFirstPage->firstPage = header->firstPage;
      newFirstPage->lastPage = header->lastPage;
      newFirstPage->nextAvailable = header->nextAvailable;
      bookkeepingPage = newFirstPage->thisPage;
    }
    free_page(thisPage->thisPage);
  }



  return (void*)((size_t)(minBlock->block) + sizeof(block_header_t));
}

void 
kma_free(void* ptr, kma_size_t size)
{
  block_header_t* block = (block_header_t*)((size_t)ptr - sizeof(block_header_t));

  bookkeeping_header_t* header = (bookkeeping_header_t*)(bookkeepingPage->ptr);

  block_header_t* buddy = Buddy(block, block->info & ~(PAGESIZE << 1));
  block_t* i;

  bool coalesced = FALSE;
  
  while(buddy != NULL && !Used(buddy))
  {
    coalesced = TRUE;
    for(i = header->firstBlock; i != NULL; i = i->next)
    {
      if(i->block == buddy)
      {
        i->size <<= 1;
        i->block = (((size_t)block < (size_t)buddy) ? block : buddy);
        block = i->block;
        buddy = Buddy(block, i->size);
        break;
      }
    }
  }
  for(i = header->firstBlock; i->block != block; i = i->next){}
  if(buddy == NULL)
  {
    if(i->prev != NULL)
      i->prev->next = i->next;
    if(i->next != NULL)
      i->next->prev = i->prev;

    bookkeeping_header_t* thisPage = (bookkeeping_header_t*)BASEADDR(i);
    thisPage->numHeaders--;
    if(thisPage->numHeaders == 0)
    {
      if(thisPage == header)
      {
        bookkeeping_header_t* newFirstPage = (bookkeeping_header_t*)BASEADDR(header->firstBlock);
        newFirstPage->firstBlock = header->firstBlock;
        newFirstPage->lastBlock = header->lastBlock;
        newFirstPage->firstPage = header->firstPage;
        newFirstPage->lastPage = header->lastPage;
        newFirstPage->nextAvailable = header->nextAvailable;
        bookkeepingPage = newFirstPage->thisPage;
        header = (bookkeeping_header_t*)(bookkeepingPage->ptr);
      }
      free_page(thisPage->thisPage);
    }

    void* page = (void*)(i->block);
    page_t* j;
    for(j = header->firstPage; j != NULL; j = j->next)
    {
      if(j->page->ptr == page)
      {
        if(j->prev != NULL)
          j->prev->next = NULL;
        if(j->next != NULL)
          j->next->prev = NULL;
        thisPage = (bookkeeping_header_t*)BASEADDR(j);
        thisPage->numHeaders--;
        if(thisPage->numHeaders == 0)
        {
          if(thisPage == header)
          {
            bookkeeping_header_t* newFirstPage = (bookkeeping_header_t*)BASEADDR(header->firstBlock);
            newFirstPage->firstBlock = header->firstBlock;
            newFirstPage->lastBlock = header->lastBlock;
            newFirstPage->firstPage = header->firstPage;
            newFirstPage->lastPage = header->lastPage;
            newFirstPage->nextAvailable = header->nextAvailable;
            bookkeepingPage = newFirstPage->thisPage;
            header = (bookkeeping_header_t*)(bookkeepingPage->ptr);
          }
          free_page(thisPage->thisPage);
        }
        free_page(j->page);
      }
    }
    return;
  }

  if(!coalesced)
  {
    void* nextAvailable = (void*)((size_t)(header->nextAvailable) + sizeof(block_t)) - 1;
    if(BASEADDR(nextAvailable) != BASEADDR(header->lastPage) ||
       BASEADDR(nextAvailable) != BASEADDR(header->lastBlock))
    {
      CreateBookkeepingPage();
    }
    block_t* nextBlockStruct = (block_t*)(header->nextAvailable);
    nextBlockStruct->block = block;
    nextBlockStruct->block->info &= ~(PAGESIZE << 1);
    nextBlockStruct->size = nextBlockStruct->block->info;
    nextBlockStruct->next = NULL;
    nextBlockStruct->prev = header->lastBlock;
    header->lastBlock->next = nextBlockStruct;
    header->lastBlock = nextBlockStruct;
    header->nextAvailable = (void*)((size_t)nextAvailable + 1);
    ((bookkeeping_header_t*)BASEADDR(nextBlockStruct))->numHeaders++;
    
  }

}

#endif // KMA_BUD
