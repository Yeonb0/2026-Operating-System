#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <list.h>
#include <stdbool.h>
#include <stdint.h>
#include "filesys/off_t.h"

struct file;

/* [3-2-2] Eviction : swap 슬롯 없음 표시
   목적 : 페이지가 swap 에 없음을 나타내는 swap_slot 값
   입력 : 없음
   출력 : SIZE_MAX
   참고 : Pintos manual 4.1.5, 4.1.6, 조교 슬라이드 13
   주의 : 실제 슬롯 번호는 이 값보다 항상 작다 */
#define PAGE_NO_SLOT SIZE_MAX

/* [3-3-1] Stack Growth : 스택 최대 크기
   목적 : 스택이 자랄 수 있는 한계를 PHYS_BASE 아래로 정한다
   입력 : 없음
   출력 : 8MB
   참고 : Pintos manual 4.3.3, 조교 슬라이드 11 · 14 · 15 · 16
   주의 : PHYS_BASE - STACK_LIMIT 보다 아래 주소는 스택 접근으로 보지 않는다 */
#define STACK_LIMIT (8 * 1024 * 1024)

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
    /* [3-4-1] Memory Mapped Files : mmap 페이지 종류 추가
       변경 내용 : enum page_type 에 PAGE_MMAP 을 추가한다
       변경 이유 : mmap 으로 매핑한 파일 페이지를 실행 파일 페이지와 구분하기 위해 (매뉴얼 4.3.4)
       영향 범위 : PAGE_BIN 과 PAGE_ANON 사이에 끼워 PAGE_ANON 의 값이 1 에서 2 로 바뀐다, 열거값 숫자에 의존하는 코드는 없다 */
    PAGE_MMAP,    /* mmap 페이지 : file 의 ofs 에서 read_bytes 읽고 나머지 0, 원본은 매핑된 파일 */
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
    /* [3-2-2] Eviction : swap 슬롯 필드
       변경 내용 : swap 에 내보낸 페이지의 슬롯 번호를 기록하는 swap_slot 을 추가한다
       변경 이유 : fault 때 swap 에서 다시 읽어 오기 위해 (매뉴얼 4.1.6)
       영향 범위 : struct page 의 크기, page_add_file () 과 page_add_anon () 이 PAGE_NO_SLOT 으로 시작한다 */
    size_t swap_slot;         /* swap 에 있으면 슬롯 번호, 없으면 PAGE_NO_SLOT */
    struct hash_elem elem;    /* Spt 의 원소 */
  };

bool page_table_init (struct hash *spt);
void page_table_destroy (struct hash *spt);
struct page *page_add_file (void *upage, struct file *file, off_t ofs,
                            uint32_t read_bytes, uint32_t zero_bytes, bool writable);
struct page *page_add_anon (void *upage, bool writable);
struct page *page_find (struct hash *spt, const void *vaddr);
bool handle_mm_fault (struct page *p);
bool page_pin (struct page *p);
bool page_is_stack_access (const void *addr, const void *esp);
bool page_grow_stack (void *addr);

/* [3-4-1] Memory Mapped Files : 매핑 하나의 정보
   목적 : mmap () 한 번으로 만든 매핑의 번호, 파일, 주소 범위를 기록한다
   입력 : mmap () 이 채운다
   출력 : 프로세스별 Mmap_list 의 원소
   참고 : Pintos manual 4.1.7, 4.3.4
   주의 : file 은 file_reopen () 으로 얻은 독립 참조라 close · remove 뒤에도 매핑이 유효하다
          addr 은 페이지 정렬된 주소이고 page_cnt 는 실제로 만든 페이지 수다 */
struct mmap_file
  {
    int mapid;                /* 매핑 번호, 프로세스 안에서 유일 */
    struct file *file;        /* file_reopen () 으로 얻은 독립 참조 */
    void *addr;               /* 매핑 시작 주소 (페이지 정렬) */
    size_t page_cnt;          /* 매핑한 페이지 수 */
    struct list_elem elem;    /* Mmap_list 의 원소 */
  };

struct page *page_add_mmap (void *upage, struct file *file, off_t ofs, uint32_t read_bytes, uint32_t zero_bytes);
void page_unmap (struct mmap_file *m);
void page_unmap_all (void);

#endif /* vm/page.h */
