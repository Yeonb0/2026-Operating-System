#include "vm/frame.h"
#include <debug.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include <string.h>
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

/* [3-1-1] Frame Table : 할당된 모든 user frame 의 목록과 이를 보호하는 락 */
static struct list Frame_list;
static struct lock Frame_lock;

/* [3-2-2] Eviction : second chance 의 시계 바늘
   목적 : 다음에 살펴볼 Frame_list 원소를 기억한다
   입력 : 없음
   출력 : 없음
   참고 : Pintos manual 4.1.5, 4.1.5.1, 4.3.2, 조교 슬라이드 4 · 12 (second chance)
   주의 : NULL 이면 목록 처음에서 시작한다, Frame_lock 을 잡은 상태에서만 바꾼다
          원소가 목록에서 빠질 때 바늘이 가리키고 있으면 다음 원소로 옮긴다 */
static struct list_elem *Clock_hand;

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
  /* [3-2-2] Eviction : 시계 바늘 초기화
     변경 내용 : frame_init () 에서 Clock_hand 를 NULL 로 둔다
     변경 이유 : second chance 가 목록 처음에서 시작하게 하기 위해 (매뉴얼 4.1.5.1)
     영향 범위 : frame_init () 의 마지막 줄 */
  Clock_hand = NULL;
}

/* [3-2-2] Eviction : 희생 frame 을 골라 내보내기
   목적 : user pool 이 비었을 때 second chance (clock) 로 희생 frame 을 골라
          내용을 swap 에 쓰거나 버리고 그 물리 페이지를 돌려준다
   입력 : flags - frame_alloc () 이 받은 palloc 플래그, PAL_ZERO 면 돌려주는 페이지를 0 으로 채운다
   출력 : 비워진 물리 페이지의 커널 가상 주소
   참고 : Pintos manual 4.1.5, 4.1.5.1, 4.3.2, 조교 슬라이드 4 · 12 (second chance)
   주의 : p->kpage = NULL → pagedir_clear_page → pagedir_is_dirty 순서를 지킨다, PTE 를 먼저 끊어 그 뒤의 쓰기가 사라지지 않게 한다
          깨끗한 실행 파일 페이지는 swap 에 쓰지 않는다 (매뉴얼 4.3.2), swap 에 쓴 페이지는 PAGE_ANON 으로 바꾼다
          Frame_lock 을 잡은 채 swap_out () 을 부르지만 Filesys_lock 은 잡지 않는다
          항목 free 는 락을 놓은 뒤에 한다, 고를 frame 이 없으면 panic 한다 */
static void *
frame_evict (enum palloc_flags flags)
{
  struct frame *victim = NULL;
  struct page *p;
  struct thread *t;
  void *kpage;
  size_t n;
  size_t i;

  lock_acquire (&Frame_lock);

  n = list_size (&Frame_list);
  for (i = 0; i < 2 * n; i++)
    {
      struct frame *f;

      if (Clock_hand == NULL || Clock_hand == list_end (&Frame_list))
        Clock_hand = list_begin (&Frame_list);
      if (Clock_hand == list_end (&Frame_list))
        break;

      f = list_entry (Clock_hand, struct frame, elem);
      Clock_hand = list_next (Clock_hand);

      if (f->pinned || f->page == NULL)
        continue;
      if (pagedir_is_accessed (f->owner->pagedir, f->upage))
        {
          pagedir_set_accessed (f->owner->pagedir, f->upage, false);
          continue;
        }

      victim = f;
      break;
    }

  if (victim == NULL)
    PANIC ("frame_evict: no frame to evict");

  p = victim->page;
  t = victim->owner;
  kpage = victim->kpage;

  if (Clock_hand == &victim->elem)
    Clock_hand = list_next (Clock_hand);
  list_remove (&victim->elem);

  p->kpage = NULL;
  pagedir_clear_page (t->pagedir, p->upage);
  if (p->type == PAGE_ANON || pagedir_is_dirty (t->pagedir, p->upage))
    {
      p->swap_slot = swap_out (kpage);
      p->type = PAGE_ANON;
    }

  lock_release (&Frame_lock);

  free (victim);

  if ((flags & PAL_ZERO) != 0)
    memset (kpage, 0, PGSIZE);
  return kpage;
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
  /* [3-2-2] Eviction : frame 이 없으면 희생 frame 을 내보낸다
     변경 내용 : palloc_get_page () 가 NULL 이면 frame_evict () 로 물리 페이지를 얻는다
     변경 이유 : user pool 이 차도 swap 으로 내보내 계속 적재하기 위해 (매뉴얼 4.1.5)
     영향 범위 : frame_alloc () 의 할당 직후, 그래도 NULL 이면 기존대로 NULL 을 반환한다 */
  if (kpage == NULL)
    kpage = frame_evict (flags);
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
  /* [3-2-2] Eviction : 새 frame 의 초기 상태
     변경 내용 : page = NULL, pinned = true 로 시작한다
     변경 이유 : 적재가 끝나 frame_install () 이 불리기 전의 frame 은 eviction 대상에서 뺀다 (매뉴얼 4.3.5)
     영향 범위 : frame_alloc () 이 만드는 모든 frame */
  f->page = NULL;
  f->pinned = true;

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
          /* [3-2-2] Eviction : 시계 바늘 보정
             변경 내용 : 지우는 원소를 Clock_hand 가 가리키면 다음 원소로 옮긴다
             변경 이유 : 지워진 원소를 바늘이 가리키지 않게 하기 위해
             영향 범위 : frame_free () 의 제거 직전 */
          if (Clock_hand == e)
            Clock_hand = list_next (e);
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
          /* [3-2-2] Eviction : 시계 바늘 보정
             변경 내용 : 지우는 원소를 Clock_hand 가 가리키면 다음 원소로 옮긴다
             변경 이유 : 지워진 원소를 바늘이 가리키지 않게 하기 위해
             영향 범위 : frame_remove_owner () 의 제거 직전 */
          if (Clock_hand == e)
            Clock_hand = list_next (e);
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

/* [3-2-2] Frame 에 페이지 연결
   목적 : 적재가 끝난 frame 에 SPT 항목을 연결하고 eviction 대상으로 푼다
   입력 : kpage - frame_alloc () 이 돌려준 커널 가상 주소
          page  - 이 frame 에 실린 SPT 항목
   출력 : 없음
   참고 : Pintos manual 4.1.5, 4.1.5.1, 4.3.2, 조교 슬라이드 4 · 12 (second chance)
   주의 : kpage 의 frame 이 없으면 아무것도 하지 않는다 */
void
frame_install (void *kpage, struct page *page)
{
  struct list_elem *e;

  lock_acquire (&Frame_lock);
  for (e = list_begin (&Frame_list); e != list_end (&Frame_list);
       e = list_next (e))
    {
      struct frame *f = list_entry (e, struct frame, elem);
      if (f->kpage == kpage)
        {
          f->page = page;
          f->pinned = false;
          break;
        }
    }
  lock_release (&Frame_lock);
}

/* [3-2-2] 메모리에 있는 페이지의 frame 고정
   목적 : 페이지가 메모리에 있으면 그 frame 을 pinned 로 해 eviction 에서 뺀다
   입력 : page - 고정할 SPT 항목
   출력 : 메모리에 있어 고정했으면 true, 없으면 false
   참고 : Pintos manual 4.3.5 (pinning)
   주의 : 검사와 고정을 Frame_lock 안에서 한 번에 해 그 사이에 eviction 이 끼어들지 못하게 한다 */
bool
frame_pin_resident (struct page *page)
{
  struct list_elem *e;
  bool found = false;

  lock_acquire (&Frame_lock);
  if (page->kpage != NULL)
    {
      for (e = list_begin (&Frame_list); e != list_end (&Frame_list);
           e = list_next (e))
        {
          struct frame *f = list_entry (e, struct frame, elem);
          if (f->kpage == page->kpage)
            {
              f->pinned = true;
              found = true;
              break;
            }
        }
    }
  lock_release (&Frame_lock);
  return found;
}

/* [3-2-2] 프로세스 frame 고정 해제
   목적 : 시스템 콜이 끝나면 그 프로세스의 frame 을 다시 eviction 대상으로 푼다
   입력 : t - 시스템 콜을 처리한 프로세스의 thread
   출력 : 없음
   참고 : Pintos manual 4.3.5 (pinning)
   주의 : 페이지가 연결된 frame 만 푼다, 적재 중인 frame (page 가 NULL) 은 건드리지 않는다 */
void
frame_unpin_owner (struct thread *t)
{
  struct list_elem *e;

  lock_acquire (&Frame_lock);
  for (e = list_begin (&Frame_list); e != list_end (&Frame_list);
       e = list_next (e))
    {
      struct frame *f = list_entry (e, struct frame, elem);
      if (f->owner == t && f->page != NULL)
        f->pinned = false;
    }
  lock_release (&Frame_lock);
}
