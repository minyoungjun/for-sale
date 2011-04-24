#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);


// Project3를 위한 함수
struct parameter {
	char *str;
	struct list_elem elem;
};

struct argaddress {
	char *arg;
	struct list_elem elem;
};

bool arg_parse (const char *file_name_, struct list *argv, int *argc);
void *arg_push (struct list *argv, const int argc);


#endif /* userprog/process.h  */
