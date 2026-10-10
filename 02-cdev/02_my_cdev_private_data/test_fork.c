// SPDX-License-Identifier: GPL-2.0
/*
 * fork() and /dev/my_cdev0 (lesson 07-02 driver):
 *  - an fd opened BEFORE fork() is shared by parent and child
 *    (same struct file: same buffer, same offset)
 *  - an fd opened by the child AFTER fork() is a new struct file
 *
 * Usage: ./test_fork [/dev/my_cdev0]
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void show(const char *who, int fd, int rewind)
{
	char buf[128];
	off_t pos = lseek(fd, 0, SEEK_CUR);
	ssize_t n;

	if (rewind)
		lseek(fd, 0, SEEK_SET);
	n = read(fd, buf, sizeof(buf) - 1);
	buf[n > 0 ? n : 0] = '\0';
	printf("  %-6s fd %d, offset was %ld%s: read %zd bytes \"%s\"\n",
	       who, fd, (long)pos, rewind ? " (rewound to 0)" : "", n, buf);
}

int main(int argc, char *argv[])
{
	const char *path = argc > 1 ? argv[1] : "/dev/my_cdev0";
	int fd, own;
	pid_t pid;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		perror(path);
		return 1;
	}

	pid = fork();
	if (pid < 0) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {			/* child */
		printf("child (pid %d): writes \"from child\" to inherited fd %d\n",
		       getpid(), fd);
		if (write(fd, "from child", 10) != 10)
			perror("write");

		own = open(path, O_RDWR);	/* opened after fork */
		printf("child: its own open() after fork gets fd %d\n", own);
		show("child", own, 1);
		close(own);
		close(fd);		/* parent still holds it: no release */
		return 0;
	}

	waitpid(pid, NULL, 0);		/* parent */
	printf("parent (pid %d): child has exited\n", getpid());
	show("parent", fd, 0);		/* shared offset: already at 10 */
	show("parent", fd, 1);		/* shared buffer: sees the child's data */
	close(fd);			/* last fd -> release */
	return 0;
}
