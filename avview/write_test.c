#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>





int fd;
char buffer[1024*1024];
long buffer_free;
long buffer_size=1024*1024;
long count;


int main(int argc, char * argv[])
{
int a;
if(argc<3){
	fprintf(stderr,"Usage: %s filename megabytes\n", argv[0]);
	return -1;
	}
fd=open(argv[1], O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
if(fd<0){
	fprintf(stderr,"Could not open file \"%s\" for writing: ", argv[1]);
	perror("");
	return -1;
	}
count=atol(argv[2]);

while(count>0){
	/* we truly don't care what we write */
	buffer_free=0;
	while(buffer_free<buffer_size){
		a=write(fd, buffer+buffer_free, buffer_size-buffer_free);
		if(a>0)buffer_free+=a;
			else
		if(a<0){
		  	perror("writing file");
			return -1;
			}
		}
	count--;
	}
close(fd);
}
