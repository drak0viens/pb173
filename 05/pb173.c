#include <linux/mm.h>
#include <linux/module.h>

static int my_init(void)
{
	/*
	u32 *p_virt = ioremap(0x000000003e7f0000, 8);
	int i;
	
	for (i = 0; i < 4; i++) {
		printk(KERN_INFO "%c\n", ((char*) p_virt)[i]);
	}

	printk(KERN_INFO "len: %d\n", p_virt[1]);

   	iounmap(p_virt);
	*/


	/* allocate free page */
	char *virt = __get_free_page(GFP_KERNEL);
	/* copy some string to page */
	strcpy(virt, "Example text");
	/* get physical address */
	phys_addr_t phys = virt_to_phys(virt);
	/* get page struct address */
	struct page *page = virt_to_page(virt);
	SetPageReserved(page);
	/* remap page */
	char * map = ioremap(phys, 12);

	printk(KERN_INFO "\n\nvirt: %pS \n", (void *) virt);
	printk(KERN_INFO "phys: %pS\n", phys);
	printk(KERN_INFO "page: %pS\n", (void *) page);
	printk(KERN_INFO "map: %pS\n", (void *) map);
	printk(KERN_INFO "pfn: %lu\n", page_to_pfn(page));
	printk(KERN_INFO "*virt: %s\n", virt);
	printk(KERN_INFO "*map: %s\n", map);
	
	/* rewrite mapped memory */
	strcpy(map, "Another text");
	
	printk(KERN_INFO "*virt: %s\n", virt);

	iounmap(map);
	ClearPageReserved(page);
	free_page((int) virt); 

	/* don't allow loading of the module */
	return -EIO;
}

static void my_exit(void)
{
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");


