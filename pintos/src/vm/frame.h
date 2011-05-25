#ifndef FRAME_H_
#define FRAME_H_

#include <list.h>
#include <stdint.h>
#include "threads/palloc.h"
#include "devices/rtc.h"

// 프레임 구조체.
struct frame {
  time_t finish;          // 마지막으로 엑세스 된 시간을 잡기 위해 선언.
  void *address;          // 이 프레임에 대한 실제 메모리 주소.
  struct thread *owner;   // 이 프레임을 소유한 쓰레드. NULL인 경우 free.
  void *page;             // 현재의 가상 메모리 주소. owner가 null이면 null.
  bool evictable;         /* 한번 시스템에 의해 선택된 프레임은, 완전히 다른
                             프로세스에 할당될 때 까지 선택되지 않는다.
                             그 여부를 결정하기 위한 bool 값. */
  bool writable;          // 이 프레임에 쓸 수 있는지 없는지를 나타내기 위함. 
  struct list_elem elem;  // 프레임 테이블 내에 list될 list element들.
};
void initFTable();
struct list* retFTable(void);
struct frame* scanFTable(void *);
/* 스왑 아웃 될 프레임을 선택하기 위해 선언된 함수.(LRU 알고리즘)
성공적으로 가장 엑세스된지 오래된 프레임을 찾으면 리턴한다. */
struct frame* getminFTable();
struct frame* insertFTable(void *, void *, bool);
struct frame* getzeroFTable(void *, bool);
struct frame* getvictimFTable(void);
void removeFTable(struct frame*);
void raFTable(struct thread *);
void ref_accessed_bitFTable(void);

#endif
