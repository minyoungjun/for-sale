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

// 특정 가상 메모리 주소에 해당하는 페이지를 찾아 반환한다.
struct page* scanST (uint8_t *vm_addr) {
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

struct page* scanST_ext (uint8_t *vm_addr, struct thread *cur)
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

// 주어진 페이지를 해당 supplemental table에서 제거한다.
void freepageST (struct page *pg)
{
  lock_acquire (&thread_current ()-> lock_spt);
  list_remove (&pg->elem);
  lock_release (&thread_current ()-> lock_spt);

  free (pg);
}

// 페이지를 유저 풀의 파일이나 스왑으로부터 불러온다.
void loadpageST (struct page *page)
{
  struct frame *f = NULL;

  switch((int) page->pg)
  {   
    case PAG_EXEC:
      f = getzeroFTable(page->page, page->writable); // 기본적으로 페이지는 0으로 채워져있다.
      // 실행 파일로부터 페이지를 불러온다.
      if(page->zero_b != (uint32_t)PGSIZE) {
        if(file_read_at ((struct file *)page->location, f->address, page->read_b, page->ofs) != (int) page->read_b) {
          removeFTable(f);
          thread_exit ();
        }
      }

      if(!install_page(page->page, f->address, page->writable)) {
    	  removeFTable(f);
        thread_exit();
      }
      
      if(page->writable)
        freepageST (page);
      break;
    case PAG_FILE:
      f = getzeroFTable(page->page, page->writable);
      if(page->zero_b != (uint32_t)PGSIZE) {
        if(file_read_at ((struct file *)page->location, f->address, page->read_b, page->ofs) != (int) page->read_b) {
          removeFTable(f);
          thread_exit ();
        }
      }

      if(!install_page(page->page, f->address, page->writable)) {
    	  removeFTable(f);
        thread_exit();
      }
      break;
    case PAG_SWAP:
      // 프레임 테이블로부터 사용되지 않은 프레임을 가져온다.
      f = getzeroFTable(page->page, page->writable);      
      read_swap (f->address, (struct slot *)page->location);
      if(!install_page(page->page, f->address, page->writable)) {
        removeFTable(f);
        thread_exit();
      }
      freepageST (page);
      break;
  }
  
  // 이 시점에서는 다시 프레임이 evictable하도록 해줘야 한다.
  f->evictable = true;
}

void addframeST (struct frame *f, struct slot *s) {
  struct thread *old_owner = f->owner;
  struct page *p = (struct page *) malloc (sizeof(struct page));
  
  if(p==NULL) {
    printf ("Can't allocate the page.\n");
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

void destroyST () {
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
