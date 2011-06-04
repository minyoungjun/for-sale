#include "filesys/buf_cache.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "devices/disk.h"
#include "filesys/inode.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "lib/string.h"
#include <string.h>
#include <stdio.h>
#include <round.h>

static struct list buffer_cache;
static uint32_t entries;  /* buffer_cache의 entry수 */
struct lock bfc_lock;

int64_t bfc_tick;

//victim을 고르는 함수 - clock algorithm 사용
static struct bfc_entry * select_victim (void);
static struct list_elem *cur_victim; //선정된 victim을 가리킴

void init_buffer_cache(void)
{
	list_init(&buffer_cache);
	lock_init(&bfc_lock);
  entries = 0;
  cur_victim = NULL;
	bfc_tick = 0;
}

void check_buffer_cache(void)
{
	enum intr_level old_level;
	old_level = intr_disable();

	bfc_tick++;
	if (bfc_tick == BFC_TICK_FEQ) {
		buffer_cache_write_behind_all();
		bfc_tick = 0;
	}

	intr_set_level (old_level);
}
  
static struct bfc_entry *select_victim() 
{
  struct bfc_entry *cur;
  bool cond;
  
  while (true)
  {
    cond = false;
    if (cur_victim == NULL)
      cur_victim = list_begin (&buffer_cache);
          
    cur = list_entry (cur_victim, struct bfc_entry, elem);
        
    //lock_acquire(&cur->lock);
    cond = (cur->num_of_accessor == 0 &&  cur->evictable);
    //lock_release (&cur->lock);
      
    if (cond) {
			// cur이 accessed라면 우선 false로 만들고 다른 entry를 찾아본다.
      if (cur->accessed) 
        cur->accessed = false;
      else //accessed가 아니라면 cur을 victim으로 삼고 while문을 빠져나간다.
        break;
    }
        
    if (list_next(cur_victim) == list_end(&buffer_cache))
      cur_victim = list_begin(&buffer_cache);
    else
      cur_victim = list_next(cur_victim);
  }
      
  return cur;
}

/* entry가 새로 필요할 때 entry를 만들어준다.
	 entry를 만들고 data 공간을 할당받고 disk I/O를 통해 data를 읽어온다.
	 만약 buffer_cache가 꽉찼으면 victim을 write-behind-all하고 그 부분을 새 entry로 쓴다.*/
/* 이 함수는 look_up()의 결과가 NULL일때 호출된다. */
struct bfc_entry *buffer_cache_get_entry (struct inode *inode, off_t offset)
{
  //ASSERT (offset % DISK_SECTOR_SIZE == 0);
    
  struct bfc_entry *bfce;
  disk_sector_t sector_idx = byte_to_sector(inode, offset);

	if ((int)sector_idx == -1)
		return NULL;
    
  if (entries < BUF_CACHE_SIZE)
  {
    bfce = (struct bfc_entry *) malloc(sizeof(struct bfc_entry));
    if (bfce == NULL) {
      printf ("ERROR: fail to allocate memory for buffer cache entry\n");
      return NULL;
    }
          
    bfce->evictable = true;
    entries++;
    list_push_front (&buffer_cache, &bfce->elem);
        
		//실제 data를 저장할 공간 할당 (SECTOR 사이즈 만큼)
    bfce->addr = malloc(DISK_SECTOR_SIZE);
    if(bfce == NULL) {
      printf ("ERROR: fail to allocate memory for buffer cache data\n");
      free (bfce);
      return NULL;
    }
          
		// 그 섹터의 data를 disk에서 읽어옴
    disk_read (filesys_disk, sector_idx, bfce->addr);
   
    bfce->inode = inode;
    bfce->offset = offset;
    bfce->dirty = false;
    bfce->accessed = false;
        
    bfce->num_of_accessor = 0;
    lock_init (&bfce->lock);

#ifdef BFC_DEBUG
		printf("DISK READ for making new bfc_entry: inode=%x, ofs=%d\n", 
					  bfce->inode, bfce->offset);
#endif
  }
  else
  { 
		// buffer_cache가 꽉차있으므로 victim 선정
    bfce = select_victim ();
       
    // 선정된 entry가 dirty일 경우 disk에 write-behind
    if (bfce->dirty)
			buffer_cache_write_behind(bfce);
        
		lock_acquire(&bfce->lock);
    disk_read (filesys_disk, sector_idx, bfce->addr);
    bfce->inode = inode;
    bfce->offset = offset;
    bfce->num_of_accessor = 0;
		bfce->dirty = false;
		bfce->accessed = false;
		lock_release(&bfce->lock);
#ifdef BFC_DEBUG
		printf("DISK READ for overwriting a victim bfc_entry: inode=%x, ofs=%d\n",
				   bfce->inode, bfce->offset);
#endif
  }
      
  return bfce;
}

/* buffer_cache를 순회하며 inode, offset이 같은 entry를 찾아 리턴
   만약 원하는 entry를 찾을 수 없으면 NULL 리턴*/
struct bfc_entry *buffer_cache_look_up (struct inode *inode, off_t offset)
{
  struct list_elem *e;
  struct bfc_entry *cur;
	disk_sector_t sector_idx;
 
	lock_acquire(&bfc_lock);

  for (e = list_begin(&buffer_cache) ; e != list_end(&buffer_cache) ; 
			 e = list_next(e)) {
    cur = list_entry(e, struct bfc_entry, elem);
    if (cur->inode == inode && cur->offset == offset) {
			/*if (cur->dirty)
				buffer_cache_write_behind(cur);
			
			lock_acquire(&cur->lock);
			sector_idx = byte_to_sector(inode, offset);
      disk_read(filesys_disk, sector_idx, cur->addr);
			lock_release(&cur->lock);*/

			lock_release(&bfc_lock);
#ifdef BFC_DEBUG
			printf("LOOK UP Success: inode=%x, ofs=%d\n", cur->inode, cur->offset);
#endif
      return cur;
    }
  }

	lock_release(&bfc_lock);
#ifdef BFC_DEBUG
	printf("LOOK UP Fail: inode=%x, ofs=%d\n", inode, offset);
#endif
  return NULL;
}

void buffer_cache_write_behind(struct bfc_entry *bfce)
{
	lock_acquire(&bfce->lock);
	disk_sector_t sector_idx = byte_to_sector(bfce->inode, bfce->offset);
  disk_write(filesys_disk, sector_idx, bfce->addr);
	bfce->dirty = false;
  bfce->accessed = false;
	lock_release(&bfce->lock);

#ifdef BFC_DEBUG
	printf("Write-Behind: inode=%x, ofs=%d\n", bfce->inode, bfce->offset);
#endif
}
  
/* buffer_cache를 순회하며 dirty인 entry를 모두 write-behind */
void buffer_cache_write_behind_all()
{
  struct list_elem *e;
  struct bfc_entry *cur;
	int cnt = 0;
  disk_sector_t sector_idx;

#ifdef BFC_DEBUG
	printf("Write-Behind-All START\n");
#endif
    
	lock_acquire(&bfc_lock);

  for (e = list_begin(&buffer_cache) ; e != list_end(&buffer_cache) ; 
			 e = list_next(e)) {
    cur = list_entry(e, struct bfc_entry, elem);
		cur->accessed = false;
    if (cur->dirty) {
			buffer_cache_write_behind(cur);
			cnt++;
		}
  }

	lock_release(&bfc_lock);

#ifdef BFC_DEBUG
	printf("Write-Behind-All END: count=%d\n", cnt);
#endif
}

/* buffer_cache를 순회하며 dirty인 entry를 모두 write-behind */
void buffer_cache_write_behind_inode(struct inode *inode)
{
  struct list_elem *e;
  struct bfc_entry *cur;
	int cnt = 0;
  disk_sector_t sector_idx;

#ifdef BFC_DEBUG
	printf("Write-Behind-Inode START: inode=%x\n", inode);
#endif

	lock_acquire(&bfc_lock);

  for (e = list_begin(&buffer_cache) ; e != list_end(&buffer_cache) ; 
			 e = list_next(e)) {
    cur = list_entry(e, struct bfc_entry, elem);
		if (cur->inode == inode && cur->dirty) {
			cur->accessed = false;
		  buffer_cache_write_behind(cur);
			cnt++;
		}
  }

	lock_release(&bfc_lock);

#ifdef BFC_DEBUG
	printf("Write-Behind-Inode END: inode=%x, count=%d\n", inode, cnt);
#endif
}

/* buffer_cache의 모든 entry와 entry->addr을 모두 free.
	 dirty 인지 아닌지는 검사 안하므로 이 함수를 호출하기 전에는
	 반드시 write_behind_all()을 호출하여 write-behind를 완료해야한다. */
void buffer_cache_flush ()
{
  struct list_elem *e;
  struct bfc_entry *cur;
  
	lock_acquire(&bfc_lock);

  while (!list_empty(&buffer_cache))
  {
    e = list_pop_front(&buffer_cache);
    cur = list_entry(e, struct bfc_entry, elem);
    free(cur->addr);
    free(cur);
  }

	lock_release(&bfc_lock);
#ifdef BFC_DEBUG
	printf("BufferCache FLUSHED\n");
#endif
}

/* 주로 inode_write_at()에 의해 호출된다. */
uint32_t buffer_cache_write(struct inode *inode, off_t offset,
														const void *buffer_, int size)
{
	struct bfc_entry *bfce;
	const uint8_t *buffer = buffer_;
	int sector_ofs = offset % DISK_SECTOR_SIZE;

	//먼저 buffer_cache 내에 원하는 entry가 이미 있는지 검사
	bfce = buffer_cache_look_up(inode, offset);

	//만약 새롭게 entry를 할당해야할 경우
	if (bfce == NULL) {
		bfce = buffer_cache_get_entry(inode, offset);
		if (bfce == NULL) //새 entry할당에 실패했을 경우 스레드 종료
			thread_exit();
	}

	lock_acquire(&bfce->lock);
	bfce->num_of_accessor++;

	// 이제 bfce->addr에 buffer의 데이터를 write한다.
	memcpy(bfce->addr + sector_ofs, buffer, size);
	bfce->dirty = true;
	bfce->accessed = true;

	bfce->num_of_accessor--;
	lock_release(&bfce->lock);
#ifdef BFC_DEBUG
	printf("MEMCPY for write: inode=%x, ofs=%d\n", inode, offset);
#endif
	return size;
}
	
uint32_t buffer_cache_read(struct inode *inode, off_t offset,
													 void *buffer_, int size) 
{
	struct bfc_entry *bfce;
	const uint8_t *buffer = buffer_;
	int sector_ofs = offset % DISK_SECTOR_SIZE;

	bfce = buffer_cache_look_up(inode, offset);

	if (bfce == NULL) {
		bfce = buffer_cache_get_entry(inode, offset);
		if (bfce == NULL) 
			thread_exit();
	}	

	lock_acquire(&bfce->lock);
	// buffer_는 오프셋까지 고려된 위치, size는 inode_read_at에서 계산된 chunk size.
	bfce->num_of_accessor++;
	memcpy(buffer, bfce->addr + sector_ofs, size);
	bfce->accessed = true;
	bfce->num_of_accessor--;
	lock_release(&bfce->lock);

#ifdef BFC_DEBUG
	printf("MEMCPY for read: inode=%x, ofs=%d\n", inode, offset);
#endif

	/* read-ahead 정책을 위해 DISK_SECTOR_SIZE만큼 offset을 증가시키고
	   해당 부분의 버퍼 캐시를 찾는다. */
	offset += DISK_SECTOR_SIZE;
	bfce = buffer_cache_look_up(inode, offset);

	if (bfce == NULL) 
		bfce = buffer_cache_get_entry(inode, offset);
#ifdef BFC_DEBUG
	printf("READ-AHEAD: inode=%x, ofs=%d\n", inode, offset);
#endif

	return size;
}
