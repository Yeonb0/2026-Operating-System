#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* [1-1-7] exec, wait 의 반환형과 인자형인 tid_t 를 쓰기 위해 포함
   참고 : threads/thread.h */
#include "threads/thread.h"

void syscall_init (void);

/* [1-1-3] User Memory Access : 사용자 포인터 검사 함수 선언
   목적 : syscall.c 이외의 파일에서도 같은 검사 규칙을 쓸 수 있도록 공개한다
   참고 : proj1 슬라이드 53 - syscall.h 에 시스템 콜 프로토타입을 작성하라는 안내
   주의 : 구현은 userprog/syscall.c 에 있다 */
void check_address (const void *addr, unsigned size);

/* [1-1-4] System Call Handler : 인자 추출 함수 선언
   목적 : 사용자 스택에 쌓인 시스템 콜 인자를 커널 배열로 옮긴다
   참고 : proj1 슬라이드 25, 28
   주의 : 구현은 userprog/syscall.c 에 있다 */
void get_argument (void *esp, int *Arg, int count);

/* [1-1-5] 시스템 콜 프로토타입 : halt, exit
   목적 : switch 분기에서 호출할 실제 구현의 선언
   참고 : Pintos manual 3.3.4 System Calls, proj1 슬라이드 42 - 43
   주의 : 빌드 옵션에 -Wmissing-prototypes 가 있으므로 전역 함수는
          반드시 헤더에 선언을 두어야 경고 없이 컴파일된다 */
void halt (void);
void exit (int status);

/* [1-1-6] 시스템 콜 프로토타입 : read, write
   목적 : 표준 입출력 전용 구현의 선언
   참고 : proj1 슬라이드 45, Pintos manual 3.3.4
   주의 : 1-2 단계에서 fd 기반 파일 입출력으로 확장되며, 시그니처는 유지된다 */
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);

/* [1-1-7] 시스템 콜 프로토타입 : exec, wait
   목적 : 자식 프로세스 생성과 종료 대기
   참고 : Pintos manual 3.3.4, proj1 슬라이드 44
   주의 : 실제 프로세스 관리 로직은 userprog/process.c 에 둔다 */
tid_t exec (const char *cmd_line);
int wait (tid_t tid);

/* [1-1-8] 시스템 콜 프로토타입 : fibonacci, max_of_four_int
   목적 : 추가 시스템 콜의 커널 측 구현을 switch 분기에서 호출하기 위한 선언
   참고 : proj1 슬라이드 54, 58 - userprog/syscall.h 에 프로토타입 작성
   주의 : lib/user/syscall.h 에도 같은 이름의 선언이 있지만 그쪽은
          사용자 프로그램 전용이라 커널 빌드에는 포함되지 않는다
          read, write, exit 도 이미 같은 구조이므로 충돌하지 않는다 */
int fibonacci (int n);
int max_of_four_int (int a, int b, int c, int d);

/* [1-2-2] 시스템 콜 프로토타입 : create, remove
   목적 : switch 분기에서 호출할 파일 생성 / 삭제 구현의 선언
   참고 : Pintos manual 3.3.4 create, remove
          proj1 슬라이드 71 - filesys/filesys.h 의 API 를 사용한다
   주의 : bool 은 위에서 포함한 threads/thread.h 가 <stdbool.h> 를 들여오므로
          별도 포함이 필요 없다
          lib/user/syscall.h 에도 같은 이름의 선언이 있지만 그쪽은
          사용자 프로그램 전용이라 커널 빌드에는 포함되지 않는다 */
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);

/* [1-2-3] 시스템 콜 프로토타입 : open, filesize, close
   목적 : 파일 디스크립터를 배정하고 조회하고 반납하는 구현의 선언
   참고 : Pintos manual 3.3.4 open / filesize / close, proj1 슬라이드 69, 71
   주의 : fd 를 struct file * 로 바꾸는 get_file () 은 static 이라
          여기에 선언하지 않는다
          close 는 반환값이 없으므로 switch 에서 f->eax 를 건드리지 않는다 */
int open (const char *file);
int filesize (int fd);
void close (int fd);

/* [1-2-5] 시스템 콜 프로토타입 : seek, tell
   목적 : 파일 내 임의 위치 접근을 처리하는 구현의 선언
   참고 : Pintos manual 3.3.4 seek / tell, proj1 슬라이드 71
   주의 : tell 의 반환형은 매뉴얼 표기대로 unsigned 다
          seek 은 반환값이 없으므로 switch 에서 f->eax 를 건드리지 않는다 */
void seek (int fd, unsigned position);
unsigned tell (int fd);

/* [1-2-7] 파일 시스템 동기화 : 전역 락 선언
   목적 : userprog/process.c 의 load () 와 process_exit () 도 같은 락으로
          파일 시스템 접근을 직렬화할 수 있게 공개한다
   참고 : Pintos manual 3.1.2 - 기본 파일 시스템에는 내부 동기화가 없다
   주의 : 실체는 userprog/syscall.c 에 있고 syscall_init () 에서 초기화된다
          struct lock 은 threads/thread.h 가 들여오는 threads/synch.h 에 있다
          락을 쥔 채로 exit(-1) 경로에 들어가면 시스템이 멈추므로,
          사용자 포인터 검증은 반드시 락 밖에서 끝내야 한다 */
extern struct lock Filesys_lock;

#endif /* userprog/syscall.h */