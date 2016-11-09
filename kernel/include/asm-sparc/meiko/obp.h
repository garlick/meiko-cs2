/*
 * Define OBP paths for Meiko and some OBP utility functions.
 * Also convert between OBP values and CAN encodings.
 */
#ifndef _SPARC_MEIKO_OBP_H
#define _SPARC_MEIKO_OBP_H

#define OBP_AUTO_BOOT			"/options/auto-boot?"
#define OBP_BOOT_DEVICE			"/options/boot-device"
#define OBP_INPUT_DEVICE		"/options/input-device"
#define OBP_OUTPUT_DEVICE		"/options/output-device"
#define OBP_CANCON_HOST			"/options/cancon-host"

#define OBP_CAN_REG			"/obio/can/reg"
#define OBP_CAN_NODEID			"/obio/can/can-nodeid"
#define OBP_CAN_MEIKO_BOARD_TYPE	"/obio/can/can-meiko-board-type"
#define OBP_CAN_ELAN_REG		"/obio/can/elan/reg"
#define OBP_LEDS_REG			"/obio/leds/reg"
#define OBP_TIMER_REG			"/obio/counter/reg"
#define OBP_INTERRUPT_REG		"/obio/interrupt/reg"

#define OBP_MAX_PATHLEN			80
#define OBP_MAXSTR			80 /* option strings, etc.. */

#define OBP_BOOT_DEVICE_VALUES  {  			\
	"", "elan", "disk", "net", "disk0", "disk1",	\
	"disk2", "disk3", "can", "eip", NULL, 		\
}
#define OBP_INPUT_DEVICE_VALUES  {			\
	"keyboard", "can", "ttya", "ttyb", NULL,	\
}
#define OBP_OUTPUT_DEVICE_VALUES  {			\
	"screen", "can", "ttya", "ttyb", NULL,		\
}
#define OBP_BOOLEAN  {					\
	"false", "true", NULL,				\
}

/*
 * These functions convert between Meiko CAN encodings and OBP strings
 * using null-terminated string lists defined above.
 */

static __inline__ int
obp_strtonum(char *str, char *names[])
{
	int i = 0;

	while (names[i]) {
		if (strcmp(names[i], str) == 0)
			break;
		i++;
	}
	return names[i] ? i : -1;
}	

static __inline__ char *
obp_numtostr(int num, char *names[])
{
	int i = 0;

	while (names[i])
		i++;
	return (num >= 0 && num < i) ? names[num] : NULL;
}

#ifdef __KERNEL__
#include <asm/oplib.h>

/* 
 * These functions get and set prom properties by full path name.
 */

extern __inline__ int
__obp_lookup(int node, char *path)
{
	char *name = path;

	path = strchr(path, '/');
	if (!path)
		return prom_node_has_property(node, name) ? node : -1;
	*path++ = '\0';
	node = prom_getchild(node);
	if (node == -1)
		return -1;
	node = prom_searchsiblings(node, name);
	if (node == 0)
		return -1;
	return __obp_lookup(node, path);
}

extern __inline__ int
obp_lookup(char *path)
{
	char path_cpy[OBP_MAX_PATHLEN + 1];

	if (!path)
		return -1;
	if (*path == '/')
		path++;
	strncpy(path_cpy, path, OBP_MAX_PATHLEN + 1);
	return __obp_lookup(prom_root_node, path_cpy);
}

extern __inline__ char *
basename(char *path)
{
	char *p = strrchr(path, '/');

	return p ? p + 1 : path;
}

extern __inline__ int
obp_getprop(char *path, void *buf, int size)
{
	int node = obp_lookup(path);

	if (node == -1)
		return -1;
	/* prom_getproperty() returns size, or -1 on failure */
	return prom_getproperty(node, basename(path), buf, size);	
}

extern __inline__ int
obp_setprop(char *path, void *buf, size_t size)
{
	int node = obp_lookup(path);

	if (node == -1)
		return -1;
	/* 
	 * XXX arch/sparc/prom/tree.c::prom_setprop() claims to
	 * return the number of bytes accepted, however, we get
	 * back smaller positive ints and everything seems OK.
	 */
	return prom_setprop(node, basename(path), buf, size);
}

/*
 * These function use Meiko CAN encodings to get/set properties.
 */

extern __inline__ int
obp_getcan(char *path, char *table[], u32 *val)
{
	char str[OBP_MAXSTR];

	if (obp_getprop(path, str, OBP_MAXSTR) == -1)
		return -1;
	*val = obp_strtonum(str, table);
	return 0;
}

extern __inline__ int
obp_setcan(char *path, char *table[], int val)
{
	char *str = obp_numtostr(val, table);

	if (!str)
		return -1;
	return obp_setprop(path, str, strlen(str) + 1);
}
#endif /* __KERNEL__ */
#endif /* _SPARC_MEIKO_OBP_H */
