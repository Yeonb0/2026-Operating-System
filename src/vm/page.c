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

  if (p->type == PAGE_BIN)
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
