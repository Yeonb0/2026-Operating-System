#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"

/* [1-2-7] 파일 시스템 동기화 : 전역 락을 쓰기 위해 포함
   목적 : load () 와 process_exit () 이 시스템 콜과 같은 락으로
          파일 시스템 접근을 직렬화한다
   참고 : userprog/syscall.h 의 extern struct lock Filesys_lock
   주의 : 실체와 초기화는 userprog/syscall.c 에 있다 */
#include "userprog/syscall.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* [1-1-2] Argument Passing : 명령행 처리 한계값
   목적 : 명령행 사본 버퍼와 argv 배열의 크기를 한곳에서 관리한다
   참고 : Pintos manual 3.3.3 - pintos 유틸리티가 커널에 전달할 수 있는
          명령행 인자는 128바이트로 제한된다, proj1 슬라이드 35
   주의 : 128바이트를 한 글자 인자로 모두 채워도 토큰 수는 64개를 넘지 않는다 */
#define MAX_CMD_LEN 128
#define MAX_ARGC 64

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* [1-1-2] Argument Passing : 사용자 스택 구성 함수 선언
   참고 : Pintos manual 3.5 (80x86 Calling Convention), proj1 슬라이드 35 */
static void construct_stack (const char *cmdline, void **esp);

/* [1-1-7] exec, wait : 자식 프로세스 검색 함수 선언
   참고 : proj1 슬라이드 44 - 자식 스레드 ID 가 유효한지 확인한다 */
static struct thread *get_child_process (tid_t tid);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* [1-1-2] Argument Passing : 프로그램 이름 분리용 버퍼
     목적 : 명령행 전체가 아닌 첫 토큰만 thread_create() 에 넘겨
            스레드 이름이 실행 파일 이름과 일치하도록 한다
     입력 : file_name - "args-single onearg" 형태의 원본 명령행
     출력 : Prog_name 에 첫 토큰만 남는다
     참고 : proj1 슬라이드 48 - 49, Pintos manual 3.3.3
     주의 : strtok_r() 은 대상 문자열을 직접 변형하므로 const 인 file_name 이나
            load() 로 전달되는 fn_copy 를 그대로 넘기면 안 된다 */
  char Prog_name[MAX_CMD_LEN];
  char *save_ptr;

  /* [1-1-7] 자식의 적재 결과를 확인하기 위한 포인터 */
  struct thread *Child;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* [1-1-2] 명령행에서 첫 토큰만 추출
     참고 : strtok_r() 은 lib/string.c 에 정의되어 있다 (proj1 슬라이드 48)
     주의 : 연속 공백은 strtok_r() 이 자동으로 건너뛰므로
            args-dbl-space 가 요구하는 argc = 3 동작이 그대로 만족된다 */
  strlcpy (Prog_name, file_name, sizeof Prog_name);
  strtok_r (Prog_name, " ", &save_ptr);

  /* Create a new thread to execute FILE_NAME. */

  /* [1-1-2] 수정 : thread_create() 에 넘기는 이름을 file_name 에서 Prog_name 으로 교체
     변경 내용 : 명령행 전체 대신 첫 토큰만 스레드 이름으로 등록
     변경 이유 : 스레드 이름이 1-1-5 의 Process Termination Message 에 쓰이며,
                args-single.ck 은 "args-single: exit(0)" 을 기대한다
     영향 범위 : fn_copy 는 그대로 유지되므로 인자 정보는 load() 까지 보존된다 */
  tid = thread_create (Prog_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    {
      palloc_free_page (fn_copy);
      return tid;
    }

  /* [1-1-7] 수정 : 자식의 실행 파일 적재 결과를 기다린 뒤 반환
     변경 내용 : thread_create() 성공 후 자식이 Load_sema 를 올릴 때까지 대기하고,
                적재에 실패했으면 TID_ERROR 를 반환한다
     변경 이유 : exec 은 프로그램을 적재할 수 없으면 -1 을 반환해야 한다
                기다리지 않으면 부모가 성공 여부를 알기 전에 tid 를 돌려주게 된다
     영향 범위 : 적재에 성공하는 정상 경로에서는 자식이 곧바로 세마포어를 올리므로
                지연이 사실상 없다
     참고 : Pintos manual 3.3.4 exec, proj1 슬라이드 44 */
  Child = get_child_process (tid);
  if (Child == NULL)
    return TID_ERROR;

  sema_down (&Child->Load_sema);
  if (!Child->is_loaded)
    return TID_ERROR;

  return tid;
}

/* [1-1-7] exec, wait : tid 로 자식 프로세스 찾기
   목적 : 현재 스레드의 Child_list 를 훑어 해당 tid 를 가진 자식을 반환한다
   입력 : tid - 찾을 자식의 스레드 식별자
   출력 : 찾으면 struct thread *, 없으면 NULL
   참고 : proj1 슬라이드 44, lib/kernel/list.h 의 list_entry 매크로
   주의 : 자기 자식이 아닌 tid 를 넘기면 NULL 이 반환되어야 한다
          wait-bad-pid 는 존재하지 않는 pid 로 wait 을 호출하고 -1 을 기대한다
          이미 회수된 자식은 process_wait() 에서 목록에서 제거되므로
          두 번째 wait 호출도 자연히 NULL 을 받는다 (wait-twice) */
static struct thread *
get_child_process (tid_t tid)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->Child_list); e != list_end (&cur->Child_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, Child_elem);
      if (t->tid == tid)
        return t;
    }

  return NULL;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* [1-1-5] Process Termination Message : 종료 상태 초기값 설정
     목적 : 이 프로세스가 exit() 을 거치지 않고 강제 종료되는 경우에도
            종료 코드 -1 이 남도록 미리 설정한다
     참고 : Pintos manual 3.3.2, bad-read.ck / bad-write.ck 의 기대 출력
     주의 : page fault 는 exception.c 의 kill() 을 거쳐 thread_exit() 으로
            이어지므로 exit() 을 호출하지 않는다. 그 경로에서도 -1 이 나오도록
            exception.c 를 고치는 대신 초기값으로 해결한다 */
  thread_current ()->exit_status = -1;

  /* [1-2-1] File Descriptor : 파일 디스크립터 테이블 할당
     목적 : 이 프로세스가 열게 될 파일들을 담을 4 kB 페이지를 확보한다
     입력 : 없음
     출력 : thread_current ()->Fd_table 이 0 으로 채워진 페이지를 가리킨다
     참고 : proj1 슬라이드 69 - 각 스레드가 독립적인 파일 디스크립터를 관리한다
            threads/palloc.h 의 void *palloc_get_page (enum palloc_flags)
     주의 : PAL_ZERO 로 받아야 FD_BASE 부터 FD_MAX 까지 모든 칸이 NULL 이 된다
            빈 칸 판정을 NULL 로 하기 때문에 이 초기화가 곧 정확성 조건이다
            커널 풀에서 페이지를 받으므로 PAL_USER 를 주지 않는다
            메모리가 부족해 실패하면 적재 실패와 똑같이 처리한다
            multi-oom 처럼 메모리를 고갈시키는 테스트에서 실제로 발생할 수 있다 */
  thread_current ()->Fd_table = palloc_get_page (PAL_ZERO);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  /* [1-2-1] 수정 : 테이블 확보 실패를 적재 실패와 동일하게 취급
     변경 내용 : load() 호출 앞에 Fd_table 확보 여부를 검사하는 조건을 붙였다
     변경 이유 : 파일 디스크립터 테이블이 없으면 open 이후의 시스템 콜을
                정상 처리할 수 없으므로, 프로그램을 시작시켜서는 안 된다
     영향 범위 : 실패 시 아래 기존 경로가 그대로 동작한다
                is_loaded 가 false 로 기록되고 Load_sema 가 올라가므로
                부모의 exec 는 -1 을 받는다
     주의 : && 의 단축 평가 덕분에 테이블이 없으면 load() 자체를 부르지 않는다 */
  success = thread_current ()->Fd_table != NULL
            && load (file_name, &if_.eip, &if_.esp);

  /* [1-1-7] 수정 : 적재 결과를 부모에게 알린다
     변경 내용 : load() 결과를 is_loaded 에 기록하고 Load_sema 를 올린다
     변경 이유 : process_execute() 에서 대기 중인 부모가 exec 의 반환값을
                결정하려면 적재 성공 여부를 알아야 한다
     영향 범위 : 적재 실패 시에도 반드시 세마포어를 올려야 한다
                올리지 않으면 부모가 영원히 깨어나지 못한다
     주의 : thread_exit() 보다 먼저 수행해야 한다 */
  thread_current ()->is_loaded = success;
  sema_up (&thread_current ()->Load_sema);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  /* [1-1-7] 수정 : 임시 대기 루프를 제거하고 정식 wait 구현으로 교체
     변경 내용 : 자식을 tid 로 찾아 종료를 기다린 뒤 종료 상태를 반환하고,
                목록에서 제거한 다음 자식의 자원 회수를 허용한다
     변경 이유 : 1-1-2 에서 넣은 for (;;) thread_yield () 는 자식이 실행될
                시간을 벌기 위한 임시 조치였다
     영향 범위 : 이제 run_task() 가 정상적으로 반환되어 Pintos 가 스스로 종료된다
                Ctrl + C 로 강제 종료할 필요가 없어진다
     참고 : Pintos manual 3.3.4 wait, proj1 슬라이드 44
     주의 : 자식이 아니거나 이미 회수한 tid 이면 -1 을 반환한다
            Destroy_sema 를 올리기 전에 exit_status 를 먼저 읽어야 한다
            자식이 깨어나면 struct thread 가 해제될 수 있기 때문이다 */
  struct thread *Child;
  int status;

  Child = get_child_process (child_tid);
  if (Child == NULL)
    return -1;

  /* 자식이 종료할 때까지 기다린다 */
  sema_down (&Child->Exit_sema);

  status = Child->exit_status;

  /* 회수한 자식은 목록에서 제거해 두 번째 wait 이 -1 을 받도록 한다 */
  list_remove (&Child->Child_elem);

  /* 자식이 남은 정리를 마치고 소멸할 수 있도록 풀어 준다 */
  sema_up (&Child->Destroy_sema);

  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* [1-1-7] 사용자 프로세스 여부를 미리 기록해 둔다
     pagedir 은 아래에서 NULL 로 바뀌므로 판정에 쓸 수 없다 */
  bool is_user_process = (cur->pagedir != NULL);
  struct list_elem *e;
  int fd;

  /* [1-1-7] 아직 회수되지 않은 자식들을 먼저 풀어 준다
     목적 : 부모가 wait 없이 먼저 종료하는 경우 자식이 Destroy_sema 에서
            영원히 대기하는 것을 막는다
     참고 : Pintos manual 3.3.4 wait - 부모가 기다리지 않고 종료할 수 있다
     주의 : 자식 목록에서 제거하지는 않는다. 이 스레드는 곧 사라지므로
            목록 자체가 함께 사라진다 */
  for (e = list_begin (&cur->Child_list); e != list_end (&cur->Child_list);
       e = list_next (e))
    sema_up (&list_entry (e, struct thread, Child_elem)->Destroy_sema);

  /* [1-2-1] File Descriptor : 열린 파일 정리와 테이블 반납
     목적 : 종료하는 프로세스가 열어 둔 파일을 모두 닫고 페이지를 반납한다
     입력 : cur->Fd_table
     출력 : 모든 칸이 닫히고 Fd_table 이 NULL 이 된다
     참고 : Pintos manual 3.3.2 - 프로세스가 종료되면 열린 파일 디스크립터는
            운영체제가 대신 닫아 주어야 한다
            filesys/file.h 의 void file_close (struct file *)
     주의 : 0 번과 1 번 칸은 콘솔 몫이라 항상 NULL 이므로 FD_BASE 부터 순회한다
            여기서 닫지 않으면 파일의 inode 참조가 남아 remove 이후에도
            디스크 공간이 회수되지 않는다
            커널 스레드는 Fd_table 이 NULL 이므로 통째로 건너뛴다
            페이지 반납 뒤 NULL 로 되돌려, 남은 종료 경로에서 해제된 메모리를
            다시 참조하지 않도록 한다 */
  /* [1-2-7] 수정 : 종료 시 파일 정리도 같은 락으로 보호
     변경 내용 : 열린 파일을 닫는 반복문과 실행 파일 닫기를
                Filesys_lock 으로 감쌌다
     변경 이유 : 닫기는 inode 참조 수를 줄이고 필요하면 디스크 공간을 회수한다
                다른 프로세스의 파일 접근과 겹치면 자료구조가 깨진다
     영향 범위 : 테이블 페이지 반납은 프로세스 자신의 자료라 락 밖에서 한다
     주의 : 커널 스레드는 Fd_table 과 Exec_file 이 모두 NULL 이라
            락 자체를 건드리지 않고 지나간다 */
  if (cur->Fd_table != NULL)
    {
      lock_acquire (&Filesys_lock);

      for (fd = FD_BASE; fd < FD_MAX; fd++)
        if (cur->Fd_table[fd] != NULL)
          {
            file_close (cur->Fd_table[fd]);
            cur->Fd_table[fd] = NULL;
          }

      lock_release (&Filesys_lock);

      palloc_free_page (cur->Fd_table);
      cur->Fd_table = NULL;
    }

  /* [1-2-6] Denying Writes to Executables : 실행 파일 반납
     목적 : 프로세스가 끝났으므로 실행 파일을 닫아 쓰기 금지를 푼다
     입력 : cur->Exec_file
     출력 : 파일이 닫히고 Exec_file 이 NULL 이 된다
     참고 : proj1 슬라이드 72, Pintos manual 3.3.5
     주의 : file_close () 가 내부에서 file_allow_write () 를 부르므로
            따로 허용 처리를 하지 않는다
            커널 스레드와 적재에 실패한 프로세스는 NULL 이라 건너뛴다
            자식이 아직 돌고 있어도 각자 자기 실행 파일을 따로 붙잡고 있으므로
            부모가 먼저 끝나도 자식 쪽 금지는 풀리지 않는다 (rox-multichild) */
  if (cur->Exec_file != NULL)
    {
      lock_acquire (&Filesys_lock);
      file_close (cur->Exec_file);
      lock_release (&Filesys_lock);

      cur->Exec_file = NULL;
    }

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* [1-1-5] Process Termination Message : 종료 메시지 출력
         목적 : 사용자 프로세스가 종료될 때 "이름: exit(상태)" 형식으로 출력한다
         입력 : cur->name        - 1-1-2 에서 명령행 인자를 뗀 프로그램 이름
                cur->exit_status - exit() 이 기록했거나 초기값 -1
         출력 : 콘솔에 종료 메시지 한 줄
         참고 : Pintos manual 3.3.2, proj1 슬라이드 31 - 32
         주의 : pd != NULL 조건 안에 두어야 커널 스레드에는 출력되지 않는다
                매뉴얼 3.3.2 가 사용자 프로세스가 아닌 커널 스레드 종료 시에는
                출력하지 말라고 명시한다
                halt 로 종료할 때는 shutdown_power_off() 가 곧바로 전원을 내려
                이 함수 자체가 호출되지 않으므로 별도 분기가 필요 없다 */
      printf ("%s: exit(%d)\n", cur->name, cur->exit_status);

      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /* [1-1-7] 부모에게 종료를 알리고, 회수될 때까지 이 자리에서 대기한다
     목적 : 부모가 process_wait() 에서 exit_status 를 읽어 가기 전에
            이 스레드의 struct thread 가 해제되지 않도록 붙잡는다
     참고 : Pintos manual 3.3.4 wait
     주의 : 사용자 프로세스에만 적용한다
            커널 스레드까지 여기서 대기하면 부모가 없어 영원히 멈추고
            Pintos 가 전원을 내리지 못한다
            부모가 이미 종료한 경우에는 위 반복문에서 Destroy_sema 가
            미리 올라가 있으므로 곧바로 통과한다 */
  if (is_user_process)
    {
      sema_up (&cur->Exit_sema);
      sema_down (&cur->Destroy_sema);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* [1-1-2] Argument Passing : 실행 파일 이름 분리용 버퍼
     목적 : filesys_open() 에 명령행 전체가 아닌 첫 토큰만 넘겨
            "echo a b" 같은 입력에서도 실행 파일 echo 를 찾도록 한다
     입력 : file_name - start_process() 가 넘긴 명령행 전체
     출력 : Prog_name 에 첫 토큰만 남는다
     참고 : Pintos manual 3.3.3 Argument Passing, proj1 슬라이드 48
     주의 : file_name 은 const 이고 이후 1-1-2 의 스택 구성에서
            명령행 전체가 그대로 필요하므로 원본을 변형하면 안 된다 */
  char Prog_name[MAX_CMD_LEN];
  char *save_ptr;

  /* [1-2-7] 수정 : 적재 전 구간 전체를 파일 시스템 락으로 보호
     변경 내용 : load () 첫머리에서 Filesys_lock 을 잡고, done 라벨에서 푼다
     변경 이유 : load () 는 filesys_open () 과 file_read () 로 파일 시스템을
                직접 사용한다, 시스템 콜 쪽만 잠그면 exec 로 프로세스가
                뜨는 도중과 다른 프로세스의 파일 접근이 겹쳐 깨진다
     영향 범위 : 락을 잡기 전에는 goto done 이 없으므로 잡지 않은 락을
                푸는 경우가 생기지 않는다
     주의 : 첫 goto done 보다 앞에서 잡아야 한다
            pagedir_create () 실패 경로도 done 을 지나가기 때문이다
            적재에 실패하면 start_process () 가 thread_exit () 을 부르고
            process_exit () 이 다시 이 락을 잡으므로,
            반드시 done 에서 먼저 풀고 반환해야 한다 */
  lock_acquire (&Filesys_lock);

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */

  /* [1-1-2] 수정 : filesys_open() 대상을 file_name 에서 Prog_name 으로 교체
     변경 내용 : 명령행에서 첫 토큰을 잘라 낸 뒤 그 이름으로 실행 파일을 연다
     변경 이유 : 기존 코드는 "echo a b" 라는 이름의 파일을 찾으므로 항상 실패한다
     영향 범위 : 인자가 없는 명령행에서는 첫 토큰이 곧 전체 문자열이므로 동작이 같다 */
  strlcpy (Prog_name, file_name, sizeof Prog_name);
  strtok_r (Prog_name, " ", &save_ptr);

  file = filesys_open (Prog_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", Prog_name);
      goto done; 
    }

  /* [1-2-6] Denying Writes to Executables : 실행 파일 쓰기 금지
     목적 : 프로세스가 도는 동안 자신의 실행 파일이 수정되지 않게 한다
     입력 : file - 방금 연 실행 파일
     출력 : 파일이 쓰기 금지 상태가 되고 Exec_file 에 보관된다
     참고 : proj1 슬라이드 72 - Denying Writes to Executable files
            Pintos manual 3.3.5 - void file_deny_write (struct file *)
     주의 : 금지 상태는 inode 에 걸리므로, 다른 프로세스가 같은 파일을
            새로 열어 얻은 fd 로 써도 0 바이트만 기록된다 (rox-child)
            파일을 닫으면 금지가 풀리므로 여기서 닫으면 안 되고,
            프로세스가 끝나는 process_exit () 까지 열어 둔 채로 붙잡는다
            그래서 아래 done 라벨의 file_close () 를 실패한 경우로 한정한다 */
  file_deny_write (file);
  thread_current ()->Exec_file = file;

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* [1-1-2] 수정 : 스택 페이지 할당 직후 인자를 실제로 쌓는다
     변경 내용 : setup_stack() 이 *esp 를 PHYS_BASE 로 설정한 뒤
                construct_stack() 을 호출해 argc / argv 를 구성
     변경 이유 : 기존 코드는 빈 스택만 만들어 _start() 가 argc, argv 를
                PHYS_BASE 위쪽에서 읽다가 page fault 를 일으킨다
     영향 범위 : setup_stack() 실패 시에는 호출되지 않는다
     참고 : Pintos manual 3.5, proj1 슬라이드 35 */
  construct_stack (file_name, esp);

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */

  /* [1-2-6] 수정 : 적재에 성공한 실행 파일은 닫지 않는다
     변경 내용 : 무조건 부르던 file_close () 를 실패한 경우로 한정하고,
                이때 Exec_file 도 NULL 로 되돌린다
     변경 이유 : file_close () 는 내부에서 file_allow_write () 를 부른다
                여기서 닫으면 방금 건 쓰기 금지가 곧바로 풀려 rox 계열이 깨진다
     영향 범위 : 실패 경로의 동작은 그대로다
                file_close (NULL) 은 아무 일도 하지 않으므로
                파일을 열기 전에 goto done 으로 온 경우도 안전하다
     주의 : 성공한 파일을 닫는 책임은 process_exit () 로 넘어갔다
            여기서 닫고 Exec_file 도 남겨 두면 종료 시 이중 해제가 된다 */
  if (!success)
    {
      thread_current ()->Exec_file = NULL;
      file_close (file);
    }

  lock_release (&Filesys_lock);

  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* [1-1-2] Argument Passing : 사용자 스택 구성
   목적 : setup_stack() 이 할당한 4 KB 스택 페이지 위에 80x86 calling convention 에
          맞추어 인자 문자열, word align, null sentinel, argv 포인터 배열,
          argv, argc, fake return address 순으로 데이터를 쌓는다
   입력 : cmdline - load() 가 받은 원본 명령행 문자열
          esp     - setup_stack() 직후 PHYS_BASE 를 가리키는 스택 포인터
   출력 : *esp 가 fake return address 를 가리키도록 갱신된다
   참고 : Pintos manual 3.5 (80x86 Calling Convention), proj1 슬라이드 35 - 41
   주의 : 문자열은 역순으로 push 해야 hex_dump 결과가 매뉴얼 예시와 일치한다
          strtok_r() 은 대상을 변형하므로 cmdline 사본을 만들어 사용한다
          argv[argc] 자리에 null sentinel 을 반드시 넣어야 args.c 의
          i <= argc 순회에서 null 이 출력된다 */
static void
construct_stack (const char *cmdline, void **esp)
{
  char Cmd_copy[MAX_CMD_LEN];
  char *Argv[MAX_ARGC];
  char *token;
  char *save_ptr;
  char **argv_base;
  int argc = 0;
  int i;
  int len;

  /* 1. 명령행을 사본에 복사한 뒤 공백 기준으로 토큰 분리
        연속 공백은 strtok_r() 이 하나의 구분자로 처리한다 */
  strlcpy (Cmd_copy, cmdline, sizeof Cmd_copy);
  for (token = strtok_r (Cmd_copy, " ", &save_ptr);
       token != NULL && argc < MAX_ARGC;
       token = strtok_r (NULL, " ", &save_ptr))
    Argv[argc++] = token;

  /* 2. 인자 문자열을 역순으로 push 하고, 스택상의 주소를 다시 Argv 에 기록 */
  for (i = argc - 1; i >= 0; i--)
    {
      len = strlen (Argv[i]) + 1;
      *esp = (uint8_t *) *esp - len;
      memcpy (*esp, Argv[i], len);
      Argv[i] = *esp;
    }

  /* 3. word align : esp 를 4의 배수로 내림 */
  while ((uintptr_t) *esp % 4 != 0)
    {
      *esp = (uint8_t *) *esp - 1;
      * (uint8_t *) *esp = 0;
    }

  /* 4. argv[argc] 자리에 null sentinel push */
  *esp = (uint8_t *) *esp - 4;
  * (uint32_t *) *esp = 0;

  /* 5. argv[i] 주소를 역순으로 push */
  for (i = argc - 1; i >= 0; i--)
    {
      *esp = (uint8_t *) *esp - 4;
      * (char **) *esp = Argv[i];
    }

  /* 6. argv (argv[0] 이 놓인 주소) push */
  argv_base = *esp;
  *esp = (uint8_t *) *esp - 4;
  * (char ***) *esp = argv_base;

  /* 7. argc push */
  *esp = (uint8_t *) *esp - 4;
  * (int *) *esp = argc;

  /* 8. fake return address push
        _start() 는 일반 함수처럼 호출된 것이 아니지만, calling convention 상
        반환 주소 자리가 있어야 argc 와 argv 의 오프셋이 맞는다 */
  *esp = (uint8_t *) *esp - 4;
  * (uint32_t *) *esp = 0;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}