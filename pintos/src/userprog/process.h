#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);


// Project3를 위한 함수
// argv[] list에 인자를 넣기 위한 wrapper 구조체
struct parameter {
	char *str;
	struct list_elem elem;
};

// argv[]의 각 요소의 주소를 저장할 wrapper 구조체
struct argaddress {
	char *arg;
	struct list_elem elem;
};

//exec() 호출에 필요한 구조체
struct exec_arg {
	char *filename;
	struct semaphore sema;
	bool success;
};

bool arg_parse (const char *file_name_, struct list *argv, int *argc);
void *arg_push (struct list *argv, const int argc);

//child process를 위한 함수
struct child_process *find_child (tid_t pid, struct thread *t);


#endif /* userprog/process.h  */
