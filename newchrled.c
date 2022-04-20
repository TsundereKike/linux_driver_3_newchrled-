#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define NEWCHRLED_NAME      "newchrled"
#define NEWCHRLED_COUNT     1
struct newchrled_dev
{
    struct cdev led_cdev;   /*字符设备*/
    dev_t devid;            /*设备号*/
    struct class *class;    /*类*/
    struct device *device;  /*设备*/
    int major;              /*主设备号*/
    int minor;              /*次设备号*/

};
struct newchrled_dev newchrled;/*led设备*/

/*寄存器物理地址*/
#define CCM_CCGR1_BASE          (0x020c406c)
#define SW_MUX_GPIO1_IO03_BASE  (0x020e0068)
#define SW_PAD_GPIO_IO03_BASE   (0x020e02f4)
#define GPIO1_DR_BASE           (0x0209c000)
#define GPIO1_GDIR_BASE         (0x0209c004)

#define LED_OFF         0
#define LED_ON          1

/*映射后的寄存器虚拟地址指针*/
static void __iomem *IMX6U_CCM_CCGR1;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;

static void newchrled_switch(unsigned char sta)
{
    int val = 0;
    switch (sta)
    {
    case LED_OFF:
        /*关闭LED*/
        val = readl(GPIO1_DR);
        val |= (1<<3);
        writel(val,GPIO1_DR);
        break;
    case LED_ON:
        /*打开LED*/
        val = readl(GPIO1_DR);
        val &= ~(1<<3);
        writel(val,GPIO1_DR);
        break;
    default:
        break;
    }
}
static int newchrled_open(struct inode *inode,struct file *filp)
{
    filp->private_data = &newchrled;
    return 0;
}
static int newchrled_release(struct inode *inode,struct file *filp)
{
    filp->private_data = NULL;
    return 0;
}
static ssize_t newchrled_write(struct file *filp,const char __user *buf,
                                size_t count, loff_t *ppos)
{
    int ret = 0;
    struct newchrled *led_dev = (struct newchrled *)filp->private_data;
    unsigned char databuf[1];
    ret = copy_from_user(databuf,buf,count);
    if(ret<0)
    {
        printk("kernel write failed\r\n");
        return -EFAULT;
    }
    newchrled_switch(databuf[0]);
    return 0;
}
static const struct file_operations newchrled_fops =
{
    .owner = THIS_MODULE,
    .open = newchrled_open,
    .release = newchrled_release,
    .write = newchrled_write,

};
/*入口*/
static int __init newchrled_init(void)
{
    int ret = 0;
    int val = 0;
    /*初始化LED*/
    IMX6U_CCM_CCGR1 = ioremap(CCM_CCGR1_BASE,4);
    SW_MUX_GPIO1_IO03 = ioremap(SW_MUX_GPIO1_IO03_BASE,4);
    SW_PAD_GPIO1_IO03 = ioremap(SW_PAD_GPIO_IO03_BASE,4);
    GPIO1_DR = ioremap(GPIO1_DR_BASE,4);
    GPIO1_GDIR = ioremap(GPIO1_GDIR_BASE,4);

    val = readl(IMX6U_CCM_CCGR1);
    val &= ~(3<<26);
    val |= (3<<26);
    writel(val,IMX6U_CCM_CCGR1);

    writel(0x05,SW_MUX_GPIO1_IO03);

    writel(0x10b0,SW_PAD_GPIO1_IO03);

    val = readl(GPIO1_GDIR);
    val &= ~(1<<3);
    val |= (1<<3);
    writel(val,GPIO1_GDIR);

    val = readl(GPIO1_DR);
    val |= (1<<3);
    writel(val,GPIO1_DR);

    /*注册字符设备*/
    newchrled.major = 0;/*表示由系统申请设备号*/
    if(newchrled.major)/*给定主设备号*/
    {
        newchrled.devid = MKDEV(newchrled.major,0);
        ret = register_chrdev_region(newchrled.devid,NEWCHRLED_COUNT,NEWCHRLED_NAME);
    }
    else/*没有给定主设备号*/
    {
        ret = alloc_chrdev_region(&newchrled.devid,0,NEWCHRLED_COUNT,NEWCHRLED_NAME);
        newchrled.major = MAJOR(newchrled.devid);
        newchrled.minor = MINOR(newchrled.devid);
    }
    if(ret<0){
        printk("newchrled chrdev_region err!\r\n");
        goto fail_devid;
    }
    printk("newchrled major=%d,minor=%d\r\n",newchrled.major,newchrled.minor);

    /*添加字符设备*/
    newchrled.led_cdev.owner = THIS_MODULE;
    cdev_init(&newchrled.led_cdev,&newchrled_fops);
    ret = cdev_add(&newchrled.led_cdev,newchrled.devid,NEWCHRLED_COUNT);
    if(ret<0)
    {
        goto fail_cdev_add;
    }
    /*自动创建设备节点*/
    newchrled.class = class_create(THIS_MODULE,NEWCHRLED_NAME);
    if(IS_ERR(newchrled.class))
    {
        ret = PTR_ERR(newchrled.class);
        goto fail_class_create;
    }
    newchrled.device = device_create(newchrled.class,NULL,newchrled.devid,NULL,NEWCHRLED_NAME);
    if(IS_ERR(newchrled.device))
    {
        ret = PTR_ERR(newchrled.device);
        goto fail_device_create;
    }    

    printk("newchrled init\r\n");
    return 0;

fail_device_create:
    /*删除类*/
    class_destroy(newchrled.class);
fail_class_create:
    /*删除字符设备*/
    cdev_del(&newchrled.led_cdev);
fail_cdev_add:
    /*注销设备号*/
    unregister_chrdev_region(newchrled.devid,NEWCHRLED_COUNT);
fail_devid:
    return ret;
}
/*出口*/
static void __exit newchrled_exit(void)
{
    /*取消映射*/
    iounmap(IMX6U_CCM_CCGR1);
    iounmap(SW_MUX_GPIO1_IO03);
    iounmap(SW_PAD_GPIO1_IO03);
    iounmap(GPIO1_GDIR);
    iounmap(GPIO1_DR);
    /*删除字符设备*/
    cdev_del(&newchrled.led_cdev);
    /*注销设备号*/
    unregister_chrdev_region(newchrled.devid,NEWCHRLED_COUNT);
    /*删除设备*/
    device_destroy(newchrled.class,newchrled.devid);
    /*删除类*/
    class_destroy(newchrled.class);
    printk("newchrled exit\r\n");
}
/*注册和卸载驱动*/
module_init(newchrled_init);
module_exit(newchrled_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("tanminghang");


