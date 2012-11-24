#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/ioport.h>

#define REG_RAISED 0x0040
#define REG_ENABLE 0x0044
#define REG_RAISE  0x0060
#define REG_ACK    0x0064

void * my_data = NULL;
struct resource * combo_port_raised = NULL;
struct resource * combo_port_enable = NULL;
struct resource * combo_port_raise = NULL;
struct resource * combo_port_ack = NULL;

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
    void * virt;
    char * my_data;
    struct timer_list * timer;
};

LIST_HEAD(pdev_list);

MODULE_DEVICE_TABLE(pci, my_table);

/* TODO
static irqreturn_t my_handler(int irq, void * data, struct pt_regs * p)
*/
static irqreturn_t my_handler(int irq, void * data)
{
    int state;
    
    /* get register 0x0040 */
    state = inl(REG_RAISED);
    printk(KERN_INFO "Reg 0x0040: %x\n", state);
    
    if (!state)
        return IRQ_NONE;
    
    /* acknowledge interrupt */
    outl(0x1000, REG_ACK);
    return IRQ_HANDLED;
}

static void raise_intr(unsigned long data)
{
     /* raise interrupt */
     if (combo_port_raise) {
         outl(0x1000, REG_RAISE);  
     }
     
     /* set timer */
     if (data)
         mod_timer((struct timer_list *) data, jiffies + msecs_to_jiffies(100));
}

int my_probe(struct pci_dev * pdev, const struct pci_device_id *id)
{
    int err;
    void * virt;
    struct timer_list my_timer;
    struct pdev_help_struct *p;
    u32 info_low, info_up;
    printk(KERN_INFO "[Probe]\n");
    printk(KERN_INFO "bus number: %2.x\n", pdev->bus->number);
    printk(KERN_INFO "slot: %2.x\n", PCI_SLOT(pdev->devfn));
    printk(KERN_INFO "func: %2.x\n", PCI_FUNC(pdev->devfn));

    /* enable device, request region and print phys address */    
    err = pci_enable_device(pdev);
    if (err) {
        printk(KERN_INFO "Error: device can not be enabled\n");  
        return -EIO;
    }
    
    err = pci_request_region(pdev, 0, "my_resource");
    if (err){
        printk(KERN_INFO "Error: can not reserve PCI I/O and memory resource\n");  
	return -EBUSY;
    }
    
    printk(KERN_INFO "phys addr bar 0: %llx\n", pci_resource_start(pdev, 0));
    
    /* map region 0 */
    virt = pci_ioremap_bar(pdev, 0);
    if (!virt) {
	printk(KERN_INFO "Error: can not remap memory\n");  
	return -ENOMEM;
    }

    /* read 8 bytes from region 0 */
    info_low = readl(virt);
    info_up = readl((((char*) virt) + 4));

    /* decode info */
    printk(KERN_INFO "Bridge revision:\n");
    printk(KERN_INFO "Major revision - %x\n", ((char*) &info_low)[2]);
    printk(KERN_INFO "Minor revision - %x\n", ((char*) &info_low)[3]); 
 
    printk(KERN_INFO "Bridge built time:\n");
    printk(KERN_INFO "Year (+2k) - %d\n", (((char*) &info_up)[0] & 0x000000f0) >> 4);
    printk(KERN_INFO "Month - %d\n", ((char*) &info_up)[0] & 0x0000000f);
    printk(KERN_INFO "Day - %d\n", ((char*) &info_up)[1]);
    printk(KERN_INFO "Time - %d:%d\n", ((char*) &info_up)[2], ((char*) &info_up)[3]);
    
    /* allocate ports */
    combo_port_raised = request_region(REG_RAISED, 1, "combo card");
    combo_port_enable = request_region(REG_ENABLE, 1, "combo card");
    combo_port_raise = request_region(REG_RAISE, 1, "combo card");
    combo_port_ack = request_region(REG_ACK, 1, "combo card");
    
    if (!combo_port_raised || !combo_port_enable || !combo_port_raise || !combo_port_ack)
        return -EBUSY;
    
    my_data = kmalloc(GFP_KERNEL, 1);
    if (!my_data)
         return -ENOMEM;
    
    /* set timer to raise interrupts periodicaly */
    setup_timer(&my_timer, raise_intr,(unsigned long) &my_timer);
    mod_timer(&my_timer, jiffies + msecs_to_jiffies(100));
    
    /* allocate pdev help structure */
    p = kmalloc(sizeof(*p), GFP_KERNEL);
    if (!p) {
        return -ENOMEM;
    }
    
    /* store pointers to help struct */
    p->virt = virt;
    p->my_data = my_data;
    p->timer = &my_timer;
    
    /* associate help struct with current pci device */
    pci_set_drvdata(pdev, p);
    
    /* map irqs */
    err = request_irq(pdev->irq, my_handler, IRQF_SHARED, "my_irq", my_data);
    if (err) {
        printk(KERN_INFO "Error: can not map interrupts\n");
	return err;
    }
    
    /* enable interrupts */
    outl(0x1000, REG_ENABLE); 
    return 0;
}

void my_remove(struct pci_dev * pdev) 
{
    /* get help struct associated with current pci device */
    struct pdev_help_struct *p;
    p = pci_get_drvdata(pdev);
    
    /* stop timer */
    del_timer_sync(p->timer);
  
    /* unmap irqs */
    free_irq(pdev->irq, p->my_data);
  
    if (p->my_data)
        kfree(p->my_data);
    
    /* deallocate ports */
    release_region(REG_RAISED, 1);
    release_region(REG_ENABLE, 1);
    release_region(REG_RAISE, 1);
    release_region(REG_ACK, 1);
    
    printk(KERN_INFO "[Remove]\n");
    printk(KERN_INFO "bus number: %2.x\n", pdev->bus->number);
    printk(KERN_INFO "slot: %2.x\n", PCI_SLOT(pdev->devfn));
    printk(KERN_INFO "func: %2.x\n", PCI_FUNC(pdev->devfn));    

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
    struct pci_dev * pdev = NULL;
    
    /* initialize list */
    INIT_LIST_HEAD(&pdev_list);

    /* iterate all pci devices */
    while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev))){
        struct pdev_struct *p;
        p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
	    return -EIO;
	}
        /* increase reference counter */
        p->pdev = pci_dev_get(pdev);
        /* add device struct to list */
        list_add(&p->list, &pdev_list);
    }
    
    /* register my_driver */
    return pci_register_driver(&my_driver);
}

static void my_exit(void)
{
    struct pdev_struct *p, *p1;
    struct pci_dev * pdev = NULL;

    /* iterate available pci devices in system */
    while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev))){
        bool found = false;
        
        /* try to find match in list (domain:bus:slot.func) */
        list_for_each_entry(p, &pdev_list, list){
            if (pci_domain_nr(pdev->bus) == pci_domain_nr(p->pdev->bus)
                && pdev->bus->number == p->pdev->bus->number
                && PCI_SLOT(pdev->devfn) == PCI_SLOT(p->pdev->devfn) 
                && PCI_FUNC(pdev->devfn) == PCI_FUNC(p->pdev->devfn)) 
            {
                    found = true;
            } 
        }

        /* no match found - device was plugged in */
        if (!found){
            /* print vendorID:deviceID */
            printk(KERN_INFO "%2.x:%2.x\n", pdev->vendor, pdev->device);     
        }
    }    
        
    /* iterate listed pci devices */
    list_for_each_entry(p, &pdev_list, list){
        bool found = false;    
      
	/* try to find match in system (domain:bus:slot.func) */
        while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev))){
	    if (pci_domain_nr(pdev->bus) == pci_domain_nr(p->pdev->bus)
                && pdev->bus->number == p->pdev->bus->number
                && PCI_SLOT(pdev->devfn) == PCI_SLOT(p->pdev->devfn) 
                && PCI_FUNC(pdev->devfn) == PCI_FUNC(p->pdev->devfn)) 
            {
                    found = true;
            }
	
	/* no match found - device was removed */
        if (!found){
            /* print vendorID:deviceID */
            printk(KERN_INFO "%2.x:%2.x\n", pdev->vendor, pdev->device);     
           }
	}
    } 
   
  
   list_for_each_entry_safe(p, p1, &pdev_list, list){
        /* decrease reference counter */
        pci_dev_put(p->pdev);
        /* remove item from list */
        list_del(&p->list);
        kfree(p);
   }
 
   /* unregister my_driver */
   pci_unregister_driver(&my_driver);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
