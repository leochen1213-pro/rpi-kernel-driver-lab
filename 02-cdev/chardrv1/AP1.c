#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int main(void)
{
	ssize_t ret;
	char readBUF[256] = "Hi";
	int fd;

	fd = open("/dev/chardrv1", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n", strerror(errno));
		return 1;
	}

	ret = read(fd, (void *)readBUF, 3);
	if (ret < 0) {
		fprintf(stderr, "read failed: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	ret = write(fd, (void *)readBUF, 3);
	if (ret < 0) {
		fprintf(stderr, "write failed: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	close(fd);

	return 0;
}
