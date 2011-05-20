#include <stdbool.h>
#include "vm/swap.h"
#include "threads/malloc.h"
#include <string.h>
#include "threads/synch.h"

static struct list free_swap;        /* The swap table for free slots*/

static struct disk *partition;       /* The swap partition*/

static disk_sector_t cnt = 0;        /* The number of slots allocated so far*/

static disk_sector_t slots_in_disk;  /* Number of slots in the swap partition*/

static disk_sector_t size = 0;       /* The number of sectors within a page */

static struct lock lock_swap_disk;   /* Lock useful for ensure syncronization in swap partition */


/* We initialize the swap table, the swap partition and set the number of slots
   in that partition*/
void swap_init()
{
  list_init(&free_swap);
  
  lock_init (&lock_swap_disk);
  
  partition = disk_get(1, 1);
  if(partition == NULL)
    PANIC("couldn't open swap partition");

  slots_in_disk = disk_size(partition);
  int s = 0;
  while(s < PGSIZE){
    size++;
    s += DISK_SECTOR_SIZE;
  }
}

/* We are going to write BUFFER (ideally a frame whose size is PGSIZE)
   to a free swap slot, it may be in the free swap table or in the slot pool*/
struct slot* write_swap(void *buffer){
  void * b = malloc(DISK_SECTOR_SIZE);
  if(b == NULL)
    PANIC("couldn't allocate buffer");

  struct slot * s;
  bool increment = true;  // Thus we know if we have to increment CNT
  
  if(!list_empty(&free_swap))
  {  // If possible, takes a slot from the free swap table
    lock_acquire (&lock_swap_disk);
    s = list_entry (list_pop_front(&free_swap), struct slot, elem);
    lock_release (&lock_swap_disk);
    increment = false;
  }
  else
  { 
    // If not, we take a slot from the slot pool, so our counter 'cnt'
	  //is incremented. However, if there is no more room then panic the kernel
    if(cnt >= slots_in_disk)
      PANIC("Swap partition overflows");

    s = (struct slot*) malloc (sizeof (struct slot)); 
    s->start = cnt;
  }
  
  disk_sector_t i = 0, j= 0, sector = s->start;
  uint32_t offset;
  
  while(j < PGSIZE)
  {
	// Move to the correct position within the frame
    offset = (uint32_t)buffer + (i*DISK_SECTOR_SIZE);
    memcpy(b, (uint8_t *)offset, DISK_SECTOR_SIZE);
    disk_write(partition, sector++, b);
    i++;
    j += DISK_SECTOR_SIZE;
  }

  if(increment)
    cnt = sector;

  free(b);
  return s;
}

void read_swap(void *buffer, struct slot* s)
{
  void * b = malloc(DISK_SECTOR_SIZE);
  if(b == NULL)
    PANIC("couldn't allocate buffer");

  disk_sector_t i = 0, j= 0, sector = s->start;
  uint32_t offset;
  
  while(j < PGSIZE)
  {
    disk_read(partition, sector++, b);
    offset = (uint32_t)buffer + (i*DISK_SECTOR_SIZE);
    memcpy((uint8_t *)offset, b, DISK_SECTOR_SIZE);
    i++;
    j += DISK_SECTOR_SIZE;
  }

  free(b);
  free_slot(s);
}

void free_slot(struct slot* s)
{
  // If we are removing the last slot being allocated, we free it
  // as well as any SUBSEQUENT slot in the free swap table
  if(s->start + size == cnt)
  {
    free(s);
    cnt -= size;
    lock_acquire (&lock_swap_disk);
    while(!list_empty(&free_swap))
    {
      if(list_entry(list_back(&free_swap), struct slot, elem)->start + size
    		  == cnt)
      {
		    free(list_entry(list_pop_back(&free_swap), struct slot, elem));
		    cnt -= size;
		    continue;
      }
      else
        break;
    }
    lock_release (&lock_swap_disk);
  }
  else
  {
    lock_acquire (&lock_swap_disk);
    list_insert_ordered(&free_swap, &s->elem, slot_less, NULL);
    lock_release (&lock_swap_disk);
  }
}

bool slot_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
  struct slot* sa = list_entry(a, struct slot, elem);
  struct slot* sb = list_entry(b, struct slot, elem);
  return (sa->start) < (sb->start);
}
