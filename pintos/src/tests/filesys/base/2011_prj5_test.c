#include <stdio.h>
#include <stdlib.h>
#include "tests/filesys/seq-test.h"
#include "tests/lib.h"

#define BLOCK_SIZE 20

static void 
seq_test_open(const char *file_name, void *buf, size_t size, size_t initial_size,
          size_t (*block_size_func) (void),
          void (*check_func) (int fd, long ofs)) 
{
  size_t ofs;
  int fd;
  
  random_bytes (buf, size);
  //CHECK (create (file_name, initial_size), "create \"%s\"", file_name);
  CHECK ((fd = open (file_name)) > 1, "open \"%s\"", file_name);

  ofs = 0;
  msg ("writing \"%s\"", file_name);
  while (ofs < size) 
    {
      size_t block_size = block_size_func ();
      if (block_size > size - ofs)
        block_size = size - ofs;

      if (write (fd, buf + ofs, block_size) != (int) block_size)
        fail ("write %zu bytes at offset %zu in \"%s\" failed",
              block_size, ofs, file_name);

      ofs += block_size;
      if (check_func != NULL)
        check_func (fd, ofs);
    }
  msg ("close \"%s\"", file_name);
  close (fd);
  check_file (file_name, buf, size);
}


static char buf[BLOCK_SIZE];

static size_t return_block_size(void)
{
	return sizeof(buf);
}

int main(void)
{
	int i;
	seq_test("myfile", buf, sizeof(buf), sizeof(buf), return_block_size, NULL);

	for (i = 0 ; i < 10 ; i++) {
		seq_test_open("myfile", buf, sizeof(buf), sizeof(buf), return_block_size, NULL);
	}

	for (i = 0 ; i < 10 ; i++) {
		int fd = open("myfile");
		printf("Success to open 'myfile'\n");
		
		while(1) {
			char buffer[BLOCK_SIZE];
			int read_num = read(fd, buffer, sizeof(buffer));
	
			if (read_num <= 0)
				break;
		}
		printf("Success to read 'myfile'\n");
		close(fd);
	}

	return 0;
}
