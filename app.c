#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVICE "/dev/eeprom0"

int main()
{
	char rbuf[5];
	const char *wbuf = "data";

	int fd;
	if (open(DEVICE, O_RDWR) < 0)
		return perror("open"), -1;

	if (write(fd, wbuf, strlen(wbuf)) < 0)
		return perror("write"), -1;

	if (read(fd, rbuf, sizeof(rbuf)) < 0)
		return perror("read"), -1;

	printf("%s\n", rbuf);
	close(fd);
	return 0;
}
