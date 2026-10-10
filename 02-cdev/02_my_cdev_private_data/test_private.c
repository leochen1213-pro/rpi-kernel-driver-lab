// SPDX-License-Identifier: GPL-2.0
/*
 * Shows that each open() of /dev/my_cdev0 has its own buffer, and that
 * dup() shares it (same struct file, different fd number).
 *
 * Usage: ./test_private [/dev/my_cdev0]
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void read_back(const char *name, int fd)
{
	char buf[128];
	ssize_t n;

	lseek(fd, 0, SEEK_SET);
	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) {
		perror("read");
		return;
	}
	buf[n] = '\0';
	printf("  %s reads %zd bytes: \"%s\"\n", name, n, buf);
}

/* Read from the CURRENT offset, without lseek() first */
static void read_here(const char *name, int fd)
{
	char buf[128];
	ssize_t n;

	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) {
		perror("read");
		return;
	}
	buf[n] = '\0';
	printf("  %s reads %zd bytes from offset %ld: \"%s\"\n",
	       name, n, (long)lseek(fd, 0, SEEK_CUR) - (long)n, buf);
}

int main(int argc, char *argv[])
{
	const char *path = argc > 1 ? argv[1] : "/dev/my_cdev0";
	int fd_a, fd_b, fd_c, fd_d;

	fd_a = open(path, O_RDWR);
	fd_b = open(path, O_RDWR);
	if (fd_a < 0 || fd_b < 0) {
		perror(path);
		return 1;
	}

	printf("1. two opens, each writes its own text\n");
	if (write(fd_a, "from A", 6) != 6 || write(fd_b, "from B", 6) != 6)
		perror("write");
	read_back("fd A", fd_a);
	read_back("fd B", fd_b);

	printf("2. close A, open C: C starts empty\n");
	close(fd_a);
	fd_c = open(path, O_RDWR);
	if (fd_c < 0) {
		perror(path);
		return 1;
	}
	read_back("fd C", fd_c);

	printf("3. B still has its data\n");
	read_back("fd B", fd_b);

	printf("4. dup(C) -> D: new fd number, same struct file\n");
	fd_d = dup(fd_c);
	if (fd_d < 0) {
		perror("dup");
		return 1;
	}
	printf("  fd C is %d, fd D is %d\n", fd_c, fd_d);
	if (write(fd_c, "hello", 5) != 5)	/* offset of the shared file: 0 -> 5 */
		perror("write");
	read_here("fd D", fd_d);	/* 0 bytes: D shares C's offset (5) */
	read_back("fd D", fd_d);	/* after lseek(0): sees C's "hello" */

	printf("5. close C: D keeps the buffer alive (no release yet)\n");
	close(fd_c);
	read_back("fd D", fd_d);

	printf("6. close D: last fd of that struct file -> release\n");
	close(fd_d);

	close(fd_b);
	return 0;
}