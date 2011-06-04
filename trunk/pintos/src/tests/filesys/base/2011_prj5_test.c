#include <stdio.h>
#include <stdlib.h>
#include "tests/filesys/seq-test.h"
#include "tests/lib.h"

#define BLOCK_SIZE 20

int main(void)
{
	char filename[] = "myfile";
	char str[BLOCK_SIZE] = "PintosIsExcellentOS";
	char buffer[BLOCK_SIZE];
	int i,j;

  if (!create(filename, BLOCK_SIZE)) {
		printf("ERROR: fail to create file %s\n", filename);
		exit(1);
	}

	for (i = 0 ; i < 10 ; i++)
	{
		int fd = open(filename);
		if (fd <= 1) {
			printf("ERROR: fail to open file %s(fd=%d) - to write in loop #%d\n", filename, fd, i);
			exit(1);
		}

		for (j = 0 ; j < 1 ; j++) {
			printf("START WRITING %d/%d\n", j, i);
			int write_num = write(fd, str, BLOCK_SIZE);
			if (write_num != BLOCK_SIZE) {
				printf("ERROR: fail to write 'str'in loop #%d - wrote %d bytes\n", j, write_num);
				exit(1);
			}
			printf("END WRITING %d/%d\n", j, i);
		}
		
		close(fd);
	}

	for (i = 0 ; i < 10 ; i++) {
		int fd = open(filename);
		
		printf("START READING %d\n", i);
		while(1) {
			int read_num = read(fd, buffer, sizeof(buffer));
	
			if (read_num <= 0)
				break;
			else
				printf("%s", buffer);
		}
		printf("\n");
		printf("END READING %d\n", i);
		close(fd);
	}

	return 0;
}
