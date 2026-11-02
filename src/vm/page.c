#include "vm/page.h"
#include <debug.h>
#include <string.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"

/* [3-1-2] Supplemental Page Table : 해시 함수
   목적 : struct page 의 upage 를 키로 해시 값을 만든다
   입력 : e - struct page 의 elem
   출력 : upage 의 해시 값
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : upage 값만 해시하며 aux 는 쓰지 않는다 */
static unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *p = hash_entry (e, struct page, elem);
  return hash_bytes (&p->upage, sizeof p->upage);
}

/* [3-1-2] Supplemental Page Table : 비교 함수
   목적 : 두 항목을 upage 주소 순으로 비교한다
   입력 : a, b - 비교할 struct page 의 elem
   출력 : a 의 upage 가 b 의 upage 보다 작으면 true
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : aux 는 쓰지 않는다 */
static bool
page_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux UNUSED)
{
  const struct page *pa = hash_entry (a, struct page, elem);
  const struct page *pb = hash_entry (b, struct page, elem);
  return pa->upage < pb->upage;
}

/* [3-1-2] Supplemental Page Table : 항목 해제 함수
   목적 : hash_destroy () 가 각 항목마다 불러 항목을 free 한다
   입력 : e - 해제할 struct page 의 elem
   출력 : 없음
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : 물리 페이지와 frame 항목은 process_exit () 의 frame_remove_owner () 와 pagedir_destroy () 가 정리한다 */
static void
page_destructor (struct hash_elem *e, void *aux UNUSED)
{
  /* [3-2-2] Eviction : swap 에 남은 슬롯 해제
     변경 내용 : 메모리에 없고 swap 슬롯이 있는 항목은 free 하기 전에 슬롯을 비운다
     변경 이유 : 프로세스가 끝날 때 swap 에 남은 페이지의 슬롯이 새지 않게 하기 위해 (매뉴얼 4.1.6)
     영향 범위 : page_destructor () 의 첫머리 */
  struct page *p = hash_entry (e, struct page, elem);
  if (p->kpage == NULL && p->swap_slot != PAGE_NO_SLOT)
    swap_free (p->swap_slot);
  free (hash_entry (e, struct page, elem));
}

/* [3-1-2] Supplemental Page Table 초기화
   목적 : 프로세스의 Spt 를 빈 해시 테이블로 만든다
   입력 : spt - 초기화할 hash
   출력 : 성공하면 true, hash_init () 의 buckets 할당에 실패하면 false
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : false 를 돌려받은 hash 에는 page_table_destroy () 를 부르지 않는다 */
bool
page_table_init (struct hash *spt)
{
  return hash_init (spt, page_hash, page_less, NULL);
}

/* [3-1-2] Supplemental Page Table 해제
   목적 : Spt 의 모든 항목과 buckets 를 free 한다
   입력 : spt - page_table_init () 이 성공한 hash
   출력 : 없음
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : frame 과 물리 페이지는 건드리지 않는다 */
void
page_table_destroy (struct hash *spt)
{
  hash_destroy (spt, page_destructor);
}

/* [3-1-2] 실행 파일 페이지 항목 추가
   목적 : 실행 파일 세그먼트의 페이지 하나를 현재 프로세스의 Spt 에 기록한다
   입력 : upage - 사용자 가상 페이지 (페이지 정렬)
          file, ofs - 실행 파일과 이 페이지가 시작하는 파일 위치
          read_bytes, zero_bytes - 파일에서 읽을 바이트 수와 0 으로 채울 바이트 수
          writable - 쓰기 가능 여부
   출력 : 만든 항목, malloc 실패나 같은 upage 가 이미 있으면 NULL
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : kpage 는 NULL 로 두고 처음 접근할 때 handle_mm_fault () 가 채운다 */
struct page *
page_add_file (void *upage, struct file *file, off_t ofs,
               uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct page *p;

  ASSERT (pg_ofs (upage) == 0);

  p = malloc (sizeof *p);
  if (p == NULL)
    return NULL;

  p->upage = upage;
  p->kpage = NULL;
  p->type = PAGE_BIN;
  p->writable = writable;
  p->file = file;
  p->ofs = ofs;
  p->read_bytes = read_bytes;
  p->zero_bytes = zero_bytes;
  /* [3-2-2] Eviction : swap 슬롯 초기값
     변경 내용 : swap_slot 을 PAGE_NO_SLOT 으로 둔다
     변경 이유 : 새 항목은 swap 에 없다 (매뉴얼 4.1.6)
     영향 범위 : page_add_file () 가 만드는 항목 */
  p->swap_slot = PAGE_NO_SLOT;

  if (hash_insert (&thread_current ()->Spt, &p->elem) != NULL)
    {
      free (p);
      return NULL;
    }
  return p;
}

/* [3-1-2] 익명 페이지 항목 추가
   목적 : 파일 원본이 없는 페이지 (스택) 하나를 현재 프로세스의 Spt 에 기록한다
   입력 : upage - 사용자 가상 페이지 (페이지 정렬)
          writable - 쓰기 가능 여부
   출력 : 만든 항목, malloc 실패나 같은 upage 가 이미 있으면 NULL
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : zero_bytes 는 PGSIZE 로 두고 kpage 는 호출한 쪽이 채운다 */
struct page *
page_add_anon (void *upage, bool writable)
{
  struct page *p;

  ASSERT (pg_ofs (upage) == 0);

  p = malloc (sizeof *p);
  if (p == NULL)
    return NULL;

  p->upage = upage;
  p->kpage = NULL;
  p->type = PAGE_ANON;
  p->writable = writable;
  p->file = NULL;
  p->ofs = 0;
  p->read_bytes = 0;
  p->zero_bytes = PGSIZE;
  /* [3-2-2] Eviction : swap 슬롯 초기값
     변경 내용 : swap_slot 을 PAGE_NO_SLOT 으로 둔다
     변경 이유 : 새 항목은 swap 에 없다 (매뉴얼 4.1.6)
     영향 범위 : page_add_anon () 가 만드는 항목 */
  p->swap_slot = PAGE_NO_SLOT;

  if (hash_insert (&thread_current ()->Spt, &p->elem) != NULL)
    {
      free (p);
      return NULL;
    }
  return p;
}

/* [3-1-2] Spt 에서 항목 찾기
   목적 : 주소가 속한 페이지의 항목을 찾는다
   입력 : spt - 찾을 hash
          vaddr - 사용자 가상 주소 (페이지 정렬이 아니어도 된다)
   출력 : 항목, 없으면 NULL
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : vaddr 를 pg_round_down () 으로 내려 찾는다 */
struct page *
page_find (struct hash *spt, const void *vaddr)
{
  struct page key;
  struct hash_elem *e;

  key.upage = pg_round_down (vaddr);
  e = hash_find (spt, &key.elem);
  if (e == NULL)
    return NULL;
  return hash_entry (e, struct page, elem);
}

/* [3-1-3] Page Fault 처리 : 페이지 적재
   목적 : 아직 메모리에 없는 페이지에 frame 을 얻어 내용을 채우고 매핑한다
   입력 : p - 적재할 페이지의 Spt 항목 (kpage 가 NULL)
   출력 : 성공하면 true, frame 이 없거나 읽기 · 매핑에 실패하면 false
   참고 : Pintos manual 4.1.4, 4.3.2, 조교 슬라이드 10 · 12
   주의 : Filesys_lock 을 잡지 않은 상태에서만 부른다, frame 이 없으면 false 를 돌려주며 교체 알고리즘은 3-2-2 에서 넣는다
          파일 위치를 공유하지 않도록 file_read () 대신 file_read_at () 을 쓴다 */
bool
handle_mm_fault (struct page *p)
{
  uint8_t *kpage;

  ASSERT (p != NULL && p->kpage == NULL);

  kpage = frame_alloc (PAL_USER, p->upage);
  if (kpage == NULL)
    return false;

  /* [3-4-1] Memory Mapped Files : mmap 페이지도 파일에서 읽기
     변경 내용 : PAGE_BIN 에 더해 PAGE_MMAP 도 file_read_at () 로 채운다
     변경 이유 : mmap 페이지는 처음 접근할 때 매핑된 파일에서 지연 적재한다 (매뉴얼 4.3.4)
     영향 범위 : handle_mm_fault () 의 내용 채우기 분기 */
  if (p->type == PAGE_BIN || p->type == PAGE_MMAP)
    {
      off_t n;

      lock_acquire (&Filesys_lock);
      n = file_read_at (p->file, kpage, p->read_bytes, p->ofs);
      lock_release (&Filesys_lock);

      if (n != (off_t) p->read_bytes)
        {
          frame_free (kpage);
          return false;
        }
      memset (kpage + p->read_bytes, 0, p->zero_bytes);
    }
  else
    memset (kpage, 0, PGSIZE);

  /* [3-4-1] Memory Mapped Files : swap 에서 돌아오는지 기억
     변경 내용 : swap_in 블록 전에 swap_slot 이 있었는지를 from_swap 에 담아 둔다
     변경 이유 : swap_in 블록이 swap_slot 을 지우므로 매핑 뒤에 dirty 표시를 되살릴 때 쓰기 위해
     영향 범위 : handle_mm_fault () 의 지역 변수 */
  bool from_swap = p->swap_slot != PAGE_NO_SLOT;

  /* [3-2-2] Eviction : swap 에 있는 페이지 읽기
     변경 내용 : swap_slot 이 있으면 swap_in () 으로 내용을 덮고 슬롯을 PAGE_NO_SLOT 으로 되돌린다
     변경 이유 : eviction 이 PAGE_ANON 으로 바꿔 둔 페이지는 위에서 0 으로만 채워지므로 여기서 swap 내용으로 덮는다 (매뉴얼 4.1.6)
     영향 범위 : handle_mm_fault () 의 내용 채우기 직후 */
  if (p->swap_slot != PAGE_NO_SLOT)
    {
      swap_in (p->swap_slot, kpage);
      p->swap_slot = PAGE_NO_SLOT;
    }

  if (!pagedir_set_page (thread_current ()->pagedir, p->upage, kpage,
                         p->writable))
    {
      frame_free (kpage);
      return false;
    }

  /* [3-4-1] Memory Mapped Files : swap 에서 돌아온 mmap 페이지의 dirty 복원
     변경 내용 : mmap 페이지가 swap 에서 돌아왔으면 사용자 PTE 의 D 비트를 켠다
     변경 이유 : swap 에서 돌아온 mmap 페이지는 커널 주소로 채워 사용자 PTE 의 D 비트가 0 이므로
                 파일과 다르다는 표시를 되살린다, 그래야 munmap · 종료 때 파일에 쓴다
     영향 범위 : handle_mm_fault () 의 매핑 직후 */
  if (p->type == PAGE_MMAP && from_swap)
    pagedir_set_dirty (thread_current ()->pagedir, p->upage, true);

  p->kpage = kpage;
  /* [3-2-2] Eviction : 적재가 끝난 frame 연결
     변경 내용 : p->kpage 를 채운 뒤 frame_install () 로 frame 에 항목을 연결하고 eviction 대상으로 푼다
     변경 이유 : 적재 중인 frame 은 pinned 로 보호하고 끝난 뒤에 풀기 위해 (매뉴얼 4.3.5)
     영향 범위 : handle_mm_fault () 의 성공 경로 */
  frame_install (kpage, p);
  return true;
}

/* [3-2-2] 페이지를 메모리에 올려 고정
   목적 : 페이지를 메모리에 올리고 그 frame 을 pinned 로 한다
   입력 : p - 고정할 SPT 항목
   출력 : 성공하면 true, 적재에 실패하면 false
   참고 : Pintos manual 4.3.5 (pinning)
   주의 : 올린 직후 다른 프로세스의 eviction 이 끼어들어 내보냈을 수 있어 고정에 성공할 때까지 되풀이한다
          Filesys_lock 을 잡지 않은 상태에서만 부른다 */
bool
page_pin (struct page *p)
{
  while (!frame_pin_resident (p))
    if (!handle_mm_fault (p))
      return false;
  return true;
}

/* [3-3-1] 스택 접근 판정
   목적 : SPT 에 없는 주소가 스택을 늘려야 하는 접근인지 판정한다
   입력 : addr - fault 가 난 주소 또는 시스템 콜이 닿는 주소
          esp  - 사용자 스택 포인터
   출력 : 스택 접근이면 true
   참고 : Pintos manual 4.3.3, 조교 슬라이드 11 · 14 · 15 · 16
   주의 : PUSH 는 esp - 4, PUSHA 는 esp - 32 에서 fault 를 낼 수 있어 esp - 32 까지 허용한다, esp - 32 보다 아래는 스택 접근으로 보지 않는다 (pt-grow-bad)
          사용자 주소이고 PHYS_BASE - STACK_LIMIT 이상이어야 한다 */
bool
page_is_stack_access (const void *addr, const void *esp)
{
  return is_user_vaddr (addr)
         && (const uint8_t *) addr >= (const uint8_t *) PHYS_BASE - STACK_LIMIT
         && (const uint8_t *) addr >= (const uint8_t *) esp - 32;
}

/* [3-3-1] 스택 늘리기
   목적 : 주소가 속한 페이지에 PAGE_ANON 항목을 만들어 적재한다
   입력 : addr - 스택 접근으로 판정된 주소
   출력 : 성공하면 true, 항목 생성이나 적재에 실패하면 false
   참고 : Pintos manual 4.3.3, 조교 슬라이드 11 · 14 · 15 · 16
   주의 : fault 가 난 페이지 하나만 붙인다, 그 사이 페이지는 접근할 때 따로 붙는다 (조교 슬라이드 16), 줄이는 처리는 하지 않는다
          적재에 실패해도 항목은 Spt 에 남지만 프로세스가 곧 끝나 page_table_destroy () 가 해제한다
          Filesys_lock 을 잡지 않은 상태에서만 부른다 */
bool
page_grow_stack (void *addr)
{
  struct page *p = page_add_anon (pg_round_down (addr), true);
  if (p == NULL)
    return false;
  return handle_mm_fault (p);
}

/* [3-4-1] mmap 페이지 항목 추가
   목적 : mmap 한 파일의 페이지 하나를 현재 프로세스의 Spt 에 기록한다
   입력 : upage - 사용자 가상 페이지 (페이지 정렬)
          file, ofs - 매핑된 파일과 이 페이지가 시작하는 파일 위치
          read_bytes, zero_bytes - 파일에서 읽을 바이트 수와 0 으로 채울 바이트 수
   출력 : 만든 항목, malloc 실패나 같은 upage 가 이미 있으면 NULL
   참고 : Pintos manual 4.1.7, 4.3.4
   주의 : 항상 쓰기 가능한 페이지로 만든다, kpage 는 NULL 로 두고 처음 접근할 때 handle_mm_fault () 가 채운다 */
struct page *
page_add_mmap (void *upage, struct file *file, off_t ofs,
               uint32_t read_bytes, uint32_t zero_bytes)
{
  struct page *p;

  ASSERT (pg_ofs (upage) == 0);

  p = malloc (sizeof *p);
  if (p == NULL)
    return NULL;

  p->upage = upage;
  p->kpage = NULL;
  p->type = PAGE_MMAP;
  p->writable = true;
  p->file = file;
  p->ofs = ofs;
  p->read_bytes = read_bytes;
  p->zero_bytes = zero_bytes;
  p->swap_slot = PAGE_NO_SLOT;

  if (hash_insert (&thread_current ()->Spt, &p->elem) != NULL)
    {
      free (p);
      return NULL;
    }
  return p;
}

/* [3-4-1] 매핑 하나 해제
   목적 : 매핑의 모든 페이지를 내리고 쓰인 페이지는 파일에 쓴 뒤 매핑 정보를 없앤다
   입력 : m - 현재 스레드의 매핑
   출력 : 없음
   참고 : Pintos manual 4.1.7, 4.3.4
   주의 : 쓰인 페이지만 파일에 쓴다, read_bytes 까지만 쓰고 끝 페이지의 남는 부분은 버린다 (매뉴얼 4.3.4)
          page_pin () 으로 고정한 frame 에 대해서만 frame_free () 를 부른다
          Filesys_lock 은 파일 함수 둘레에서만 잡고 그 안에서 page_pin () 이나 frame 함수를 부르지 않는다
          현재 스레드의 매핑만 다룬다 */
void
page_unmap (struct mmap_file *m)
{
  struct thread *t = thread_current ();
  size_t i;

  for (i = 0; i < m->page_cnt; i++)
    {
      uint8_t *upage = (uint8_t *) m->addr + i * PGSIZE;
      struct page *p = page_find (&t->Spt, upage);

      if (p == NULL)
        continue;

      if (page_pin (p))
        {
          void *kpage;

          if (pagedir_is_dirty (t->pagedir, upage))
            {
              lock_acquire (&Filesys_lock);
              file_write_at (m->file, p->kpage, p->read_bytes, p->ofs);
              lock_release (&Filesys_lock);
            }
          kpage = p->kpage;
          pagedir_clear_page (t->pagedir, upage);
          p->kpage = NULL;
          frame_free (kpage);
        }

      hash_delete (&t->Spt, &p->elem);
      if (p->swap_slot != PAGE_NO_SLOT)
        swap_free (p->swap_slot);
      free (p);
    }

  lock_acquire (&Filesys_lock);
  file_close (m->file);
  lock_release (&Filesys_lock);

  list_remove (&m->elem);
  free (m);
}

/* [3-4-1] 모든 매핑 해제
   목적 : 프로세스가 끝날 때 남은 매핑을 모두 내리고 dirty 페이지를 파일에 쓴다
   입력 : 없음
   출력 : 없음
   참고 : Pintos manual 4.1.7, 4.3.4
   주의 : process_exit () 에서 frame_remove_owner () 보다 먼저, pagedir 가 살아 있고 Filesys_lock 을 잡지 않은 위치에서 부른다 */
void
page_unmap_all (void)
{
  struct thread *t = thread_current ();

  while (!list_empty (&t->Mmap_list))
    page_unmap (list_entry (list_front (&t->Mmap_list), struct mmap_file, elem));
}
