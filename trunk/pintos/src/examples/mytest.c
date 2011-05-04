#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

int main()
{
	if ( !create("testfile", 128) ) {
		printf("fail to create file\n");
	}
	else {	
		printf("success to create file! - filename = %s\n", "testfile");
	}

	int fd = open("testfile");
	if (fd == -1)
		printf("fail to open - not found \n");
	else 
		printf("success to open file : fd = %d\n", fd);

	printf("file size = %d\n", filesize(fd));

	close(fd);
	printf("close file!!!\n");
		
	fd = open("testfile");
	if (fd == -1)
		printf("fail to open - not found \n");
	else
		printf("success to open file : fd = %d\n", fd);

				
	char str[] = "I admire TA!";
	char buf[100];

	write(fd, str, sizeof(str));
				
	close(fd);
	printf("close file!!!\n");
	fd = open("testfile");
	if (fd == -1)
		printf("fail to open - not found \n");
	else
		printf("success to open file : fd = %d\n", fd);

	printf("read length = %d\n", read(fd, buf, sizeof(buf)));
	printf("read string = %s\n", buf);

	close(fd);			
	return 0;
}


