/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ioctl ABI shared by my_cdev.c (kernel) and test_ioctl.c (user space).
 *
 * Both sides include this one file, so the command numbers can never drift
 * apart (the MP Coding lesson copied them into each file by hand).
 *
 * Direction is named from USER SPACE's point of view:
 *   _IO    no argument
 *   _IOR   user reads  -> kernel uses put_user() / copy_to_user()
 *   _IOW   user writes -> kernel uses get_user() / copy_from_user()
 *   _IOWR  both ways
 *
 * Use fixed-size __u32/__s32 types, never int/long/pointers inside the
 * structs: the layout must be identical for 32-bit and 64-bit programs.
 */
#ifndef MY_CDEV_IOCTL_H
#define MY_CDEV_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct mycdev_info {
	__u32 index;		/* 0 for /dev/my_cdev0, 1 for /dev/my_cdev1 */
	__u32 len;		/* bytes currently stored */
	__u32 capacity;		/* buffer size */
	__s32 value;		/* the value set with MYCDEV_SET_VALUE */
};

/*
 * 'M' is also used by other subsystems (see
 * Documentation/userspace-api/ioctl/ioctl-number.rst). Fine for a lab;
 * a real driver picks a free magic and registers it there.
 */
#define MYCDEV_MAGIC		'M'

#define MYCDEV_CLEAR		_IO(MYCDEV_MAGIC, 1)		/* empty buffer */
#define MYCDEV_SAY_HELLO	_IO(MYCDEV_MAGIC, 2)		/* dmesg only */
#define MYCDEV_GET_VALUE	_IOR(MYCDEV_MAGIC, 3, __s32)	/* read value */
#define MYCDEV_SET_VALUE	_IOW(MYCDEV_MAGIC, 4, __s32)	/* write value */
#define MYCDEV_GET_INFO		_IOR(MYCDEV_MAGIC, 5, struct mycdev_info)

#endif /* MY_CDEV_IOCTL_H */
