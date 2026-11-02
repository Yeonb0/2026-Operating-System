#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"
#include "threads/thread.h"

/* [3-1-1] Frame Table : user pool frame 의 할당 · 해제 관리
   목적 : user pool 에서 받은 물리 페이지(frame)를 frame table 로 감싸
          어느 프로세스가 어느 사용자 가상 페이지에 쓰는지 추적한다
   입력 : frame_alloc() - palloc 플래그(PAL_USER 필수), 매핑될 사용자 가상 페이지
          frame_free()  - frame_alloc() 이 돌려준 커널 가상 주소
          frame_remove_owner() - 종료하는 프로세스의 thread
   출력 : frame_alloc() 은 커널 가상 주소, 실패하면 NULL
   참고 : Pintos manual 4.1.5, 4.2, 조교 슬라이드 12 · 17
   주의 : 이 단계에는 eviction 과 swap 이 없어 user pool 이 차면 NULL 을 반환한다
          모든 함수는 프로세스 문맥에서만 부르며 Frame_list 는 Frame_lock 으로 보호한다 */
struct frame
  {
    void *kpage;              /* 커널 가상 주소 (palloc 이 돌려준 값) */
    void *upage;              /* 매핑될 사용자 가상 페이지 */
    struct thread *owner;     /* 이 frame 을 쓰는 프로세스 */
    struct list_elem elem;    /* Frame_list 의 원소 */
  };

void frame_init (void);
void *frame_alloc (enum palloc_flags flags, void *upage);
void frame_free (void *kpage);
void frame_remove_owner (struct thread *t);

#endif /* vm/frame.h */
