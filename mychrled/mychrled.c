#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>

#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

//寄存器物理地址
#define CCM_CCGR1_BASE         (0X020C406C) //GPIO 时钟控制寄存器
#define SW_MUX_GPIO1_IO03_BASE (0X020E0068) //引脚复用功能设置 将其复用为GPIO
#define SW_PAD_GPIO1_IO03_BASE (0X020E02F4) //设置GPIO属性 上下拉 速度等
#define GPIO1_GDIR_BASE        (0X0209C004) //方向寄存器 设置GPIO为输入 还是输出
#define GPIO1_DR_BASE          (0X0209C000) //数据寄存器 获取IO口数据

//映射后的寄存器地址指针
static void __iomem *IMX6U_CCM_CCGR1;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *GPIO1_GDIR;
static void __iomem *GPIO1_DR;

enum eLEDSTATE
{
    LED_OFF,
    LED_ON
};
#define MYLED_CNT 1
#define MYLED_NAME "myled"

//字符设备结构体
struct mychrled_dev
{
    dev_t devid; //设备号 由主设备号和次设备号构成
    struct cdev cdev; //cdev结构体 初始化 将cdev设备和fileoperation结构体绑定  添加 将cdev设备和devid设备号绑定到一起
    struct class *class;//class类用来自动创建设备节点 分为 创建类 和 创建类的设备
    struct device *device;
    //主设备号和此设备号共同构成devid
    int major; // 主设备号
    int minor; // 次设备号
};

struct mychrled_dev mychrled;//创建led设备



void LedSwitch(u8 sta)
{
    unsigned int val = 0;
    if(sta == LED_ON)
    {
        val = readl(GPIO1_DR);
        val &= ~(1<<3);//将第三个bit位清零
        writel(val,GPIO1_DR);//写入 0  点亮

    }
    else if(sta == LED_OFF)
    {
        val = readl(GPIO1_DR);
        val |= (1<<3);//将第三个bit位置1
        writel(val,GPIO1_DR);//写入 1  熄灭
    }
}

/*
 * @description		: 打开设备
 * @param - inode 	: 传递给驱动的inode
 * @param - filp 	: 设备文件，file结构体有个叫做private_data的成员变量
 * 					  一般在open的时候将private_data指向设备结构体。
 * @return 			: 0 成功;其他 失败
 */
static int led_open(struct inode *inode,struct file *filp)
{
    filp->private_data = &mychrled;
    return 0;
}

/*
 * @description		: 从设备读取数据 
 * @param - filp 	: 要打开的设备文件(文件描述符)
 * @param - buf 	: 返回给用户空间的数据缓冲区
 * @param - cnt 	: 要读取的数据长度
 * @param - offt 	: 相对于文件首地址的偏移
 * @return 			: 读取的字节数，如果为负值，表示读取失败
 */
static ssize_t led_read(struct file *filp, char __user *buf,size_t cnt, loff_t *offt) 
{
    unsigned int regVal = readl(GPIO1_DR);
    char leds_status = (regVal & (1<<3)) ? 1 : 0;
    copy_to_user(buf, (const void *)&leds_status, 1);      
    return 1;
}



/*
 * @description		: 向设备写数据 
 * @param - filp 	: 设备文件，表示打开的文件描述符
 * @param - buf 	: 要写给设备写入的数据
 * @param - cnt 	: 要写入的数据长度
 * @param - offt 	: 相对于文件首地址的偏移
 * @return 			: 写入的字节数，如果为负值，表示写入失败
 */
static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    int retValue;
    unsigned char dataBuf[1];
    unsigned char ledStat;

    retValue = copy_from_user(dataBuf,buf,cnt);
    if(retValue < 0 )
    {
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }
    
    ledStat = dataBuf[0];
    if(ledStat == LED_ON)
    {
        LedSwitch(LED_ON);
    }
    else if(ledStat == LED_OFF)
    {
        LedSwitch(LED_OFF);
    }
}

/*
 * @description		: 关闭/释放设备
 * @param - filp 	: 要关闭的设备文件(文件描述符)
 * @return 			: 0 成功;其他 失败
 */
static int led_release(struct inode *inode, struct file *filp)
{
	return 0;
}


//创建fileoperation 结构体 绑定 read write open close 函数
static struct file_operations mychrled_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release
};

/*
 * @description	: 驱动出口函数
 * @param 		: 无
 * @return 		: 无
 */

static int __init led_init(void)
{
    unsigned int val = 0;

    //1.初始化寄存器 进行地址映射
    IMX6U_CCM_CCGR1 = ioremap(CCM_CCGR1_BASE,4);
    SW_PAD_GPIO1_IO03 = ioremap(SW_PAD_GPIO1_IO03_BASE,4);
    SW_MUX_GPIO1_IO03 = ioremap(SW_MUX_GPIO1_IO03_BASE,4);
    GPIO1_GDIR = ioremap(GPIO1_GDIR_BASE,4);
    GPIO1_DR = ioremap(GPIO1_DR_BASE,4);


    //2.使能GPIO时钟
    val = readl(IMX6U_CCM_CCGR1);
    val &= ~(3<<26);//清除以前的设置
    val |= (3<<26); //设置新值
    writel(val,IMX6U_CCM_CCGR1);


    //3.设置GPIO1_IO03的复用功能 将其复用为GPIO1_IO03
    //---5=0b0101 //后三位 101为复用为GPIO功能
    writel(5,SW_MUX_GPIO1_IO03);


    //4.设置IO属性     	
	/*寄存器SW_PAD_GPIO1_IO03设置IO属性
	 *bit 16:0 HYS关闭
	 *bit [15:14]: 00 默认下拉
     *bit [13]: 0 kepper功能
     *bit [12]: 1 pull/keeper使能
     *bit [11]: 0 关闭开路输出
     *bit [7:6]: 10 速度100Mhz
     *bit [5:3]: 110 R0/6驱动能力
     *bit [0]: 0 低转换率
	 */
    writel(0x10B0,SW_PAD_GPIO1_IO03);


    //5.设置GPIO方向 为输出
    val = readl(GPIO1_GDIR);
    val &= ~(1<<3);
    val |= (1<<3);
    writel(val,GPIO1_GDIR);


    //6.设置默认状态 关闭LED   
    val = readl(GPIO1_DR);
    val |= (1<<3);
    writel(val,GPIO1_DR);


    //8.注册字符设备驱动
        //8.1创建设备号
        //8.1.1如果已经定义了设备号
        if(mychrled.major)
        {
            mychrled.devid = MKDEV(mychrled.major,0);
            register_chrdev_region(mychrled.devid,MYLED_CNT,MYLED_NAME);
        }
        //8.1.2如果没有定义设备号
        else
        {            
            alloc_chrdev_region(&mychrled.devid,0,MYLED_CNT,MYLED_NAME);     
            mychrled.major = MAJOR(mychrled.devid);
            mychrled.minor = MINOR(mychrled.devid);
        }           
        printk("mychrled major=%d,minor=%d\r\n",mychrled.major, mychrled.minor);	  

        //8.2初始化cdev 绑定fileoperations结构体
        mychrled.cdev.owner = THIS_MODULE;
        cdev_init(&mychrled.cdev,&mychrled_fops);

        //8.3添加cdev 绑定cdev和devid
        cdev_add(&mychrled.cdev,mychrled.devid,MYLED_CNT);

        //8.4创建设备类 让其自动创建设备节点
        mychrled.class = class_create(THIS_MODULE,MYLED_NAME);
        if (IS_ERR(mychrled.class)) {
		    return PTR_ERR(mychrled.class);
	    }

        //8.5创建设备 让其自动创建设备节点
        mychrled.device = device_create(mychrled.class,NULL,mychrled.devid,NULL,MYLED_NAME);
        if (IS_ERR(mychrled.device)) 
        {
		    return PTR_ERR(mychrled.device);
        }

    return 0;
}


static void __exit led_exit(void)
{
    /* 取消映射 */
	iounmap(IMX6U_CCM_CCGR1);
	iounmap(SW_MUX_GPIO1_IO03);
	iounmap(SW_PAD_GPIO1_IO03);
	iounmap(GPIO1_DR);
	iounmap(GPIO1_GDIR);

    /* 注销字符设备驱动 */
	cdev_del(&mychrled.cdev);/*  删除cdev */
	unregister_chrdev_region(mychrled.devid, MYLED_CNT); /* 注销设备号 */

    //删除自动创建的设备节点
    device_destroy(mychrled.class, mychrled.devid);
	class_destroy(mychrled.class);
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sun Xiaolong");