#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/input.h"

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
static int read(uint32_t *esp);
static int write(uint32_t *esp);
static bool create(uint32_t *esp);
static int open(uint32_t *esp);
static bool remove(uint32_t *esp);
static int filesize(uint32_t *esp);
static void close(uint32_t *esp);
static struct file *find_open_file (struct thread *cur_thread, const int fd);
static bool remove_open_file (struct thread *cur_thread, const int fd);


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
		case SYS_READ:
			f->eax = read(esp);
			break;
		case SYS_WRITE:
			f->eax = write(esp);
			break;
		case SYS_CREATE:
			f->eax = create(esp);
			break;
		case SYS_REMOVE:
			f->eax = remove(esp);
			break;
		case SYS_OPEN:
			f->eax = open(esp);
			break;
		case SYS_FILESIZE:
			f->eax = filesize(esp);
			break;
		case SYS_CLOSE:
			close(esp);
			break;
		default:
			printf("system call! : syscall num = %d\n", sys_num);
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

/* esp가 기리키는 위치의 값을 추출한다. */
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

static int read(uint32_t *esp)
{
	int fd = (int)extract_arg(++esp);
	if (fd == 1)  //stdout을 read할 수는 없으므로 -1 리턴
		return -1;
	
	void *buf = (void *)extract_arg(++esp);
	check_phys_base(buf);

	unsigned size = (unsigned)extract_arg(++esp);
	check_buf_size(buf, size);

	if (fd == 0) { //stdin을 read하는 경우
		unsigned len = 0;
		while (len < size) {
			*(uint8_t *)buf = input_getc();
			buf = (uint8_t *)buf + 1; //buf를 증가시킬땐 byte(8bit) 단위로 증가
			len++;
		}
		return len;
	}
	else
	{
		struct file *file = find_open_file(thread_current(), fd);
		if (file == NULL)
			return -1;

		return (int)file_read(file, buf, size);
	}

	return -1;
}

static int write(uint32_t *esp)
{

	int fd = (int)extract_arg(++esp);
	if (fd == 0) //stdin에 write할 수는 없으므로 -1 리턴
		return -1;

	void *buf = (void *)extract_arg(++esp);
	check_phys_base(buf);

	unsigned size = (unsigned) extract_arg(++esp);
	check_buf_size(buf, size);

	if (fd == 1) {  //stdout에 write하는 경우
		putbuf(buf, size);
		return (int)size;
	}
	else {  //file에 write하는 경우
		struct file *file = find_open_file(thread_current(), fd);

		if (file == NULL)
			return -1;
		
		return file_write(file, buf, size);
	}

	return -1;

}

static bool create(uint32_t *esp)
{
	char *filename = (char *)extract_arg(++esp);
	check_phys_base(filename);
	check_buf_size(filename, sizeof(filename));

	unsigned size = (unsigned)extract_arg(++esp);
	
	/* filesys_create()는 해당 파일에 대한 inode를 생성하고
		 현 directory에 inode를 추가한다. */
	return filesys_create(filename, size);
}

/* 한 thread가 한 file을 여러번 open할 수도 있다.
		 그러나 각 open된 파일은 별도의 fd를 갖도록 해야한다.
		 같은 file이라도 fd가 각각 다르므로 독립적으로 취급된다.
		 즉, 이미 열려있는 파일인지 체크할 필요가 없다. */
static int open(uint32_t *esp)
{
	char *filename = (char *)extract_arg(++esp);
	check_phys_base(filename);
	check_buf_size(filename, sizeof(filename));

	struct file *file = filesys_open(filename);
	if (file == NULL)
		return -1;

	// 현 스레드의 Open File List에 추가
	struct thread *cur_thread = thread_current();
	struct open_file *of = malloc(sizeof(struct open_file));
	of->fd = cur_thread->next_fd++;
	of->file = file;
	list_push_front(&cur_thread->open_files, &of->elem);

	return of->fd;
}

static bool remove(uint32_t *esp)
{
	char *filename = (char *)extract_arg(++esp);
	check_phys_base(filename);
	check_buf_size(filename, sizeof(filename));

	return filesys_remove(filename);
}

static int filesize(uint32_t *esp)
{
	int fd = (int)extract_arg(++esp);
	if (fd == 0 || fd == 1)
		thread_exit();

	struct file *file = find_open_file (thread_current(), fd);
	if (file == NULL)
		return -1;

	return (int)file_length(file);
}

static void close(uint32_t *esp)
{
	int fd = (int)extract_arg(++esp);
	if (fd == 0 || fd == 1)
		thread_exit();

	struct file *file = find_open_file (thread_current(), fd);
	if (file == NULL)
		printf("That file isn't opened!\n");

	file_close(file);
	if ( !remove_open_file(thread_current(), fd) )
		printf("Fail to remove a file in Open-File-List!\n");
}

/* cur_thread의 open_file_list에서 fd 값을 가지는 파일을 찾아준다. */
static struct file *find_open_file (struct thread *cur_thread, const int fd)
{
	struct list_elem *cur;
	struct list *open_files = &cur_thread->open_files; 

	for (cur = list_begin(open_files) ; cur != list_end(open_files) ;
			 cur = list_next(cur))
	{
		struct open_file *of = list_entry(cur, struct open_file, elem);
		if (fd == of->fd) {
			return of->file;
		}
	}

	return NULL;
}

/* cur_thread의 open_file_list에서 fd에 해당하는 파일을 제거한다. */
static bool remove_open_file (struct thread *cur_thread, const int fd)
{
	struct list_elem *cur;
	struct list *open_files = &cur_thread->open_files;

	for (cur = list_begin(open_files) ; cur != list_end(open_files) ;
			 cur = list_next(cur))
	{
		struct open_file *of = list_entry(cur, struct open_file, elem);
		if (fd == of->fd) {
			list_remove(cur);
			free(of);
			return true;
		}
	}

	return false;
}
