// SPDX-License-Identifier: GPL-2.0
/*
 * Exercises the my_cdev ioctl commands.
 *
 * Usage: ./test_ioctl [/dev/my_cdev0] [/dev/my_cdev1]
 * Exit code = number of failed checks.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "my_cdev_ioctl.h"

static int failures;

static void check(int ok, const char *what)
{
	printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		failures++;
}

/* Take a command number apart: dir | size | type | nr */
static void decode(const char *name, unsigned int cmd)
{
	unsigned int dir = _IOC_DIR(cmd);

	printf("  %-17s 0x%08x  dir=%-5s size=%-2u type='%c' nr=%u\n",
	       name, cmd,
	       dir == _IOC_NONE ? "none" :
	       dir == _IOC_READ ? "read" :
	       dir == _IOC_WRITE ? "write" : "rw",
	       _IOC_SIZE(cmd), _IOC_TYPE(cmd), _IOC_NR(cmd));
}

int main(int argc, char *argv[])
{
	const char *dev0 = argc > 1 ? argv[1] : "/dev/my_cdev0";
	const char *dev1 = argc > 2 ? argv[2] : "/dev/my_cdev1";
	struct mycdev_info info;
	char buf[64];
	int fd, fd1, ret;
	__s32 value;
	ssize_t n;

	printf("command numbers:\n");
	decode("MYCDEV_CLEAR", MYCDEV_CLEAR);
	decode("MYCDEV_SAY_HELLO", MYCDEV_SAY_HELLO);
	decode("MYCDEV_GET_VALUE", MYCDEV_GET_VALUE);
	decode("MYCDEV_SET_VALUE", MYCDEV_SET_VALUE);
	decode("MYCDEV_GET_INFO", MYCDEV_GET_INFO);
	printf("\n");

	fd = open(dev0, O_RDWR | O_TRUNC);
	if (fd < 0) {
		perror(dev0);
		return 1;
	}

	ret = ioctl(fd, MYCDEV_SAY_HELLO);
	check(ret == 0, "SAY_HELLO (look in dmesg)");

	value = 0;
	ret = ioctl(fd, MYCDEV_GET_VALUE, &value);
	printf("      default value = 0x%x\n", value);
	check(ret == 0 && value == 0xc0ffee, "GET_VALUE returns the default");

	value = 1234;
	ret = ioctl(fd, MYCDEV_SET_VALUE, &value);
	value = 0;
	ret |= ioctl(fd, MYCDEV_GET_VALUE, &value);
	check(ret == 0 && value == 1234, "SET_VALUE then GET_VALUE round trip");

	if (write(fd, "hello", 5) != 5)
		perror("write");
	memset(&info, 0xff, sizeof(info));
	ret = ioctl(fd, MYCDEV_GET_INFO, &info);
	printf("      info: index=%u len=%u capacity=%u value=%d\n",
	       info.index, info.len, info.capacity, info.value);
	check(ret == 0 && info.index == 0 && info.len == 5 &&
	      info.capacity == 64 && info.value == 1234,
	      "GET_INFO returns a whole struct");

	ret = ioctl(fd, MYCDEV_CLEAR);
	n = pread(fd, buf, sizeof(buf), 0);
	check(ret == 0 && n == 0, "CLEAR empties the buffer (read returns 0)");

	errno = 0;
	ret = ioctl(fd, MYCDEV_GET_VALUE, (void *)8);
	check(ret < 0 && errno == EFAULT, "bad user pointer -> EFAULT");

	errno = 0;
	ret = ioctl(fd, _IO(MYCDEV_MAGIC, 99));
	check(ret < 0 && errno == ENOTTY, "unknown nr -> ENOTTY");

	errno = 0;
	ret = ioctl(fd, _IO('Z', 1));
	check(ret < 0 && errno == ENOTTY, "wrong magic -> ENOTTY");

	errno = 0;		/* same nr as GET_VALUE, wrong size in the cmd */
	ret = ioctl(fd, _IOR(MYCDEV_MAGIC, 3, __s64), &value);
	check(ret < 0 && errno == ENOTTY,
	      "same nr but different size -> ENOTTY (size is part of cmd)");

	fd1 = open(dev1, O_RDWR);
	if (fd1 >= 0) {
		value = 0;
		ret = ioctl(fd1, MYCDEV_GET_VALUE, &value);
		check(ret == 0 && value == 0xc0ffee,
		      "my_cdev1 keeps its own value (per-device state)");
		close(fd1);
	} else {
		printf("SKIP  %s not found\n", dev1);
	}

	close(fd);
	printf("%d check(s) failed\n", failures);
	return failures;
}
