#ifndef _SCHED_H
#define _SCHED_H

#define NR_TASKS 64		// 系统中同时最多任务（进程）数
#define HZ 100			// 定义系统时钟滴答，10ms一次

#define FIRST_TASK task[0]			// 任务0比较特殊，所以特意给它单独定义一个符号
#define LAST_TASK task[NR_TASKS-1]	// 任务数组中的最后一项任务

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif

// 这里定义了进程运行可能所处的状态
#define TASK_RUNNING		0		// 进程正在运行或已准备就绪
#define TASK_INTERRUPTIBLE	1		// 进程处于可中断等待状态
#define TASK_UNINTERRUPTIBLE	2	// 进程处于不可中断等待状态，主要用于I/O操作等待
#define TASK_ZOMBIE		3			// 进程处于僵死状态，已停止运行，但父进程还没发信号
#define TASK_STOPPED		4		// 进程已停止

#ifndef NULL
#define NULL ((void *) 0)			// 定义NULL为空指针
#endif

// 复制进程的页目录页表（mm/memory.c）
extern int copy_page_tables(unsigned long from, unsigned long to, long size);
// 释放页表所指定的内存块及页表本身
extern int free_page_tables(unsigned long from, unsigned long size);

// 调度程序的初始化函数
extern void sched_init(void);
// 进程调度函数
extern void schedule(void);
// 异常(陷阱)中断处理初始化函数，设置中断调用门并允许中断请求信号。
extern void trap_init(void);
#ifndef PANIC
// 显示内核出错信息，然后陷入死循环。
volatile void panic(const char * str);
#endif
// 往tty上写指定长度的字符串
extern int tty_write(unsigned minor,char * buf,int count);

typedef int (*fn_ptr)();

// 数学协处理器使用的结构，主要用于保存进程切换时i387的执行状态信息
struct i387_struct {
	long	cwd;			// 控制字
	long	swd;			// 状态字
	long	twd;			// 标记字
	long	fip;			// 协处理器代码指针
	long	fcs;			// 协处理器代码段寄存器
	long	foo;			// 内存操作数的偏移位置
	long	fos;			// 内存操作数的段值
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
};							// 8个10字节的协处理器累加器

// 任务状态段数据结构
struct tss_struct {
	long	back_link;	/* 16 high bits zero */
	long	esp0;
	long	ss0;		/* 16 high bits zero */
	long	esp1;
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	long	cr3;
	long	eip;
	long	eflags;
	long	eax,ecx,edx,ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	long	ldt;		/* 16 high bits zero */
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};

struct task_struct {
/* these are hardcoded - don't touch */
	long state;			/* -1 unrunnable, 0 runnable(就绪), >0 stopped */
	long counter;		// 进程运行时间计数(递减)（滴答数），运行时间片
	long priority;		// 运行优先数。任务开始运行时counter = priority，越大运行越长。
	long signal;		// 信号。是位图，每个比特位代表一种信号，信号值=位偏移值+1。
	struct sigaction sigaction[32];		// 信号执行属性结构，对应信号将要执行的操作和标志信息。
	long blocked;		// 进程信号屏蔽码（对应信号位图）
/* various fields */
	int exit_code;		// 任务执行停止的退出码，其父进程会取
	// 代码段地址，代码长度，代码长度+数据长度，总长度，堆栈段地址
	unsigned long start_code,end_code,end_data,brk,start_stack;	
	// 进程标识号，父进程号，进程组号，会话号，会话首领
	long pid,father,pgrp,session,leader;
	// 用户id，有效用户id，保存的用户id
	unsigned short uid,euid,suid;
	// 组id，有效组id，保存的组id
	unsigned short gid,egid,sgid;
	long alarm; 	//报警定时值
	// 用户态运行时间(滴答数)，系统态运行时间，子进程用户态运行时间，子进程系统态运行时间，进程开始运行时刻
	long utime,stime,cutime,cstime,start_time;
	unsigned short used_math;		// 是否使用了协处理器
/* file system info */
	int tty;						// 进程使用tty的子设备号，-1表示没有使用
	unsigned short umask;			// 文件创建属性屏蔽位
	struct m_inode * pwd;			// 当前工作目录i节点结构。
	struct m_inode * root;			// 根目录i节点结构
	struct m_inode * executable;	// 执行文件i节点结构
	unsigned long close_on_exec;	// 执行时关闭文件句柄位图标志
	struct file * filp[NR_OPEN];	// 进程使用的文件表结构
/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
	struct desc_struct ldt[3];		// 本任务的局部表描述符。0-空，1-代码段cs，2-数据和堆栈段ds&ss
/* tss for this task */
	struct tss_struct tss;			// 本进程的任务状态段信息结构
};

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x9ffff (=640kB)
 */
#define INIT_TASK \
/* state etc */	{ 0,15,15, \		// state，counter，priority
/* signals */	0,{{},},0, \		// signal，sigaction[32]，blocked
/* ec,brk... */	0,0,0,0,0,0, \		// exit_code，start_code，end_code，end_data，brk，start_stack
/* pid etc.. */	0,-1,0,0,0, \		// pid， father，pgrp，session，leader
/* uid etc */	0,0,0,0,0,0, \		// uid，euid，suid，gid，egid，sgid
/* alarm */	0,0,0,0,0,0, \			// alam，utime，stime，cutime，cstime，start_time
/* math */	0, \					// used_math
/* fs info */	-1,0022,NULL,NULL,NULL,0, \		// tty，umask，pwd，root，executable，close_on_exec
/* filp */	{NULL,}, \				// filp[20]
	{ \								// ldt[3]
		{0,0}, \
/* ldt */	{0x9f,0xc0fa00}, \		// 代码长640K，基址0x0,G=1,D=1,DPL=3,P=1 TYPE=0x0a
		{0x9f,0xc0f200}, \			// 数据长640K，基址0x0,G=1,D=1,DPL=3,P=1 TYPE=0x02
	}, \
/*tss*/	{0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&pg_dir,\
	 0,0,0,0,0,0,0,0, \
	 0,0,0x17,0x17,0x17,0x17,0x17,0x17, \
	 _LDT(0),0x80000000, \
		{} \
	}, \
}

extern struct task_struct *task[NR_TASKS];			// 任务指针数组
extern struct task_struct *last_task_used_math;		// 上一个使用过协处理器的进程
extern struct task_struct *current;					// 当前进程结构指针变量
extern long volatile jiffies;						// 从开机开始算起的滴答数
extern long startup_time;							// 开机时间，从1970:0:0:0开始计时的秒数

#define CURRENT_TIME (startup_time+jiffies/HZ)		// 当前时间（秒数）

// 添加定时器函数（定时时间jiffies滴答数，定时到时调用函数*fn()）
extern void add_timer(long jiffies, void (*fn)(void));
// 不可中断的等待睡眠
extern void sleep_on(struct task_struct ** p);
// 可中断的等待睡眠
extern void interruptible_sleep_on(struct task_struct ** p);
// 明确唤醒睡眠的进程
extern void wake_up(struct task_struct ** p);

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1 etc ...
 */
// 全局表中第1个任务状态段(TSS)描述符的选择符索引号
#define FIRST_TSS_ENTRY 4
// 全局表中第1个局部描述符表(LDT)描述符的选择符索引号
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)
// 计算全局表中第n个任务的TSS段描述符的选择符值（偏移量）
// 因每个描述符占8字节，因此FIRST_TSS_ENTRY<<3表示该描述符在GDT表中的起始偏移位置
// 因为每个任务使用1个TSS和1个LDT描述符，共占16字节，因此需要n<<4来表示对应TSS起始
// 位置。该宏得到的值正好也是该TSS的选择符值。
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
// 计算全局表中第n个任务的LDT段描述符的选择符值（偏移值）
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))
// 把第n个任务的TSS段选择符加载到任务寄存器TR中
#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))
// 把第n个任务的LDT段选择符加载到局部描述符表寄存器LDTR中。
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))
// 取当前运行任务的任务号（是任务数组中的索引值，与进程号pid不同）
// 返回：n-当前任务号
#define str(n) \
__asm__("str %%ax\n\t" \		// 将任务寄存器中TSS段的选择符复制到ax中
	"subl %2,%%eax\n\t" \		// (eax - FIRST_TSS_ENTRY * 8)->eax
	"shrl $4,%%eax" \			// (eax / 16)->eax = 当前任务号
	:"=a" (n) \
	:"a" (0),"i" (FIRST_TSS_ENTRY<<3))
/*
 *	switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag(cr0) if the task we switched to has used
 * tha math co-processor latest.
 */
// 跳转到一个任务的TSS段选择符组成的地址处会造成CPU进行任务切换操作。
// 输入：	%0 - 指向__tmp;			%1 - 指向__tmp.b处，用于存放新TSS的选择符
//			dx - 新任务n的TSS段选择符	ecx - 新任务n的任务结构指针task[n]
// 其中临时数据结构__tmp用于组建远跳转(ljmp)指令的操作数。该操作数由4字节偏移地址和2
// 字节的段选择符组成。因此__tmp中a的值是32为偏移值，而b的低2字节是新TSS段的选择符
// (高2字节不用)。跳转到TSS端选择符会造成任务切换到该TSS对应的进程。对于造成任务切换
// 的长跳转，a值无用。ljmp内存间接跳转指令使用6字节操作数作为跳转目的地的长指针，其格
// 式为：jmp 16位段选择符:32位偏移值。但在内存中操作数的表示顺序与这里正好相反。
// 任务切换回来后，在判断原任务上次执行是否使用过协处理器时，是哦嗯过将原任务指针与保存
// 在last_task_used_math变量中的上次使用过协处理器任务指针进行比较而作出的，参见文件
// kernel/sched.c中有关math_state_restore()函数的说明。
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,current\n\t" \			// 任务n是当前任务么?(current==task[n]?)
	"je 1f\n\t" \							// 跳转退出
	"movw %%dx,%1\n\t" \					// 将新任务TSS的16位选择符存入__tmp.b中
	"xchgl %%ecx,current\n\t" \				// current=task[n]; ecx=被切换出的任务
	"ljmp *%0\n\t" \						// 执行长跳转至 *&__tmp，造成任务切换
	"cmpl %%ecx,last_task_used_math\n\t" \	// 原任务上次使用过协处理器么？
	"jne 1f\n\t" \							// 跳转退出
	"clts\n" \								// 清除cr0中的任务切换
	"1:" \									// 标志TS。
	::"m" (*&__tmp.a),"m" (*&__tmp.b), \	// 第1个":"表示输出寄存器为空。第二个冒号表示输入寄存器，
	"d" (_TSS(n)),"c" ((long) task[n])); \	// %0   内存地址    __tmp.a的地址，用来放偏移
}											// %1   内存地址    __tmp.b的地址，用来放TSS选择符
											// %2   edx         任务号为n的TSS选择符
											// %3   ecx         task[n] 

// 页面地址对齐（内核代码中没有任何地方引用）
#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000)

// 设置位于地址addr处描述符中的各基地址字段（基地址是base）。
// %0 - 地址addr偏移2; %1 - 地址addr偏移; %2 - 地址addr偏移7; edx - 基地址base
#define _set_base(addr,base)  \
__asm__ ("push %%edx\n\t" \
	"movw %%dx,%0\n\t" \		// 基地址base低16位(位15-0)->[addr+2]
	"rorl $16,%%edx\n\t" \		// edx中基址高16位(位31-16)->dx
	"movb %%dl,%1\n\t" \		// 基址高16位中的低8位(位23-16)->[addr+4]
	"movb %%dh,%2\n\t" \		// 基址高16位中的高8位(位31-24)->[addr+7]
	"pop %%edx" \
	::"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)), \
	 "d" (base) \
	)

// 设置位于地址addr处描述符中的段限长字段（段长是limit）
// %0 - 地址addr; %1 - 地址addr偏移6处; edx - 段长值limit
#define _set_limit(addr,limit) \
__asm__ ("push %%edx\n\t" \
	"movw %%dx,%0\n\t" \		// 段长limit低16位(位15-0)->[addr]
	"rorl $16,%%edx\n\t" \		// edx中的段长高4位(位19-16)->dl
	"movb %1,%%dh\n\t" \		// 取原[addr+6]字节->dh，其中高4位是些标志
	"andb $0xf0,%%dh\n\t" \		// 清dh的低4位(将存放段长的位19-16)
	"orb %%dh,%%dl\n\t" \		// 将原高4位标志和段长的高4位置(位19-16)合成1字节
	"movb %%dl,%1\n\t" \		// 并放回[addr+6]处
	"pop %%edx" \
	::"m" (*(addr)), \
	 "m" (*((addr)+6)), \
	 "d" (limit) \
	)

// 设置局部描述符中ldt描述符的基地址字段
#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , (base) )
// 设置局部描述符中ldt描述符的段长字段
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 )

/**
#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \
	"movb %2,%%dl\n\t" \
	"shll $16,%%edx\n\t" \
	"movw %1,%%dx" \
	:"=d" (__base) \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)) \
        :"memory"); \
__base;})
**/

static inline unsigned long _get_base(char * addr)
{
         unsigned long __base;
         __asm__("movb %3,%%dh\n\t"
                 "movb %2,%%dl\n\t"
                 "shll $16,%%edx\n\t"
                 "movw %1,%%dx"
                 :"=&d" (__base)
                 :"m" (*((addr)+2)),
                  "m" (*((addr)+4)),
                  "m" (*((addr)+7)));
         return __base;
}

// 取局部描述符表中ldt所指段描述符中的基地址
#define get_base(ldt) _get_base( ((char *)&(ldt)) )

// 取段选择符segment指定的描述符中的段限长值
// 指令lsl是Load Segment Limit缩写，它从指定段描述符中取出分散的限长比特位拼成完整的
// 段限长值放入指定寄存器中。所得的段限长是实际字节数减1，因此这里还需要加1后才返回。
// %0 - 存放段长值(字节数); %1 - 段选择符segment
#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
