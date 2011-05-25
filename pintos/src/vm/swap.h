#ifndef SWAP_H_
#define SWAP_H_

#include <list.h>
#include "devices/disk.h"
#include "threads/vaddr.h"

struct slot{
  disk_sector_t start;    // 현재 슬롯의 시작 부분 섹터를 얻기 위해 선언.
  struct list_elem elem;  // list될 원소들.
};

void swap_init(void);
struct slot* write_swap(void *);
void read_swap(void*, struct slot*);
void free_slot(struct slot*);
bool slot_less(const struct list_elem *a, const struct list_elem *b, void *aux);

#endif
