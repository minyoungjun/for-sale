#ifndef PAGE_H_
#define PAGE_H_

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "vm/swap.h"
#include "vm/frame.h"


enum flags_spt {
  NORMAL = 000,
  FORCE  = 001 
};

enum page_location {
  PAG_SWAP, // 페이지가 스왑 파티션 내에 있는 경우.
  PAG_FILE, // 페이지가 메모리에 할당된 파일인 경우.
  PAG_EXEC  // 페이지가 실행 파일로부터 읽힌 경우.
};

// 페이지 구조체.
struct page {
    void *location;          // 페이지가 어디에 있는지에 관한 정보. 스왑 디스크 슬롯이나 파일을 가리킬 수 있다.
    enum page_location pg;   // 페이지 로케이션 정보.
    bool writable;           // 읽기 전용인지, 쓸 수 있는지 결정하는 변수.
    uint32_t ofs;            // 읽기를 시작하기 위한 파일 내의 오프셋.
    uint32_t read_b;         // 파일로부터 읽어야 할 바이트 수.
    uint32_t zero_b;         // 페이지의 나머지는 0으로 채운다.
    uint8_t *page;           // 이 페이지가 시작되는 위치의 가상 메모리 주소.
    struct list_elem elem;   /* supplemental page table 내의 list element들.
                                페이지 폴트를 처리하기 위해 필요하다. */
};

void loadpageST(struct page *);
void freepageST(struct page *);
struct page *scanST(uint8_t *);
struct page *scanST_ext(uint8_t *, struct thread *t);
void addframeST(struct frame *, struct slot *);
void destroyST(void);

#endif
