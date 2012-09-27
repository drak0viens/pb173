#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include "pb173.h"

int value = 23;

static int my_init(void){
	void *mem = kmalloc(100, GFP_KERNEL);
	if (!mem) {
		printk(KERN_INFO "Memory allocation error\n");
	} else {
		printk(KERN_INFO "%s %p\n", "kmalloc variable address: ", mem);
		kfree(mem);
	}
	printk(KERN_INFO "%s %p\n", "stack variable address: ", &value);
	printk(KERN_INFO"%s %p\n", "jiffies variable address: ", &jiffies);
	printk(KERN_INFO "%s %p\n", "my_init function address: ", my_init);
	printk(KERN_INFO "%s %p\n", "bus_register function address: ",
	       bus_register);
	printk(KERN_INFO "%s %pF\n", "__builtin_return_address(0): ",
		       __builtin_return_address(0));
	return 0;
}

static void my_exit(void){
	char *str1 = "Bye";
	void *mem = kmalloc(1000, GFP_KERNEL);
	if (!mem) {
		printk(KERN_INFO "Memory allocation error\n");
	} else {
		strcpy(mem, str1);
		printk(KERN_INFO "%s", ((char *) mem));
		kfree(mem);
	}
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
