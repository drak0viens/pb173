#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

#define REG_RAISED(bar) (bar + 0x0040)
#define REG_ENABLE(bar) (bar + 0x0044)
#define REG_RAISE(bar)  (bar + 0x0060)
#define REG_ACK(bar)	(bar + 0x0064)
#define REG_SRC(bar)    (bar + 0x0080)
#define REG_DST(bar)    (bar + 0x0084)
#define REG_COUNT(bar)  (bar + 0x0088)
#define REG_CMD(bar)    (bar + 0x008c)


/* COMBO card - 0x18ec:0xc058 */
struct pci_device_id my_table[] = {
{ PCI_DEVICE(0x18ec, 0xc058) }, { 0, }
};

/* list of pci_dev structs */
struct pdev_struct {
	struct pci_dev * pdev;
	struct list_head list;
};

/* help struct associated with pdev */
struct pdev_help_struct {
	void * virt; /* mapped bar 0 */
	void * dma_virt;
	dma_addr_t dma_phys;
	struct timer_list * timer; /* irq timer */
};

struct tasklet_struct * task = NULL;
void * dma_virt_addr = NULL;

MODULE_DEVICE_TABLE(pci, my_table);

void tasklet_fn(unsigned long data)
{
	printk(KERN_INFO "Running tasklet..\n");
	printk(KERN_INFO "%s \n", (char *) data);
}

static irqreturn_t my_handler(int irq, void * data, struct pt_regs * pt)
{
	int state;
        struct pdev_help_struct * p = (struct pdev_help_struct *) data;
	void * virt = p->virt;

	/* get register 0x0040 */
	state = readl(REG_RAISED(virt));
	printk(KERN_INFO "Register 0x0040: %x\n", state);
	
	if (!state)
		return IRQ_NONE;
	
	/* acknowledge interrupt */
	writel(state, REG_ACK(virt));

	if (irq == 0x8){
		printk(KERN_INFO "INT 8 \n");
		tasklet_init(task, tasklet_fn, (unsigned long) p->dma_virt + 0x14);
		tasklet_schedule(task);
		printk(KERN_INFO "Tasklet scheduled \n");
	}

	return IRQ_HANDLED;
}

static void raise_intr(unsigned long data)
{
	struct pdev_help_struct * p = (struct pdev_help_struct *) data;
	void * virt = p->virt;

	/* raise interrupt */
	writel(0x1000, REG_RAISE(virt));  
	
	 /* set timer */
	mod_timer(p->timer, jiffies + msecs_to_jiffies(100));
}

int my_probe(struct pci_dev * pdev, const struct pci_device_id *id)
{
	int err, cmd;
	void * virt;
	dma_addr_t dma_phys;
	struct timer_list * my_timer;
	struct pdev_help_struct *p;
	
	printk(KERN_INFO "[Probe]\n");
	printk(KERN_INFO "bus number: %2.x\n", pdev->bus->number);
	printk(KERN_INFO "slot: %2.x\n", PCI_SLOT(pdev->devfn));
	printk(KERN_INFO "func: %2.x\n", PCI_FUNC(pdev->devfn));

	task = kmalloc(sizeof(struct tasklet_struct),GFP_KERNEL); 
	if (!task){ 
		printk(KERN_INFO "Error: can not allocate memory for tasklet\n");  
		return -ENOMEM;
	}

	/* enable device */	
	err = pci_enable_device(pdev);
	if (err) { 
		printk(KERN_INFO "Error: device can not be enabled\n");  
		return -EIO;
	}
	
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (err) {
		printk(KERN_INFO "Error: device can not set dma mask\n");  
		return -EIO;
	}

	pci_set_master(pdev);

	/* request region */
	err = pci_request_region(pdev, 0, "my_resource");
	if (err){
		printk(KERN_INFO "Error: can not reserve PCI I/O and memory resource\n");  
	return -EBUSY;
	}
	
	/* map region 0 */
	virt = pci_ioremap_bar(pdev, 0);
	if (!virt) {
		printk(KERN_INFO "Error: can not remap bar 0\n");  
		return -ENOMEM;
	}

        my_timer = kmalloc(GFP_KERNEL, sizeof(*my_timer));
	if (!my_timer)
		return -ENOMEM;

	/* allocate pdev help structure */
	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		return -ENOMEM;
	}
	
	dma_virt_addr = dma_alloc_coherent(&pdev->dev, PAGE_SIZE, &dma_phys, GFP_KERNEL);
	if (!dma_virt_addr){
		printk(KERN_INFO "Error: can not allocate dma memory");
		return -ENOMEM;
	}

	strcpy(dma_virt_addr, "Hello,man!");
	
	/* transfer 1 - PCI -> PowerPC */
	/* src addr */
	writel((unsigned long) dma_phys, REG_SRC(virt));

	/* dst addr */
	writel(0x40000, REG_DST(virt));

	/* set transfer count */
	writel(10, REG_COUNT(virt));

	/* set command */
	cmd = 1 | 2 << 1 | 4 << 4 | 1 << 7;

	writel(cmd, REG_CMD(virt));

	while (cmd & 1)
		cmd = readl(REG_CMD(virt));

	/* transfer 2 - PowerPC -> PCI */
	/* src addr */
	writel(0x40000, REG_SRC(virt));

	/* dst addr */
	writel((unsigned long) dma_phys + 0xa, REG_DST(virt));

	/* set transfer count */
	writel(10, REG_COUNT(virt));

	/* set command */
	cmd = 1 | 4 << 1 | 2 << 4 | 1 << 7;

	writel(cmd, REG_CMD(virt));

	while (cmd & 1)
		cmd = readl(REG_CMD(virt));

	printk(KERN_INFO "%s\n", (char *) dma_virt_addr);

	/* enable interrupts */
	writel(0x100, REG_ENABLE(virt));
	
	/* transfer 3 - PowerPC -> PCI with interrupt */
	/* src addr */
	writel(0x40000, REG_SRC(virt));

	/* dst addr */
	writel((unsigned long) dma_phys + 0x14, REG_DST(virt));

	/* set transfer count */
	writel(10, REG_COUNT(virt));

	/* set command */
	cmd = 1 | 4 << 1 | 2 << 4;

	writel(cmd, REG_CMD(virt));

printk(KERN_INFO "DMA transfer with interrupt has been set.");

	/* set timer to raise interrupts periodicaly */
	setup_timer(my_timer, raise_intr, (unsigned long) p);

	/* store pointers to help struct */
	p->virt = virt;
	p->dma_virt = dma_virt_addr;
	p->dma_phys = dma_phys;
	p->timer = my_timer;
	
	/* associate help struct with current pci device */
	pci_set_drvdata(pdev, p);
	
	/* map irqs */
	err = request_irq(pdev->irq, my_handler, IRQF_SHARED, "my_irq",(void *) p);
	if (err) {
		printk(KERN_INFO "Error: can not map interrupts\n");
		return err;
	}
	
	/* enable interrupts */
	writel(0x1000, REG_ENABLE(virt));
	
	/* trigger timer */
	mod_timer(my_timer, jiffies + msecs_to_jiffies(100)); 
	return 0;
}

void my_remove(struct pci_dev * pdev) 
{	
	struct pdev_help_struct *p;
	
	printk(KERN_INFO "[Remove]\n");
	printk(KERN_INFO "bus number: %2.x\n", pdev->bus->number);
	printk(KERN_INFO "slot: %2.x\n", PCI_SLOT(pdev->devfn));
	printk(KERN_INFO "func: %2.x\n", PCI_FUNC(pdev->devfn));	

	if (task){
		tasklet_kill(task);		
		kfree(task);
		task = NULL;
	}

	/* get help struct associated with current pci device */
	p = pci_get_drvdata(pdev);
	
	/* stop timer */
	del_timer_sync(p->timer);
	if (p->timer)
		kfree(p->timer);
	
	/* unmap irqs */
	free_irq(pdev->irq, p);
	
	dma_free_coherent(&pdev->dev, PAGE_SIZE, p->dma_virt, p->dma_phys);
  
	/* unmap memory, release region and disable device */
	iounmap(p->virt);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	kfree(p);
}

int my_mmap(struct file *filp, struct vm_area_struct *vma){
	unsigned long pgoff;
	pgoff = vma->vm_pgoff;

	if((vma->vm_end - vma->vm_start) > PAGE_SIZE)
		return -EINVAL;
	
	if ((vma->vm_flags & (VM_WRITE | VM_READ)) != VM_READ)
		return -EINVAL;

	if (!dma_virt_addr)
		return -EBUSY;

	return remap_pfn_range(vma, vma->vm_start, virt_to_phys(dma_virt_addr) >> PAGE_SHIFT, PAGE_SIZE, vma->vm_page_prot);
	
	return 0;
}

struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.mmap = my_mmap,
};

struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "my_device",
	.fops = &my_fops,
};

struct pci_driver my_driver = {
	.name = "my_driver",
	.id_table = my_table,
	.probe = my_probe,
	.remove = my_remove,
};


static int my_init(void)
{
	misc_register(&misc);

	/* register my_driver */
	return pci_register_driver(&my_driver);
}

static void my_exit(void)
{
	misc_deregister(&misc);

	/* unregister my_driver */
	pci_unregister_driver(&my_driver);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
