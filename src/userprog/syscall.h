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

#endif /* userprog/syscall.h */