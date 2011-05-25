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

static struct list frmList;
static struct lock lock;
static struct list_elem *vicFrame;

// 테이블 프레임의 구성요소들을 초기화한다.
void initFTable () {
  list_init (&frmList);
  lock_init (&lock);
  vicFrame = NULL;
}

// 프레임 테이블을 리턴한다.
struct list* retFTable () {
  return &frmList;
}

// 프레임 테이블에서 주어진 프레임을 찾는다. 만약 없으면 NULL을 리턴한다.
struct frame* scanFTable (void *_frame)
{
  struct list *list = retFTable ();
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
  return NULL;
}

// 가장 오래 사용되지 않은 프레임을 찾기 위해 선언된 함수
struct frame* getminFTable ()
{
  struct list *list = retFTable ();
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

// 주어진 프레임을 프레임 테이블에 삽입한다. 중복일 경우 삽입하지 않는다.
struct frame *insertFTable (void *_frame, void *upage, bool writable)
{
  struct frame* f;

  f = (struct frame *) malloc (sizeof(struct frame));

  if(f == NULL) // 이 경우는 대개 예측 불가능한 에러이므로 쓰레드를 종료한다.
    thread_exit ();

  f->address = _frame;
  f->owner = thread_current ();
  f->page = upage;
  f->writable = writable;
  f->evictable = false;

  lock_acquire(&lock);
  list_push_front(retFTable (), &f->elem);
  lock_release(&lock);
  
  return f;
}

// 유저 풀에서 새 zeroed frame을 리턴한다.
struct frame *getzeroFTable (void *upage, bool writable) {
  struct frame *f = NULL;
  struct slot *slot;
  struct page *p;
  void * user_page = palloc_get_page (PAL_USER | PAL_ZERO);
  bool dirty;
  enum intr_level prev;
  
  // 메인 메모리에서 페이지를 받는다.
  if(user_page == NULL) {
    lock_acquire (&lock);
    
	// 프레임 테이블에서 victim을 찾아서 받는다.
    f = getvictimFTable ();
    
    if(f==NULL)
    {
      lock_release (&lock);
      printf ("Failed to find the victim frame.\n");
      thread_exit ();
    }
    
    // 스왑이나 파일 시스템에 프레임을 쓴다.
    p = scanST_ext (f->page, f->owner);
    if (p!=NULL)
    {
      // 경우에 따라 중지한다.
      sema_down (&f->owner->sema_pf);
      
      prev = intr_disable ();
      dirty = pagedir_is_dirty (f->owner->pagedir, f->page);
      pagedir_clear_page (f->owner->pagedir, f->page);
      intr_set_level (prev);
      
      if(dirty && f->writable)
        if(file_write_at ((struct file *)p->location, f->address, p->read_b, p->ofs) != (int)p->read_b)
          thread_exit ();
      
      // 정상적으로 진행한다.
      sema_up (&f->owner->sema_pf);
    }
    else
    {
      // 필요한 경우 중지.
      sema_down (&f->owner->sema_pf);
      
      pagedir_clear_page (f->owner->pagedir, f->page); 
      // 선택된 프레임의 내용을 쓴다.
      slot = write_swap (f->address);
      /* 선택된 프레임을 프레임을 주려 하는
      	 프로세스의 supplemental table에 추가한다. */
      addframeST (f, slot);
      
      // 계속 진행.
      sema_up (&f->owner->sema_pf);
    }
    
    lock_release (&lock);
    
    // 주어진 프레임을 zerofy한다.
    memset (f->address, 0, PGSIZE);
    
    // 현재 쓰레드의 주인에 대한 프레임을 만들고, 현재 시간을 받는다.
    f->owner = thread_current ();
    f->page = upage;
    f->writable = writable;
    f->finish = rtc_get_time();
  } 
  else {
    f = insertFTable (user_page, upage, writable);
    f->finish = rtc_get_time();
  }
  return f;
}

// LRU 알고리즘을 이용하여 victim을 찾는다.
struct frame *getvictimFTable () {
  struct list_elem *temp = NULL;
  struct frame *f;

  if(!list_empty (retFTable ())) {
    if(vicFrame == NULL)
        vicFrame = getminFTable ();

    while(true)
    {
      f = list_entry (vicFrame, struct frame, elem);
      if(f->evictable)
      {
        if(!pagedir_is_accessed (f->owner->pagedir, f->page))
        {
          temp = vicFrame;
    
          if(list_next (vicFrame) == list_end (retFTable ()))
            vicFrame = list_begin (retFTable ());
          else
            vicFrame = list_next (vicFrame);
            
          break;
        }
        else
          pagedir_set_accessed (f->owner->pagedir, f->page, false);
      }
      else
      {
        if(list_next (vicFrame) == list_end (retFTable ()))
          vicFrame = list_begin (retFTable ());
        else
          vicFrame = list_next (vicFrame);
      }
    }
    
    f->evictable = false;
    return f;
  }
  
  return NULL;
}

// 프레임 테이블에서 주어진 프레임을 찾아 제거한다.
void removeFTable (struct frame *f) {
  struct frame *temp;
  
  if(f != NULL) 
  {
    lock_acquire (&lock);
    
    temp = scanFTable (f);
    
    if(temp != NULL) {
      palloc_free_page (f->address);
      list_remove (&f->elem);
    }
    
    lock_release (&lock); 
  }
}

// 주어진 쓰레드와 관련된 모든 프레임들을 제거한다.
void raFTable (struct thread *t)
{
  struct list_elem *e;
  struct list_elem *temp;
  struct frame *f;
  
  lock_acquire (&lock);
  for(e=list_begin (retFTable ()); e!=list_end (retFTable ());)
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

// 프레임 테이블 내의 모든 프레임들에 관련된 accessed bit들을 새로 고친다.
void ref_accessed_bitFTable ()
{
  struct list_elem *e;
  struct frame *cf;
  
  for(e=list_begin (retFTable ()); e!=list_end (retFTable ()); e=list_next(e)) 
  {
    cf = list_entry (e, struct frame, elem);

    pagedir_set_accessed (cf->owner->pagedir, cf->page, false);
  }
}
