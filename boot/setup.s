!
!	setup.s		(C) 1991 Linus Torvalds
!
! setup.s is responsible for getting the system data from the BIOS,
! and putting them into the appropriate places in system memory.
! both setup.s and system has been loaded by the bootblock.
!
! This code asks the bios for memory/disk/other parameters, and
! puts them in a "safe" place: 0x90000-0x901FF, ie where the
! boot-block used to be. It is then up to the protected mode
! system to read them from there before the area is overwritten
! for buffer-blocks.

! NOTE! These had better be the same as in bootsect.s!

INITSEG  = 0x9000	! we move boot here - out of the way
SYSSEG   = 0x1000	! system loaded at 0x10000 (65536 64KB).
SETUPSEG = 0x9020	! this is the current segment 本程序所在的段地址

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

entry start
start:

! ok, the read went well so we get current cursor position and save it for
! posterity.

! 这段代码使用BIOS中断读取屏幕当前光标位置（列、行），并保存在内存0x90000处（2字节）。
! 控制台初始化程序会到此处读取该值。
! BIOS中断0x10功能号ah=0x03, 读光标位置。
! 输入：bh = 页号 
! 返回：ch = 扫描开始线; cl = 扫描结束线; dh = 行号（0x00顶端）; dl = 列号（0x00最左边） 
!
! 下句将ds置成INITSEG（0x9000）。

	mov	ax,#INITSEG	! this is done in bootsect already, but...
	mov	ds,ax
	mov	ah,#0x03	! read cursor pos
	xor	bh,bh
	int	0x10		! save it in known place, con_init fetches
	mov	[0],dx		! it from 0x90000.
! Get memory size (extended mem, kB)
! 利用BIOS中断0x15功能号ah=0x88取系统所含扩展内存大小并保存在内存0x90002处。
! 返回：ax = 从0x100000（1M）处开始的扩展内存大小（KB）。若出错则CF置位，ax=出错码。

	mov	ah,#0x88
	int	0x15
	mov	[2],ax		! 将扩展内存数值存在0x90002处（1个字）

! Get video-card data:
! 下面这段用于取显示卡当前显示模式。
! 调用BIOS中断0x10, 功能号ah=0x0f。
! 返回：ah=字符列数; al=显示模式; bh=当前显示页。
! 0x90004（1字）存放当前页; 0x90006存放显示模式; 0x90007存放字符列数。

	mov	ah,#0x0f
	int	0x10
	mov	[4],bx		! bh = display page
	mov	[6],ax		! al = video mode, ah = window width

! check for EGA/VGA and some config parameters
! 调用BIOS中断0x10，附加功能选择方式信息。功能号：ah=0x12, bl=0x10
! 返回：bh = 显示状态。 0x00-彩色模式，I/O端口=0x3dX; 0x01-单色模式，I/O端口=0x3bX。
! bl = 安装的显示内存。 0x00-64K; 0x01-128K; 0x02-192K; 0x03=256K
! cx = 显示卡特性参数（参加程序后对BIOS视频中断0x10的说明）。

	mov	ah,#0x12
	mov	bl,#0x10
	int	0x10
	mov	[8],ax		! 0x90008=？？
	mov	[10],bx		! 0x9000A=安装的显示内存，0x9000B=显示状态（彩色/单色）
	mov	[12],cx		! 0x9000C=显示卡特性参数。

! Get hd0 data
! 取第一个硬盘的信息（复制硬盘参数表）。
! 第1个硬盘参数表的首地址是中断向量0x41的向量值。而第2个硬盘参数表紧接在第1个表后面，
! 中断向量0x46的向量值也指向第2个硬盘的参数表首址。表的长度是16个字节（0x10）。
! 下面两段程序分别复制BIOS有关两个硬盘的参数表，0x90080处存放第1个硬盘的表。0x90090
! 处存放第2个硬盘的表。

! 从内存指定位置读取一个长指针值并放入ds和si寄存器中。ds中放段地址，si是段内偏移地址。
! 这里是把内存地址 4 * 0x41 (=0x104)处保存的4个字节（段和偏移值）读出。
	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x41]		! 取中断向量0x41的值，也即hd0参数表的地址->ds:si
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0080		! 传输的目的地址：0x9000:0x0080->es:di
	mov	cx,#0x10		! 共传输16字节
	rep
	movsb

! Get hd1 data

	mov	ax,#0x0000
	mov	ds,ax
	lds	si,[4*0x46]		! 取中断向量0x46的值，也即hd1参数表的地址->ds:si
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090		! 传输的目的地址：0x9000:0x0090->es:di
	mov	cx,#0x10
	rep
	movsb

! Check that there IS a hd1 :-)
! 检查系统是否有第2个硬盘。如果没有则把第2个表清零。
! 利用BIOS中断调用0x13的取盘类型功能，功能号ah=0x15
! 输入：dl=驱动器号（0x8X是硬盘：0x80指第1个硬盘，0x81第2个硬盘）
! 输出：ah=类型码; 00-没有这个盘，CF置位; 01-是软驱，没有change-line支持;
! 				 02-是软驱（或其它可移动设备），有change-line支持; 03-是硬盘。

	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13
	jc	no_disk1
	cmp	ah,#3
	je	is_disk1
no_disk1:
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	mov	ax,#0x00
	rep
	stosb
is_disk1:

! now we want to move to protected mode ...

	cli			! no interrupts allowed !

! first we move the system to it's rightful place
! bootsect引导程序将system模块读入到0x10000（64KB）开始的位置。由于但是假设system
! 模块最大长度不会超过0x80000（512KB），即其末端不会超过内存地址0x90000,所以bootsect 
! 会把自己移动到0x90000开始的地方，并把setup加载到它的后面。

	mov	ax,#0x0000
	cld			! 'direction'=0, movs moves forward
do_move:
	mov	es,ax		! destination segment  es:di是目的地址（初始为0x0:0x0）
	add	ax,#0x1000
	cmp	ax,#0x9000	! 已经把最后一段（从0x8000段开始的64KB）代码移动完 ？
	jz	end_move
	mov	ds,ax		! source segment
	sub	di,di
	sub	si,si
	mov 	cx,#0x8000	! 移动0x8000字（64KB字节）
	rep
	movsw
	jmp	do_move

! then we load the segment descriptors
! 加载段描述符
! 进入保护模式中运行之前，需要首先设置号需要使用的段描述符表。这里需要设置全局描述符表和中断描述符表。
! 下面指令lidt用于加载中断描述符表（IDT）寄存器。它的操作数（idt_48）有6字节。前2字节
! （字节0-1）是描述符表的字节长度值; 后4字节（字节2-5）是描述符表的32为线性基地址，其形式见下面注释。
! 中断描述符表中的每一个8字节表项指出发生中断时需要调用的代码信息。与中断向量相似，但要包含更多的信息。
! lgdt指令用于加载全局描述符表（GDT）寄存器，其操作数格式与lidt指令的相同。全局描述符表中的每个描述符
! 项（8字节）描述了保护模式下数据段和代码段的信息。其中包括段的最大长度限制、段的线性地址基址、段的特权级、
! 段是否在内存、读写许可权以及其它一些保护模式运行的标志。


end_move:
	mov	ax,#SETUPSEG	! right, forgot this at first. didn't work :-)
	mov	ds,ax
	lidt	idt_48		! load idt with 0,0
	lgdt	gdt_48		! load gdt with whatever appropriate

! that was painless, now we enable A20
! 为了能够使用1MB以上的物理内存，我们首先开启A20地址线。至于是否真正开启了A20地址线，head.S中有测试工作

	call	empty_8042	
	mov	al,#0xD1		! command write
	out	#0x64,al
	call	empty_8042
	mov	al,#0xDF		! A20 on
	out	#0x60,al
	call	empty_8042

! well, that went ok, I hope. Now we have to reprogram the interrupts :-(
! we put them right after the intel-reserved hardware interrupts, at
! int 0x20-0x2F. There they won't mess up anything. Sadly IBM really
! messed this up with the original PC, and they haven't been able to
! rectify it afterwards. Thus the bios puts interrupts at 0x08-0x0f,
! which is used for the internal hardware interrupts as well. We just
! have to reprogram the 8259's, and it isn't fun.

	mov	al,#0x11		! initialization sequence
	out	#0x20,al		! send it to 8259A-1	发送到8259主芯片
	.word	0x00eb,0x00eb		! jmp $+2, jmp $+2
	out	#0xA0,al		! and to 8259A-2		发送到8259从芯片
	.word	0x00eb,0x00eb
	mov	al,#0x20		! start of hardware int's (0x20)
	out	#0x21,al		! 送主芯片ICW2命令字，设置起始中断号，要送奇端口
! Linux系统硬件中断号被设置成从0x20开始。
	.word	0x00eb,0x00eb
	mov	al,#0x28		! start of hardware int's 2 (0x28)
	out	#0xA1,al		! 送从芯片ICW2命令字，从芯片的起始中断号
	.word	0x00eb,0x00eb
	mov	al,#0x04		! 8259-1 is master
	out	#0x21,al		! 送主芯片ICW3命令字，主芯片的IR2连从芯片INT。
	.word	0x00eb,0x00eb
	mov	al,#0x02		! 8259-2 is slave
	out	#0xA1,al		! 送从芯片ICW3命令，表示从芯片INT连主芯片IR2引脚上。
	.word	0x00eb,0x00eb
	mov	al,#0x01		! 8086 mode for both
	out	#0x21,al		! 送主片ICW4命令字。8086模式：普通EOI、非缓冲方式。
						! 需发指令来复位。初始化结束。
	.word	0x00eb,0x00eb
	out	#0xA1,al		! 送从芯片ICW4命令字，内容同上。
	.word	0x00eb,0x00eb
	mov	al,#0xFF		! mask off all interrupts for now
	out	#0x21,al		! 屏蔽主芯片所有中断请求
	.word	0x00eb,0x00eb
	out	#0xA1,al		! 屏蔽从芯片所有中断请求

! well, that certainly wasn't fun :-(. Hopefully it works, and we don't
! need no steenking BIOS anyway (except for the initial loading :-).
! The BIOS-routine wants lots of unnecessary data, and it's less
! "interesting" anyway. This is how REAL programmers do it.
!
! Well, now's the time to actually move into protected mode. To make
! things as simple as possible, we do no register set-up or anything,
! we let the gnu-compiled 32-bit programs do that. We just jump to
! absolute address 0x00000, in 32-bit protected mode.
	mov	ax,#0x0001	! protected mode (PE) bit
	lmsw	ax		! This is it! 加载机器状态字
	jmpi	0,8		! jmp offset 0 of segment 8 (cs)

! This routine checks that the keyboard command queue is empty
! No timeout is used - if this hangs there is something wrong with
! the machine, and we probably couldn't proceed anyway.
empty_8042:
	.word	0x00eb,0x00eb	! 这是两个跳转指令的机器码（跳到下一句），相当于延时操作
	in	al,#0x64	! 8042 status port	读AT键盘控制器状态寄存器
	test	al,#2		! is input buffer full?
	jnz	empty_8042	! yes - loop
	ret

! 全局描述符表开始处。描述符表由多个8字节长的描述符项组成。这里给出了3个描述符项目。
! 第1项无用，但须存在。第2项是系统代码段描述符，第3项是系统数据段描述符。
gdt:
	.word	0,0,0,0		! dummy		第1个描述符，不用

! 在GDT表中这里的偏移量是0x08。它是内核代码段选择符的值。
	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0	代码段基地址
	.word	0x9A00		! code read/exec	代码段为只读、可执行
	.word	0x00C0		! granularity=4096, 386

	.word	0x07FF		! 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		! base address=0	数据段基地址
	.word	0x9200		! data read/write
	.word	0x00C0		! granularity=4096, 386

! CPU要求在进入保护模式之前需设置IDT表，因此这里先设置一个长度为0的空表
idt_48:
	.word	0			! idt limit=0
	.word	0,0			! idt base=0L

gdt_48:
	.word	0x800		! gdt limit=2048, 256 GDT entries（2048/8）
	.word	512+gdt,0x9	! gdt base = 0X9xxxx
	
.text
endtext:
.data
enddata:
.bss
endbss:
