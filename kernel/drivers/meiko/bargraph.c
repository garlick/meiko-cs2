/* 
 * $Id: bargraph.c,v 1.3 2001/07/24 15:51:13 garlick Exp $
 * 
 * Device driver for Meiko CS/2 bargraph (char, major 10, minor 160). 
 * The driver keeps a default pattern going on the bargraph as long as
 * no processes have the special file open.
 * 
 * Setting a bit turns off an LED.  
 * Bits are assigned to the 4x4 grid as follows:
 *       3  2  1  0
 *       7  6  5  4
 *       11 10 9  8
 *       15 14 13 12
 * 
 * Example using ioctl (turn off all LED's, then read back value):
 *    #include <sys/ioctl.h>
 *    #include <stdint.h>
 *    #include <asm/meiko/bargraph.h>
 *
 *    uint32_t value = 0xffff;
 *
 *    ioctl(fd, BGSET, &value);
 *    ioctl(fd, BGGET, &value);
 *
 * Example using mmap:
 *    bargraph_reg_t *reg; = mmap(0, sizeof(bargraph_reg_t), 
 *                                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
 *    if (reg != MAP_FAILED) {
 *    		value = reg->leds;
 *    		reg->leds = value;
 *    }
 * 
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/stddef.h>	/* NULL */
#include <linux/errno.h>	/* Evalues */
#include <linux/miscdevice.h>	/* misc_register */
#include <linux/kernel.h>	/* printk */
#include <linux/sched.h>	/* jiffies */
#include <linux/timer.h>	/* add_timer / del_timer */
#include <asm/uaccess.h>	/* copy_from_user */
#include <asm/pgtable.h>	/* io_remap_page_range */
#include <asm/io.h>		/* sparc_alloc_io */

#include <asm/meiko/obp.h>
#include <asm/meiko/bargraph.h>

/* update internal pattern every 1/10 sec */
#define HZ_DELAY		HZ/10

/* timer for internal pattern */
struct timer_list 		bg_timer;

/* only one user can open at a time */
static int 			bg_usecount = 0;

/* address of bargraph register */
static volatile bargraph_reg_t	*reg = NULL;
static int			bg_which_io;
static unsigned long		bg_phys_addr;

static void bg_stop_pattern(void);
static void bg_start_pattern(int);
static void bg_update_pattern(unsigned long);

/*
 * Map the page containing the bargraph register into user space.
 * XXX Page offset is hard coded in bargraph_reg_t (bargraph.h).
 */
static int
bg_mmap (struct file *file, struct vm_area_struct *vma)
{
	size_t len = vma->vm_end - vma->vm_start;

	if (len > PAGE_SIZE)
		return -EINVAL;
	if (vma->vm_offset != 0)
		return -EINVAL;

	vma->vm_flags |= (VM_SHM | VM_LOCKED);	/* avoid the swapper */
	vma->vm_flags |= VM_IO;			/* don't include in a core */

	if (io_remap_page_range(vma->vm_start, bg_phys_addr, len, 
				vma->vm_page_prot, bg_which_io)) {
		return -EAGAIN;
	}
#ifdef	VERBOSE
	printk("bargraph: mmap to %lx\n", vma->vm_start);
#endif
	return 0;
}

static int 
bg_ioctl(struct inode *inode, struct file *file, unsigned int cmd, 
		unsigned long arg)
{
	if (reg == NULL)
		return -ENXIO;

	switch (cmd) {
		case BGGET:
			copy_to_user_ret((uint16_t *)arg, &reg->leds, 
					sizeof(uint16_t), -EFAULT);
			break;
		case BGSET:
			copy_from_user_ret(&reg->leds, (uint16_t *)arg, 
					sizeof(uint16_t), -EFAULT);
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int 
bg_open(struct inode *inode, struct file *file)
{
	if (bg_usecount > 0)
		return -EBUSY;
	if (reg == NULL)		/* device not mapped */
		return -ENXIO;
	bg_stop_pattern();
	bg_usecount++;
	MOD_INC_USE_COUNT;
	return 0;
}

static int 
bg_release(struct inode *inode, struct file *file)
{
	bg_start_pattern(0);
	bg_usecount--;
	MOD_DEC_USE_COUNT;

	return 0;
}

static struct file_operations bg_fops = {
	ioctl:		bg_ioctl,
	mmap:		bg_mmap,
	open:		bg_open,
	release:	bg_release,
};

static struct miscdevice bg_dev = 
    { DINO1_BARGRAPH_MINOR, "CS2 Blinking Light Unit", &bg_fops };

EXPORT_NO_SYMBOLS;

/*
 * Start updating bargraph with internal pattern.
 */
static void 
bg_start_pattern(int quiet)
{
	if (!quiet) {
#ifdef	VERBOSE
		printk("bargraph: starting default pattern\n");
#endif
	}
	init_timer(&bg_timer);
	bg_timer.expires = jiffies + HZ_DELAY;
	bg_timer.data = 0L;
	bg_timer.function = bg_update_pattern;
	add_timer(&bg_timer);
}

/*
 * User has device open--stop updating bargraph.
 */
static void 
bg_stop_pattern(void)
{
#ifdef	VERBOSE
	printk("bargraph: suspending default pattern\n");
#endif
	del_timer(&bg_timer);
	reg->leds = ~0;
}

/*
 * Called by timer to update internal pattern on bargraph.
 */
static void 
bg_update_pattern(unsigned long dummy)
{
	static unsigned int next = 0;
#if 0
	static unsigned short patterns[] = { 
	    0x0001, 0x0002, 0x0004, 0x0008,
	    0x0010, 0x0020, 0x0040, 0x0080,
	    0x0100, 0x0200, 0x0400, 0x0800, 
	    0x1000, 0x2000, 0x4000, 0x8000 };	/* light each LED in turn */
#endif
#if 0
        static unsigned short patterns[] = {
            0x0001, 0x0002, 0x0004, 
	    0x0008, 0x0080, 0x0800, 
	    0x8000, 0x4000, 0x2000, 
	    0x1000, 0x0100, 0x0010 };		/* counterclockwise rotation */
#endif
#if 0
	static unsigned short patterns[] = { 
	    0x6666, 0x8421, 0x0ff0, 0x1248 };	/* propeller */
#endif
        static unsigned short patterns[] = {
	    0x040f, 0x0417, 0x0513, 0x1151,
	    0x3150, 0x7140, 0xf020, 0xe820,
	    0xc8a0, 0x8a88, 0x0a8c, 0x028e };	/* like old cs/2 pattern */

	if (reg == NULL)
		return;

	reg->leds = ~patterns[next++];
	if (next == sizeof(patterns) / sizeof(unsigned short))
		next = 0;			/* wrap */

	bg_start_pattern(1);			/* requeue timeout */	
}

/*
 * Map the bargraph page for the kernel.
 */
static bargraph_reg_t *
bg_map(void)
{
	struct linux_prom_registers promregs[PROMREG_MAX];
	int size;
	int regcount;

	size = obp_getprop(OBP_LEDS_REG, promregs, sizeof(promregs));
	regcount = size / sizeof(promregs[0]);
        prom_apply_obio_ranges(promregs, regcount);

	bg_which_io = promregs[0].which_io;	/* remember phys for mmap */
	bg_phys_addr = promregs[0].phys_addr & PAGE_MASK;

	return (bargraph_reg_t *)sparc_alloc_io(bg_phys_addr, 0, PAGE_SIZE, 
			"CS2 Blinking Light Unit", bg_which_io, 0);
}

/*
 * Unmap the bargraph page for the kernel.
 */
static void
bg_unmap(volatile bargraph_reg_t *addrp)
{
	sparc_free_io((void *)addrp, PAGE_SIZE);
}

#ifdef MODULE
int init_module(void)
#else
__initfunc(int bg_init(void))
#endif
{
	int error;

	reg = bg_map();
	if (reg == NULL) {
		/* silently fail here if bargraph not present */
		return -1;
	}
#ifdef	VERBOSE
	printk("bargraph: init (phys %x,%lx)\n", 
			bg_which_io, bg_phys_addr);
#endif
	error = misc_register(&bg_dev);
	if (error) {
		printk(KERN_ERR "bargraph: unable to get misc minor\n");
		bg_unmap(reg);
		return error;
	}
	bg_start_pattern(0);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	bg_stop_pattern();
	bg_unmap(reg);
	misc_deregister(&bg_dev);
#ifdef	VERBOSE
	printk("bargraph: fini\n");
#endif
}
#endif
