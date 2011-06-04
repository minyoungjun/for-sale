#ifndef SWAP_H_
#define SWAP_H_

#include <list.h>
#include "devices/disk.h"
#include "threads/vaddr.h"

struct slot{
  disk_sector_t start;    /* We want to know the starting sector of this slot,
                             we know we will use SLOT_SIZE sectors for this slot.
                             We just state the first one */
  struct list_elem elem;  /* Element to be listed*/
};

void swap_init(void);
struct slot* write_swap(void *);
void read_swap(void*, struct slot*);
void free_slot(struct slot*);
bool slot_less(const struct list_elem *a, const struct list_elem *b, void *aux);

#endif /* SWAP_H_ */
