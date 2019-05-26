#ifndef _BLK_H
#define _BLK_H

#define NR_BLK_DEV	7		// 块设备数量
/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 *
 * 32 seems to be a reasonable number: enough to get some benefit
 * from the elevator-mechanism, but not so much as to lock a lot of
 * buffers when they are in the queue. 64 seems to be too many (easily
 * long pauses in reading when heavy writing/syncing is going on)
 */
#define NR_REQUEST	32

/*
 * Ok, this is an expanded form so that we can use the same
 * request for paging requests when that is implemented. In
 * paging, 'bh' is NULL, and 'waiting' is used to wait for
 * read/write completion.
 */
struct request {
	int dev;		/* -1 if no request */	// 发请求的设备号
	int cmd;		/* READ or WRITE */		// READ或WRITE命令
	int errors;								// 操作时引起的错误次数
	unsigned long sector;					// 起始扇区(1块=2扇区)
	unsigned long nr_sectors;				// 读/写扇区数
	char * buffer;							// 数据缓冲区
	struct task_struct * waiting;			// 任务等待操作执行完成的地方
	struct buffer_head * bh;				// 缓冲区头指针
	struct request * next;					// 指向下一请求项
};

/*
 * This is used in the elevator algorithm(电梯算法): Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical(时间要求严格) than writes.
 */
// 下面宏判断两个请求项结构的前后排列顺序。这个顺序将用作访问块设备时的请求
// 项执行顺序。这个宏会在程序ll_rw_blk.c中的函数add_request()中被调用。
// 该宏部分地实现了I/O调度功能，即实现了对请求项的排序功能(另一个是请求项合并功能)。
#define IN_ORDER(s1,s2) \
((s1)->cmd<(s2)->cmd || ((s1)->cmd==(s2)->cmd && \
((s1)->dev < (s2)->dev || ((s1)->dev == (s2)->dev && \
(s1)->sector < (s2)->sector))))

// 块设备结构
struct blk_dev_struct {
	void (*request_fn)(void);			// 请求操作的函数指针
	struct request * current_request;	// 当前正在处理的请求信息结构
};

extern struct blk_dev_struct blk_dev[NR_BLK_DEV];	// 块设备表(数组)，每种块设备占用一项
extern struct request request[NR_REQUEST];			// 请求项队列数组
extern struct task_struct * wait_for_request;		// 等待空闲请求项的进程队列头指针

// 在块设备驱动程序(如hd.c)包含此头文件时，必须先定义驱动程序处理设备的主设备号
// 这样下面就能为包含本文件的驱动程序给出正确的宏定义。
#ifdef MAJOR_NR

/*
 * Add entries as needed. Currently the only block devices
 * supported are hard-disks and floppies.
 */

#if (MAJOR_NR == 1)
/* ram disk */
#define DEVICE_NAME "ramdisk"
#define DEVICE_REQUEST do_rd_request
#define DEVICE_NR(device) ((device) & 7)
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == 2)
/* floppy */
#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ((device) & 3)
#define DEVICE_ON(device) floppy_on(DEVICE_NR(device))
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == 3)
/* harddisk */
#define DEVICE_NAME "harddisk"
#define DEVICE_INTR do_hd
#define DEVICE_REQUEST do_hd_request
#define DEVICE_NR(device) (MINOR(device)/5)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif
/* unknown blk device */
#error "unknown blk device"

#endif

#define CURRENT (blk_dev[MAJOR_NR].current_request)
#define CURRENT_DEV DEVICE_NR(CURRENT->dev)

#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif
static void (DEVICE_REQUEST)(void);

static inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)
		printk(DEVICE_NAME ": free buffer being unlocked\n");
	bh->b_lock=0;
	wake_up(&bh->b_wait);
}

static inline void end_request(int uptodate)
{
	DEVICE_OFF(CURRENT->dev);
	if (CURRENT->bh) {
		CURRENT->bh->b_uptodate = uptodate;
		unlock_buffer(CURRENT->bh);
	}
	if (!uptodate) {
		printk(DEVICE_NAME " I/O error\n\r");
		printk("dev %04x, block %d\n\r",CURRENT->dev,
			CURRENT->bh->b_blocknr);
	}
	wake_up(&CURRENT->waiting);
	wake_up(&wait_for_request);
	CURRENT->dev = -1;
	CURRENT = CURRENT->next;
}

#define INIT_REQUEST \
repeat: \
	if (!CURRENT) \
		return; \
	if (MAJOR(CURRENT->dev) != MAJOR_NR) \
		panic(DEVICE_NAME ": request list destroyed"); \
	if (CURRENT->bh) { \
		if (!CURRENT->bh->b_lock) \
			panic(DEVICE_NAME ": block not locked"); \
	}

#endif

#endif
