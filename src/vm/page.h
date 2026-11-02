#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include <stdint.h>
#include "filesys/off_t.h"

struct file;

/* [3-1-2] Supplemental Page Table : 페이지 종류
   목적 : 사용자 가상 페이지의 내용이 어디서 오는지 구분한다
   입력 : 없음
   출력 : PAGE_BIN  - 실행 파일 세그먼트
          PAGE_ANON - 파일 원본이 없는 페이지 (스택)
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : 이 단계에서는 이 두 종류만 쓴다 */
enum page_type
  {
    PAGE_BIN,     /* 실행 파일 세그먼트 : file 의 ofs 에서 read_bytes 읽고 zero_bytes 를 0 으로 */
    PAGE_ANON     /* 파일 원본이 없는 페이지 : 스택 */
  };

/* [3-1-2] Supplemental Page Table : 페이지 항목
   목적 : 사용자 가상 페이지 하나가 어디서 오고 어떤 속성인지 기록한다
   입력 : page_add_file (), page_add_anon () 이 채운다
   출력 : 프로세스별 Spt (hash) 의 원소
   참고 : Pintos manual 4.1.4, 4.2, 4.3.2, 조교 슬라이드 8 · 17
   주의 : upage 는 페이지 정렬된 주소이고 해시 키이다
          kpage 는 handle_mm_fault () 가 frame 을 얻어 매핑한 뒤 채운다, 스택 첫 페이지만 setup_stack () 이 채운다 */
struct page
  {
    void *upage;              /* 사용자 가상 페이지 (페이지 정렬), 해시 키 */
    void *kpage;              /* 실린 frame 의 커널 가상 주소, 메모리에 없으면 NULL */
    enum page_type type;
    bool writable;
    struct file *file;        /* PAGE_BIN : 실행 파일 */
    off_t ofs;                /* PAGE_BIN : 이 페이지가 시작하는 파일 위치 */
    uint32_t read_bytes;      /* PAGE_BIN : 파일에서 읽을 바이트 수 */
    uint32_t zero_bytes;      /* PAGE_BIN : 0 으로 채울 바이트 수 */
    struct hash_elem elem;    /* Spt 의 원소 */
  };

bool page_table_init (struct hash *spt);
void page_table_destroy (struct hash *spt);
struct page *page_add_file (void *upage, struct file *file, off_t ofs,
                            uint32_t read_bytes, uint32_t zero_bytes, bool writable);
struct page *page_add_anon (void *upage, bool writable);
struct page *page_find (struct hash *spt, const void *vaddr);
bool handle_mm_fault (struct page *p);

#endif /* vm/page.h */
