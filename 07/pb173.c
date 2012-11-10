#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kfifo.h>

/* declare static 8KB char fifo */
DECLARE_KFIFO(my_fifo, char, 8192);

ssize_t my_write(struct file *filp, const char __user *buf, size_t count, loff_t *off){
	int nbytes;
	/* copy user buffer to fifo */
	kfifo_from_user(&my_fifo, buf, count, &nbytes);
	return nbytes;
}

ssize_t my_read(struct file *filp, char __user *buf, size_t count, loff_t *off){
	int nbytes;
	/* copy data from fifo to user */
	kfifo_to_user(&my_fifo, buf, count, &nbytes);
	return nbytes;
}

struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.write = my_write,
	.read = my_read,
};

struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "my_fifo_device",
	.fops = &my_fops,
};

static int my_init(void)
{
	INIT_KFIFO(my_fifo);
	misc_register(&misc);
	/* initialize fifo  */
	return 0;
}

static void my_exit(void)
{
	misc_deregister(&misc);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
