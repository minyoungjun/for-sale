#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/page.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include <stdio.h>
#include <string.h>
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "threads/intr-stubs.h"
#include "threads/interrupt.h"
#include "userprog/pagedir.h"
#include "devices/rtc.h"

static struct list frame_list;

static struct list_elem *victim_frame;

static struct lock lock;

/* Initializes frame table components */
void
frame_table_init () 
{
  list_init (&frame_list);
  lock_init (&lock);
  victim_frame = NULL;
}

/* Return the frame_table */
struct list*
frame_table ()
{
  return &frame_list;
}

/* Looks for a given frame in the frame table
   if the given frame does not exist then returns NULL,
   Note tha you must use apropiade sychronization when you 
   want to use this function, is a desicion of desing don't use
   synchronization in this function, so never add this feature
   because can cuase several problems */
struct frame*
frame_table_look_up (void *_frame)
{
  struct list *list = frame_table ();
  struct list_elem *e;
  struct frame *frame = NULL;

  for(e=list_begin(list); e!=list_end(list); e=list_next(e))
  {
    frame = list_entry(e, struct frame, elem);
	  if(_frame==frame->address) {
			frame->finish = rtc_get_time();
	    return frame;
		}
  }

  return NULL; // Really in most cases this is an error, take care about this situaton
}

// 가장 오래 사용되지 않은 프레임을 찾기 위해 선언된 구조체.
struct frame*
frame_table_find_min ()
{
  struct list *list = frame_table ();
  struct list_elem *e;
  struct frame *frame = NULL;
  struct frame *retframe = NULL;
  time_t min, temp;

  // min의 초기값은  첫째 프레임의 finish값으로 한다.
  e=list_begin(list);
  frame = list_entry(e, struct frame, elem);
  min = frame->finish;
  retframe = frame;
 
  // 리스트를 돌면서 최장시간 사용되지 않은 프레임을 찾는다.
  for(e=list_begin(list); e!=list_end(list); e=list_next(e))
  {
    frame = list_entry(e, struct frame, elem);
    temp = frame->finish;
    if(min>temp) {
      min = temp;
      retframe = frame;
    }
  }
  return retframe;
}

/* Inserts the given frame if and only if it's not already */
struct frame *
frame_table_insert (void *_frame, void *upage, bool writable)
{
  struct frame* f;

  f = (struct frame *) malloc (sizeof(struct frame));

  if(f == NULL) // Unexpected error
    thread_exit ();

  f->address = _frame;
  f->owner = thread_current ();
  f->page = upage;
  f->writable = writable;
  f->evictable = false;

  lock_acquire(&lock);
  list_push_front(frame_table (), &f->elem);
  lock_release(&lock);
  
  return f;
}

/* Returns a new zeroed frame from the user pool */
struct frame *
frame_table_get_frame (void *upage, bool writable)
{
  struct frame *f = NULL;
  void * user_page = palloc_get_page (PAL_USER | PAL_ZERO);
  struct slot *slot;
  struct page *p;
  bool dirty;
  enum intr_level prev;
  
  // Gets the page from main memory
  if(user_page == NULL)
  {
    lock_acquire (&lock);
    
    // Chosses a victim from page table
    f = frame_table_choose_victim ();
    
    if(f==NULL)
    {
      lock_release (&lock);
      printf ("What the fuck in get_frame?\n");
      thread_exit ();
    }
    
    // Writes the frame in swap or file system if necessary
    p = supplemental_table_look_up_ext (f->page, f->owner);
    if (p!=NULL)
    {
      // Stop if necessary
      sema_down (&f->owner->sema_pf);
      
      prev = intr_disable ();
      dirty = pagedir_is_dirty (f->owner->pagedir, f->page);
      pagedir_clear_page (f->owner->pagedir, f->page);
      intr_set_level (prev);
      
      if(dirty && f->writable)
        if(file_write_at ((struct file *)p->location, f->address, p->read_b, p->ofs) != (int)p->read_b)
          thread_exit ();
      
      // continue normally 
      sema_up (&f->owner->sema_pf);
    }
    else
    {
      // Stop if necessary
      sema_down (&f->owner->sema_pf);
      
      pagedir_clear_page (f->owner->pagedir, f->page); 
      // Writes the content of fram chosen
      slot = write_swap (f->address);
      // Adds the chosen frame to the supplemental table
      // of the process that is going to give the frame
      supplemental_table_add_frame (f, slot);
      
      // continue normally 
      sema_up (&f->owner->sema_pf);
    }
    
    lock_release (&lock);
    
    // Fills with zeros the chose frame
    memset (f->address, 0, PGSIZE);
    
    // Makes this frame's owner to the current thread
    f->owner = thread_current ();
    f->page = upage;
    f->writable = writable;
    f->finish = rtc_get_time();
  } 
  else {
    f = frame_table_insert (user_page, upage, writable);
    f->finish = rtc_get_time();
  }
  return f;
}

/* Returns a victim following the enhenced second chance
   algorithm, at the beginning use FIFO algorithm,
   use synch to use correctly this fucntion */
struct frame *
frame_table_choose_victim ()
{
  struct list_elem *temp = NULL;
  struct frame *f;

  if(!list_empty (frame_table ()))
  {
    if(victim_frame == NULL)
        victim_frame = frame_table_find_min ();

//      victim_frame = list_begin (frame_table ());

    while(true)
    {
      f = list_entry (victim_frame, struct frame, elem);
      if(f->evictable)
      {
        if(!pagedir_is_accessed (f->owner->pagedir, f->page))
        {
          temp = victim_frame;
    
          if(list_next (victim_frame) == list_end (frame_table ()))
            victim_frame = list_begin (frame_table ());
          else
            victim_frame = list_next (victim_frame);
            
          break;
        }
        else
          pagedir_set_accessed (f->owner->pagedir, f->page, false);
      }
      else
      {
        if(list_next (victim_frame) == list_end (frame_table ()))
          victim_frame = list_begin (frame_table ());
        else
          victim_frame = list_next (victim_frame);
      }
    }
    
    f->evictable = false;
    return f;
  }
  
  return NULL;
}

/* Removes a frame from the frame table */
void 
frame_table_remove (struct frame *f)
{
  struct frame *temp;
  
  if(f != NULL) 
  {
    lock_acquire (&lock);
    
    temp = frame_table_look_up (f);
    
    if(temp != NULL) {
      palloc_free_page (f->address);
      list_remove (&f->elem);
    }
    
    lock_release (&lock); 
  }
}

/* Removes all the frames asociated with a given thread */
void
frame_table_rmf (struct thread *t)
{
  struct list_elem *e;
  struct list_elem *temp;
  struct frame *f;
  
  lock_acquire (&lock);
  for(e=list_begin (frame_table ()); e!=list_end (frame_table ());)
  {
    f = list_entry (e, struct frame, elem); 
    if(f->owner == t && f->evictable)
    {
      temp = list_next (e);
      list_remove (e);
      e = temp;
      free (f);
    }
    else
      e = list_next (e);
  }
  lock_release (&lock);
}

/* Update the accesed bit for all frames in frame table */
void
frame_table_update_accessed_bit ()
{
  struct list_elem *e;
  struct frame *cf;
  
  for(e=list_begin (frame_table ()); e!=list_end (frame_table ()); e=list_next(e)) 
  {
    cf = list_entry (e, struct frame, elem);

    pagedir_set_accessed (cf->owner->pagedir, cf->page, false);
  }
}
