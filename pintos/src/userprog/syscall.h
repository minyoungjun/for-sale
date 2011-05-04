#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

void syscall_init (void);

struct lock lock_open_files;
struct lock lock_syscall;



#endif /* userprog/syscall.h  */
