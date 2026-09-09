#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* [1-1-3] User Memory Access : 주소 검증에 필요한 헤더 추가
   목적 : is_user_vaddr(), PHYS_BASE, pg_round_down() 과
          pagedir_get_page() 를 사용하기 위해 포함한다
   참고 : threads/vaddr.h, userprog/pagedir.h
   주의 : vaddr.h 는 수정하지 않는다. is_user_vaddr() 가 이미 제공된다 */
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

/* [1-1-5] halt 구현에 필요한 헤더 추가
   목적 : shutdown_power_off() 를 호출하기 위해 포함한다
   참고 : Pintos manual 3.3.4 - halt 는 shutdown_power_off() 를 호출한다
          proj1 슬라이드 42 */
#include "devices/shutdown.h"

/* [1-1-6] read 구현에 필요한 헤더 추가
   목적 : 키보드 입력을 한 글자씩 받는 input_getc() 를 호출한다
   참고 : proj1 슬라이드 45 - devices/input.c 의 uint8_t input_getc (void)
   주의 : write 에 쓰는 putbuf() 는 lib/kernel/stdio.h 에 선언되어 있고
          이미 포함된 <stdio.h> 를 통해 딸려 오므로 별도 포함이 필요 없다 */
#include "devices/input.h"

/* [1-1-7] exec, wait 구현에 필요한 헤더 추가
   목적 : process_execute() 와 process_wait() 를 호출한다
   참고 : userprog/process.h */
#include "userprog/process.h"

/* [1-2-2] create, remove 구현에 필요한 헤더 추가
   목적 : filesys_create() 와 filesys_remove() 를 호출한다
   참고 : proj1 슬라이드 71 - filesys/filesys.h 의 API 를 사용하라는 안내
          bool filesys_create (const char *name, off_t initial_size)
          bool filesys_remove (const char *name)
   주의 : off_t 는 filesys/off_t.h 에 정의되어 있고 filesys.h 가 이미 포함한다 */
#include "filesys/filesys.h"

/* [1-2-3] open, close, filesize 구현에 필요한 헤더 추가
   목적 : file_close() 와 file_length() 를 호출한다
   참고 : proj1 슬라이드 71 - filesys/file.h 의 API
          struct file *filesys_open (const char *name) 이 돌려준 포인터를
          이 헤더의 함수들로 다룬다
   주의 : struct file 의 내부는 filesys/file.c 에만 있고 여기서는 다루지 않는다 */
#include "filesys/file.h"

static void syscall_handler (struct intr_frame *);

/* [1-2-4] 수정 : get_file() 전방 선언 추가
   변경 내용 : 파일 목록 아래쪽에 정의된 get_file() 을 read 와 write 에서
              쓸 수 있도록 선언만 앞으로 뺀다
   변경 이유 : read 와 write 는 1-1-6 에서 만들어져 get_file() 정의보다 위에 있다
              정의 자체를 위로 옮기면 1-2-3 에서 확인한 코드 배치가 흔들리므로
              선언만 추가하는 쪽을 택했다
   영향 범위 : 없음, 링크 대상과 호출 방식은 그대로다 */
static struct file *get_file (int fd);

/* [1-2-7] 파일 시스템 동기화 : 전역 락 정의
   목적 : 파일 시스템 코드를 한 번에 한 스레드만 실행하도록 직렬화한다
   참고 : Pintos manual 3.1.2 - 기본 파일 시스템은 내부 동기화가 전혀 없어
          여러 프로세스가 동시에 접근하면 자료구조가 깨진다
          proj1 슬라이드 71 - filesys 계층 API 사용 안내
   주의 : 락을 잡기 전에 사용자 포인터 검증을 모두 끝내야 한다
          검증 실패는 exit(-1) 로 빠져나가는데, 락을 쥔 채 나가면
          그 락을 아무도 풀 수 없어 시스템 전체가 멈춘다
          Project 3 에서 파일 시스템 자체를 개선하면 이 락은 잘게 쪼개진다 */
struct lock Filesys_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  /* [1-2-7] 수정 : 파일 시스템 락 초기화
     변경 내용 : 시스템 콜 등록 뒤에 lock_init () 호출을 추가한다
     변경 이유 : 락은 사용 전에 반드시 초기화되어야 한다
     영향 범위 : syscall_init () 은 커널 부팅 중 한 번만 불리므로
                사용자 프로그램이 돌기 전에 초기화가 끝난다
     참고 : threads/synch.h 의 void lock_init (struct lock *) */
  lock_init (&Filesys_lock);
}

/* [1-1-3] User Memory Access : 사용자 포인터 유효성 검사
   목적 : 시스템 콜을 통해 커널로 전달된 주소가 실제로 접근 가능한
          사용자 영역인지 확인하고, 아니면 프로세스를 종료시킨다
   입력 : addr - 검사할 시작 주소
          size - 접근할 바이트 수 (예 : 4바이트 정수는 sizeof (int))
   출력 : 유효하면 그냥 반환하고, 유효하지 않으면 반환하지 않는다
   참고 : Pintos manual 3.1.5 Accessing User Memory, proj1 슬라이드 57
   주의 : 세 가지를 모두 확인해야 한다
          1) 널 포인터 - bad-read, bad-write 가 NULL 역참조를 시도한다
          2) PHYS_BASE 이상 - sc-bad-arg 는 esp 는 유효하지만
             인자를 읽을 esp + 4 가 PHYS_BASE 를 넘는다
          3) 매핑되지 않은 페이지 - sc-bad-sp 는 코드 영역보다 64MB 아래를 가리킨다
          범위가 두 페이지에 걸칠 수 있으므로 걸쳐 있는 모든 페이지를 확인한다
          sc-boundary 는 시스템 콜 번호와 인자가 서로 다른 페이지에 있어도
          정상 동작해야 하므로 "같은 페이지" 조건을 걸면 안 된다
          잘못된 접근일 때의 종료 코드는 -1 이어야 한다 (bad-read.ck 등) */

/* [1-1-5] 수정 : 종료 수단을 thread_exit() 에서 exit(-1) 로 교체
   변경 내용 : 검사 실패 시 종료 상태 -1 을 남기고 프로세스를 끝낸다
   변경 이유 : bad-read, bad-write, sc-bad-sp, sc-bad-arg 는 모두
              "이름: exit(-1)" 형태의 종료 메시지를 기대한다
   영향 범위 : 검사를 통과하는 정상 경로에는 변화가 없다 */
void
check_address (const void *addr, unsigned size)
{
  struct thread *t = thread_current ();
  const uint8_t *begin = addr;
  const uint8_t *end;
  const uint8_t *page;

  if (size == 0)
    return;

  if (addr == NULL)
    exit (-1);

  end = begin + size - 1;

  /* 주소 덧셈이 되돌아간 경우도 잘못된 접근으로 본다 */
  if (end < begin)
    exit (-1);

  if (!is_user_vaddr (begin) || !is_user_vaddr (end))
    exit (-1);

  /* 접근 범위가 걸쳐 있는 모든 페이지가 현재 프로세스에 매핑되어 있는지 확인 */
  for (page = pg_round_down (begin); page <= end; page += PGSIZE)
    if (pagedir_get_page (t->pagedir, page) == NULL)
      exit (-1);
}

/* [1-1-4] System Call Handler : 사용자 스택에서 인자 꺼내기
   목적 : 시스템 콜 번호 바로 위 칸부터 count 개의 4바이트 인자를
          Arg 배열로 복사한다
   입력 : esp   - 시스템 콜 진입 시점의 스택 포인터 (번호가 놓인 위치)
          Arg   - 복사받을 배열, 호출자가 count 개 이상 크기로 준비한다
          count - 꺼낼 인자 개수
   출력 : Arg[0] ~ Arg[count - 1] 에 인자가 채워진다
   참고 : proj1 슬라이드 25 - 사용자 스택에 시스템 콜 번호와 인자가
          차례로 쌓이는 구조, Pintos manual 3.5
   주의 : 인자 하나하나를 읽기 전에 반드시 check_address() 로 검증한다
          sc-bad-arg 는 번호는 유효하지만 첫 인자 위치가 PHYS_BASE 를 넘는다
          포인터 인자의 경우 여기서는 값만 가져오고, 그 포인터가 가리키는
          대상의 유효성은 각 시스템 콜 구현부에서 따로 검사한다 */
void
get_argument (void *esp, int *Arg, int count)
{
  int *sp = (int *) esp + 1;
  int i;

  for (i = 0; i < count; i++)
    {
      check_address (sp + i, sizeof (int));
      Arg[i] = * (sp + i);
    }
}

/* [1-1-5] halt 시스템 콜 : 운영체제 종료
   목적 : shutdown_power_off() 를 호출해 Pintos 를 즉시 끈다
   입력 : 없음
   출력 : 반환하지 않는다
   참고 : Pintos manual 3.3.4 halt, proj1 슬라이드 42
   주의 : 매뉴얼 3.3.2 에 따라 halt 로 종료할 때는 종료 메시지를 출력하지 않는다
          shutdown_power_off() 가 곧바로 전원을 내리므로 process_exit() 자체가
          호출되지 않아 별도 처리가 필요 없다 */
void
halt (void)
{
  shutdown_power_off ();
}

/* [1-1-5] exit 시스템 콜 : 현재 프로세스 종료
   목적 : 종료 상태를 스레드에 기록하고 프로세스를 끝낸다
   입력 : status - 사용자 프로그램이 넘긴 종료 상태, 관례상 0 이 정상
   출력 : 반환하지 않는다
   참고 : Pintos manual 3.3.2 Process Termination Messages, 3.3.4 exit
          proj1 슬라이드 31 - 34
   주의 : 종료 메시지는 여기서 찍지 않고 process_exit() 에서 출력한다
          exit 뿐 아니라 page fault 등으로 강제 종료되는 경로도 있으므로,
          출력 지점을 한 곳으로 모아야 메시지 누락과 중복을 함께 막을 수 있다
          종료 상태는 1-1-7 의 wait 구현에서 부모가 읽어 가게 된다 */
void
exit (int status)
{
  thread_current ()->exit_status = status;
  thread_exit ();
}

/* [1-1-6] write 시스템 콜 : 표준 출력 전용 구현
   목적 : fd 가 1(STDOUT) 일 때 buffer 의 size 바이트를 콘솔에 출력한다
   입력 : fd     - 파일 디스크립터, 이번 단계에서는 1 만 처리한다
          buffer - 출력할 내용이 담긴 사용자 영역 주소
          size   - 출력할 바이트 수
   출력 : 실제로 쓴 바이트 수, 처리할 수 없는 fd 이면 -1
   참고 : proj1 슬라이드 45 - lib/kernel/console.c 의 void putbuf (...)
          Pintos manual 3.3.4 write
   주의 : buffer 는 사용자가 넘긴 포인터이므로 사용 전에 반드시 검증한다
          fd >= 2 인 파일 쓰기는 1-2 단계에서 Fd_table 을 도입하며 확장한다
          그때 이 함수의 fd == 1 경로는 그대로 두고 분기만 추가한다 */
/* [1-2-4] 수정 : write 를 파일 디스크립터 기반으로 확장
   변경 내용 : fd == 1 경로는 그대로 두고, 그 아래에 Fd_table 에서 찾은
              struct file * 로 file_write () 를 호출하는 분기를 추가한다
   변경 이유 : write-normal 은 open 으로 받은 fd 에 실제로 기록하고
              쓴 바이트 수가 요청한 크기와 같기를 요구한다
   영향 범위 : fd == 1 의 동작은 변화 없다
   참고 : proj1 슬라이드 71 - filesys/file.h 의 off_t file_write (struct file *, const void *, off_t)
   주의 : 유효하지 않은 fd 와 fd == 0 은 -1 만 돌려주고 프로세스를 죽이지 않는다
          (write-bad-fd, write-stdin)
          실행 파일에 대한 쓰기 금지는 여기서 처리하지 않는다
          1-2-6 에서 file_deny_write () 로 파일 쪽에 표시하며,
          그 경우 file_write () 가 0 을 돌려준다 */
int
write (int fd, const void *buffer, unsigned size)
{
  struct file *File;
  int bytes;

  check_address (buffer, size);

  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }

  File = get_file (fd);
  if (File == NULL)
    return -1;

  /* [1-2-7] 수정 : 파일 쓰기를 락으로 보호
     변경 내용 : file_write () 호출을 Filesys_lock 으로 감쌌다
     변경 이유 : syn-write 는 여러 자식이 같은 파일의 서로 다른 구간을
                동시에 기록한다
     영향 범위 : fd == 1 의 콘솔 출력 경로는 감싸지 않는다
                putbuf () 는 콘솔 쪽에서 이미 자체 동기화를 한다 */
  lock_acquire (&Filesys_lock);
  bytes = file_write (File, buffer, size);
  lock_release (&Filesys_lock);

  return bytes;
}

/* [1-1-6] read 시스템 콜 : 표준 입력 전용 구현
   목적 : fd 가 0(STDIN) 일 때 키보드에서 size 바이트를 읽어 buffer 에 채운다
   입력 : fd     - 파일 디스크립터, 이번 단계에서는 0 만 처리한다
          buffer - 읽은 내용을 담을 사용자 영역 주소
          size   - 읽을 바이트 수
   출력 : 실제로 읽은 바이트 수, 처리할 수 없는 fd 이면 -1
   참고 : proj1 슬라이드 45 - devices/input.c 의 uint8_t input_getc (void)
          Pintos manual 3.3.4 read
   주의 : buffer 에 쓰기를 하므로 검증이 특히 중요하다
          input_getc() 는 입력이 들어올 때까지 대기하는 blocking 호출이다
          fd >= 2 인 파일 읽기는 1-2 단계에서 확장한다 */
/* [1-2-4] 수정 : read 를 파일 디스크립터 기반으로 확장
   변경 내용 : fd == 0 경로는 그대로 두고, 그 아래에 Fd_table 에서 찾은
              struct file * 로 file_read () 를 호출하는 분기를 추가한다
   변경 이유 : 이제 open 이 fd 를 배정하므로 실제 파일을 읽을 수 있어야 한다
   영향 범위 : fd == 0 의 동작은 변화 없다
   참고 : proj1 슬라이드 71 - filesys/file.h 의 off_t file_read (struct file *, void *, off_t)
   주의 : 유효하지 않은 fd 는 -1 만 돌려주고 프로세스를 죽이지 않는다
          read-bad-fd 는 INT_MIN, INT_MAX 를 포함한 7가지를 연달아 넘기며
          조용한 실패와 exit(-1) 을 모두 정답으로 인정한다
          fd == 1 로 읽으려는 시도도 get_file () 이 NULL 을 돌려주어 -1 이 된다
          size 가 0 이면 check_address () 가 그대로 통과시키고
          file_read () 도 0 을 돌려주므로 버퍼는 손대지 않는다 (read-zero) */
int
read (int fd, void *buffer, unsigned size)
{
  uint8_t *buf = buffer;
  unsigned i;
  struct file *File;
  int bytes;

  check_address (buffer, size);

  if (fd == STDIN_FILENO)
    {
      for (i = 0; i < size; i++)
        buf[i] = input_getc ();
      return size;
    }

  File = get_file (fd);
  if (File == NULL)
    return -1;

  /* [1-2-7] 수정 : 파일 읽기를 락으로 보호
     변경 내용 : file_read () 호출을 Filesys_lock 으로 감쌌다
     변경 이유 : syn-read 는 여러 자식이 같은 파일을 동시에 읽는다
     영향 범위 : fd == 0 의 키보드 입력 경로는 파일 시스템과 무관하므로
                락을 잡지 않는다, 그쪽을 감싸면 입력을 기다리는 동안
                다른 프로세스의 파일 접근까지 멈춘다
     주의 : buffer 검증은 함수 첫머리에서 이미 끝났다 */
  lock_acquire (&Filesys_lock);
  bytes = file_read (File, buffer, size);
  lock_release (&Filesys_lock);

  return bytes;
}

/* [1-1-7] User Memory Access : 문자열 인자 전체 검증
   목적 : 널 종료 문자열이 끝까지 유효한 사용자 영역에 있는지 확인한다
   입력 : str - 검사할 문자열의 시작 주소
   출력 : 유효하면 그냥 반환하고, 아니면 반환하지 않는다
   참고 : Pintos manual 3.1.5 Accessing User Memory
   주의 : 문자열은 길이를 미리 알 수 없으므로 한 바이트씩 확인한다
          exec-bad-ptr 은 잘못된 포인터를 exec 에 넘기고 exit(-1) 을 기대한다
          중간에 페이지 경계를 넘어갈 수 있어 시작 주소만 검사해서는 안 된다 */
static void
check_string (const char *str)
{
  check_address (str, 1);

  while (*str != '\0')
    {
      str++;
      check_address (str, 1);
    }
}

/* [1-1-7] exec 시스템 콜 : 자식 프로세스 생성
   목적 : cmd_line 이 가리키는 프로그램을 새 프로세스로 실행한다
   입력 : cmd_line - 프로그램 이름과 인자가 담긴 명령행 문자열
   출력 : 성공하면 자식의 pid, 프로그램을 적재할 수 없으면 -1
   참고 : Pintos manual 3.3.4 exec, proj1 슬라이드 44
   주의 : 적재 성공 여부를 확인하기 전에는 반환하면 안 된다
          이 대기는 process_execute() 안에서 Load_sema 로 처리한다 */
tid_t
exec (const char *cmd_line)
{
  check_string (cmd_line);

  return process_execute (cmd_line);
}

/* [1-1-7] wait 시스템 콜 : 자식 프로세스 종료 대기
   목적 : 지정한 자식이 끝날 때까지 기다린 뒤 종료 상태를 돌려준다
   입력 : tid - 기다릴 자식의 pid
   출력 : 자식의 종료 상태, 자식이 아니거나 이미 기다린 적이 있으면 -1
   참고 : Pintos manual 3.3.4 wait, proj1 슬라이드 44
   주의 : 실제 동작은 process_wait() 에 있다
          시스템 콜 계층과 프로세스 관리 계층을 분리해 두면
          Prj1-2 와 Project 2 에서 프로세스 관리 쪽만 확장할 수 있다 */
int
wait (tid_t tid)
{
  return process_wait (tid);
}

/* [1-1-8] fibonacci 시스템 콜 : n 번째 피보나치 수 계산
   목적 : 사용자 프로그램이 요청한 n 번째 피보나치 수를 커널에서 계산해 돌려준다
   입력 : n - 몇 번째 항인지를 나타내는 정수
   출력 : n 번째 피보나치 수, n 이 0 이하이면 0
   참고 : proj1 슬라이드 54 - Return N th value of Fibonacci sequence
          슬라이드 55 의 예시에서 fibonacci (10) 은 55 다
   주의 : F(1) = 1, F(2) = 1 기준이어야 fibonacci (10) 이 55 가 된다
          재귀로 구현하면 커널 스택이 깊어지고 n 이 조금만 커져도 매우 느려지므로
          반복문으로 계산한다
          n 이 46 이상이면 int 범위를 넘어 값이 깨지지만, 과제 명세에
          범위 제한이 없으므로 별도 처리는 두지 않는다
          n 이 0 이하일 때 반복문이 돌지 않고 0 을 반환하도록 먼저 걸러 낸다 */
int
fibonacci (int n)
{
  int prev = 0;
  int cur = 1;
  int next;
  int i;

  if (n <= 0)
    return 0;

  for (i = 1; i < n; i++)
    {
      next = prev + cur;
      prev = cur;
      cur = next;
    }

  return cur;
}

/* [1-1-8] max_of_four_int 시스템 콜 : 네 정수 중 최댓값
   목적 : 사용자 프로그램이 넘긴 정수 4개 중 가장 큰 값을 돌려준다
   입력 : a, b, c, d - 비교할 정수 4개
   출력 : 네 값 중 최댓값
   참고 : proj1 슬라이드 54 - Return the maximum of a, b, c and d
          슬라이드 55 의 예시에서 max_of_four_int (10, 20, 62, 40) 은 62 다
   주의 : 인자가 모두 값 전달이므로 포인터 검증은 필요 없다
          인자를 꺼내는 쪽(syscall_handler)에서 이미 주소를 검증했다 */
int
max_of_four_int (int a, int b, int c, int d)
{
  int max = a;

  if (b > max)
    max = b;
  if (c > max)
    max = c;
  if (d > max)
    max = d;

  return max;
}

/* [1-2-2] create 시스템 콜 : 파일 생성
   목적 : initial_size 바이트 크기의 빈 파일을 file 이름으로 만든다
   입력 : file         - 만들 파일의 이름, 사용자 영역 문자열
          initial_size - 만들 때의 크기, 바이트 단위
   출력 : 성공하면 true, 실패하면 false
   참고 : Pintos manual 3.3.4 create
          proj1 슬라이드 71 - filesys/filesys.h 의 filesys_create ()
   주의 : 파일을 만드는 것과 여는 것은 별개다, 여기서 fd 는 배정하지 않는다
          이름 포인터는 사용자 값이므로 check_string () 으로 끝까지 검증한다
          create-null 은 NULL 을, create-bad-ptr 은 매핑되지 않은 주소를 넘기며
          두 경우 모두 exit(-1) 을 기대하는데 check_string () 이 이를 처리한다
          빈 이름과 이름 길이 초과는 dir_add () 가 걸러 false 를 돌려주므로
          여기서 따로 길이를 검사하지 않는다
          create-empty 와 create-long 이 그 경로를 확인한다
          이미 있는 이름이면 filesys_create () 가 false 를 돌려준다 (create-exists) */
bool
create (const char *file, unsigned initial_size)
{
  bool success;

  check_string (file);

  /* [1-2-7] 수정 : 파일 시스템 접근을 락으로 보호
     변경 내용 : filesys_create () 호출을 Filesys_lock 으로 감쌌다
     변경 이유 : 여러 프로세스가 동시에 파일을 만들면 free map 과 디렉터리가
                동시에 갱신되어 깨진다
     영향 범위 : 단일 프로세스 동작에는 변화가 없다
     주의 : check_string () 은 락 밖에서 먼저 부른다
            검증 실패 시 exit(-1) 로 빠져나가는데 락을 쥔 채 나가면 안 된다 */
  lock_acquire (&Filesys_lock);
  success = filesys_create (file, initial_size);
  lock_release (&Filesys_lock);

  return success;
}

/* [1-2-2] remove 시스템 콜 : 파일 삭제
   목적 : file 이름의 파일을 파일 시스템에서 지운다
   입력 : file - 지울 파일의 이름, 사용자 영역 문자열
   출력 : 성공하면 true, 실패하면 false
   참고 : Pintos manual 3.3.4 remove 와 3.3.5 Removing an Open File
          proj1 슬라이드 71 - filesys/filesys.h 의 filesys_remove ()
   주의 : 파일이 열려 있어도 삭제는 성공한다
          이미 연 프로세스는 자신의 fd 로 계속 읽고 쓸 수 있고,
          마지막 참조가 닫힐 때 inode 계층이 실제 공간을 회수한다
          따라서 여기서 열린 fd 를 뒤져 닫는 처리를 하면 안 된다
          삭제된 이름으로 새로 open 하는 것만 실패한다 */
bool
remove (const char *file)
{
  bool success;

  check_string (file);

  /* [1-2-7] 수정 : 파일 시스템 접근을 락으로 보호
     변경 내용 : filesys_remove () 호출을 Filesys_lock 으로 감쌌다
     변경 이유 : syn-remove 는 파일을 지우는 도중에도 다른 프로세스가
                그 파일을 읽고 쓴다
     영향 범위 : 단일 프로세스 동작에는 변화가 없다 */
  lock_acquire (&Filesys_lock);
  success = filesys_remove (file);
  lock_release (&Filesys_lock);

  return success;
}

/* [1-2-3] File Descriptor : fd 로 열린 파일 찾기
   목적 : 사용자에게 받은 fd 번호를 이 프로세스의 struct file * 로 바꾼다
   입력 : fd - 사용자 프로그램이 넘긴 파일 디스크립터
   출력 : 유효하면 해당 struct file *, 아니면 NULL
   참고 : threads/thread.h 의 Fd_table, FD_BASE, FD_MAX
   주의 : 0 과 1 은 표준 입출력 몫이라 여기서는 항상 NULL 로 취급한다
          close-bad-fd 는 0x20101234 를 넘기므로 위쪽 범위 검사가 필수다
          범위를 벗어난 인덱스로 접근하면 커널 메모리를 읽게 된다
          커널 스레드는 Fd_table 이 NULL 이므로 먼저 걸러 낸다
          이 함수는 fd 를 검증만 하고 상태를 바꾸지 않는다 */
static struct file *
get_file (int fd)
{
  struct thread *t = thread_current ();

  if (t->Fd_table == NULL)
    return NULL;

  if (fd < FD_BASE || fd >= FD_MAX)
    return NULL;

  return t->Fd_table[fd];
}

/* [1-2-3] open 시스템 콜 : 파일 열기
   목적 : 이름으로 파일을 열고 이 프로세스의 fd 번호를 배정해 돌려준다
   입력 : file - 열 파일의 이름, 사용자 영역 문자열
   출력 : 성공하면 2 이상의 fd, 열 수 없으면 -1
   참고 : Pintos manual 3.3.4 open, proj1 슬라이드 69, 71
   주의 : 빈 칸은 FD_BASE 부터 찾는다, 0 과 1 은 콘솔 몫이다
          같은 파일을 두 번 열면 filesys_open() 이 서로 다른 struct file 을
          돌려주므로 fd 도 서로 달라진다 (open-twice)
          없는 파일과 빈 이름은 filesys_open() 이 NULL 을 돌려주므로
          -1 만 반환하고 프로세스를 죽이지 않는다 (open-missing, open-empty)
          NULL 이나 잘못된 주소는 check_string() 이 exit(-1) 로 처리한다
          테이블이 가득 찬 경우 이미 연 파일을 반드시 닫아야 누수가 없다 */
int
open (const char *file)
{
  struct thread *t = thread_current ();
  struct file *File;
  int fd;

  check_string (file);

  /* [1-2-7] 수정 : 파일 열기와 되돌리기를 락으로 보호
     변경 내용 : filesys_open () 과 실패 시의 file_close () 를 감쌌다
     변경 이유 : 열기는 디렉터리 탐색과 inode 열기를 함께 수행한다
     영향 범위 : Fd_table 탐색은 프로세스마다 따로 있는 자료라 락 밖에 둔다
                락 구간을 짧게 유지해야 syn-read 처럼 프로세스가 많은
                상황에서 불필요한 대기가 줄어든다 */
  lock_acquire (&Filesys_lock);
  File = filesys_open (file);
  lock_release (&Filesys_lock);

  if (File == NULL)
    return -1;

  if (t->Fd_table != NULL)
    for (fd = FD_BASE; fd < FD_MAX; fd++)
      if (t->Fd_table[fd] == NULL)
        {
          t->Fd_table[fd] = File;
          return fd;
        }

  lock_acquire (&Filesys_lock);
  file_close (File);
  lock_release (&Filesys_lock);

  return -1;
}

/* [1-2-3] close 시스템 콜 : 파일 닫기
   목적 : fd 가 가리키는 파일을 닫고 그 칸을 비운다
   입력 : fd - 닫을 파일 디스크립터
   출력 : 없음
   참고 : Pintos manual 3.3.4 close, close-twice.ck / close-stdin.ck 의 기대 출력
   주의 : 유효하지 않은 fd 는 조용히 무시한다
          close-stdin, close-stdout, close-bad-fd, close-twice 는 모두
          조용한 실패와 exit(-1) 을 함께 정답으로 인정하는데,
          프로세스를 죽이지 않는 쪽이 이후 테스트에서 부작용이 적다
          닫은 뒤 반드시 칸을 NULL 로 되돌려야 두 번째 close 가
          해제된 포인터를 다시 넘기지 않는다 (close-twice) */
void
close (int fd)
{
  struct file *File = get_file (fd);

  if (File == NULL)
    return;

  /* [1-2-7] 수정 : 파일 닫기를 락으로 보호
     변경 내용 : file_close () 호출을 Filesys_lock 으로 감쌌다
     변경 이유 : 닫기는 inode 참조 수를 줄이고 필요하면 디스크 공간을 회수한다
     주의 : 테이블 칸을 비우는 것은 프로세스 자신의 자료라 락 밖에서 해도 된다 */
  lock_acquire (&Filesys_lock);
  file_close (File);
  lock_release (&Filesys_lock);

  thread_current ()->Fd_table[fd] = NULL;
}

/* [1-2-3] filesize 시스템 콜 : 파일 크기 조회
   목적 : fd 가 가리키는 파일의 바이트 크기를 돌려준다
   입력 : fd - 크기를 물을 파일 디스크립터
   출력 : 파일 크기, 유효하지 않은 fd 이면 -1
   참고 : Pintos manual 3.3.4 filesize
          proj1 슬라이드 71 - filesys/file.h 의 off_t file_length (struct file *)
   주의 : off_t 는 32비트 정수라 int 로 돌려주어도 값이 잘리지 않는다
          tests/filesys/base 계열이 읽을 크기를 정할 때 이 값을 사용한다 */
int
filesize (int fd)
{
  struct file *File = get_file (fd);
  int length;

  if (File == NULL)
    return -1;

  /* [1-2-7] 수정 : 파일 크기 조회를 락으로 보호
     변경 내용 : file_length () 호출을 Filesys_lock 으로 감쌌다
     변경 이유 : 다른 프로세스의 쓰기로 길이가 바뀌는 도중에 읽으면
                중간 상태를 볼 수 있다 */
  lock_acquire (&Filesys_lock);
  length = file_length (File);
  lock_release (&Filesys_lock);

  return length;
}

/* [1-2-5] seek 시스템 콜 : 파일 위치 이동
   목적 : fd 가 가리키는 파일의 다음 읽기 / 쓰기 위치를 position 으로 옮긴다
   입력 : fd       - 대상 파일 디스크립터
          position - 파일 시작으로부터의 바이트 오프셋
   출력 : 없음
   참고 : Pintos manual 3.3.4 seek
          proj1 슬라이드 71 - filesys/file.h 의 void file_seek (struct file *, off_t)
   주의 : 파일 끝을 넘어선 위치로 옮기는 것도 오류가 아니다
          그 자리에서 읽으면 0 바이트를 돌려주고, 쓰면 파일이 늘어난다
          다만 Pintos 의 기본 파일 시스템은 파일 크기를 키우지 못하므로
          끝을 넘어선 쓰기는 실제로는 0 바이트만 기록된다
          위치는 struct file 마다 따로 유지되므로, 같은 파일을 두 번 열면
          한쪽의 seek 이 다른 쪽에 영향을 주지 않는다
          유효하지 않은 fd 는 조용히 무시한다 */
void
seek (int fd, unsigned position)
{
  struct file *File = get_file (fd);

  if (File == NULL)
    return;

  /* [1-2-7] 수정 : 파일 위치 이동을 락으로 보호
     변경 내용 : file_seek () 호출을 Filesys_lock 으로 감쌌다
     변경 이유 : 위치 갱신과 읽기 / 쓰기가 뒤섞이면 엉뚱한 위치에 접근한다 */
  lock_acquire (&Filesys_lock);
  file_seek (File, position);
  lock_release (&Filesys_lock);
}

/* [1-2-5] tell 시스템 콜 : 파일 위치 조회
   목적 : fd 가 가리키는 파일의 현재 위치를 돌려준다
   입력 : fd - 대상 파일 디스크립터
   출력 : 파일 시작으로부터의 바이트 오프셋, 유효하지 않은 fd 이면 -1
   참고 : Pintos manual 3.3.4 tell
          proj1 슬라이드 71 - filesys/file.h 의 off_t file_tell (struct file *)
   주의 : 반환형이 unsigned 라 -1 은 0xffffffff 로 전달된다
          매뉴얼이 실패 시 반환값을 규정하지 않으므로, 다른 시스템 콜과 같은
          실패 표시를 쓰는 편이 사용자 쪽에서 구분하기 쉽다
          정상적인 위치 값이 0xffffffff 까지 커질 일은 없다 */
unsigned
tell (int fd)
{
  struct file *File = get_file (fd);
  unsigned position;

  if (File == NULL)
    return -1;

  /* [1-2-7] 수정 : 파일 위치 조회를 락으로 보호
     변경 내용 : file_tell () 호출을 Filesys_lock 으로 감쌌다
     변경 이유 : 다른 시스템 콜과 같은 규칙으로 파일 계층 접근을 직렬화한다 */
  lock_acquire (&Filesys_lock);
  position = file_tell (File);
  lock_release (&Filesys_lock);

  return position;
}

/* [1-1-3] 수정 : 시스템 콜 진입 시 스택 포인터부터 검증
   변경 내용 : 기존 골격 앞에 check_address() 호출을 추가해
              f->esp 가 가리키는 4바이트(시스템 콜 번호)의 유효성을 먼저 확인
   변경 이유 : sc-bad-sp 처럼 esp 자체가 잘못된 경우 커널이 그대로
              역참조하면 커널 패닉으로 이어진다
   영향 범위 : esp 가 정상인 기존 동작에는 변화가 없다 */

/* [1-1-4] 수정 : 시스템 콜 번호 추출과 switch 분기 골격 도입
   변경 내용 : "system call!" 출력 대신 f->esp 에서 번호를 읽어
              lib/syscall-nr.h 의 enum 값으로 분기하는 구조를 세운다
   변경 이유 : 이후 단계에서 각 시스템 콜 구현을 case 안에 채워 넣기 위한 뼈대
   영향 범위 : 아직 어떤 case 도 동작을 수행하지 않으므로, 모든 시스템 콜은
              기존과 마찬가지로 thread_exit() 으로 끝난다
   참고 : proj1 슬라이드 28 - esp 로 스택 접근, 반환값은 f->eax 에 저장
          proj1 슬라이드 53 - lib/syscall-nr.h 의 번호를 참고해 switch 로 분류
   주의 : 각 case 의 구현 담당 단계는 아래와 같다
          1-1-5 : SYS_HALT, SYS_EXIT
          1-1-6 : SYS_READ, SYS_WRITE
          1-1-7 : SYS_EXEC, SYS_WAIT
          1-2 단계에서 파일 관련 시스템 콜이 추가된다 */

/* [1-1-6] 수정 : SYS_READ, SYS_WRITE 분기 구현 및 종료 흐름 정리
   변경 내용 : 두 case 를 채우고 반환값을 f->eax 에 저장한다
              switch 뒤에 있던 무조건 thread_exit() 을 제거하고,
              아직 구현되지 않은 case 와 default 에서만 종료하도록 바꿨다
   변경 이유 : 구현된 시스템 콜은 사용자 프로그램으로 정상 복귀해야 한다
              기존 구조는 write 를 처리한 뒤에도 프로세스를 죽여 버린다
   영향 범위 : SYS_EXEC, SYS_WAIT 는 1-1-7 까지 기존과 동일하게 종료된다
   참고 : proj1 슬라이드 28 - 반환값은 f->eax 에 저장한다 */

/* [1-1-7] 수정 : SYS_EXEC, SYS_WAIT 분기 구현
   변경 내용 : 두 case 에서 각각 exec() 과 wait() 을 호출하고 반환값을 f->eax 에 담는다
   변경 이유 : 이제 Prj1-1 범위의 시스템 콜이 모두 채워졌다
   영향 범위 : default 로 떨어지는 것은 Prj1-2 에서 구현할 파일 관련 시스템 콜뿐이다 */

/* [1-1-8] 수정 : 인자 배열 확장과 추가 시스템 콜 분기 구현
   변경 내용 : Arg 배열 크기를 3 에서 4 로 늘리고,
              SYS_FIBONACCI 와 SYS_MAX_OF_FOUR_INT 두 case 를 추가한다
   변경 이유 : max_of_four_int 는 인자가 4개라 기존 Arg[3] 으로는 담을 수 없다
              Arg[3] 인 상태로 get_argument (f->esp, Arg, 4) 를 부르면
              커널 스택을 넘어서 써 버리는 심각한 버그가 된다
   영향 범위 : 배열이 4바이트 커지는 것 외에 기존 case 의 동작은 그대로다
   참고 : proj1 슬라이드 56 - 시스템 콜 번호는 lib/syscall-nr.h 참고,
          반환값은 struct intr_frame 의 eax 로 돌려준다 */

/* [1-2-2] 수정 : SYS_CREATE, SYS_REMOVE 분기 구현
   변경 내용 : 두 case 를 추가하고 반환값(bool)을 f->eax 에 저장한다
              create 는 인자가 2개, remove 는 1개다
   변경 이유 : 지금까지는 두 번호가 default 로 떨어져 exit(-1) 로 끝났다
              create-empty 처럼 우연히 기대 출력과 맞던 항목이 있었으나
              실제 파일 생성이 이루어진 것은 아니었다
   영향 범위 : default 로 떨어지는 것은 open, close, filesize, seek, tell 뿐이다
   참고 : proj1 슬라이드 28 - 반환값은 f->eax 에 저장한다
   주의 : case 는 lib/syscall-nr.h 의 번호 순서가 아니라
          기존 분기 뒤 파일 관련 시스템 콜끼리 모이도록 배치했다 */

/* [1-2-3] 수정 : SYS_OPEN, SYS_FILESIZE, SYS_CLOSE 분기 구현
   변경 내용 : 세 case 를 추가하고, 값을 돌려주는 open 과 filesize 만
              f->eax 에 결과를 저장한다
   변경 이유 : 파일을 열어 fd 를 받을 수 있어야 read, write 를 fd 기반으로
              확장할 수 있다
   영향 범위 : default 로 떨어지는 것은 seek, tell 뿐이다
   주의 : close 는 반환값이 없으므로 f->eax 를 건드리지 않는다
          1-1-9 시점에 우연히 통과하던 close-stdin, close-stdout, close-bad-fd 는
          이제 실제 구현을 거치지만 조용한 실패로 같은 결과가 유지된다 */

/* [1-2-5] 수정 : SYS_SEEK, SYS_TELL 분기 구현
   변경 내용 : 두 case 를 추가한다, seek 은 인자가 2개이고 반환값이 없으며
              tell 은 인자가 1개이고 결과를 f->eax 에 저장한다
   변경 이유 : 파일 임의 접근이 가능해야 tests/filesys/base 의 random 계열이
              동작한다
   영향 범위 : 이제 default 로 떨어지는 파일 관련 시스템 콜은 없다
               Project 3 이후의 SYS_MMAP 등만 남는다
   주의 : seek 의 position 은 부호 없는 값이므로 Arg[1] 을 unsigned 로 변환한다 */
static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_number;
  int Arg[4];

  check_address (f->esp, sizeof (int));
  syscall_number = * (int *) f->esp;

  switch (syscall_number)
    {
    case SYS_HALT:
      halt ();
      break;

    case SYS_EXIT:
      get_argument (f->esp, Arg, 1);
      exit (Arg[0]);
      break;

    case SYS_READ:
      get_argument (f->esp, Arg, 3);
      f->eax = read (Arg[0], (void *) Arg[1], (unsigned) Arg[2]);
      break;

    case SYS_WRITE:
      get_argument (f->esp, Arg, 3);
      f->eax = write (Arg[0], (const void *) Arg[1], (unsigned) Arg[2]);
      break;

    case SYS_EXEC:
      get_argument (f->esp, Arg, 1);
      f->eax = exec ((const char *) Arg[0]);
      break;

    case SYS_WAIT:
      get_argument (f->esp, Arg, 1);
      f->eax = wait ((tid_t) Arg[0]);
      break;

    case SYS_CREATE:
      get_argument (f->esp, Arg, 2);
      f->eax = create ((const char *) Arg[0], (unsigned) Arg[1]);
      break;

    case SYS_REMOVE:
      get_argument (f->esp, Arg, 1);
      f->eax = remove ((const char *) Arg[0]);
      break;

    case SYS_OPEN:
      get_argument (f->esp, Arg, 1);
      f->eax = open ((const char *) Arg[0]);
      break;

    case SYS_FILESIZE:
      get_argument (f->esp, Arg, 1);
      f->eax = filesize (Arg[0]);
      break;

    case SYS_CLOSE:
      get_argument (f->esp, Arg, 1);
      close (Arg[0]);
      break;

    case SYS_SEEK:
      get_argument (f->esp, Arg, 2);
      seek (Arg[0], (unsigned) Arg[1]);
      break;

    case SYS_TELL:
      get_argument (f->esp, Arg, 1);
      f->eax = tell (Arg[0]);
      break;

    case SYS_FIBONACCI:
      get_argument (f->esp, Arg, 1);
      f->eax = fibonacci (Arg[0]);
      break;

    case SYS_MAX_OF_FOUR_INT:
      get_argument (f->esp, Arg, 4);
      f->eax = max_of_four_int (Arg[0], Arg[1], Arg[2], Arg[3]);
      break;

    default:
      /* 아직 지원하지 않는 시스템 콜 번호는 잘못된 요청으로 본다 */
      exit (-1);
      break;
    }
}