#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

/* [3-2-1] Swap Table : swap 장치의 슬롯 관리
   목적 : BLOCK_SWAP 장치를 페이지 크기 슬롯으로 나누고 비트맵으로 사용 중인 슬롯과 빈 슬롯을 관리한다
   입력 : swap_out()  - 내보낼 페이지의 커널 가상 주소
          swap_in()   - 슬롯 번호, 읽어 들일 페이지의 커널 가상 주소
          swap_free() - 해제할 슬롯 번호
   출력 : swap_out() 은 페이지를 기록한 슬롯 번호
   참고 : Pintos manual 4.1.5, 4.1.6, 조교 슬라이드 13
   주의 : 이 단계에서는 아무도 부르지 않는다, 사용은 3-2-2 에서 한다
          swap 장치가 없는 실행에서도 부팅은 되며 그때는 슬롯이 0개다 */
void swap_init (void);
size_t swap_out (const void *kpage);
void swap_in (size_t slot, void *kpage);
void swap_free (size_t slot);

#endif /* vm/swap.h */
