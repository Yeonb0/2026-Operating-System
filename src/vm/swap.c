#include "vm/swap.h"
#include <bitmap.h>
#include <debug.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* [3-2-1] Swap Table : 슬롯 하나의 섹터 수
   목적 : 페이지 하나가 차지하는 연속된 섹터 수를 정한다
   입력 : 없음
   출력 : 8
   참고 : Pintos manual 4.1.5, 4.1.6, 조교 슬라이드 13
   주의 : 페이지 4096 바이트 = 512 바이트 섹터 8개, 슬롯 하나가 연속된 섹터 8개 */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

/* [3-2-1] Swap Table : swap 장치, 슬롯 사용 비트맵, 비트맵을 보호하는 락
   목적 : swap_init () 이 채우고 나머지 함수가 공유하는 전역 상태
   입력 : 없음
   출력 : 없음
   참고 : Pintos manual 4.1.5, 4.1.6, 조교 슬라이드 13
   주의 : Swap_bitmap 접근은 모두 Swap_lock 을 잡은 상태에서 한다
          block_read () 와 block_write () 는 Swap_lock 을 놓은 상태에서 부른다 */
static struct block *Swap_block;
static struct bitmap *Swap_bitmap;
static struct lock Swap_lock;

/* [3-2-1] Swap Table 초기화
   목적 : swap 장치를 얻어 슬롯 수만큼의 비트맵을 만든다
   입력 : 없음
   출력 : 없음
   참고 : Pintos manual 4.1.5, 4.1.6, 조교 슬라이드 13
   주의 : locate_block_devices () 뒤에 불러야 block_get_role (BLOCK_SWAP) 으로 장치를 얻는다
          swap 장치가 없으면 슬롯 0개로 만들고, 그때 swap_out () 이 불리면 panic 한다 */
void
swap_init (void)
{
  size_t slot_cnt;

  lock_init (&Swap_lock);
  Swap_block = block_get_role (BLOCK_SWAP);
  slot_cnt = Swap_block == NULL ? 0 : block_size (Swap_block) / SECTORS_PER_PAGE;
  Swap_bitmap = bitmap_create (slot_cnt);
  if (Swap_bitmap == NULL)
    PANIC ("swap_init: cannot create swap bitmap");
}

/* [3-2-1] 페이지를 swap 장치에 기록
   목적 : 빈 슬롯 하나를 잡아 페이지 내용을 섹터 8개에 나누어 쓴다
   입력 : kpage - 내보낼 페이지의 커널 가상 주소
   출력 : 페이지를 기록한 슬롯 번호
   참고 : Pintos manual 4.1.5, 4.1.6, 조교 슬라이드 13
   주의 : 슬롯이 없으면 panic 한다 (매뉴얼 4.1.5), 디스크 쓰기 중에는 Swap_lock 을 잡지 않는다 */
size_t
swap_out (const void *kpage)
{
  size_t slot;
  size_t i;

  lock_acquire (&Swap_lock);
  slot = bitmap_scan_and_flip (Swap_bitmap, 0, 1, false);
  lock_release (&Swap_lock);

  if (slot == BITMAP_ERROR)
    PANIC ("swap_out: swap is full");

  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_write (Swap_block, slot * SECTORS_PER_PAGE + i,
                 (const uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);
  return slot;
}

/* [3-2-1] 슬롯의 내용을 페이지로 읽기
   목적 : 슬롯의 섹터 8개를 읽어 페이지를 채우고 슬롯을 비운다
   입력 : slot  - swap_out () 이 돌려준 슬롯 번호
          kpage - 내용을 채울 페이지의 커널 가상 주소
   출력 : 없음
   참고 : Pintos manual 4.1.5, 4.1.6, 조교 슬라이드 13
   주의 : 읽어 들인 슬롯은 바로 해제한다 (매뉴얼 4.1.6)
          디스크 읽기 중에는 Swap_lock 을 잡지 않는다 */
void
swap_in (size_t slot, void *kpage)
{
  size_t i;

  lock_acquire (&Swap_lock);
  ASSERT (bitmap_test (Swap_bitmap, slot));
  lock_release (&Swap_lock);

  for (i = 0; i < SECTORS_PER_PAGE; i++)
    block_read (Swap_block, slot * SECTORS_PER_PAGE + i,
                (uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);

  lock_acquire (&Swap_lock);
  bitmap_reset (Swap_bitmap, slot);
  lock_release (&Swap_lock);
}

/* [3-2-1] 슬롯 해제
   목적 : 읽지 않고 슬롯만 비운다
   입력 : slot - 해제할 슬롯 번호
   출력 : 없음
   참고 : Pintos manual 4.1.5, 4.1.6, 조교 슬라이드 13
   주의 : 프로세스가 끝날 때 swap 에 남은 페이지의 슬롯을 읽지 않고 해제하는 용도다 */
void
swap_free (size_t slot)
{
  lock_acquire (&Swap_lock);
  ASSERT (bitmap_test (Swap_bitmap, slot));
  bitmap_reset (Swap_bitmap, slot);
  lock_release (&Swap_lock);
}
