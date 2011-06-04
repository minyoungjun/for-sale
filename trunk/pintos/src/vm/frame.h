#ifndef FRAME_H_
#define FRAME_H_

#include <list.h>
#include <stdint.h>
#include "threads/palloc.h"
#include "devices/rtc.h"

/*
 * This is the structure of each frame in real
 * main memory
 */
struct frame {
  time_t finish;          /* 마지막으로 엑세스 된 시간을 잡기 위해 선언 */
  void *address;          /* Real address in main memory for this frame */
  
  struct thread *owner;   /* Current owner of this frame, if this field is
                             null, then  is free */
                             
  void *page;             /* This is the current virtual memory address
                             for this frame, i.e how is referenced this
                             frame by its owner, obviously it's null only
                             if the owner is null */
  
  bool evictable;         /* Once a frame has been chosen to evict the system,
                             this frame cannot be chosen again until it's completly
                             allocated to other proccess */
  
  bool writable;          /* Indicates if this frame is writable */
  
  struct list_elem elem;  /* List element to be listed in the frame table */
};

/*
 * This function initialize every component needed to work
 * with the frame_table
 */
void frame_table_init (void);

/*
 * This function returns the current frame table,
 * By starting this table is represented with a
 * double-linked list, but it is a better idea to
 * use something like a hash table because can be a
 * better performance in most operations
 */
struct list* frame_table (void);

/*
 * This function check that a frame is in
 * the frame table, if it is then return a reference to
 * that frame else return NULL
 * PARAMS:
 *    -void *: is the physical memory address for the
 *             given frame
 * RETURN:
 *    -struct frame*: pointer to the desired frame
 *                    if it exists
 */
struct frame* frame_table_look_up (void *);

/*
스왑 아웃 될 프레임을 선택하기 위해 선언된 함수.
성공적으로 가장 엑세스된지 오래된 프레임을 찾으면 리턴한다.
*/
struct frame* frame_table_find_min (void);

/*
 * This function insert a new frame in the frame table or
 * if the entry for the given frame exits then updates its
 * owner
 * PARAM:
 *   - void *: physical address to the frame
 */
struct frame *frame_table_insert (void *, void *, bool);

/*
 * This function uses the page replacement algorithm if necessary,
 * in pintos context the eviction policy. Given the information
 * about each frame in  the frame table (accessed and dirty bit),
 * we have decide using the page replacement algorithm "enhenced
 * second chance", this algorithm is explained in the class book
 * in the chapter Virtual memory, section page replacement algorithm.
 * However if there is a free page then we return it immediately.
 * It's important to notice that we have to use swap mechanism to
 * replace a page in a safe way
 */
struct frame* frame_table_get_frame (void *, bool);

/*
 * Performs Enhenced second chance algorithm
 */
struct frame *frame_table_choose_victim (void);

/*Removes a frame in the frame_table*/
void frame_table_remove (struct frame*);

/*  */
void frame_table_rmf (struct thread *);

/*  */
void frame_table_update_accessed_bit (void);


#endif /* FRAME_H_ */
