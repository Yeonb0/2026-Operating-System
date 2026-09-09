#ifndef __LIB_SYSCALL_NR_H
#define __LIB_SYSCALL_NR_H

/* System call numbers. */
enum 
  {
    /* Projects 2 and later. */
    SYS_HALT,                   /* Halt the operating system. */
    SYS_EXIT,                   /* Terminate this process. */
    SYS_EXEC,                   /* Start another process. */
    SYS_WAIT,                   /* Wait for a child process to die. */
    SYS_CREATE,                 /* Create a file. */
    SYS_REMOVE,                 /* Delete a file. */
    SYS_OPEN,                   /* Open a file. */
    SYS_FILESIZE,               /* Obtain a file's size. */
    SYS_READ,                   /* Read from a file. */
    SYS_WRITE,                  /* Write to a file. */
    SYS_SEEK,                   /* Change position in a file. */
    SYS_TELL,                   /* Report current position in a file. */
    SYS_CLOSE,                  /* Close a file. */

    /* Project 3 and optionally project 4. */
    SYS_MMAP,                   /* Map a file into memory. */
    SYS_MUNMAP,                 /* Remove a memory mapping. */

    /* Project 4 only. */
    SYS_CHDIR,                  /* Change the current directory. */
    SYS_MKDIR,                  /* Create a directory. */
    SYS_READDIR,                /* Reads a directory entry. */
    SYS_ISDIR,                  /* Tests if a fd represents a directory. */
    SYS_INUMBER,                /* Returns the inode number for a fd. */

    /* [1-1-8] 추가 시스템 콜 번호 : fibonacci, max_of_four_int
       목적 : 과제에서 새로 요구하는 두 시스템 콜에 번호를 부여한다
       입력 : 없음 (enum 정의)
       출력 : SYS_FIBONACCI = 20, SYS_MAX_OF_FOUR_INT = 21
       참고 : proj1 슬라이드 56, 58, 82 - lib/syscall-nr.h 에 번호를 추가하라는 안내
       주의 : 반드시 enum 의 맨 끝에 붙여야 한다
              중간에 끼워 넣으면 기존 시스템 콜 번호가 전부 밀려서
              이미 통과한 테스트가 한꺼번에 깨진다
              바로 위 SYS_INUMBER 는 마지막 항목이 아니게 되었으므로
              쉼표만 추가했고 주석은 그대로 두었다 */
    SYS_FIBONACCI,              /* Return the n-th Fibonacci number. */
    SYS_MAX_OF_FOUR_INT         /* Return the maximum of four integers. */
  };

#endif /* lib/syscall-nr.h */