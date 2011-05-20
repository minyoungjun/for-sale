#include "vm/page.h"
#include "threads/thread.h"
#include "vm/frame.h"
#include "userprog/process.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <stdio.h>
#include "threads/palloc.h"
#include "userprog/pagedir.h"

/* Looks up a page according to its specific virtual memory
   address (vm_addr), if the page is not found the returns
   NULL */
struct page*
supplemental_table_look_up (uint8_t *vm_addr)
{
  struct thread *cur;
  struct list_elem *e;
  struct page *res = NULL;
  
  cur = thread_current ();
  
  lock_acquire (&cur->lock_spt);
  for(e=list_begin (&cur->sup_page_table); e!=list_end (&cur->sup_page_table);
  e=list_next (e))
  {
    res = list_entry (e, struct page, elem);
    if (res->page == vm_addr)
    {
      lock_release (&cur->lock_spt);
      return res;
    }
  }
  lock_release (&cur->lock_spt);
  
  return NULL;
}


struct page*
supplemental_table_look_up_ext (uint8_t *vm_addr, struct thread *cur)
{ 
  struct list_elem *e;
  struct page *res = NULL;
  
  lock_acquire (&cur->lock_spt);
  for(e=list_begin (&cur->sup_page_table); e!=list_end (&cur->sup_page_table);
      e=list_next (e))
  {
    res = list_entry (e, struct page, elem);
    if (res->page == vm_addr)
    {
      lock_release (&cur->lock_spt);
      return res;
    }
  }
  lock_release (&cur->lock_spt);
  
  return NULL;
}

/* Removes a given pages from its own supplemental table */
void
supplemental_table_free_page (struct page *pg)
{
  lock_acquire (&thread_current ()-> lock_spt);
  list_remove (&pg->elem);
  lock_release (&thread_current ()-> lock_spt);

  free (pg);
}

/* Loads the page (page) may be froma file or form swap,
   in a frame from user pool, and aditionally adds the
   loaded page to the user pagedir */
void
supplemental_table_load_page (struct page *page)
{
  struct frame *f = NULL;

  switch((int) page->pg)
  {   
    case PAG_EXEC:
      f = frame_table_get_frame (page->page, page->writable); // the page is filled with zeros by defualt
      // Load the this page from a executable file
      if(page->zero_b != (uint32_t)PGSIZE)
      {
        if(file_read_at ((struct file *)page->location, f->address, page->read_b, page->ofs) != (int) page->read_b)
        {
          printf ("Wtf? reading execf\n");
          frame_table_remove (f);
          thread_exit ();
        }
      }

      if(!install_page(page->page, f->address, page->writable))
      {
        printf ("Wtf? installing execf\n");
    	  frame_table_remove (f);
        thread_exit();
      }
      
      // In this case is not a good idea to delete this entry
      // in the supplemental page table, because the information
      // it contains could be useful during eviction policy.
      if(page->writable)
        supplemental_table_free_page (page);
      break;
    case PAG_FILE:
      f = frame_table_get_frame (page->page, page->writable); // the page is filled with zeros by defualt
      // Load the this page from a executable file
      if(page->zero_b != (uint32_t)PGSIZE)
      {
        if(file_read_at ((struct file *)page->location, f->address, page->read_b, page->ofs) != (int) page->read_b)
        {
          printf ("Wtf? reading execf\n");
          frame_table_remove (f);
          thread_exit ();
        }
      }

      if(!install_page(page->page, f->address, page->writable))
      {
        printf ("Wtf? installing execf\n");
    	  frame_table_remove (f);
        thread_exit();
      }
      
      // In this case is not a good idea to delete this entry
      // in the supplemental page table, because the information
      // it contains could be useful during eviction policy.
      break;
    case PAG_SWAP:
      // Extracts a cleaned frame from the frame table
      f = frame_table_get_frame (page->page, page->writable); // the page is filled with zeros by defualt
      
      read_swap (f->address, (struct slot *)page->location);
      
      if(!install_page(page->page, f->address, page->writable))
      {
        printf ("Wtf? installing swap\n");
    	  frame_table_remove (f);
        thread_exit();
      }
      
      supplemental_table_free_page (page);
      break;
  }
  
  // Makes the frame evictable again
  f->evictable = true;
}

void
supplemental_table_add_frame (struct frame *f, struct slot *s)
{
  struct thread *old_owner = f->owner;
  struct page *p = (struct page *) malloc (sizeof(struct page));
  
  if(p==NULL)
  {
    printf ("What the fuck in add_frame?\n");
    thread_exit ();
  }
  
  p->page = f->page;
  p->writable = f->writable;
  p->location = s;
  p->pg = PAG_SWAP;
  
  lock_acquire (&old_owner->lock_spt);
  list_push_front (&old_owner->sup_page_table, &p->elem);
  lock_release (&old_owner->lock_spt);
}


void
supplemental_table_destroy ()
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct page *p;
  
  lock_acquire (&cur->lock_spt);
  
  while(!list_empty (&cur->sup_page_table))
  {
    e = list_pop_front (&cur->sup_page_table);
    p = list_entry (e, struct page, elem);
    if(p->pg == PAG_SWAP)
	    free_slot ((struct slot *)p->location);
    free (p);
  }
  
  lock_release (&cur->lock_spt);
}

void
mf_table_destroy ()
{
  struct list_elem *e;
  struct thread *cur;
  struct mapped_file *mf;
  uint32_t size;
  void *kpage;
  void *addr;
  struct page *p;
  uint32_t write_b;
  
  cur = thread_current ();
  
  while (!list_empty (&cur->mf_table))
  {
    e = list_pop_front (&cur->mf_table);
    mf = list_entry (e, struct mapped_file, elem);
    
    size = mf->size;
    addr = mf->addr;
    
    while (size > 0)
    {
      p = supplemental_table_look_up (addr);
      if(p == NULL)
      {
        printf ("WTF unmap?\n");
        thread_exit ();
      }
      
      write_b = p->read_b;
        
      lock_acquire (&cur->lock_spt);
      
      kpage = pagedir_get_page (cur->pagedir, p->page);
      
      if(kpage != NULL)
      {
        if(pagedir_is_dirty (cur->pagedir, p->page))
        {
          if(file_write_at (mf->file, kpage, write_b, p->ofs) != (int)write_b)
          {
            printf ("WTF unmap? writing\n");
            thread_exit ();
          }
        }
        
        pagedir_clear_page (cur->pagedir, mf->addr);
      }
      
      lock_release (&thread_current ()->lock_spt);
      
      supplemental_table_free_page (p);
      
      size -= write_b;
      addr += write_b;
    }
    
    file_close (mf->file);
    
    // TODO: synchronization?
    list_remove (&mf->elem);
    
    free (mf);
  }
}



















