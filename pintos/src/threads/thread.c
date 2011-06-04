#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"  /* bitmap 연산을 위해 include */
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Priority Scheduling을 위해 사용될 runqueue */
static struct runqueue run_queue;

/* Stack frame for kernel_thread() */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This cannot work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
	/* run_queue 초기화 */
	runqueue_init(&run_queue);

  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

	/* 기존 코드 : Enforce preemption
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
		*/

	/* 바꾼 코드 : struct thread 안에 timeslice 멤버를 추가하고 초기화시켰으므로
	   그 값과 thread_ticks를 비교하면 된다.
	 */
	if (++thread_ticks >= t->timeslice)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's "priority" member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the "stack" 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock()

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);

//	list_push_back (&ready_list, &t->elem);
  /* ready_list가 아니라 priority array에 삽입한다. 위 줄은 없애버려야한다.*/
	active_insert (&run_queue, t);	
  t->status = THREAD_READY;

	/* 단, 만약 running_thread보다 t 스레드의 우선순위가 더 높다면 
		 running_thread를 yield하고, t 스레드를 run해야한다.
		 단 running_thread가 idle_thread일 경우 예외로 한다.
		 t의 우선순위가 높다면 active_array 안의 모든 스레드 중
		 t의 우선순위가 가장 높을 것이다. (Priority Scheduling을 하고 있으므로)
		 그러므로 단순히 thread_yield()를 호출하면 t를 실행시킬 수 있다.*/
	struct thread *cur = thread_current();
	if (cur != idle_thread && t->priority > cur->priority) 
		thread_yield();

  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it call schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
	/* 기존 코드 : ready_list의 맨 뒤에 넣고 READY 상태로 변경
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY; */

	/* 변경한 코드 : active_array의 해당 priority queue의
		 맨 뒤에 넣고 READY 상태로 변경 */
	if (cur != idle_thread)
		active_insert (&run_queue, cur);
	cur->status = THREAD_READY;

  schedule ();
  intr_set_level (old_level);
}

/* Invoke function "func" on all threads, passing along "aux".
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See: [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->timeslice = priority + 5;  /* timeslice 초기화 */
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);

#ifdef USERPROG
	//Open File List 초기화
	list_init (&t->open_files);
	t->next_fd = 2;		// fd=0은 STDIN_FILEN, fd=1은 STDOUT_FILENO으로 예약되어있으므로 open file의 fd는 2에서 시작

	//Child 관련 초기화
	t->exited_by_exit_call = false;
	t->exit_status = -1;
	t->is_userprog = false;
	if (running_thread() != NULL && running_thread()->magic == THREAD_MAGIC)
		t->parent = running_thread();
	else
		t->parent = NULL;
	list_init(&t->childs);

	lock_init(&t->lock);
	t->file = NULL;

	//Lock List 초기화
	list_init (&t->locks);
#endif

#ifdef VM
	//Supplemental Page Table 초기화
	list_init(&t->sup_page_table);
	lock_init(&t->lock_spt);
	//Mapped file 관련 초기화
	t->mmid = 0;
	list_init(&t->mf_table);
	t->max_code_seg_addr = 0;
	
	sema_init(&t->sema_pf, 1);
#endif	

}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
	/* 기존 코드 
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem); */

	/* 변경한 코드 */
	/* run_queue가 비어있으면 idle_thread를 리턴한다.
		 active array만 비어있으면(모두 expired array에 있다면) array swap 한 후 
		 우선순위가 가장 높은 스레드를 꺼낸다.
		 active array가 비어있지 않으면 그 중 우선순위가 가장 높은 것을 꺼낸다. */
	if (runqueue_empty(&run_queue))
		return idle_thread;
	else if (prioarr_empty(run_queue.active))
		prioarr_swap(&run_queue);

	return active_remove(&run_queue);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
schedule_tail (struct thread *prev) 
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We do not free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until schedule_tail() has
   completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  schedule_tail (prev); 
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of "stack" member within "struct thread".
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);





/* "runqueue 초기화"
   두개의 priority array를 초기화하고 그 포인터를 active, expired에 할당한다.*/
void runqueue_init (struct runqueue *run_queue)
{
	prioarr_init (&run_queue->arrays[0]);
	prioarr_init (&run_queue->arrays[1]);

	run_queue->active = &run_queue->arrays[0];
	run_queue->expired = &run_queue->arrays[1];
}

/* priority array 초기화 */
void prioarr_init (struct prio_array *arr)
{
	int i;

	arr->num = 0;
	arr->map = bitmap_create_in_buf(PRI_MAX+1, arr->maparr, sizeof(arr->maparr));
	for (i = 0 ; i < PRI_MAX+1 ; i++ ) {
		list_init (&arr->queue[i]);
	}
}

/* run_queue가 비어있는지 검사. active, expired array가 모두 empty일 때 true 리턴 */
int runqueue_empty (struct runqueue *run_queue)
{
	return prioarr_empty(run_queue->active) && prioarr_empty(run_queue->expired);
}

int prioarr_empty (struct prio_array *arr)
{
	return arr->num == 0;
}

/* Priority Array에 스레드를 추가. active_insert, expired_insert의 내부함수로 사용됨*/
void prioarr_insert(struct prio_array* p_arr, struct thread* t)
{
	p_arr->num++;
	bitmap_mark(p_arr->map, PRI_MAX - (t->priority));
	list_push_back (&p_arr->queue[t->priority], &t->elem);
}

struct thread *prioarr_remove(struct prio_array *p_arr)
{
	struct thread* t;

	ASSERT(p_arr->num != 0);

	size_t first_bit = bitmap_scan(p_arr->map, 0, 1, true);
	int highest_prio = PRI_MAX - first_bit;
	t = list_entry(list_pop_front (&p_arr->queue[highest_prio]), struct thread, elem );
	
	if (list_empty(&p_arr->queue[highest_prio]))
		bitmap_reset(p_arr->map, first_bit);

	p_arr->num--;

	return t;
}
	
/* Active Priority Array에 스레드 추가
   queue에서 priority에 맞는 list에 바로 접근하여 맨 뒤에 삽입한다.*/
void active_insert (struct runqueue *run_queue, struct thread* t)
{
	prioarr_insert (run_queue->active, t);
}

/* Expired Array에 스레드 추가.
   추가되기 전에 반드시 timeslice가 recompute 되도록 해주자.
   remove 함수는 구현할 필요 없다. Expired Array에서 remove는 일어날리 없다. */
void expired_insert (struct runqueue *run_queue, struct thread* t)
{
	recompute_tslice (t);	
	prioarr_insert (run_queue->expired, t);
}

/* Active Priority Array에서 스레드 제거
   우선순위가 가장 높은 스레드 중 가장 먼저 들어온 것을 pop */
struct thread *active_remove (struct runqueue *run_queue)
{
	return prioarr_remove(run_queue->active);
}

/* 두 Priority Array를 swap한다. pointer swap이므로 O(1) 시간이 걸린다. */
void prioarr_swap (struct runqueue *run_queue)
{
	struct prio_array *temp;

	ASSERT(run_queue->active->num == 0);

	temp = run_queue->active;
	run_queue->active = run_queue->expired;
	run_queue->expired = temp;
}

/* timeslice를 다시 계산하는 함수
   timeslice가 expire되어 Expired Array로 넘어갈때 다시 계산된다.*/
void recompute_tslice (struct thread* t)
{
	t->timeslice = t->priority + 5;
}

