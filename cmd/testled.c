/*****************************************************************************\
 *  Copyright (c) 2000 Regents of the University of California
 *  the Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  UCRL-CODE-2000-010 All rights reserved.
 *
 *  This file is part of the M/Linux linux port to Meiko CS/2.
 *  For details, see https://github.com/garlick/meiko-cs2
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/*
 * $Id: leddemo.c,v 1.4 2001/07/30 19:16:30 garlick Exp $
 *
 * Test bargraph device driver.
 * This module can test either the ioctl way or the mmap way of updating
 * the bargraph.  Set USE_MMAP to 1 for mmap, 0 for ioctl.
 */

#define USE_MMAP 0

#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <asm/meiko/bargraph.h>

#if 0
static uint16_t	patterns[] = {
    0x0001, 0x0002, 0x0004, 0x0008,
    0x0010, 0x0020, 0x0040, 0x0080,
    0x0100, 0x0200, 0x0400, 0x0800,
    0x1000, 0x2000, 0x4000, 0x8000 };   /* light each LED in turn */

static uint16_t patterns[] = {
    0x6666, 0x8421, 0x0ff0, 0x1248 };   /* propeller */

static uint16_t patterns[] = {
    0x000f, 0x00f0, 0x0f00, 0xf000 };   /* horiz bar */

static uint16_t patterns[] = {
    0x1111, 0x2222, 0x4444, 0x8888 };   /* vert bar */

static uint16_t patterns[] = {
    0x1111, 0x2222, 0x4444, 0x8888, 0x4444, 0x2222 };   /* vert bar eyeball */
#endif

static uint16_t patterns[] = {
    0x1000, 0x2000, 0x4000, 0x8000, 0x4000, 0x2000 };   /* eyeball */

#if	USE_MMAP
static bargraph_reg_t *reg;
#endif
static int fd;

void 
update(uint16_t val)
{
#if	USE_MMAP
	reg->leds = val;
#else
	if (ioctl(fd, BGSET, &val) < 0) {
		perror("ioctl");
		exit(1);
	}
#endif
}

int
main(int argc, char *argv[])
{
	uint16_t next = 0;
	uint16_t value;

	fd = open("/dev/bargraph", O_RDWR);
	if (fd < 0) {
		perror("bargraph");
		exit(1);
	}
#if 	USE_MMAP
	reg = mmap(0, sizeof(bargraph_reg_t), PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	if (reg == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
#endif
	for (;;) {
		value = ~patterns[next++];
		update(value);
		if (next == sizeof(patterns) / sizeof(next))
			next = 0;                       /* wrap */
		usleep(50000);
	}
#if	USE_MMAP
	if (munmap(reg, sizeof(bargraph_reg_t)) < 0) {
		perror("munmap");
		exit(1);
	}
#endif
	close(fd);
	exit(0);
}
