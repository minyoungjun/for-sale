#ifndef BUF_CACHE_H
#define BUF_CACHE_H

#include "filesys/inode.h"
#include "filesys/off_t.h"
#include "threads/synch.h"
#include <list.h>
#include <stdbool.h>

#define BUF_CACHE_SIZE 64
#define BFC_TICK_FEQ 20

struct bfc_entry
{
	struct inode *inode;  //이 entry를 소유하고 있는 inode
	off_t offset;
	uint8_t bytes;        //이 entry가 포함하고 있는 데이터의 byte수
	void *addr;						//이 entry의 실제 데이터가 위치한 곳의 주소
	uint32_t num_of_accessor;  //이 entry에 접근하고있는 프로세스의 수
	
	bool dirty;
	bool accessed;        //이 entry가 read/write되었었는지
	bool evictable;
	struct lock lock;
	struct list_elem elem;
};

uint32_t buffer_cache_write (struct inode *, off_t, const void *, int);
uint32_t buffer_cache_read (struct inode *, off_t, void *, int);

struct bfc_entry *buffer_cache_get_entry (struct inode *, off_t);
struct bfc_entry *buffer_cache_look_up (struct inode *, off_t);
void init_buffer_cache (void);
void buffer_cache_write_behind (struct bfc_entry *);
void buffer_cache_write_behind_all (void);
void buffer_cache_write_behind_inode (struct inode *);
void buffer_cache_flush (void);
void check_buffer_cache (void);


#endif
