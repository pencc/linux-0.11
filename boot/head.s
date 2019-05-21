/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */
.text
.globl idt,gdt,pg_dir,tmp_floppy_area
pg_dir:					# 页目录将会存放于此
/*
 * 这里已经处于32位运行模式，因此这里的$0x10并不是把地址0x10装入各个段寄存器，它现在其实
 * 是全局段描述符表中的偏移值，或者说是一个描述符表项的选择符。这里$0x10的含义是请求特权级
 * 0（位0-1=0）、选择全局描述符表（位2=0）、选择表中第2项（3-15=2）。它正好指向表中的数据
 * 段描述符项。
 * 下面代码的含义是：设置ds，es，fs，gs为setup.s中构造的数据段（全局段描述符表第2项）的选择
 * 符=0x10，并将堆栈放置在stack_start指向user_stack数组区，然后使用本程序后面定义的新中断
 * 描述符表和全局段描述表。新全局段描述表中初始内容与setup.s中的基本一样，仅段限长从8MB修改成
 * 了16MB。stack_start定义在kernel/sched.c，69行。它是指向user_stack数组末端的一个长指
 * 针。lss stack_start,%esp设置这里使用的栈，姑且称为系统栈，但在移动到任务0执行（init/main.c
 * 中）以后该栈就被用作任务0和任务1共同使用的用户栈了。
 */
.globl startup_32
startup_32:				# 设置各个数据段寄存器 
	movl $0x10,%eax		# 对于GNU汇编，每个直接操作数要以`$`开始，否则表示地址。每个寄存器以%开头
	mov %ax,%ds			# 所有段寄存器均指向setup.s设置的GDT表中数据段选择符的基址
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	lss stack_start,%esp	# 表示stack_start->ss:esp，设置系统堆栈。
							# esp寄存器指向sched.c中的user_stack数组最后一项后面 
	call setup_idt			# 调用设置终端描述符表子程序
	call setup_gdt			# 调用设置全局描述符表子程序
	movl $0x10,%eax		# reload all the segment registers
	mov %ax,%ds		# after changing gdt. CS was already
	mov %ax,%es		# reloaded in 'setup_gdt'
	mov %ax,%fs		# 因为修改了gdt，所以需要重新装载所有的段寄存器。
	mov %ax,%gs		# CS代码寄存器已经在setup_gdt中重新加载过了。
	lss stack_start,%esp
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled
	movl %eax,0x000000	# loop forever if it isn't
	cmpl %eax,0x100000
	je 1b

/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
 # 486 CPU中cr0控制器的位16是写保护标志WP（Write-Protect），用于禁止超级用户级的程序
 # 向一般用户只读页面中进行写操作。该标志主要用于操作系统在创建新进程时实现写时复制（copy-on-write）方法
 # 下面这段程序用于检查数学协处理器芯片是否存在。方法是修改控制寄存器CR0，在假设存在协处理器的情况下执行
 # 一个协处理器指令，如果出错的话则说明协处理器芯片不存在，需要设置CR0中协处理器仿真位EM(位2)，并复位
 # 协处理器存在标志MP(位1)
	movl %cr0,%eax		# check math chip
	andl $0x80000011,%eax	# Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
	orl $2,%eax		# set MP
	movl %eax,%cr0
	call check_x87
	jmp after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
 # 下面fninit和fstsw是数学协处理器(80287/80387)的指令
 # finit向协处理器发出初始化命令，它会把协处理器置于一个未受以前操作影响的已知状态，设置其控制字为默认值、
 # 清除状态字和所有浮点栈式寄存器。非等待形式的这条指令(fninit)还会让协处理器终止执行当前执行的任何先前的
 # 算术操作。fstsw指令取协处理器的状态字。如果系统中存在协处理器的话，那么在执行了fninit指令后其状态字低
 # 字节肯定为0
check_x87:
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f			/* no coprocessor: have to set bits */
	movl %cr0,%eax		# 如果存在则向前跳到标号1处，否则改写cr0
	xorl $6,%eax		/* reset MP, set EM */
	movl %eax,%cr0
	ret
.align 2
1:	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */
setup_idt:
	lea ignore_int,%edx		# 将ignore_int的有效地址（偏移值）-> edx寄存器
	movl $0x00080000,%eax	# 将选择符0x0008置入eax的高16位中
	movw %dx,%ax		/* selector = 0x0008 = cs */
						# 偏移值的低16位置入eax的低16位中。此时eax含有门描述符低4字节的值
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */
						# 此时edx含有门描述符的高4字节的值
						# _idt是中断描述符表的地址。
	lea idt,%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)	# 将哑中断门描述符存入表中
	movl %edx,4(%edi)	# eax内容放到edi+4所指向内存位置处
	addl $8,%edi		# edi指向表中下一项
	dec %ecx
	jne rp_sidt
	lidt idt_descr		# 加载中断描述符表寄存器值
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */
setup_gdt:
	lgdt gdt_descr
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */
 # 每个页表长为4KB（1页内存页面），而每个页表项需要4字节，因此一个页表共可以存放
 # 1024个表项。如果一个页表项寻址4KB的地址空间，则一个页表就可以寻址4MB的物理内存。
 # 页表项的格式为：项的0-11位存放一些标志，例如是否在内存中（P位0）、读写许可（R/W位1）
 # 普通用户还是超级用户使用（U/S位2）、是否修改过（是否脏了）（D位6）等; 表项的位
 # 12-31是页框地址，用于指出一页物理内存的物理起始地址。
.org 0x1000		# 从偏移0x1000处开始是第1个页表（偏移0开始处将存放页表目录）
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000		# 定义下面的内存数据块从偏移0x5000处开始
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
tmp_floppy_area:
	.fill 1024,1,0	# 共保留1024项，每项1字节，填充数值0

after_page_tables:
	pushl $0		# These are the parameters to main :-)
	pushl $0		# 签名3个入栈0值分别表示envp、argv、argc，但main()没用到
	pushl $0
	pushl $L6		# return address for main, if it decides to.
	pushl $main		# 这样在设置分页处理结束后，执行`ret`返回指令时就会将main.c程序的地址弹出，并执行main.c程序
	jmp setup_paging
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

/* This is the default interrupt "handler" :-) */
int_msg:
	.asciz "Unknown interrupt\n\r"
.align 2
ignore_int:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	pushl $int_msg
	call printk
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */
 # 机器物理内存中大于1MB的内存空间主要被用于主内存区。主内存区由mm模块管理。
 # 它设计到页面映射操作。内核中所有其它函数就是这里指的一般(普通)函数。若要
 # 使用主内存区的页面，就需要使用get_free_page()等函数获取。因为主内存区
 # 中内存页面是共享资源，必须有程序进行统一管理以避免资源竞争。
 #
 # 在内存物理地址0x0处开始存放1页页目录和4页页表。页目录表是系统所有进程共用
 # 的，而这里的4页页表则属于内核专用，它们一一映射线性地址起始16MB空间范围到
 # 物理内存上。对于新的进程，系统会在主内存区为其申请页面存放页表。另外，1页
 # 内存长度是4096字节。
 
.align 2
setup_paging:				# 首先对5页内存（1页目录+4页页表）清零
	movl $1024*5,%ecx		/* 5 pages - pg_dir+4 page tables */
	xorl %eax,%eax
	xorl %edi,%edi			/* pg_dir is at 0x000 */
	cld;rep;stosl			# eax内容存到es：edi所指内存位置处，且edi增4
# 下面4句设置页目录表中的项，因为内核共有4个页表所以只需要设置4项。
# 页目录项的结构与页表中项的结构一样，4个字节为1项。
# 例如“$pg0+7”表示：0x00001007，是页目录表中的第1项
# 则第1个页表所在的地址 = 0x00001007 & 0xfffff000 = 0x1000
# 第1个页表的属性标志 = 0x00001007 & 0x00000fff = 0x07，表示该页存在、用户可读写。
	movl $pg0+7,pg_dir		/* set present bit/user r/w */
	movl $pg1+7,pg_dir+4		/*  --------- " " --------- */
	movl $pg2+7,pg_dir+8		/*  --------- " " --------- */
	movl $pg3+7,pg_dir+12		/*  --------- " " --------- */
	movl $pg3+4092,%edi		# edi-> 最后一页的最后一项
	movl $0xfff007,%eax		/*  16Mb - 4096 + 7 (r/w user,p) */
	std
1:	stosl			/* fill pages backwards - more efficient :-) */
	subl $0x1000,%eax		# 每填写好一项，物理地址值减0x1000
	jge 1b
# 设置页目录基址寄存器cr3的值，指向页目录表。cr3中保存的是页目录表的物理地址。
	xorl %eax,%eax		/* pg_dir is at 0x0000 */
	movl %eax,%cr3		/* cr3 - page directory start */
	movl %cr0,%eax
	orl $0x80000000,%eax	# 添上PG标志
	movl %eax,%cr0		/* set paging (PG) bit */
	ret			/* this also flushes prefetch-queue */

# 在改变分页处理标志后要求使用转移指令刷新预取指令队列，这里用的是返回指令ret。
# 该返回指令的另一个作用是将140行压入堆栈中的main程序地址弹出，并跳转到init/main.c程序去运行
	
.align 2
.word 0
# 下面是加载中断描述表寄存器idtr的指令lidt要求的6字节操作数。前2字节是idt表的限长，后4字节
# 是idt表在线性地址空间中的32为基地址。
idt_descr:
	.word 256*8-1		# idt contains 256 entries  共256项，限长=长度-1
	.long idt
.align 2
.word 0
# 下面加载全局描述符表寄存器gdtr的指令lgdt要求的6字节操作数。前2字节是gdt表的限长，后4字节是
# gdt表的线性基地址。这里全局表长度设置为2KB字节（0x7ff即可），因为每8字节组成一个描述符项，
# 所以表中共可有256项。符号_gdt是全局表在本程序中的偏移地址
gdt_descr:
	.word 256*8-1		# so does gdt (not that that's any
	.long gdt		# magic number, but it works for me :^)

	.align 8
idt:	.fill 256,8,0		# idt is uninitialized

# 全局表。前4项分别是空项、代码短描述符、数据段描述符、临时描述符，后面还预留了252项的空间，用于
# 放置所创建任务的局部描述符（LDT）和对应的任务状态段TSS的描述符。
# （0-nul，1-cs，2-ds，3-syscall，4-TSS0，5-LDT0,6-TSS1,7-LDT1,8-TSS2 etc...）
gdt:	.quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x00c09a0000000fff	/* 16Mb */ # 0x08，内核代码段最大长度16MB
	.quad 0x00c0920000000fff	/* 16Mb */ # 0x10，内核数据段最大长度16MB
	.quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252,8,0			/* space for LDT's and TSS's etc */
