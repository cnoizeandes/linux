
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#ifdef CONFIG_PROC_FS

// FIXME: make this FPGA-depedent
#define HIJACK_START	0x3fe00000
#define HIJACK_SIZE	(0x40000000-HIJACK_START)

void *hijack;
static int hijack_show(struct seq_file *m, void *v)
{
	int i;
	unsigned char *p = (unsigned char *)hijack;

	if (hijack != NULL && p[0] == 0) {
		seq_printf(m, "#!/bin/sh\necho Normal boot.\n");
	} else if (hijack != NULL) {
		for (i = 0; i < HIJACK_SIZE/PAGE_SIZE; i+=PAGE_SIZE) {
			seq_write(m, (void*)&p[i], PAGE_SIZE);
		}
	} else {
		seq_printf(m, "#!/bin/sh\necho not found: ioremap failed.\n");
	}
	return 0;
}

static int hijack_open(struct inode *inode, struct file *file)
{
	return single_open(file, hijack_show, NULL);
}

static const struct file_operations hijack_fops = {
	.open		= hijack_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init hijack_init(void)
{
	unsigned char *p;
	hijack = (void*)ioremap((phys_addr_t)HIJACK_START, HIJACK_SIZE);
	p = (unsigned char *)hijack;
	if (p[0] != '#' || p[0] != 0x79) {
		p[0] = 0;
	}
	proc_create("hijack", 0444, NULL, &hijack_fops);
	return 0;
}
arch_initcall(hijack_init);
#endif /* CONFIG_PROC_FS */
