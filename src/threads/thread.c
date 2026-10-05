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
/* [2-3-1] 수정 : 고정소수점 헤더 포함 추가
   변경 내용 : #include "threads/fixed-point.h" 한 줄 추가
   변경 이유 : BSD Scheduler 의 recent_cpu, load_avg 계산에 쓰기 위해
   영향 범위 : 아직 호출하는 곳이 없어 동작은 같다 */
#include "threads/fixed-point.h"
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

/* [2-1-2] Alarm Clock : 잠든 스레드 목록
   목적 : 잠든 스레드를 wakeup_tick 오름차순으로 보관
   참고 : Pintos manual 2.2.2, 조교 슬라이드 24
   주의 : 접근할 때는 인터럽트가 꺼져 있어야 한다 */
static struct list Sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
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

/* [2-2-4] Priority Aging : aging 사용 여부 플래그 정의
   목적 : 커널 옵션 -aging 이 주어졌는지 저장한다 (기본값 false)
   참고 : 조교 슬라이드 30 - 31
   주의 : -aging 커널 옵션이 있을 때만 true, userprog 빌드에서는 선언되지 않는다 */
#ifndef USERPROG
/* Project #3. */
bool thread_prior_aging;
#endif

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* [2-3-2] BSD Scheduler : 시스템 전체 load_avg
   목적 : ready 상태 스레드 수의 평균 추정값을 저장한다
   참고 : 조교 슬라이드 39, Pintos manual B.4
   주의 : 17.14 고정소수점 값이다, 부팅 때 0 으로 초기화한다 */
static int load_avg;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* [2-1-2] Alarm Clock : Sleep_list 정렬용 비교 함수 선언 */
static bool wakeup_tick_less (const struct list_elem *a,
                              const struct list_elem *b, void *aux UNUSED);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
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
  /* [2-1-2] 수정 : Sleep_list 초기화 추가
     변경 내용 : list_init (&Sleep_list) 한 줄 추가
     변경 이유 : thread_sleep () 이 쓰기 전에 목록을 초기화해야 한다
     영향 범위 : thread_init () 만, 기존 동작은 그대로 */
  list_init (&Sleep_list);
  list_init (&all_list);
  /* [2-3-2] 수정 : load_avg 초기화 추가
     변경 내용 : load_avg = fp_from_int (0) 한 줄 추가
     변경 이유 : 슬라이드 39 - 부팅 때 load_avg 는 0 이다
     영향 범위 : thread_init () 만, 기존 동작은 그대로 */
  load_avg = fp_from_int (0);

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

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();

  /* [2-1-2] 수정 : 깨우기 호출 추가
     변경 내용 : 선점 처리 아래에 thread_wake_up () 호출 추가
     변경 이유 : 매 tick 마다 깰 시간이 된 스레드를 깨우기 위해
     영향 범위 : thread_tick () 만, 호출하는 쪽이 없으면 Sleep_list 가
                 비어 있어 동작이 같다 */
  /* [2-1-2] Alarm Clock : 매 tick 마다 잠든 스레드 깨우기
     목적 : wakeup_tick 이 지난 스레드를 Sleep_list 에서 꺼내 깨운다
     입력 : 없음
     출력 : 없음
     참고 : Pintos manual 2.2.2, 조교 슬라이드 24, 조교 슬라이드 31
     주의 : 슬라이드 31 배치를 따라 #ifndef USERPROG 안에 둔다,
            userprog 빌드에서는 호출되지 않는다 */
#ifndef USERPROG
  /* Project #3. */
  thread_wake_up ();

  /* [2-2-5] 수정 : aging 호출 추가
     변경 내용 : 기존 #ifndef USERPROG 블록 안에 thread_aging () 호출 추가
     변경 이유 : -aging 일 때 매 tick 마다 ready_list 스레드의 우선순위를 올리기 위해
                 (조교 슬라이드 31)
     영향 범위 : thread_tick () 만, -aging 이 없으면 동작이 같다 */
  /* Project #3. */
  if (thread_prior_aging == true)
    thread_aging ();

  /* [2-3-3] 수정 : mlfqs 처리 호출 추가
     변경 내용 : 기존 #ifndef USERPROG 블록 안에 thread_mlfqs_tick () 호출 추가
     변경 이유 : -mlfqs 일 때 매 tick 마다 recent_cpu, load_avg, 우선순위를 갱신하기 위해
     영향 범위 : thread_tick () 만, -mlfqs 가 없으면 동작이 같다 */
  if (thread_mlfqs)
    thread_mlfqs_tick ();
#endif
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

   The code provided sets the new thread's `priority' member to
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

#ifdef USERPROG
  /* [1-1-7] exec, wait : 생성된 스레드를 부모의 자식 목록에 등록
     목적 : 이후 process_wait() 이 tid 로 자식을 찾을 수 있게 한다
     참고 : proj1 슬라이드 44 - wait 은 자식 스레드 ID 가 유효한지 확인해야 한다
     주의 : thread_create() 은 thread_init() 이 끝난 뒤에만 호출되므로
            이 시점에는 thread_current() 를 안전하게 쓸 수 있다
            커널 스레드도 함께 등록되지만 pagedir 이 NULL 이라
            종료 메시지나 wait 대상이 되지 않으므로 문제가 없다 */
  t->Parent = thread_current ();
  list_push_back (&thread_current ()->Child_list, &t->Child_elem);
#endif

  /* [2-3-2] 수정 : nice, recent_cpu 상속과 초기 우선순위 계산 추가
     변경 내용 : 부모의 nice, recent_cpu 를 복사하고 thread_mlfqs 이면 우선순위를 계산
     변경 이유 : 슬라이드 36, 38 - 부모 값을 물려받는다,
                 슬라이드 37 - 초기 우선순위는 thread_create () 에서 정한다
     영향 범위 : thread_create () 만, thread_mlfqs 가 아니면 우선순위는 인자 그대로다,
                 thread_unblock () 의 정렬 삽입 전에 우선순위가 정해진다 */
  old_level = intr_disable ();
  t->nice = thread_current ()->nice;
  t->recent_cpu = thread_current ()->recent_cpu;
  /* [2-3-3] 수정 : idle 스레드를 초기 우선순위 계산에서 제외
     변경 내용 : 조건을 thread_mlfqs && function != idle 로 바꿈
     변경 이유 : idle 이 공식으로 63 이 되면 깨운 스레드가 idle 을 선점하지 못한다,
                 원본처럼 idle 은 PRI_MIN 을 유지한다
     영향 범위 : thread_create () 만, idle 이 아닌 스레드는 2-3-2 와 같다 */
  if (thread_mlfqs && function != idle)
    thread_calc_priority (t);
  intr_set_level (old_level);

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

  /* Add to run queue. */
  thread_unblock (t);

  /* [2-2-2] 수정 : 생성 직후 선점 검사 추가
     변경 내용 : thread_unblock (t) 다음에 thread_check_preempt () 호출 추가
     변경 이유 : 새 스레드의 우선순위가 더 높으면 바로 양보하기 위해
     영향 범위 : thread_create () 만, 반환값은 그대로 */
  thread_check_preempt ();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

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
  /* [2-2-1] 수정 : ready_list 삽입을 우선순위 정렬 삽입으로 교체
     변경 내용 : list_push_back 을 list_insert_ordered 로 교체
     변경 이유 : ready_list 를 우선순위 내림차순으로 유지하기 위해
     영향 범위 : thread_unblock () 만, 인터럽트 구간과 양보 없음은 그대로 */
  list_insert_ordered (&ready_list, &t->elem, thread_priority_greater, NULL);
  t->status = THREAD_READY;
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
     have overflowed its stack.  Each thread has less than 4 kB
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
     when it calls thread_schedule_tail(). */
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
  if (cur != idle_thread) 
    /* [2-2-1] 수정 : ready_list 삽입을 우선순위 정렬 삽입으로 교체
       변경 내용 : list_push_back 을 list_insert_ordered 로 교체
       변경 이유 : ready_list 를 우선순위 내림차순으로 유지하기 위해
       영향 범위 : thread_yield () 만, idle 스레드 제외 조건은 그대로 */
    list_insert_ordered (&ready_list, &cur->elem, thread_priority_greater,
                         NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* [2-1-2] Alarm Clock : 현재 스레드를 WAKEUP_TICK 까지 재운다
   목적 : 현재 스레드를 Sleep_list 에 정렬 삽입하고 BLOCKED 로 만든다
   입력 : wakeup_tick - 깨어날 절대 tick
   출력 : 없음
   참고 : Pintos manual 2.2.2, 조교 슬라이드 24
   주의 : 인터럽트를 끈 상태에서 목록을 조작하고 thread_block () 을 부른다
          idle 스레드는 재울 수 없다 */
void
thread_sleep (int64_t wakeup_tick)
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

  old_level = intr_disable ();
  ASSERT (cur != idle_thread);

  cur->wakeup_tick = wakeup_tick;
  list_insert_ordered (&Sleep_list, &cur->elem, wakeup_tick_less, NULL);
  thread_block ();
  intr_set_level (old_level);
}

/* [2-1-2] Alarm Clock : 깰 시간이 된 스레드를 모두 깨운다
   목적 : Sleep_list 앞쪽부터 wakeup_tick <= 현재 tick 인 스레드를 unblock
   입력 : 없음
   출력 : 없음
   참고 : Pintos manual 2.2.2, 조교 슬라이드 24
   주의 : timer_interrupt () -> thread_tick () 경로의 인터럽트 컨텍스트에서
          불린다, thread_unblock () 만 쓰고 yield · block · sema_down ·
          lock_acquire 는 부르지 않는다 */
void
thread_wake_up (void)
{
  int64_t now = timer_ticks ();

  while (!list_empty (&Sleep_list))
    {
      struct thread *t = list_entry (list_front (&Sleep_list),
                                     struct thread, elem);
      if (t->wakeup_tick > now)
        break;
      list_pop_front (&Sleep_list);
      thread_unblock (t);
    }

  /* [2-2-2] 수정 : 깨운 뒤 선점 검사 추가
     변경 내용 : while 루프 뒤에 thread_check_preempt () 호출 추가
     변경 이유 : 깨운 스레드가 더 높으면 인터럽트 복귀 시 양보하기 위해
     영향 범위 : thread_wake_up () 만, 인터럽트 컨텍스트라 intr_yield_on_return ()
                 이 쓰인다 */
  thread_check_preempt ();
}

/* [2-1-2] Alarm Clock : Sleep_list 정렬 비교 함수
   목적 : wakeup_tick 오름차순 정렬 기준 제공
   입력 : a, b - 비교할 list_elem
   출력 : a 의 wakeup_tick 이 b 보다 작으면 true
   참고 : Pintos manual 2.2.2, 조교 슬라이드 24
   주의 : 같으면 false, 같은 tick 끼리는 먼저 잔 순서가 유지된다 */
static bool
wakeup_tick_less (const struct list_elem *a, const struct list_elem *b,
                  void *aux UNUSED)
{
  return list_entry (a, struct thread, elem)->wakeup_tick
         < list_entry (b, struct thread, elem)->wakeup_tick;
}

/* [2-2-1] Priority Scheduling : ready_list 우선순위 비교 함수
   목적 : priority 내림차순 정렬 기준 제공
   입력 : a, b - 비교할 list_elem
   출력 : a 의 priority 가 b 보다 크면 true, 같거나 작으면 false
   참고 : Pintos manual 2.2.3, 조교 슬라이드 25 - 27
   주의 : 같은 우선순위는 false 를 돌려 삽입 순서를 유지한다,
          2-2-3 에서 synch.c 도 사용한다 */
bool
thread_priority_greater (const struct list_elem *a, const struct list_elem *b,
                         void *aux UNUSED)
{
  return list_entry (a, struct thread, elem)->priority
         > list_entry (b, struct thread, elem)->priority;
}

/* [2-2-2] Priority Scheduling : ready_list 맨 앞이 더 높으면 양보
   목적 : ready_list 맨 앞 스레드의 priority 가 현재보다 크면 CPU 를 양보
   입력 : 없음
   출력 : 없음
   참고 : Pintos manual 2.2.3, 조교 슬라이드 26 - 27
   주의 : 같은 우선순위면 양보하지 않는다,
          인터럽트 컨텍스트에서는 intr_yield_on_return () 을 쓴다,
          thread_unblock () 은 선점하지 않으므로 unblock 한 쪽이 이 함수를 부른다 */
void
thread_check_preempt (void)
{
  enum intr_level old_level;
  bool need_yield = false;

  old_level = intr_disable ();
  if (!list_empty (&ready_list)
      && list_entry (list_front (&ready_list), struct thread, elem)->priority
         > thread_current ()->priority)
    need_yield = true;
  intr_set_level (old_level);

  if (need_yield)
    {
      if (intr_context ())
        intr_yield_on_return ();
      else
        thread_yield ();
    }
}

/* [2-2-5] Priority Aging : ready_list 스레드 우선순위 1 씩 올리기
   목적 : ready 에 오래 머문 스레드가 결국 실행되도록 우선순위를 올린다
   입력 : 없음
   출력 : 없음
   참고 : 조교 슬라이드 30, 32
   주의 : 매 tick 마다 ready_list 의 모든 스레드를 1 씩 올리므로 ready 에 머문 시간에 비례한다,
          모두 같은 폭으로 오르므로 내림차순이 유지되어 재정렬하지 않는다,
          PRI_MAX 에서 멈춘다, 인터럽트 컨텍스트에서 불린다 */
void
thread_aging (void)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&ready_list); e != list_end (&ready_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, elem);
      if (t->priority < PRI_MAX)
        t->priority++;
    }

  thread_check_preempt ();
}

/* [2-3-2] BSD Scheduler : nice 와 recent_cpu 로 우선순위 다시 계산
   목적 : T 의 priority 를 PRI_MAX - (recent_cpu / 4) - (nice * 2) 로 정한다
   입력 : T - 우선순위를 다시 계산할 스레드
   출력 : 없음, T->priority 를 바꾼다
   참고 : 조교 슬라이드 37, 40, Pintos manual B.2
   주의 : 소수점 아래는 버리고 PRI_MIN ~ PRI_MAX 로 자른다,
          ready_list 위치와 선점은 호출한 쪽이 맞춘다,
          인터럽트를 끈 상태에서 부른다 */
void
thread_calc_priority (struct thread *t)
{
  int priority;

  ASSERT (intr_get_level () == INTR_OFF);

  priority = fp_to_int_zero (fp_sub_int (fp_sub (fp_from_int (PRI_MAX),
                                                 fp_div_int (t->recent_cpu, 4)),
                                         t->nice * 2));
  if (priority < PRI_MIN)
    priority = PRI_MIN;
  else if (priority > PRI_MAX)
    priority = PRI_MAX;
  t->priority = priority;
}

/* [2-3-3] BSD Scheduler : 한 스레드의 recent_cpu 재계산
   목적 : recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice 를 적용한다
   입력 : T - 대상 스레드, AUX - 쓰지 않음
   출력 : 없음, T->recent_cpu 를 바꾼다
   참고 : 조교 슬라이드 38, Pintos manual B.3
   주의 : 넘침을 막기 위해 계수를 먼저 구한 뒤 곱한다,
          thread_foreach () 로 모든 스레드에 1초마다 부른다 */
static void
thread_update_recent_cpu (struct thread *t, void *aux UNUSED)
{
  int coef = fp_div (fp_mul_int (load_avg, 2),
                     fp_add_int (fp_mul_int (load_avg, 2), 1));
  t->recent_cpu = fp_add_int (fp_mul (coef, t->recent_cpu), t->nice);
}

/* [2-3-3] BSD Scheduler : 한 스레드의 우선순위 재계산
   목적 : thread_foreach () 로 모든 스레드의 우선순위를 다시 계산한다
   입력 : T - 대상 스레드, AUX - 쓰지 않음
   출력 : 없음, T->priority 를 바꾼다
   참고 : 조교 슬라이드 37, 40
   주의 : idle 스레드는 PRI_MIN 을 유지하도록 건너뛴다 */
static void
thread_update_priority (struct thread *t, void *aux UNUSED)
{
  if (t != idle_thread)
    thread_calc_priority (t);
}

/* [2-3-3] BSD Scheduler : 매 tick 의 mlfqs 처리
   목적 : recent_cpu 증가, 1초마다 load_avg 와 recent_cpu 재계산, 4 tick 마다 우선순위 재계산
   입력 : 없음
   출력 : 없음
   참고 : 조교 슬라이드 37 - 40, Pintos manual B.2 - B.4
   주의 : 1초마다 load_avg 를 먼저 갱신하고 그 값으로 recent_cpu 를 계산한다,
          ready_threads 와 recent_cpu 증가에서 idle 은 뺀다,
          1개 큐(정렬된 ready_list)를 쓰므로 우선순위 재계산 뒤 다시 정렬한다,
          인터럽트 컨텍스트에서 불린다 */
void
thread_mlfqs_tick (void)
{
  struct thread *cur = thread_current ();
  int64_t now = timer_ticks ();
  int ready_threads;

  ASSERT (intr_get_level () == INTR_OFF);

  if (cur != idle_thread)
    cur->recent_cpu = fp_add_int (cur->recent_cpu, 1);

  if (now % TIMER_FREQ == 0)
    {
      ready_threads = (int) list_size (&ready_list)
                      + (cur != idle_thread ? 1 : 0);
      load_avg = fp_add (fp_mul (fp_div (fp_from_int (59), fp_from_int (60)),
                                 load_avg),
                         fp_mul_int (fp_div (fp_from_int (1), fp_from_int (60)),
                                     ready_threads));
      thread_foreach (thread_update_recent_cpu, NULL);
    }

  if (now % TIME_SLICE == 0)
    {
      thread_foreach (thread_update_priority, NULL);
      list_sort (&ready_list, thread_priority_greater, NULL);
      thread_check_preempt ();
    }
}

/* Invoke function 'func' on all threads, passing along 'aux'.
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
  /* [2-2-2] 수정 : 우선순위 변경 직후 선점 검사 추가
     변경 내용 : 대입 다음에 thread_check_preempt () 호출 추가
     변경 이유 : 낮춘 결과 ready_list 맨 앞이 더 높아지면 바로 양보하기 위해
     영향 범위 : thread_set_priority () 만 */
  thread_check_preempt ();
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
  /* [2-3-2] 수정 : nice 설정 구현
     변경 내용 : nice 를 저장하고 thread_mlfqs 이면 우선순위를 다시 계산한 뒤 선점 검사
     변경 이유 : 슬라이드 36 - 새 nice 로 우선순위를 다시 계산하고, 더 이상 가장 높지 않으면 양보한다
     영향 범위 : thread_set_nice () 만, 원본 서명의 UNUSED 와 주석은 그대로 둔다 */
  enum intr_level old_level;

  old_level = intr_disable ();
  thread_current ()->nice = nice;
  if (thread_mlfqs)
    thread_calc_priority (thread_current ());
  intr_set_level (old_level);

  if (thread_mlfqs)
    thread_check_preempt ();
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  /* [2-3-2] 수정 : nice 반환 구현
     변경 내용 : return 0 을 현재 스레드의 nice 반환으로 교체
     변경 이유 : 슬라이드 36 - 현재 스레드의 nice 를 돌려준다
     영향 범위 : thread_get_nice () 만 */
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  /* [2-3-2] 수정 : load_avg 반환 구현
     변경 내용 : return 0 을 load_avg 의 100 배 반올림 값 반환으로 교체
     변경 이유 : 슬라이드 39 - 100 배 값을 가장 가까운 정수로 반올림한다
     영향 범위 : thread_get_load_avg () 만 */
  enum intr_level old_level;
  int value;

  old_level = intr_disable ();
  value = fp_to_int_nearest (fp_mul_int (load_avg, 100));
  intr_set_level (old_level);
  return value;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  /* [2-3-2] 수정 : recent_cpu 반환 구현
     변경 내용 : return 0 을 현재 스레드 recent_cpu 의 100 배 올림 값 반환으로 교체
     변경 이유 : 슬라이드 38 - 100 배 값을 올림한다 (지침 7.1, 올림 채택)
     영향 범위 : thread_get_recent_cpu () 만 */
  enum intr_level old_level;
  int value;

  old_level = intr_disable ();
  value = fp_to_int_up (fp_mul_int (thread_current ()->recent_cpu, 100));
  intr_set_level (old_level);
  return value;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
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

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
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
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  /* [2-3-2] 수정 : nice, recent_cpu 초기화 추가
     변경 내용 : 두 필드를 0 으로 대입
     변경 이유 : 슬라이드 36, 38 - 처음 만든 스레드는 0 이다,
                 부모 값 상속은 thread_current () 를 쓸 수 있는 thread_create () 에서 한다
     영향 범위 : init_thread () 만, memset 으로 이미 0 이므로 값은 같다 */
  t->nice = 0;
  t->recent_cpu = fp_from_int (0);

#ifdef USERPROG
  /* [1-1-7] exec, wait : 부모-자식 관리 필드 초기화
     목적 : 자식 목록과 세 개의 세마포어를 사용 가능한 상태로 만든다
     참고 : threads/thread.h 의 Child_list, Load_sema, Exit_sema, Destroy_sema
     주의 : 여기서는 thread_current() 를 호출하지 않는다
            thread_init() 이 최초 스레드에 대해 이 함수를 부를 때는
            아직 현재 스레드를 안전하게 조회할 수 없기 때문이다
            부모 등록은 thread_create() 에서 수행한다 */
  t->Parent = NULL;
  t->is_loaded = false;
  list_init (&t->Child_list);
  sema_init (&t->Load_sema, 0);
  sema_init (&t->Exit_sema, 0);
  sema_init (&t->Destroy_sema, 0);

  /* [1-2-1] File Descriptor : 파일 디스크립터 테이블 포인터 초기화
     목적 : 아직 테이블을 갖지 않은 상태임을 NULL 로 명시한다
     참고 : threads/thread.h 의 struct file **Fd_table
            proj1 슬라이드 69 - 각 스레드가 독립적인 파일 디스크립터를 관리한다
     주의 : 위 memset() 이 이미 0 으로 채우지만, 바로 위 1-1-7 필드들과 같은
            이유로 초기화 의도를 코드에 남긴다
            실제 페이지 할당은 사용자 프로세스에만 필요하므로 여기서 하지 않고
            start_process() 에서 palloc_get_page (PAL_ZERO) 로 수행한다
            커널 스레드는 이 값이 NULL 인 채로 유지되며,
            시스템 콜 구현부는 NULL 검사로 두 경우를 구분한다 */
  t->Fd_table = NULL;

  /* [1-2-6] Denying Writes to Executables : 실행 파일 포인터 초기화
     목적 : 아직 실행 파일을 붙잡지 않은 상태임을 NULL 로 명시한다
     참고 : threads/thread.h 의 struct file *Exec_file
     주의 : 실제 값은 load () 가 실행 파일을 연 뒤에 채운다
            커널 스레드와 적재에 실패한 프로세스는 NULL 인 채로 남고,
            process_exit () 은 NULL 이면 아무 일도 하지 않는다 */
  t->Exec_file = NULL;
#endif

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
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
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
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
thread_schedule_tail (struct thread *prev)
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
     pull out the rug under itself.  (We don't free
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

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
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

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);