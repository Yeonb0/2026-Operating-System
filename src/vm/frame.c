#include "vm/frame.h"
#include <debug.h>
#include "threads/malloc.h"
#include "threads/synch.h"

/* [3-1-1] Frame Table : 할당된 모든 user frame 의 목록과 이를 보호하는 락 */
static struct list Frame_list;
static struct lock Frame_lock;

/* [3-1-1] Frame Table 초기화
   목적 : Frame_list 와 Frame_lock 을 초기화한다
   입력 : 없음
   출력 : 없음
   참고 : Pintos manual 4.1.5, 4.2, 조교 슬라이드 12 · 17
   주의 : malloc_init () 과 paging_init () 뒤, 첫 사용자 프로세스 전에 한 번만 부른다 */
void
frame_init (void)
{
  list_init (&Frame_list);
  lock_init (&Frame_lock);
}

/* [3-1-1] Frame 할당
   목적 : user pool 에서 페이지를 받아 frame 항목을 만들고 Frame_list 에 등록한다
   입력 : flags - palloc 플래그, PAL_USER 가 반드시 켜져 있어야 한다
          upage - 이 frame 이 매핑될 사용자 가상 페이지
   출력 : 성공하면 kpage (커널 가상 주소), 실패하면 NULL
   참고 : Pintos manual 4.1.5, 4.2, 조교 슬라이드 12 · 17
   주의 : eviction 은 3-2-2 에서 구현하므로 여기서는 하지 않는다
          락을 잡은 채 palloc 함수를 부르지 않는다 */
void *
frame_alloc (enum palloc_flags flags, void *upage)
{
  void *kpage;
  struct frame *f;

  ASSERT ((flags & PAL_USER) != 0);

  kpage = palloc_get_page (flags);
  if (kpage == NULL)
    return NULL;

  f = malloc (sizeof *f);
  if (f == NULL)
    {
      palloc_free_page (kpage);
      return NULL;
    }

  f->kpage = kpage;
  f->upage = upage;
  f->owner = thread_current ();

  lock_acquire (&Frame_lock);
  list_push_back (&Frame_list, &f->elem);
  lock_release (&Frame_lock);

  return kpage;
}

/* [3-1-1] Frame 해제
   목적 : kpage 의 frame 항목을 Frame_list 에서 빼고 물리 페이지를 해제한다
   입력 : kpage - frame_alloc () 이 돌려준 커널 가상 주소
   출력 : 없음
   참고 : Pintos manual 4.1.5, 4.2, 조교 슬라이드 12 · 17
   주의 : 항목이 없어도 페이지는 해제한다, ASSERT 나 PANIC 을 하지 않는다
          락을 놓은 뒤에 palloc_free_page () 를 부른다 */
void
frame_free (void *kpage)
{
  struct list_elem *e;
  struct frame *found = NULL;

  lock_acquire (&Frame_lock);
  for (e = list_begin (&Frame_list); e != list_end (&Frame_list);
       e = list_next (e))
    {
      struct frame *f = list_entry (e, struct frame, elem);
      if (f->kpage == kpage)
        {
          list_remove (e);
          found = f;
          break;
        }
    }
  lock_release (&Frame_lock);

  if (found != NULL)
    free (found);
  palloc_free_page (kpage);
}

/* [3-1-1] 프로세스 소유 frame 항목 제거
   목적 : 종료하는 프로세스가 소유한 frame 항목을 모두 Frame_list 에서 빼고 free 한다
   입력 : t - 종료하는 프로세스의 thread
   출력 : 없음
   참고 : Pintos manual 4.1.5, 4.2, 조교 슬라이드 12 · 17
   주의 : 물리 페이지는 해제하지 않는다, process_exit () 에서 pagedir_destroy () 직전에 불러 해제는 pagedir_destroy () 가 맡는다 */
void
frame_remove_owner (struct thread *t)
{
  struct list_elem *e;
  struct list removed;

  list_init (&removed);

  lock_acquire (&Frame_lock);
  e = list_begin (&Frame_list);
  while (e != list_end (&Frame_list))
    {
      struct frame *f = list_entry (e, struct frame, elem);
      if (f->owner == t)
        {
          e = list_remove (e);
          list_push_back (&removed, &f->elem);
        }
      else
        e = list_next (e);
    }
  lock_release (&Frame_lock);

  /* free () 가 내부에서 palloc_free_page () 를 부를 수 있어 락을 놓은 뒤 해제한다 */
  while (!list_empty (&removed))
    {
      struct list_elem *r = list_pop_front (&removed);
      free (list_entry (r, struct frame, elem));
    }
}
