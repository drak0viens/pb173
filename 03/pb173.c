#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
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
u32 count = 0;
struct dentry * debug_dir;
struct dentry * counter_file;
struct dentry * bindata_file;

int my_open(struct inode *inode, struct file *filp){
	printk(KERN_INFO "Opening device..\n");
	count++;
	return 0;
}

int my_release(struct inode *inode, struct file *filp){
	printk(KERN_INFO "Releasing device..\n");
	count--;
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

/* read file operation */
static ssize_t myreader(struct file *fp, char __user *user_buffer, size_t count, loff_t *position) 
{ 
	return simple_read_from_buffer(user_buffer, count, position, THIS_MODULE->module_core,
				       THIS_MODULE->core_text_size);
} 

static const struct file_operations fops_debug = { 
        .read = myreader,
}; 

static int my_init(void)
{
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, THIS_MODULE->module_core,THIS_MODULE->core_text_size);
	misc_register(&misc);
	printk(KERN_INFO "ioctl GET cmd: %d\n", MY_READ);
	printk(KERN_INFO "ioctl SET cmd: %d\n", MY_WRITE);
	debug_dir = debugfs_create_dir("pb173", NULL);
	counter_file = debugfs_create_u32("counter", S_IRUSR, debug_dir, &count);
	bindata_file = debugfs_create_file("bindata", S_IRUSR, debug_dir, THIS_MODULE->module_core,
					   &fops_debug);
	return 0;
}

static void my_exit(void)
{
	misc_deregister(&misc);
	debugfs_remove(bindata_file);
	debugfs_remove(counter_file);
	debugfs_remove(debug_dir);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
