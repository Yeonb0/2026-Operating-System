#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include <stdbool.h>
#include <debug.h>

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

/* Maximum characters in a filename written by readdir(). */
#define READDIR_MAX_LEN 14

/* Typical return values from main() and arguments to exit(). */
#define EXIT_SUCCESS 0          /* Successful execution. */
#define EXIT_FAILURE 1          /* Unsuccessful execution. */

/* Projects 2 and later. */
void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
pid_t exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

/* Project 3 and optionally project 4. */
mapid_t mmap (int fd, void *addr);
void munmap (mapid_t);

/* Project 4 only. */
bool chdir (const char *dir);
bool mkdir (const char *dir);
bool readdir (int fd, char name[READDIR_MAX_LEN + 1]);
bool isdir (int fd);
int inumber (int fd);

/* [1-1-8] 추가 시스템 콜 API 프로토타입 : fibonacci, max_of_four_int
   목적 : 사용자 프로그램이 두 시스템 콜을 일반 함수처럼 호출할 수 있게 한다
   입력 : fibonacci - 몇 번째 항인지를 나타내는 정수 n
          max_of_four_int - 비교할 정수 4개
   출력 : 각각 n 번째 피보나치 수와 네 정수 중 최댓값
   참고 : proj1 슬라이드 54, 58 - lib/user/syscall.h 에 API 프로토타입 작성
   주의 : 이 헤더는 사용자 프로그램 전용이며 커널은 포함하지 않는다
          커널 쪽 선언은 userprog/syscall.h 에 따로 둔다
          함수 이름은 과제에서 지정한 그대로여야 한다 (슬라이드 54) */
int fibonacci (int n);
int max_of_four_int (int a, int b, int c, int d);

#endif /* lib/user/syscall.h */