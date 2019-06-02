/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
struct request request[NR_REQUEST];

/*
 * 用于在请求数组没有空闲项时进程的临时等待处
 */
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *	request_fn			// 对应主设备号的请求处理指针
 *	current_request		// 当前正在处理的请求（链表结构体，成员中包含了该设备的下一个请求指针）
 */
// 块设备数组。该数组使用主设备号作为索引。实际内容将在各块设备驱动程序初始化时填入
// 比如，硬盘驱动程序初始化时候就设置了blk_dev[3].request_fn = DEVICE_REQUEST;
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */
	{ NULL, NULL },		/* dev mem */
	{ NULL, NULL },		/* dev fd */
	{ NULL, NULL },		/* dev hd */
	{ NULL, NULL },		/* dev ttyx */
	{ NULL, NULL },		/* dev tty */
	{ NULL, NULL }		/* dev lp */	// lp打印设备
};

// 锁定指定缓冲块
// 如果指定的缓冲块已经被其它任务锁定，则使自己睡眠（不可中断的等待），直到
// 被执行解锁缓冲块的任务明确地唤醒
static inline void lock_buffer(struct buffer_head * bh)
{
	cli();						// 关中断
	while (bh->b_lock)			// 如果缓冲区已被锁定则睡眠
		sleep_on(&bh->b_wait);
	bh->b_lock=1;				// 立刻锁定该缓冲区
	sti();						// 开中断
}

// 解锁锁定的缓冲区
static inline void unlock_buffer(struct buffer_head * bh)
{
	if (!bh->b_lock)
		printk("ll_rw_block.c: buffer not locked\n\r");
	bh->b_lock = 0;			// 清除锁定标志
	wake_up(&bh->b_wait);	// 唤醒等待该缓冲区的任务
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.安全的处理请求链表
 */
// dev是指定块设备结构指针，该结构中有处理请求项函数指针和当前正在请求项指针
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;

	req->next = NULL;
	cli();								// 关中断
	if (req->bh)
		req->bh->b_dirt = 0;			// 清缓冲区“脏”标志
	if (!(tmp = dev->current_request)) {// dev当前请求项链表为空，表示目前该设备没有请求项
		dev->current_request = req;
		sti();							// 开中断
		(dev->request_fn)();			// 执行请求函数，对于硬盘是do_hd_request()
		return;
	}
	for ( ; tmp->next ; tmp=tmp->next)	// 电梯算法搜索最佳插入位置
		if ((IN_ORDER(tmp,req) || 
		    !IN_ORDER(tmp,tmp->next)) &&
		    IN_ORDER(req,tmp->next))
			break;
	req->next=tmp->next;
	tmp->next=req;						// 加入设备链表
	sti();								// 开中断
}

// 创建请求项并插入请求队列中
static void make_request(int major,int rw, struct buffer_head * bh)
{
	struct request * req;
	int rw_ahead;			// 逻辑值用于判断是否为READA或者WRITEA命令

/* WRITEA/READA is special case - it is not really needed, so if the */
/* buffer is locked, we just forget about it, else it's a normal read */
	if ((rw_ahead = (rw == READA || rw == WRITEA))) {
		if (bh->b_lock)
			return;
		if (rw == READA)
			rw = READ;
		else
			rw = WRITE;
	}
	if (rw!=READ && rw!=WRITE)
		panic("Bad block dev command, must be R/W/RA/WA");
	lock_buffer(bh);
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) {
		unlock_buffer(bh);
		return;
	}
repeat:
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 */
	if (rw == READ)
		req = request+NR_REQUEST;
	else
		req = request+((NR_REQUEST*2)/3);
/* find an empty request */
	while (--req >= request)
		if (req->dev<0)
			break;
/* if none found, sleep on new requests: check for rw_ahead */
	if (req < request) {
		if (rw_ahead) {
			unlock_buffer(bh);
			return;
		}
		sleep_on(&wait_for_request);
		goto repeat;
	}
/* fill up the request-info, and add it to the queue */
	req->dev = bh->b_dev;
	req->cmd = rw;
	req->errors=0;
	req->sector = bh->b_blocknr<<1;
	req->nr_sectors = 2;
	req->buffer = bh->b_data;
	req->waiting = NULL;
	req->bh = bh;
	req->next = NULL;
	add_request(major+blk_dev,req);
}

void ll_rw_block(int rw, struct buffer_head * bh)
{
	unsigned int major;

	if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
	!(blk_dev[major].request_fn)) {
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	make_request(major,rw,bh);
}

// 块设备初始化函数，由初始化程序main.c调用
// 初始化请求数组，将所有请求项置为空闲（dev = -1）,有32项（NR_REQUEST = 32）
void blk_dev_init(void)
{
	int i;

	for (i=0 ; i<NR_REQUEST ; i++) {
		request[i].dev = -1;
		request[i].next = NULL;
	}
}
