
// SPDX-License-Identifier: GPL-2.0
/*
 * Checks the "per-device state" behaviour of my_cdev.ko and my_misc.ko.
 *
 * Usage:
 *   ./test_devs /dev/my_cdev0 /dev/my_cdev1
 *   ./test_devs /dev/my_misc0 /dev/my_misc1
 *
 * Exit code = number of failed checks.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(int ok, const char *what, const char *got)
{
	printf("%s  %s (got \"%s\")\n", ok ? "PASS" : "FAIL", what, got);
	if (!ok)
		failures++;
}

/* Read the whole buffer from offset 0 into out (NUL-terminated) */
static void read_all(int fd, char *out, size_t size)
{
	ssize_t n;

	lseek(fd, 0, SEEK_SET);
	n = read(fd, out, size - 1);
	out[n > 0 ? n : 0] = '\0';
}

static int open_rw(const char *path)
{
	int fd = open(path, O_RDWR | O_TRUNC);

	if (fd < 0)
		perror(path);
	return fd;
}

int main(int argc, char *argv[])
{
	char buf[128];
	int a, b, other, again;

	if (argc < 3) {
		fprintf(stderr, "usage: %s DEV0 DEV1\n", argv[0]);
		return 1;
	}

	a = open_rw(argv[1]);		/* two opens of the same node */
	b = open(argv[1], O_RDWR);
	other = open_rw(argv[2]);	/* a different node */
	if (a < 0 || b < 0 || other < 0)
		return 1;

	if (write(a, "shared", 6) != 6 || write(other, "other", 5) != 5)
		perror("write");

	read_all(b, buf, sizeof(buf));
	check(!strcmp(buf, "shared"),
	      "same node: second open sees data written by the first", buf);

	read_all(other, buf, sizeof(buf));
	check(!strcmp(buf, "other"),
	      "other node keeps its own data", buf);

	read_all(a, buf, sizeof(buf));
	check(!strcmp(buf, "shared"),
	      "first node not overwritten by the other node", buf);

	close(a);
	close(b);

	again = open(argv[1], O_RDONLY);
	if (again < 0) {
		perror(argv[1]);
		return 1;
	}
	read_all(again, buf, sizeof(buf));
	check(!strcmp(buf, "shared"),
	      "data survives after every fd is closed", buf);

	close(again);
	close(other);
	printf("%d check(s) failed\n", failures);
	return failures;
}
