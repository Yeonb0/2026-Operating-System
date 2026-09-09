#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* [1-1-7] exec, wait : 부모-자식 동기화에 세마포어를 사용하기 위해 포함
   참고 : threads/synch.h
   주의 : synch.h 는 thread.h 를 포함하지 않으므로 순환 참조가 생기지 않는다
          struct lock 안의 struct thread * 는 불완전 타입으로도 문제가 없다 */
#include "threads/synch.h"
#include <stdbool.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
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
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */

    /* [1-1-5] Process Termination Message : 종료 상태 저장
       목적 : exit() 이 넘긴 종료 상태를 process_exit() 이 출력할 때까지 보관한다
       참고 : Pintos manual 3.3.2, proj1 슬라이드 31 - 32
              슬라이드 32 가 "프로세스 이름은 struct thread 를 참고하라" 고 안내하며,
              종료 상태도 같은 구조체에 두는 것이 자연스럽다
       주의 : 초기값은 start_process() 에서 -1 로 설정한다
              page fault 등으로 강제 종료되는 경로는 exit() 을 거치지 않는데,
              그때 기대되는 종료 코드가 -1 이기 때문이다
              1-1-7 의 wait 구현에서 부모가 이 값을 읽어 간다 */
    int exit_status;                    /* Exit status for termination message. */

    /* [1-1-7] exec, wait : 부모-자식 프로세스 관리
       목적 : 부모가 자식의 적재 결과와 종료 상태를 기다릴 수 있게 한다
       참고 : Pintos manual 3.3.4 exec / wait, proj1 슬라이드 44
       주의 : Load_sema  - 부모가 process_execute() 에서 자식의 load 완료를 기다린다
              Exit_sema  - 부모가 process_wait() 에서 자식의 종료를 기다린다
              Destroy_sema - 자식이 부모에게 종료 상태를 넘겨줄 때까지
                             자신의 struct thread 가 해제되지 않도록 붙잡는다
              세 개를 분리한 이유는 각각 기다리는 주체와 시점이 다르기 때문이다
              이 구조는 Prj1-2 와 Project 2 에서 그대로 재사용된다 */
    struct thread *Parent;              /* Parent process. */
    struct list Child_list;             /* List of child processes. */
    struct list_elem Child_elem;        /* Element in parent's Child_list. */
    bool is_loaded;                     /* True if executable loaded. */
    struct semaphore Load_sema;         /* Signals load completion. */
    struct semaphore Exit_sema;         /* Signals process exit. */
    struct semaphore Destroy_sema;      /* Waits until parent reaps status. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

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

#endif /* threads/thread.h */