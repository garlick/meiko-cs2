/*
 * Map the elan chip's nanosecond clock and allow it to be mmapped.
 */

#include <linux/config.h>       /* includes <linux/autoconf.h> */
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#define MODVERSIONS
#endif
#ifdef MODVERSIONS
#include <linux/modversions.h>
#endif
#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>	/* misc_register */
#include <linux/malloc.h>	/* kmalloc */
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/list.h>		/* list_add */
#include <linux/proc_fs.h>	/* create_proc_entry */
#include <linux/major.h>	/* MISC_MAJOR */
#include <linux/fs.h>		/* register_chrdev */
#include <asm/uaccess.h>	/* copy_*_user */
#include <linux/time.h>		/* struct timespec */
#include <asm/mman.h>		/* PROT_READ */
#include <asm/pgtable.h>	/* io_remap_page_range */
#include <asm/io.h>		/* sparc_alloc_io */ 
#include <asm/delay.h>

#include <asm/meiko/obp.h>
#include <asm/meiko/elan.h>

static int		reg_which_io; 
static unsigned long 	reg_phys_addr; 

/**
 ** File operations.
 **/

static int
elan_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		unsigned long arg)
{
	return -EINVAL;
}

static int
elan_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
elan_open (struct inode *inode, struct file *file)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int
elan_mmap (struct file *file, struct vm_area_struct *vma)
{
	size_t len = vma->vm_end - vma->vm_start;

	if (len > PAGE_SIZE)
		return -EINVAL;
	if (vma->vm_offset != 0)
		return -EINVAL;

	vma->vm_flags |= (VM_SHM | VM_LOCKED);	/* avoid the swapper */
	vma->vm_flags |= VM_IO;			/* don't include in a core */
	if (vma->vm_flags & VM_WRITE || vma->vm_flags & VM_MAYWRITE) {
		return -EPERM;			/* no writeable mappings */
	}

	/* Argh - the mapping created here is read/write despite prot bits */
	if (io_remap_page_range(vma->vm_start, reg_phys_addr, len, 
			vma->vm_page_prot, reg_which_io)) {
		return -EAGAIN;
	}
	/* XXX should fixup pte here for write protection */
#ifdef	VERBOSE
	printk("elan: mmap to %lx\n", vma->vm_start);
#endif
	return 0;
}

static struct file_operations elan_fops = {
	ioctl:          elan_ioctl,
	mmap:           elan_mmap,
	open:           elan_open,
	release:        elan_release, 
};

static int
elan_getphys(void)
{
	struct linux_prom_registers promregs[PROMREG_MAX];
	int size, regcount;

	size = obp_getprop(OBP_CAN_ELAN_REG, &promregs, sizeof(promregs)); 
	if (size == -1)
		return -1;
	regcount = size / sizeof(struct linux_prom_registers);
	prom_apply_obio_ranges(promregs, regcount);

	reg_which_io  = promregs[0].which_io;
	reg_phys_addr = promregs[0].phys_addr & PAGE_MASK;

	return 0;
}

#ifdef MODULE
int init_module(void)
#else
__initfunc(int elan_init(void))
#endif
{
	if (register_chrdev(ELAN_MAJOR, ELAN_NAME, &elan_fops)) {
		printk("elan: can't register device\n");
		return -ENXIO;
	}
	if (elan_getphys() < 0) {
		printk("elan: failed to look up elan registers\n");
		unregister_chrdev(ELAN_MAJOR, ELAN_NAME);
		return -1;
	}
#ifdef	VERBOSE
	printk("elan: found at %x,%lx\n", reg_which_io, reg_phys_addr);
#endif
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	unregister_chrdev(ELAN_MAJOR, ELAN_NAME);
} 
#endif
