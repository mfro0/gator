#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

unsigned char *chunk;
unsigned long size;

void hex_dump(unsigned char *addr, unsigned long size)
{
int i;
for(i=0;i<size;i++){
	if(!(i & 0x1f))printf("\n%08x:", i);
	if((i & 0x1f)==0x10)printf("   ");
	printf(" %02x", addr[i]);
	}
fflush(stdout);
}

int main(int argc, char *argv[])
{
int fd;
struct stat buf;
if(argc<2)return -1;

fd=open(argv[1], O_RDONLY);
if(fd<0){
	perror(argv[0]);
	return -1;
	}
if(fstat(fd, &buf)<0){
	perror(argv[0]);
	return -1;
	}
size=buf.st_size;
chunk=(unsigned char *)mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
if(chunk==MAP_FAILED){
	perror(argv[0]);
	return -1;
	}
while(1){
	hex_dump(chunk, size);
	usleep(300000);
	}
}
