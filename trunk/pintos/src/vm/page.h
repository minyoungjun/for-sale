#ifndef PAGE_H_
#define PAGE_H_

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "vm/swap.h"
#include "vm/frame.h"


enum flags_spt {
  NORMAL = 000,
  FORCE  = 001 
};

enum page_location {
  PAG_SWAP, // The page is in swap partition
  PAG_FILE, // The page is to be read from a file, usually a file mapped to memory
  PAG_EXEC  // The page is to be read from a executalbe file (lazy load of code and data segments)
};

struct page 
  {
    void *location;          /* This is the references where is located this page,
                                this pointer may point a slot in swap disk, or a file
                                in the file system, be careful using it */
    
    enum page_location pg;   /* Where is this page */
    
    bool writable;           /* If this page is read-only */
    
    uint32_t ofs;            /* Offset in the file to start reading */
    
    uint32_t read_b;         /* Bytes to read from file */
    
    uint32_t zero_b;         /* zeros to fill the page */
    
    uint8_t *page;           /* This is the vm address where this page starts */
    
    struct list_elem elem;   /* List element to list this page in the supplemental
                                page table (maybe just used in the page fault handler) */
  };

/* BASIC FUNCITONS */
void supplemental_table_load_page (struct page *);
void supplemental_table_free_page (struct page *);
struct page *supplemental_table_look_up (uint8_t *);
struct page *supplemental_table_look_up_ext (uint8_t *, struct thread *t);
void supplemental_table_add_frame (struct frame *, struct slot *);
void supplemental_table_destroy (void);
void mf_table_destroy (void);

#endif /* PAGE_H_ */
