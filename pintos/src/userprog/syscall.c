#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"

/* This is a skeleton system call handler */

static void syscall_handler (struct intr_frame *);

// Project 3을 위한 함수
// User Memory에 access하기 위한 함수
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static int get_user32 (const uint32_t *uaddr);
static bool check_phys_base(const void *ptr);
static bool check_buf_size(const void *buf, const unsigned size);
static uint32_t extract_arg(const uint32_t *esp);

// System Call 구현한 것들
static void exit(uint32_t *esp);
static int write(uint32_t *esp);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
//  printf ("system call!\n");
//  thread_exit ();
	
	int sys_num = *(int *)f->esp;
	uint32_t *esp = (uint32_t *)f->esp;

	switch (sys_num) {
		case SYS_EXIT:
			exit(esp);
			break;
		case SYS_WRITE:
			f->eax = write(esp);
			break;

		default:
			printf("system call!\n");
			thread_exit();
	}
}

/* Reads a byte at user virtual address UADDR.
	 UADDR must be below PHYS_BASE.
	 Returns the byte value if successful, -1 if a segfault
	 occurred. */
static int
get_user (const uint8_t *uaddr)
{
	int result;
	asm ("movl $1f, %0; movzbl %1, %0; 1:"
			: "=&a" (result) : "m" (*uaddr));
	return result;
}

/* Writes BYTE to user address UDST.
	 UDST must be below PHYS_BASE.
	 Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
	int error_code;
	asm ("movl $1f, %0; movb %b2, %1; 1:"
			: "=&a" (error_code), "=m" (*udst) : "r" (byte));
	return error_code != -1;
}

/* get_user()가 uint32_t를 인자로 받도록 수정 
   32bit 단위로 데이터를 처리하므로 movzbl -> movl로 수정
 */
static int
get_user32 (const uint32_t *uaddr)
{
	int result;
	asm ("movl $1f, %0; movl %1, %0; 1:"
			: "=&a" (result) : "m" (*uaddr));
	return result;
}

/* 포인터가 PHYS_BASE보다 작은지 검사한다. 만약 크다면 스레드 종료 */
static bool check_phys_base(const void *ptr)
{
	if (ptr >= PHYS_BASE)
		thread_exit();

	return true;
}

/* buf가 가리키는 문자열이 user memory 영역에 있는지 검사한다. */
static bool check_buf_size(const void *buf, const unsigned size)
{
	int i = 0;
	uint8_t *cur;

	for (cur = (uint8_t*)buf + i*PGSIZE ; cur < (uint8_t*)(buf + size-1)
				; cur = (uint8_t*)buf + (++i)*PGSIZE)
	{
		if (get_user(cur) == -1)  //segfault가 발생한 경우
			thread_exit();
	}
}

static uint32_t extract_arg(const uint32_t *esp)
{
	uint32_t value;

	check_phys_base(esp);
	if ( (value = get_user32(esp)) == -1) //segfault가 발생했다면
		thread_exit();

	return value;
}


/************ System Call 구현 **************/

static void exit(uint32_t *esp)
{
	int status = (int)extract_arg(++esp);

	thread_current()->exit_status = status;
	thread_exit();
}

static int write(uint32_t *esp)
{

	int fd = (int)extract_arg(++esp);
	if (fd == 0)
		return 0;

	void *buf = (void *)extract_arg(++esp);
	check_phys_base(buf);

	unsigned size = (unsigned) extract_arg(++esp);
	check_buf_size(buf, size);

	if (fd == 1) {  //stdout에 write하는 경우
		putbuf(buf, size);
		return (int)size;
	}
	else {  //file에 write하는 경우
		/*
		struct file* target_file = find_file(fd, &thread_current()->open_files);

		if (target_file != NULL) {
			int written_len = file_write(target_file, buf, size);
			return written_len;
		}
		*/
	}

	return 0;

}



