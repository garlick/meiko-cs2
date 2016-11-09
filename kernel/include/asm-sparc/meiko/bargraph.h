/*
 * $Id: bargraph.h,v 1.3 2001/07/24 15:51:17 garlick Exp $
 */
#ifndef _SPARC_MEIKO_BARGRAPH_H
#define _SPARC_MEIKO_BARGRAPH_H

#define BGSET _IOW('b', 42, uint16_t)
#define BGGET _IOR('b', 43, uint16_t)

typedef struct {
	uint8_t	_pad[0x600];	/* page offset */
	uint16_t _pad2;
	uint16_t leds;
} bargraph_reg_t;

#endif
