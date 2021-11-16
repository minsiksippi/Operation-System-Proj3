#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"  



enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };


typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
extern bool thread_prior_aging;

struct thread
  {
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    struct thread *parent;
    struct list_elem child_elem;
    struct list child;
    bool loadingswitch;
    bool endingswitch;
    bool waitswitch;
    int exit_status;
	int nice;
	int recent_cpu;

    struct semaphore sema_exit;
    struct semaphore sema_load;
    struct semaphore mem_lock;
    struct file *file[130];
    int64_t wake_up;
    unsigned magic;                     /* Detects stack overflow. */
  };

extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);
void thread_tick (void);
void thread_print_stats (void);
void thread_block(void);
void thread_unblock(struct thread *);
void thread_exit(void) NO_RETURN;
void thread_yield(void);

typedef void thread_func (void *aux);
struct thread *thread_current (void);
tid_t thread_tid (void);
tid_t thread_create(const char *name, int priority, thread_func *, void *);
const char *thread_name (void);

typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
int thread_get_nice (void);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
int CalculatefNumber(int a, int b, int mode, bool int_flag);
bool biggerprior(const struct list_elem *a, const struct list_elem *b, void *aux);

void thread_set_nice(int);
void thread_set_priority(int);
void thread_wake_up (void);
void thread_aging (int mode);

#endif /* threads/thread.h */
