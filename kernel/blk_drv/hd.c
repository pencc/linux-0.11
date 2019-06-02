/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#define MAJOR_NR 3			// 硬盘主设备号是3
#include "blk.h"

// 读取CMOS中的硬盘信息
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \	// 0x70是写端口号，0x80|addr是要读的CMOS内存地址
inb_p(0x71); \				// 0x71是读端口号
})

/* Max read/write errors/sector */
#define MAX_ERRORS	7		// 读/写一个扇区时允许的最多出错次数
#define MAX_HD		2		// 系统支持的最多硬盘数

// 重新矫正处理函数
static void recal_intr(void);

// 重新矫正标志。当设置了该标志，程序中会调用recal_intr()以将磁头移动到0柱面
static int recalibrate = 1;
// 复位标志。当发生读写错误时会设置标志并调用相关复位函数，以复位硬盘和控制器
static int reset = 1;

/*
 *  This struct defines the HD's and their types.
 */
// 磁头数、每磁道扇区数、柱面数、写前预补偿柱面号、磁头着陆区柱面号、控制字节
struct hd_i_struct {
	int head,sect,cyl,wpcom,lzone,ctl;
	};
#ifdef HD_TYPE
struct hd_i_struct hd_info[] = { HD_TYPE };		// 硬盘信息数组
#define NR_HD ((sizeof (hd_info))/(sizeof (struct hd_i_struct)))	// 计算硬盘的个数
#else
struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif

// 定义硬盘分区结构。给出每个分区从硬盘0道开始算起的物理起始扇区号和分区扇区总数。
// 其中的5的倍数处的项（例如hd[0]和hd[5]等）代表整个硬盘的参数
static struct hd_struct {
	long start_sect;			// 分区在硬盘中的起始物理（绝对）扇区
	long nr_sects;				// 分区中扇区总数
} hd[5*MAX_HD]={{0,0},};

// 读端口嵌入汇编宏。读端口port，共读nr字，保存在buf中
#define port_read(port,buf,nr) \
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr))

// 写端口嵌入汇编宏。写端口port，共写nr字，从buf中取数据
#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr))

extern void hd_interrupt(void);	// 硬盘中断过程
extern void rd_load(void);		// 虚拟盘创建加载函数

/* This may be used only once, enforced by 'static int callable' */
// 系统设置函数
// 函数参数BIOS是由初始化程序init/main.c中的init子程序设置为指向硬盘参数表结构的指针
// 该硬盘参数表结构包含2个硬盘参数表的内容（共32字节），是从内存0x90080复制来的。
// 0x90080处的硬盘参数表是由setup.s程序利用ROM BIOS功能取得的。本函数主要功能是读取CMOS
// 和硬盘参数表信息，用于设置硬盘分区结构hd，并尝试加载RAM虚拟盘和根文件系统。
int sys_setup(void * BIOS)
{
	static int callable = 1;
	int i,drive;
	unsigned char cmos_disks;
	struct partition *p;
	struct buffer_head * bh;

// 首先设置callable标志，本函数只能被调用一次，然后设置硬盘信息数组hd_info[]。
// 如果在include/linux/config.h文件中已定义了符号常数HD_TYPE，那么hd_info[]
// 数组已经在前面设置好了，否则就需要读取boot/setup.s程序存放在内存0x90080处
// 开始的硬盘参数表。setup.s程序在内存此处连续存放着1-2个硬盘参数表
	if (!callable)
		return -1;
	callable = 0;
#ifndef HD_TYPE
	for (drive=0 ; drive<2 ; drive++) {
		hd_info[drive].cyl = *(unsigned short *) BIOS;
		hd_info[drive].head = *(unsigned char *) (2+BIOS);
		hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
		hd_info[drive].ctl = *(unsigned char *) (8+BIOS);
		hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
		hd_info[drive].sect = *(unsigned char *) (14+BIOS);
		BIOS += 16;
	}
	if (hd_info[1].cyl)
		NR_HD=2;			// 硬盘数置
	else
		NR_HD=1;
#endif
	// 设置硬盘分区结构数组hd[]。该数组的项0和项5分别表示两个硬盘的
	// 整体参数，而项1-4和6-9分别表示两个硬盘的四个分区参数。因此这
	// 里只设置表示硬盘整体信息的两项（项0和5）
	for (i=0 ; i<NR_HD ; i++) {
		hd[i*5].start_sect = 0;					// 硬盘起始扇区号
		hd[i*5].nr_sects = hd_info[i].head*
				hd_info[i].sect*hd_info[i].cyl;	// 硬盘总扇区数
	}

	/*
		We querry CMOS about hard disks : it could be that 
		we have a SCSI/ESDI/etc controller that is BIOS
		compatable with ST-506, and thus showing up in our
		BIOS table, but not register compatable, and therefore
		not present in CMOS.

		Furthurmore, we will assume that our ST-506 drives
		<if any> are the primary drives in the system, and 
		the ones reflected as drive 1 or 2.

		The first drive is stored in the high nibble of CMOS
		byte 0x12, the second in the low nibble.  This will be
		either a 4 bit drive type or 0xf indicating use byte 0x19 
		for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

		Needless to say, a non-zero value means we have 
		an AT controller hard disk for that drive.

		
	*/

	if ((cmos_disks = CMOS_READ(0x12)) & 0xf0)
		if (cmos_disks & 0x0f)
			NR_HD = 2;
		else
			NR_HD = 1;
	else
		NR_HD = 0;
	// NR_HD = 0，则两个硬盘都不是AT控制器兼容的，则两个硬盘数据结构都清0
	for (i = NR_HD ; i < 2 ; i++) {
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sects = 0;
	}
	// NR_HD已确定，现在读取每个硬盘上第1个扇区中的分区表信息，用来设置分区
	// 结构数组hd[]中硬盘各分区的信息。
	for (drive=0 ; drive<NR_HD ; drive++) {
		if (!(bh = bread(0x300 + drive*5,0))) {
			printk("Unable to read partition table of drive %d\n\r",
				drive);
			panic("");
		}
		if (bh->b_data[510] != 0x55 || (unsigned char)
		    bh->b_data[511] != 0xAA) {		// 判断硬盘标志0xAA55
			printk("Bad partition table on drive %d\n\r",drive);
			panic("");
		}
		p = 0x1BE + (void *)bh->b_data;		// 分区位于第1扇区0x1BE处
		for (i=1;i<5;i++,p++) {
			hd[i+5*drive].start_sect = p->start_sect;
			hd[i+5*drive].nr_sects = p->nr_sects;
		}
		brelse(bh);		// 释放为存放硬盘数据块而申请的缓冲区
	}
	if (NR_HD)
		printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
	rd_load();			// 尝试创建加载虚拟盘
	mount_root();		// 安装根文件系统
	return (0);
}

// 判断并循环等待硬盘控制器就绪。
// 位7 -- 状态寄存器忙位
// 位6 -- 驱动器就绪比特位
static int controller_ready(void)
{
	int retries=10000;

	while (--retries && (inb_p(HD_STATUS)&0xc0)!=0x40);
	return (retries);
}

// 检测硬盘执行命令后的状态
static int win_result(void)
{
	int i=inb_p(HD_STATUS);

	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))
		== (READY_STAT | SEEK_STAT))
		return(0); /* ok */
	if (i&1) i=inb(HD_ERROR);
	return (1);
}

// 向硬盘控制器发送命令块
// drive - 硬盘号(0-1)； nsect - 读写扇区数； sect - 起始扇区；
// head  - 磁头号；		cyl   - 柱面号；    cmd  - 命令码；
// intr_addr() - 硬盘中断处理程序中将调用的C处理函数指针
static void hd_out(unsigned int drive,unsigned int nsect,unsigned int sect,
		unsigned int head,unsigned int cyl,unsigned int cmd,
		void (*intr_addr)(void))
{
	register int port asm("dx");

	if (drive>1 || head>15)
		panic("Trying to write bad sector");
	if (!controller_ready())
		panic("HD controller not ready");
	do_hd = intr_addr;
	outb_p(hd_info[drive].ctl,HD_CMD);
	port=HD_DATA;
	outb_p(hd_info[drive].wpcom>>2,++port);
	outb_p(nsect,++port);
	outb_p(sect,++port);
	outb_p(cyl,++port);
	outb_p(cyl>>8,++port);
	outb_p(0xA0|(drive<<4)|head,++port);
	outb(cmd,++port);
}

// 等待磁盘就绪
static int drive_busy(void)
{
	unsigned int i;

	for (i = 0; i < 10000; i++)
		if (READY_STAT == (inb_p(HD_STATUS) & (BUSY_STAT|READY_STAT)))
			break;
	i = inb(HD_STATUS);
	i &= BUSY_STAT | READY_STAT | SEEK_STAT;
	if (i == (READY_STAT | SEEK_STAT))
		return(0);
	printk("HD controller times out\n\r");
	return(1);
}

// 诊断复位硬盘控制器
static void reset_controller(void)
{
	int	i;

	outb(4,HD_CMD);
	for(i = 0; i < 100; i++) nop();
	outb(hd_info[0].ctl & 0x0f ,HD_CMD);
	if (drive_busy())
		printk("HD-controller still busy\n\r");
	if ((i = inb(HD_ERROR)) != 1)
		printk("HD-controller reset failed: %02x\n\r",i);
}

// 复位硬盘nr
static void reset_hd(int nr)
{
	reset_controller();
	hd_out(nr,hd_info[nr].sect,hd_info[nr].sect,hd_info[nr].head-1,
		hd_info[nr].cyl,WIN_SPECIFY,&recal_intr);
}

// 意外硬盘中断调用函数
void unexpected_hd_interrupt(void)
{
	printk("Unexpected HD interrupt\n\r");
}

// 读写硬盘失败处理调用函数
static void bad_rw_intr(void)
{
	if (++CURRENT->errors >= MAX_ERRORS)
		end_request(0);
	if (CURRENT->errors > MAX_ERRORS/2)
		reset = 1;
}

// 读操作中断调用函数
static void read_intr(void)
{
	if (win_result()) {
		bad_rw_intr();
		do_hd_request();
		return;
	}
	port_read(HD_DATA,CURRENT->buffer,256);
	CURRENT->errors = 0;
	CURRENT->buffer += 512;
	CURRENT->sector++;
	if (--CURRENT->nr_sectors) {
		do_hd = &read_intr;
		return;
	}
	end_request(1);
	do_hd_request();
}

// 写扇区中断调用函数
static void write_intr(void)
{
	if (win_result()) {
		bad_rw_intr();
		do_hd_request();
		return;
	}
	if (--CURRENT->nr_sectors) {
		CURRENT->sector++;
		CURRENT->buffer += 512;
		do_hd = &write_intr;
		port_write(HD_DATA,CURRENT->buffer,256);
		return;
	}
	end_request(1);
	do_hd_request();
}

// 磁盘重新矫正中断调用函数
static void recal_intr(void)
{
	if (win_result())
		bad_rw_intr();
	do_hd_request();
}

// 执行硬盘读写请求操作
void do_hd_request(void)
{
	int i,r = 0;
	unsigned int block,dev;
	unsigned int sec,head,cyl;
	unsigned int nsect;

	INIT_REQUEST;
	dev = MINOR(CURRENT->dev);
	block = CURRENT->sector;		// 请求的起始扇区数
	if (dev >= 5*NR_HD || block+2 > hd[dev].nr_sects) {
		end_request(0);
		goto repeat;
	}
	block += hd[dev].start_sect;
	dev /= 5;						// 此时dev代表硬盘号（0或1）
	__asm__("divl %4":"=a" (block),"=d" (sec):"0" (block),"1" (0),
		"r" (hd_info[dev].sect));
	__asm__("divl %4":"=a" (cyl),"=d" (head):"0" (block),"1" (0),
		"r" (hd_info[dev].head));
	sec++;
	nsect = CURRENT->nr_sectors;
	if (reset) {
		reset = 0;
		recalibrate = 1;			// 置需重新校正标志
		reset_hd(CURRENT_DEV);
		return;
	}
	if (recalibrate) {
		recalibrate = 0;
		hd_out(dev,hd_info[CURRENT_DEV].sect,0,0,0,
			WIN_RESTORE,&recal_intr);
		return;
	}	
	if (CURRENT->cmd == WRITE) {
		hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
		for(i=0 ; i<3000 && !(r=inb_p(HD_STATUS)&DRQ_STAT) ; i++)
			/* nothing */ ;
		if (!r) {
			bad_rw_intr();
			goto repeat;
		}
		port_write(HD_DATA,CURRENT->buffer,256);
	} else if (CURRENT->cmd == READ) {
		hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
	} else
		panic("unknown hd-command");
}

// 硬盘系统初始化
// 设置硬盘中断描述符，并允许硬盘控制器发送中断请求信号。
// 该函数设置硬盘设备的请求项处理函数指针为do_hd_request(),然后设置硬盘中断门
// 描述符。Hd_interrupt(kernel/system_call.s)是其中断处理过程。硬盘中断号为
// int 0x2E(46),对应8259A芯片的中断请求信号IRQ13.接着复位接联的主8250A int2
// 的屏蔽位，允许从片发出中断请求信号。再复位硬盘的中断请求屏蔽位(在从片上)，
// 允许硬盘控制器发送中断信号。中断描述符表IDT内中断门描述符设置宏set_intr_gate().
void hd_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;      // do_hd_request()
	set_intr_gate(0x2E,&hd_interrupt);
	outb_p(inb_p(0x21)&0xfb,0x21);                      // 复位接联的主8259A int2的屏蔽位
	outb(inb_p(0xA1)&0xbf,0xA1);                        // 复位硬盘中断请求屏蔽位(在从片上)
}
