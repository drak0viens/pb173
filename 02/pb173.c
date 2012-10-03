#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>
/*
 *  Define unique ioctl commands
 */
#define MY_IOC_MAGIC '$'
#define MY_READ      _IOR(MY_IOC_MAGIC, 16, int)
#define MY_WRITE     _IOW(MY_IOC_MAGIC, 17, int)
/*
 *  Global variables
 */
int char_count = 4;

int my_open(struct inode *inode, struct file *filp){
	printk(KERN_INFO "Opening device..\n");
	return 0;
}

int my_release(struct inode *inode, struct file *filp){
	printk(KERN_INFO "Releasing device..\n");
	return 0;
}

ssize_t my_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp){
	/*
	 * Write buffer from userspace as string with printk
	 */
	char* str = (char*) kmalloc(count + sizeof(char), GFP_KERNEL);
	if (copy_from_user(str, buf, count)) return -EFAULT;
	str[count] = '\0';
	printk(KERN_INFO "%s", str);
	kfree(str);
	return count;
}

ssize_t my_read(struct file *filp, char __user *buf, size_t count, loff_t *offp){
	/*
	 * Copy string to userspace
	 */
	char str[5];
	switch (char_count) {
		case 1:
			strcpy(str, "A");
			break;
		case 2:
			strcpy(str, "Ah");
			break;
		case 3:
			strcpy(str, "Aho");
			break;
		default:
			strcpy(str, "Ahoj");
	}
	if (copy_to_user(buf, str, sizeof(str))) return -EFAULT;
	return char_count;
	
}

long my_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	/*
	 * Check if cmd belongs to my_device
	 */
	if (_IOC_TYPE(cmd) != MY_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) != 16 && _IOC_NR(cmd) != 17) return -ENOTTY;

	/*
	 * Copy argument from userspace
	 */
	if (cmd == MY_WRITE){
		if (arg < 1 || arg > 4)
			return -EINVAL;
		char_count = arg;
	}

	/*
	 * Copy current char_count to userspace buffer
	 */
	if (cmd == MY_READ){
		if (copy_to_user((void *) arg, &char_count, sizeof(int))) return -EFAULT;
	}  
	return 0;
}

struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.open = my_open,
	.write = my_write,
	.read = my_read,
	.release = my_release,
	.unlocked_ioctl = my_unlocked_ioctl,
};

struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "my_device",
	.fops = &my_fops,
};

static int my_init(void)
{
	misc_register(&misc);
	printk(KERN_INFO "ioctl GET cmd: %d\n", MY_READ);
	printk(KERN_INFO "ioctl SET cmd: %d\n", MY_WRITE);
	return 0;
}

static void my_exit(void)
{
	misc_deregister(&misc);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
