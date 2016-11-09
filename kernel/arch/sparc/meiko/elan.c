/*
 * Map the elan chip's nanosecond clock.
 */

#include <linux/config.h>       /* includes <linux/autoconf.h> */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>	/* misc_register */
#include <linux/malloc.h>	/* kmalloc */
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/time.h>		/* struct timespec */
#include <asm/pgtable.h>	/* io_remap_page_range */
#include <asm/io.h>		/* sparc_alloc_io */ 

#include <asm/meiko/obp.h>
#include <asm/meiko/elan.h>
#include <asm/meiko/debug.h>

elanreg_t 	*elanreg = NULL;

void
elan_init(void)
{
	struct linux_prom_registers promregs[PROMREG_MAX];
	int size, regcount;
	unsigned int which_io;
	unsigned long phys_addr;

	size = obp_getprop(OBP_CAN_ELAN_REG, &promregs, sizeof(promregs)); 
	ASSERT(size != -1);
	regcount = size / sizeof(struct linux_prom_registers);
	prom_apply_obio_ranges(promregs, regcount);

	which_io  = promregs[0].which_io;
	phys_addr = promregs[0].phys_addr & PAGE_MASK;

        elanreg = (elanreg_t *)sparc_alloc_io(phys_addr, 0, PAGE_SIZE, 
			ELAN_NAME, which_io, 0x0);
	ASSERT(elanreg != NULL);
}

