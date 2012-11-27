#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/timer.h>

#define REG_RAISED(virt) (((char*) virt) + 0x0040)
#define REG_ENABLE(virt) (((char*) virt) + 0x0044)
#define REG_RAISE(virt)  (((char*) virt) + 0x0060)
#define REG_ACK(virt)	 (((char*) virt) + 0x0064)

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
	struct timer_list * timer; /* irq timer */
};

MODULE_DEVICE_TABLE(pci, my_table);

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
	writel(0x1000, REG_ACK(virt));
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
	int err;
	void * virt;
	struct timer_list * my_timer;
	struct pdev_help_struct *p;
	
	printk(KERN_INFO "[Probe]\n");
	printk(KERN_INFO "bus number: %2.x\n", pdev->bus->number);
	printk(KERN_INFO "slot: %2.x\n", PCI_SLOT(pdev->devfn));
	printk(KERN_INFO "func: %2.x\n", PCI_FUNC(pdev->devfn));

	/* enable device */	
	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_INFO "Error: device can not be enabled\n");  
		return -EIO;
	}
	
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
	
	/* set timer to raise interrupts periodicaly */
	setup_timer(my_timer, raise_intr, (unsigned long) p);

	/* store pointers to help struct */
	p->virt = virt;
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

	/* get help struct associated with current pci device */
	p = pci_get_drvdata(pdev);
	
	/* stop timer */
	del_timer_sync(p->timer);
	if (p->timer)
		kfree(p->timer);
	
	/* unmap irqs */
	free_irq(pdev->irq, p);
  
	/* unmap memory, release region and disable device */
	iounmap(p->virt);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	kfree(p);
}

struct pci_driver my_driver = {
.name = "my_driver",
.id_table = my_table,
.probe = my_probe,
.remove = my_remove,
};


static int my_init(void)
{
	/* register my_driver */
	return pci_register_driver(&my_driver);
}

static void my_exit(void)
{
	/* unregister my_driver */
	pci_unregister_driver(&my_driver);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
