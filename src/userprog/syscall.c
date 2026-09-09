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

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
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
int
write (int fd, const void *buffer, unsigned size)
{
  check_address (buffer, size);

  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }

  return -1;
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
int
read (int fd, void *buffer, unsigned size)
{
  uint8_t *buf = buffer;
  unsigned i;

  check_address (buffer, size);

  if (fd == STDIN_FILENO)
    {
      for (i = 0; i < size; i++)
        buf[i] = input_getc ();
      return size;
    }

  return -1;
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