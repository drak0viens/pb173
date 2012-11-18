#include <linux/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/list.h>

/* COMBO card - 0x18ec:0xc058 */
struct pci_device_id my_table[] = {
{ PCI_DEVICE(0x18ec, 0xc058) }, { 0, }
};

/* list of pci_dev structs */
struct pdev_struct {
    struct pci_dev * pdev;
    struct list_head list;
};

LIST_HEAD(pdev_list);

MODULE_DEVICE_TABLE(pci, my_table);

int my_probe(struct pci_dev * pdev, const struct pci_device_id *id)
{
    void * virt;
    u32 info_low, info_up;
    printk(KERN_INFO "[Probe]\n");
    printk(KERN_INFO "bus number: %2.x\n", pdev->bus->number);
    printk(KERN_INFO "slot: %2.x\n", PCI_SLOT(pdev->devfn));
    printk(KERN_INFO "func: %2.x\n", PCI_FUNC(pdev->devfn));

    /* enable device, request region and print phys address */    
    pci_enable_device(pdev);
    pci_request_region(pdev, 0, "my_resource");
    printk(KERN_INFO "phys addr bar 0: %llx\n", pci_resource_start(pdev, 0));
    
    /* map region 0 */
    virt = pci_ioremap_bar(pdev, 0);
    /* register structure */
    pci_set_drvdata(pdev, virt);

    /* read 8 bytes from region 0 */
    info_low = readw(virt);
    info_up = readw(((char*) virt) + 4);
 
    info_low = be32_to_cpu(info_low);
    info_up = be32_to_cpu(info_up);
    
    /* decode info */
    printk(KERN_INFO "Bridge revision:\n");
    printk(KERN_INFO "Major revision - %x\n", ((char*) &info_low)[2]);
    printk(KERN_INFO "Minor revision - %x\n", ((char*) &info_low)[3]); 
  
    printk(KERN_INFO "Bridge built time:\n");
    printk(KERN_INFO "Year (+2k) - %d\n", (((char*) &info_up)[0] & 0x000000f0) >> 4);
    printk(KERN_INFO "Month - %d\n", ((char*) &info_up)[0] & 0x0000000f);
    printk(KERN_INFO "Day - %d\n", ((char*) &info_up)[1]);
    printk(KERN_INFO "Time - %d:%d\n", ((char*) &info_up)[2], ((char*) &info_up)[3]);
    
    return 0;
}

void my_remove(struct pci_dev * pdev) 
{
    printk(KERN_INFO "[Remove]\n");
    printk(KERN_INFO "bus number: %2.x\n", pdev->bus->number);
    printk(KERN_INFO "slot: %2.x\n", PCI_SLOT(pdev->devfn));
    printk(KERN_INFO "func: %2.x\n", PCI_FUNC(pdev->devfn));    

    /* unmap memory, release region and disable device */
    iounmap(pci_get_drvdata(pdev));
    pci_release_region(pdev, 0);
    pci_disable_device(pdev);
    return ;
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

    /* iterate all pci devices */
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

        /* no match found - device was hotplugged in */
        if (!found){
            /* print vendorID:deviceID */
            printk(KERN_INFO "%2.x:%2.x\n", pdev->vendor, pdev->device);     
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