#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

#define THREAD_MAGIC 0xcd6abf4b

static struct list ready_list;
static struct list all_list;
static struct thread *idle_thread;
static struct thread *initial_thread;
static struct lock tid_lock;

struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

bool thread_mlfqs;
bool thread_prior_aging;
static struct thread *running_thread(void);
static struct thread *next_thread_to_run(void);

static int averageloading;
static bool is_thread(struct thread *) UNUSED;
static void kernel_thread (thread_func *, void *aux);
static void idle (void *aux UNUSED);
static void init_thread (struct thread *, const char *name, int priority);
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  averageloading = 0;
  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  initial_thread->nice = 0;
  initial_thread->recent_cpu = 0;
}

void
thread_start (void) 
{
  struct semaphore start_idle;
  sema_init (&start_idle, 0);
  thread_create ("idle", PRI_MIN, idle, &start_idle);
  intr_enable ();
  sema_down (&start_idle);
}

void
thread_tick (void) 
{
  struct thread *t = thread_current ();
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();

}

void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

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

  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();
  old_level = intr_disable ();

  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;
  intr_set_level (old_level);
  thread_unblock (t);

  if (priority > thread_get_priority()) thread_yield();
  return tid;
}

void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;
  ASSERT (is_thread (t));
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered(&ready_list, &t->elem, biggerprior, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

const char *
thread_name (void) 
{
  return thread_current ()->name;
}

struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);
  return t;
}

tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered(&ready_list, &cur->elem, biggerprior, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

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

void
thread_set_priority (int new_priority) 
{
	int priority;
	if (!thread_mlfqs)
	{
		struct thread *threadofcur = thread_current();
		priority = threadofcur->priority;  
		threadofcur->priority = new_priority;
		if (priority > new_priority)
		thread_yield();
	}
}

int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

void
thread_set_nice (int nice) 
{
  int secondpr;
  struct thread *threadofcur = thread_current();

  threadofcur->nice = nice;
  secondpr = CalculatefNumber(CalculatefNumber(CalculatefNumber(PRI_MAX, 0, 0, 1), CalculatefNumber(4, threadofcur->recent_cpu, 3, 1), 1, 0), CalculatefNumber(2, CalculatefNumber(threadofcur->nice, 0, 0, 1), 2, 1), 1, 0) / (1 << 14);

  if (secondpr > PRI_MAX) secondpr = PRI_MAX;
  else if (secondpr < PRI_MIN) secondpr = PRI_MIN;
  thread_set_priority(secondpr);
}

int
thread_get_nice (void) 
{
  return thread_current()->nice;
}

int
thread_get_load_avg (void) 
{
  return CalculatefNumber(100, averageloading, 2, 1) / (1 << 14);
}

int
thread_get_recent_cpu (void) 
{
  return CalculatefNumber(100, thread_current()->recent_cpu, 2, 1) / (1 << 14);
}

static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      intr_disable ();
      thread_block ();
      asm volatile ("sti; hlt" : : : "memory");
    }
}

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
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

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
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);
  t->nice = running_thread()->nice;
  t->recent_cpu = running_thread()->recent_cpu;

#ifdef USERPROG
  t->parent = running_thread();
  sema_init(&(t->sema_exit), 0);
  sema_init(&(t->sema_load), 0);
  sema_init(&(t->mem_lock),0);
  t->endingswitch = 0;
  t->loadingswitch = 0;
  t->waitswitch = 0;
  list_init(&(t->child));
  list_push_back(&(running_thread()-> child), &(t->child_elem));
  for(int i = 0; i < 128; i++)
    t->file[i] = NULL;

#endif
}

static void *
alloc_frame (struct thread *t, size_t size) 
{
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);
  cur->status = THREAD_RUNNING;
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

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
  thread_schedule_tail (prev);
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

uint32_t thread_stack_ofs = offsetof (struct thread, stack);

bool
biggerprior(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	int firstnum, secondnum;

	firstnum = list_entry(a, struct thread, elem)->priority;
	secondnum = list_entry(b, struct thread, elem)->priority;

	if (firstnum > secondnum) return 1;
	else return 0;
}


void
thread_aging (int mode)
{
	struct thread *operthread, *threadcurr = thread_current();
	struct list_elem *elemfind = list_begin(&all_list);
	int bulk, priority;

	bulk = list_size(&ready_list);
	if (!mode)
	{
		if (threadcurr != idle_thread) bulk = bulk + 1;
		averageloading = CalculatefNumber(60, CalculatefNumber(bulk, CalculatefNumber(59, averageloading, 2, 1), 0, 1), 3, 1);

		while (1)
		{
			if (elemfind == list_end(&all_list)) break;
			operthread = list_entry(elemfind, struct thread, allelem);
			if (operthread != idle_thread)
			{
				operthread->recent_cpu = CalculatefNumber(operthread->nice, CalculatefNumber(CalculatefNumber(CalculatefNumber(2, averageloading, 2, 1), CalculatefNumber(1, CalculatefNumber(2, averageloading, 2, 1), 0, 1), 3, 0), operthread->recent_cpu, 2, 0), 0, 1);
			}
			elemfind = list_next(elemfind);
		}
	}
	else if (mode)
	{
		elemfind = list_begin(&all_list);
		while (1)
		{
			if (elemfind == list_end(&all_list)) break;
			operthread = list_entry(elemfind, struct thread, allelem);
			priority = CalculatefNumber(CalculatefNumber(CalculatefNumber(PRI_MAX, 0, 0, 1), CalculatefNumber(4, operthread->recent_cpu, 3, 1), 1, 0), CalculatefNumber(2, CalculatefNumber(operthread->nice, 0, 0, 1), 2, 1), 1, 0) / (1 << 14);

			if (priority > PRI_MAX) priority = PRI_MAX;
			else if (priority < PRI_MIN) priority = PRI_MIN;
			operthread->priority = priority;
			elemfind = list_next(elemfind);
		}
		if (!list_empty(&ready_list))
		{
			elemfind = list_front(&ready_list);
			operthread = list_entry(elemfind, struct thread, elem);
			priority = operthread->priority;
			if (threadcurr->priority < priority) intr_yield_on_return();
		}
	}
}

int 
CalculatefNumber(int a, int b, int mode, bool flag)
{
	int64_t tempnum, ansnum = 0;

	switch (mode)
	{
	case 0:
		if (flag) ansnum = a * (1 << 14) + b;
		else ansnum = a + b;
		break;

	case 1:
		if (flag) ansnum = a * (1 << 14) - b;
		else ansnum = a - b;
		break;

	case 2:
		if (flag) ansnum = a * b;
		else
		{
			tempnum = a;
			tempnum = tempnum * b / (1 << 14);
			ansnum = tempnum;
		}
		break;

	case 3:
		if (flag) ansnum = b / a;
		else
		{
			tempnum = a;
			tempnum = tempnum * (1 << 14) / b;
			ansnum = tempnum;
		}
		break;
	default:
		break;
	}
	return (int)ansnum;
}

