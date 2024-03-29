#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you likes. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
		unsigned timeslice;				/* Timeslice : 여기선 priority + 5로 정의할 것이다. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
		int exit_status;
		bool exited_by_exit_call;	 /* user process이고 exit()로 종료된 경우 */
		bool is_userprog;					 /* 이 스레드가 user program인지를 나타냄 */
		struct list open_files;    /* Open File들의 리스트 */
		int next_fd;

		struct list childs;
		struct thread *parent;

		struct file *file;
		struct lock lock;
		struct list locks;									/* 이 스레드가 acquire하고 있는 locks */
#endif

#ifdef VM
		struct list sup_page_table;   /* 이 스레드가 갖고 있는 페이지의 리스트
															    (이 페이지들은 main memory에 없는 것들!) */
		struct lock lock_spt;			    /* sup_page_table을 위한 lock */
		
		struct list mf_table;         /* 이 스레드의 mapped file의 테이블 */
		uint32_t mmid;								/* 메모리 안의 mapped files의 첫번째 descriptor */
		void *max_code_seg_addr;      /* code/data segment의 maximum address */
		struct semaphore sema_pf;     /* page fault를 위한 세마포 */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

 };

struct open_file
{
	int fd;
	struct file *file;
	struct list_elem elem;
};

struct child_process
{
	tid_t pid_t;
	int exit_status;
	struct semaphore sema;
	struct list_elem elem;
};

struct mapped_file
{
	uint32_t mapid;
	struct file *file;
	uint8_t *addr;
	uint32_t size;
	struct list_elem elem;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

/* Priority Array 관련 구조체 */
struct prio_array
{
	int num;    // the number of tasks
	unsigned long maparr[PRI_MAX+1];  // 실제 bitmap 데이터가 저장될 배열
	struct bitmap *map;								// bitmap 연산을 위한 구조체 포인터
	struct list queue[PRI_MAX+1];  /* 우선순위가 같은 task들을 저장할 큐 */
};

/* 두 priority array를 하나의 구조체로 묶어 관리한다. */
struct runqueue
{
	struct prio_array *active;    /* Active Priority Array를 가리키는 포인터 */
	struct prio_array *expired;   /* Expried Priority Array를 가리키는 포인터 */
	struct prio_array arrays[2];  /* 실제로 두 Priority Array를 보관하는 배열 */
};


void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);


/* Priority scheduler를 위한 함수들 */
void runqueue_init (struct runqueue *);
void prioarr_init (struct prio_array *);

int runqueue_empty (struct runqueue *);
int prioarr_empty (struct prio_array *);

void prioarr_insert(struct prio_array* , struct thread* );
struct thread *prioarr_remove(struct prio_array *);
void active_insert (struct runqueue *, struct thread *);
struct thread *active_remove (struct runqueue *);
void expired_insert (struct runqueue *, struct thread *);

void prioarr_swap (struct runqueue *); 
void recompute_tslice (struct thread *);


#endif /* threads/thread.h */
