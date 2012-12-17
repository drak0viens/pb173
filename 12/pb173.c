#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/etherdevice.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/kfifo.h>
#include "packet.c"

/* Global variables */
struct net_device * ndev = NULL;
struct timer_list * my_timer = NULL;
struct sk_buff * skb = NULL;

DECLARE_KFIFO(queue, char, 8192);

int my_ndev_open(struct net_device * dev)
{
	printk(KERN_INFO "My_open netdev..\n");
	/* iface up - set timer */
	mod_timer(my_timer, jiffies + msecs_to_jiffies(1000));
	return 0;
}

int my_ndev_stop(struct net_device * dev)
{
	printk(KERN_INFO "My_stop netdev..\n");
	/* iface down - stop timer */
	del_timer_sync(my_timer);
	return 0;
}

netdev_tx_t my_start_xmit(struct sk_buff * skb, struct net_device * dev)
{
	printk(KERN_INFO "My_start_xmit netdev..\n");
	/* hexdump packet data */
	print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, skb->data, skb->len);
	/* copy sk_buff to queue */
	kfifo_in(&queue, (char *) &skb, sizeof(struct sk_buff));
	/* free sk_buff */
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

int my_change_mtu(struct net_device * dev, int new_mtu)
{
	printk(KERN_INFO "My_change_mtu netdev..\n");
	return 0;
}

static void timer_fn(unsigned long data)
{
	struct sk_buff * pack;
	/* allocate sk_buff */
	pack = netdev_alloc_skb(ndev, my_packet_size);
	/* set data and len */
	pack->data = my_packet;
	pack->len = my_packet_size;
	/* set protocol type */
 	pack->protocol = eth_type_trans(pack, ndev);
	/* send packet */
	netif_rx(pack);
	/* increment 24.char in packet and set timer */
	my_packet[23]++;
	mod_timer(my_timer, jiffies + msecs_to_jiffies(1000));
}

struct net_device_ops ndev_ops = {
	.ndo_open = my_ndev_open,
	.ndo_stop = my_ndev_stop,
	.ndo_start_xmit = my_start_xmit,
	.ndo_change_mtu = my_change_mtu,
};

ssize_t my_write(struct file *filp, const char __user *buf, size_t count, loff_t *offp){
	char* data = (char*) kmalloc(count, GFP_KERNEL);
	/* copy data from user */
	if (copy_from_user(data, buf, count)) return -EFAULT;
	/* send data directly */
	netif_rx((struct sk_buff *) data);
	kfree(data);
	return 0;
}

ssize_t my_read(struct file *filp, char __user *buf, size_t count, loff_t *offp){
	int bytes = 0;
	/* copy content of queue to user */
	kfifo_to_user(&queue, buf, count, &bytes);
	return bytes;
}


struct file_operations my_fops = {
	.owner = THIS_MODULE,
	.write = my_write,
	.read = my_read,
};

struct miscdevice misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "my_device",
	.fops = &my_fops,
};

static int my_init(void)
{
	int err;
	/* init fifo */
	INIT_KFIFO(queue);
	/* register device */
	misc_register(&misc);
	/* alloc device */
	ndev = alloc_etherdev(sizeof(struct timer_list));
 	if (!ndev){
		printk(KERN_INFO "Can not allocate ethdev..");
		return -EIO;
	}
	/* set net device ops */
	ndev->netdev_ops = &ndev_ops;
	/* set mac addr */
	random_ether_addr(ndev->dev_addr);
	/* get private timer */
	my_timer = netdev_priv(ndev);
	setup_timer(my_timer, timer_fn, 0);
	/* register netdevice */
	err = register_netdev(ndev);
	if (err){
		printk(KERN_INFO "Can not register netdevice..\n");
		return -EIO;		
	}
	
	return 0;
}

static void my_exit(void)
{
	misc_deregister(&misc);
	unregister_netdev(ndev);
	free_netdev(ndev);
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
