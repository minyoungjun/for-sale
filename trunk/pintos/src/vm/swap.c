#include <stdbool.h>
#include "vm/swap.h"
#include "threads/malloc.h"
#include <string.h>
#include "threads/synch.h"

static struct list free_swap;        // 빈 슬롯을 위한 스왑 테이블.
static struct disk *partition;       // 스왑 파티션.
static disk_sector_t cnt = 0;        // 현재까지 할당된 슬롯의 개수.
static disk_sector_t slots_in_disk;  // 스왑 파티션 내의 슬롯 수.
static disk_sector_t size = 0;       // 페이지 내의 섹터 개수.
static struct lock lock_swap_disk;   // 스왑 파티션 내의 동기화를 위한 락.

// 스왑 테이블, 스왑 파티션과 파티션 내의 슬롯 수를 초기화한다
void swap_init() {
  list_init(&free_swap);
  lock_init (&lock_swap_disk);
  partition = disk_get(1, 1);
  if(partition == NULL)
    PANIC("Can't open the swap partition");

  slots_in_disk = disk_size(partition);
  int s = 0;
  while(s < PGSIZE){
    size++;
    s += DISK_SECTOR_SIZE;
  }
}

// 빈 스왑 슬롯에 버퍼를 쓴다.
struct slot* write_swap(void *buffer){
  void * b = malloc(DISK_SECTOR_SIZE);
  if(b == NULL)
    PANIC("Can't allocate buffer");

  struct slot * s;
  bool increment = true;  // CNT를 증가시킬지 말지 결정.
  
  if(!list_empty(&free_swap)) {  // 가능한 빈 스왑 테이블 내에서 슬롯을 얻는다.
    lock_acquire (&lock_swap_disk);
    s = list_entry (list_pop_front(&free_swap), struct slot, elem);
    lock_release (&lock_swap_disk);
    increment = false;
  }
  else { 
    /* 불가능할 경우, 슬롯 풀에서 슬롯을 가져오고 cnt를 증가시킨다.
    더 이상 가져올 수 없을 경우 커널에 panic이 들어가게 된다. */
    if(cnt >= slots_in_disk)
      PANIC("Swap partition overflows");

    s = (struct slot*) malloc (sizeof (struct slot)); 
    s->start = cnt;
  }
  
  disk_sector_t i = 0, j= 0, sector = s->start;
  uint32_t offset;
  
  while(j < PGSIZE)
  {
	// 프레임 내의 정확한 위치로 이동한다.
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
    PANIC("Can't allocate buffer");

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

void free_slot(struct slot* s) {
  // 할당된 마지막 슬롯을 옮긴다.
  if(s->start + size == cnt) {
    free(s);
    cnt -= size;
    lock_acquire (&lock_swap_disk);
    while(!list_empty(&free_swap)) {
      if(list_entry(list_back(&free_swap), struct slot, elem)->start + size == cnt) {
		    free(list_entry(list_pop_back(&free_swap), struct slot, elem));
		    cnt -= size;
		    continue;
      }
      else
        break;
    }
    lock_release (&lock_swap_disk);
  }
  else {
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
