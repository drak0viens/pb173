#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

int my_open(struct inode *inode, struct file *filp){
	printk(KERN_INFO "Opening device..\n");
	return 0;
}

int my_release(struct inode *inode, struct file *filp){
	printk(KERN_INFO "Releasing device..\n");
	return 0;
}

ssize_t my_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp){
	return count;
}

ssize_t my_read(struct file *filp, char __user *buf, size_t count, loff_t *offp){
	return 0;
}

struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.open = my_open,
	.write = my_write,
	.read = my_read,
	.release = my_release,
};

struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "my_device",
	.fops = &my_fops,
};


static int my_init(void)
{
	misc_register(&misc);
	return 0;
}

static void my_exit(void)
{
	misc_deregister(&misc);
	return 0;
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
