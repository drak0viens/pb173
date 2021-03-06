#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

/* Define unique ioctl commands */
#define MY_IOC_MAGIC '$'
#define MY_READ      _IOR(MY_IOC_MAGIC, 16, int)
#define MY_WRITE     _IOW(MY_IOC_MAGIC, 17, int)

/* Global variables */
int char_count = 4;
int writers = 0;
u32 count = 0;
char buffer[128];
char* mem_buffer;

/* Debugfs files */
struct dentry * debug_dir;
struct dentry * counter_file;
struct dentry * bindata_file;

/* Locks */
DEFINE_MUTEX(buf_mutex);
DEFINE_SPINLOCK(writers_lock);
DEFINE_SPINLOCK(char_lock);
DEFINE_SPINLOCK(count_lock);

int my_open(struct inode *inode, struct file *filp){
	printk(KERN_INFO "Opening device..\n");
	/* increase count of opened instances */
	spin_lock_irq(&count_lock);
	count++;
	spin_unlock_irq(&count_lock);
	return 0;
}

int my_release(struct inode *inode, struct file *filp){
	printk(KERN_INFO "Releasing device..\n");
	/* decrease count of opened instances */
	spin_lock_irq(&count_lock);
	count--;
	spin_unlock_irq(&count_lock);
	return 0;
}

ssize_t my_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp){
	/* Write buffer from userspace as string with printk */
	char* str = (char*) kmalloc(count + sizeof(char), GFP_KERNEL);
	if (copy_from_user(str, buf, count)) return -EFAULT;
	str[count] = '\0';
	printk(KERN_INFO "%s", str);
	kfree(str);
	return count;
}

ssize_t my_read(struct file *filp, char __user *buf, size_t count, loff_t *offp){
	/* Copy string to userspace */
	char str[5];
	int chars_to_ret;

	spin_lock_irq(&char_lock);
	chars_to_ret = char_count;
	spin_unlock_irq(&char_lock);

	switch (chars_to_ret) {
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
	return chars_to_ret;	
}

/* hw 02 - ioctl */
long my_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	/* Check if cmd belongs to my_device */
	if (_IOC_TYPE(cmd) != MY_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) != 16 && _IOC_NR(cmd) != 17) return -ENOTTY;

	/* Copy argument from userspace */
	if (cmd == MY_WRITE){
		if (arg < 1 || arg > 4)
			return -EINVAL;
		
		spin_lock_irq(&char_lock);
		char_count = arg;
		spin_unlock_irq(&char_lock);
	}

	/* Copy current char_count to userspace buffer */
	if (cmd == MY_READ){
		int char_count_cpy;

		spin_lock_irq(&char_lock);
		char_count_cpy = char_count;  	
		spin_unlock_irq(&char_lock); 

		if (copy_to_user((void *) arg, &char_count_cpy, sizeof(int)))
			return -EFAULT;
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

/* hw 03 - read debug file operation */
static ssize_t myreader(struct file *fp, char __user *user_buffer, size_t count, loff_t *position) 
{ 
	return simple_read_from_buffer(user_buffer, count, position, THIS_MODULE->module_core,
				       THIS_MODULE->core_text_size);
} 

/* debug file operations */
static const struct file_operations fops_debug = { 
        .read = myreader,
}; 

/* hw 04 - read device */
static ssize_t buf_read(struct file *filp, char __user *buf, size_t count,
                loff_t *off)
{
        if (*off > 127 || !count)
                return 0;

	mutex_lock(&buf_mutex);
	count = simple_read_from_buffer(buf, count, off, buffer, 128);
	mutex_unlock(&buf_mutex);
	
	return count;
	}

/* read device file ops */
static struct file_operations buf_read_fops = {
        .owner = THIS_MODULE,
        .read = buf_read,
};

static struct miscdevice my_read_misc = {
        .minor = MISC_DYNAMIC_MINOR,
        .fops = &buf_read_fops,
        .name = "my_read_device",
};

/* hw 04 - write device */
int my_write_open(struct inode *inode, struct file *filp){
	/* allow only one writer */
	if (filp->f_mode & FMODE_WRITE) {
		int dev_unavailable = 0;
		
		spin_lock_irq(&writers_lock);
		dev_unavailable = writers;
		if (!dev_unavailable) {
			writers++;
		}	
		spin_unlock_irq(&writers_lock);
		
		if (dev_unavailable)
				return -EBUSY;
	}
	return 0;
}

int my_write_release(struct inode *inode, struct file *filp){
	/* decrease count of writers */
	if (filp->f_mode & FMODE_WRITE) {
                spin_lock_irq(&writers_lock);
                writers--;
                spin_unlock_irq(&writers_lock);
	}
	return 0;
}

static ssize_t buf_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *off)
{
	char cpy_buffer[5];
	int i;
	
	/* check offset */
	if (*off > 127)
		return 0;
	/* check count + offset */
	if (*off + count > 127)
		count = 127 - *off;
	/* write at most 5 bytes */
	if (count > 5)
		count = 5;	
	/* copy data from userspace */
	if (copy_from_user(cpy_buffer, buf, count))
		return -EFAULT;

	mutex_lock(&buf_mutex);
	for (i = 0; i < count; i++) {
		/* write one byte and take a nap */
		buffer[i + *off] = cpy_buffer[i];
		msleep(20);
	}
	mutex_unlock(&buf_mutex);
	
	*off += count;
	return count;
	}

/* write device file ops */
static struct file_operations buf_write_fops = {
        .owner = THIS_MODULE,
        .open = my_write_open,
	.write = buf_write,
	.release = my_write_release,
};

static struct miscdevice my_write_misc = {
        .minor = MISC_DYNAMIC_MINOR,
        .fops = &buf_write_fops,
        .name = "my_write_device",
};

/* hw 06 - read device */
static ssize_t mem_buf_read(struct file *filp, char __user *buf, size_t count,
                loff_t *off)
	{
	return simple_read_from_buffer(buf, count, off, mem_buffer, 20971520);
	}

/* read device file ops */
static struct file_operations mem_buf_read_fops = {
        .owner = THIS_MODULE,
        .read = mem_buf_read,
};

static struct miscdevice mem_buf_read_misc = {
        .minor = MISC_DYNAMIC_MINOR,
        .fops = &mem_buf_read_fops,
        .name = "my_mem_read_device",
};

/* hw 06 - write device */
static ssize_t mem_buf_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *off) 
	{
	return simple_write_to_buffer(mem_buffer, 20971520, off, buf, count);
	}

/* write device file ops */
static struct file_operations mem_buf_write_fops = {
        .owner = THIS_MODULE,
	.write = mem_buf_write,
};

static struct miscdevice mem_buf_write_misc = {
        .minor = MISC_DYNAMIC_MINOR,
        .fops = &mem_buf_write_fops,
        .name = "my_mem_write_device",
};

static int my_init(void)
{
	/* print hex dump of module */
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, THIS_MODULE->module_core,THIS_MODULE->core_text_size);

	misc_register(&misc);	
	misc_register(&my_read_misc);
	misc_register(&my_write_misc);
	misc_register(&mem_buf_read_misc);
	misc_register(&mem_buf_write_misc);

	printk(KERN_INFO "ioctl GET cmd: %d\n", MY_READ);
	printk(KERN_INFO "ioctl SET cmd: %d\n", MY_WRITE);

	/* create debug directory */
	debug_dir = debugfs_create_dir("pb173", NULL);
	/* create u32 debug file mapped to global var count */
	counter_file = debugfs_create_u32("counter", S_IRUSR, debug_dir, &count);
	/* create debug file containing binary data of this module */
	bindata_file = debugfs_create_file("bindata", S_IRUSR, debug_dir, THIS_MODULE->module_core,
					   &fops_debug);

	/* allocate buffer and zero memory */
	mem_buffer = (char*) vzalloc(20971520);
	if (!mem_buffer) {
		printk(KERN_INFO "20MB allocation error..");
		return -ENOMEM;
	}

	unsigned long i, pfn;

	/* iterate allocated pages */	
	for (i = 0; i < 20971520; i += PAGE_SIZE) {

		/* get pfn */
		pfn = vmalloc_to_pfn(&(mem_buffer[i]));
		/* get phys address */
		phys_addr_t phys = pfn << PAGE_SHIFT;
		/* write string <Virt> : <Phys>\n to current page */
		snprintf(&(mem_buffer[i]), PAGE_SIZE, "%p : %llx\n", &(mem_buffer[i]), phys);		
	}

	return 0;
}

static void my_exit(void)
{
	misc_deregister(&misc);
	misc_deregister(&my_read_misc);
        misc_deregister(&my_write_misc);
       	misc_deregister(&mem_buf_read_misc);
	misc_deregister(&mem_buf_write_misc);

	/* remove debug files and directory */
	debugfs_remove(bindata_file);
	debugfs_remove(counter_file);
	debugfs_remove(debug_dir);

	/* deallocate buffer */
	if (mem_buffer) vfree((void*) mem_buffer);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
