/*
 *    Copyright (C) 2000-2001  Regents of the University of California
 *    See ./DISCLAIMER
 */

#ifndef _CAN_LIB_H
#define _CAN_LIB_H

#include <asm/meiko/can.h>

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 128
#endif

struct canhostname {
	int cluster;
	int module;
	int node;
	char hostname[MAXHOSTNAMELEN];
};

struct canobj {
	int id;
	char name[MAXHOSTNAMELEN];
};

#ifndef PATH_CANHOSTS
#define PATH_CANHOSTS 	"/etc/canhosts"
#endif
#ifndef PATH_CANOBJ
#define PATH_CANOBJ	"/etc/canobj"
#endif

extern char *can_hb2str(unsigned long hb);
extern int can_str2hb(char *str, unsigned long *hb);
extern char *can_type2str(int type);
extern int can_str2type(char *name);
extern int can_gethostbyname(char *name, struct canhostname *canhost);
extern int can_gethostbyaddr(int c, int m, int n, struct canhostname *canhost);
extern int can_getobjbyname(char *name, struct canobj *canobj);
extern int can_getobjbyid(int id, struct canobj *canobj);

extern int can_ack(int fd, can_dat *dat, int len, int acknak, 
		struct can_packet *ack);
extern int can_send(int fd, can_header_ext *ext, can_dat *dat, int len);
extern int can_recv(int fd, can_header_ext *ext, can_dat *dat, int *len, 
		struct can_packet *ack);
extern int can_recv_ack(int fd, can_header_ext *ext, can_dat *dat, int *len);

#endif /*_CAN_LIB_H*/
