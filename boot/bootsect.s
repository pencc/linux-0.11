!
! SYS_SIZE is the number of clicks (16 bytes) to be loaded.
! 0x3000 is 0x30000 bytes = 196kB, more than enough for current
! versions of linux
!
! SYS_SIZE是要加载的系统模块长度，单位是节，16字节为1节，0x3000为
! 0x30000字节=192kB，对于当前版本以及足够了。
SYSSIZE = 0x3000
!
!	bootsect.s		(C) 1991 Linus Torvalds
!
! bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
! iself out of the way to address 0x90000(576KB), and jumps there.
!
! It then loads 'setup.s' directly after itself (0x90200), and load the system
! to 0x10000, by using BIOS interrupts. 
!
! NOTE! currently system is at most 8*65536 bytes(512KB) long. This should be no
! problem, even in the future. I want to keep it simple. This 512 kB
! kernel size should be enough, especially as this doesn't contain the
! buffer cache as in minix
!
! The loader has been made as simple as possible, and continuos
! read errors will result in a unbreakable loop. Reboot by hand. It
! loads pretty fast by getting whole sectors at a time whenever possible.

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text			! 文本段
begtext:
.data			! 数据段
begdata:
.bss			! 未初始化数据段
begbss:
.text

SETUPLEN = 4				! nr(扇区数) of setup-sectors
BOOTSEG  = 0x07c0			! original address(段地址) of boot-sector
INITSEG  = 0x9000			! we move boot here - out of the way
SETUPSEG = 0x9020			! setup starts here
SYSSEG   = 0x1000			! system loaded at 0x10000 (65536-64KB).
ENDSEG   = SYSSEG + SYSSIZE		! where to stop loading

! ROOT_DEV:	0x000 - same type of floppy as boot.根文件系统设备使用与引导时相同的软驱设备
!		0x301 - first partition on first drive etc. 根文件系统设备在第一个硬盘上的第一个分区
ROOT_DEV = 0x306
! 设备号0x306指定根文件系统设备是第二个硬盘的第一个分区。
! 设备号具体值含义如下：
! 设备号 = 主设备号 * 256 + 次设备号
!（主设备号：1-内存，2-磁盘，3-硬盘，4-ttyx，5-tty，6-并行口，7-非命名管道）
! 0x300 - /dev/hd0  代表整个第1个硬盘
! 0x301 - /dev/hd1  代表第1个盘第1个分区
! ...
! 0x304 - /dev/hd4  代表第1个盘第4个分区
! 0x305 - /dev/hd5	代表整个第2个硬盘
! 0x306 - /dev/hd6	代表第2个盘第1个分区
! ...
! 0x309 - /dev/hd9	代表第2个盘第4个分区

entry _start
_start:
	mov	ax,#BOOTSEG
	mov	ds,ax
	mov	ax,#INITSEG
	mov	es,ax
	mov	cx,#256				! 256字=512字节
	sub	si,si
	sub	di,di
	rep						! 重复执行并递减cx值，直到cx=0为止
	movw					! 将DS：SI的内容送至ES：DI，是复制过去
	jmpi	go,INITSEG
	
! 从下面开始，CPU移位到0x90000位置处的代码中执行
! 这里设置几个段寄存器，包括栈寄存器ss和sp。栈指针sp只要指向远大于512字节偏移（即
! 地址0x90200）处都可以。因为从0x90200地址开始处还要放置setup程序。而此时setup
! 程序大约为4个扇区，因此sp要指向大于（0x200 + 0x200 * 4 + 堆栈大小）处。
! 实际上BIOS把引导扇区加载到0x7c00处并转交控制权时，ss = 0x00，sp = 0xfffe
go:	mov	ax,cs			! 将ds、es和ss都置成移动后代码所在的段处（0x9000）
	mov	ds,ax			! 由于程序中有栈操作，因此必须设置堆栈
	mov	es,ax
! put stack at 0x9ff00. -- 将栈指针sp指向0x9ff00（即0x9000：0xff00）处
	mov	ss,ax
	mov	sp,#0xFF00		! arbitrary value >>512

! load the setup-sectors directly after the bootblock.
! Note that 'es' is already set up.

! 利用BIOS中断0x13将setup模块从磁盘第二个扇区开始读到0x90200开始处
! 共读4个扇区。如果读出错，则复位驱动器，并重试，没有退路。
! INT 0x13读扇区使用方法如下：
! ah = 0x02读磁盘扇区到内存； al = 需要读出的扇区数量
! ch = 磁道（柱面）号的低8位； cl = 开始扇区（位0-5），磁道号高两位（位6-7）
! dh = 磁头号； dl = 驱动器号（如果是硬盘则位7要置位）
! es：bx ->指向数据缓冲区；如果出错则CF标志置位，ah中是出错码。
load_setup:
	mov	dx,#0x0000		! drive 0, head 0
	mov	cx,#0x0002		! sector 2, track 0
	mov	bx,#0x0200		! address = 512, in INITSEG
	mov	ax,#0x0200+SETUPLEN	! service 2, nr of sectors
	int	0x13			! read it
	jnc	ok_load_setup		! ok - continue 当进位标记CF位0时跳转
	mov	dx,#0x0000
	mov	ax,#0x0000		! reset the diskette
	int	0x13
	j	load_setup

ok_load_setup:

! Get disk drive parameters, specifically nr of sectors/track
! 取磁盘驱动器参数INT 0x13调用格式和返回信息如下：
! ah = 0x08   dl = 驱动器号（如果是硬盘则要置位7为1）
! 返回信息：
! 如果出错则CF置位，并且ah = 状态码
! ah = 0， al = 0，      bl = 驱动器类型（AT/PS2）
! ch = 最大磁道号的低8位， cl = 每磁道最大扇区数（位0-5）
! dh = 最大磁头数，       dl = 驱动器数量
! es：di -> 软驱磁盘参数表

	mov	dl,#0x00
	mov	ax,#0x0800		! AH=8 is get drive parameters
	int	0x13
	mov	ch,#0x00
! 下一条语句的操作数在cs段寄存器所指的段中。只影响下一条语句。
! 由于本程序代码和数据都被设置在一个段中，即段寄存器cs和ds、es值相同，所以可以不使用该指令
	seg cs
! 下句保存每磁道扇区数，对于软盘来说最大磁道号不超过256，ch已经足够表示它了，
! 因此cl的位6-7肯定为0，之前设置ch=0，因此cx中是每磁道扇区数
	mov	sectors,cx		！把磁盘扇区数放到位置0x00的地方 ？
	mov	ax,#INITSEG
	mov	es,ax			！因为上面取磁盘参数中断改掉了es的值，这里重新改回 ？

! Print some inane message
! BIOS中断0x10功能号ah=0x03，读光标位置。
! 输入： bh = 页号
! 返回： ch = 扫描开始线； cl = 扫描结束线； dh = 行号（0x00顶端）； dl = 列号（0x00最左边）

! BIOS中断0x10功能号ah=0x13，显示字符串。
! 输入：al = 放置光标的方式及规定属性。0x01-表示使用bl中的属性值，光标停在字符串结尾处。
! es:bp此寄存器对指向要显示的字符串起始位置处。cx=显示字符串字符数。bh=显示页面号
! bl = 字符属性。 dh=行号。dl=列号。

	mov	ah,#0x03		! read cursor pos
	xor	bh,bh			! 读光标位置，返回在dx中，dh=行（0-24），dl=列（0-79）
	int	0x10
	
	mov	cx,#24
	mov	bx,#0x0007		! page 0, attribute 7 (normal)
	mov	bp,#msg1
	mov	ax,#0x1301		! write string, move cursor
	int	0x10

! ok, we've written the message, now
! we want to load the system (at 0x10000)64KB

	mov	ax,#SYSSEG
	mov	es,ax			! segment of 0x010000  es=存放system的段地址
	call	read_it 	! 读磁盘上的system模块，es为输入参数
	call	kill_motor  ! 关闭驱动器马达，这样就可以知道驱动器状态了 ？

! After that we check which root-device to use. If the device is
! defined (!= 0), nothing is done and the given device is used.
! Otherwise, either /dev/PS0 (2,28) or /dev/at0 (2,8), depending
! on the number of sectors that the BIOS reports currently.

	seg cs
	mov	ax,root_dev
	cmp	ax,#0
	jne	root_defined
	seg cs
	mov	bx,sectors
	mov	ax,#0x0208		! /dev/ps0 - 1.2Mb
	cmp	bx,#15
	je	root_defined
	mov	ax,#0x021c		! /dev/PS0 - 1.44Mb
	cmp	bx,#18
	je	root_defined
undef_root:
	jmp undef_root
root_defined:
	seg cs
	mov	root_dev,ax

! after that (everyting loaded), we jump to
! the setup-routine loaded directly after
! the bootblock:

	jmpi	0,SETUPSEG

! This routine loads the system at address 0x10000, making sure
! no 64kB boundaries are crossed. We try to load it as fast as
! possible, loading whole tracks whenever we can.
!
! in:	es - starting address segment (normally 0x1000)
!
sread:	.word 1+SETUPLEN	! sectors read of current track
head:	.word 0			! current head
track:	.word 0			! current track

read_it:
	mov ax,es
	test ax,#0x0fff
die:	jne die			! es must be at 64kB boundary
	xor bx,bx		! bx is starting address within segment
rp_read:
	mov ax,es
	cmp ax,#ENDSEG		! have we loaded all yet?
	jb ok1_read
	ret
ok1_read:
	seg cs
	mov ax,sectors
	sub ax,sread
	mov cx,ax
	shl cx,#9
	add cx,bx
	jnc ok2_read
	je ok2_read
	xor ax,ax
	sub ax,bx
	shr ax,#9
ok2_read:
	call read_track
	mov cx,ax
	add ax,sread
	seg cs
	cmp ax,sectors
	jne ok3_read
	mov ax,#1
	sub ax,head
	jne ok4_read
	inc track
ok4_read:
	mov head,ax
	xor ax,ax
ok3_read:
	mov sread,ax
	shl cx,#9
	add bx,cx
	jnc rp_read
	mov ax,es
	add ax,#0x1000
	mov es,ax
	xor bx,bx
	jmp rp_read

read_track:
	push ax
	push bx
	push cx
	push dx
	mov dx,track
	mov cx,sread
	inc cx
	mov ch,dl
	mov dx,head
	mov dh,dl
	mov dl,#0
	and dx,#0x0100
	mov ah,#2
	int 0x13
	jc bad_rt
	pop dx
	pop cx
	pop bx
	pop ax
	ret
bad_rt:	mov ax,#0
	mov dx,#0
	int 0x13
	pop dx
	pop cx
	pop bx
	pop ax
	jmp read_track

!/*
! * This procedure turns off the floppy drive motor, so
! * that we enter the kernel in a known state, and
! * don't have to worry about it later.
! */
kill_motor:
	push dx
	mov dx,#0x3f2
	mov al,#0
	outb
	pop dx
	ret

sectors:
	.word 0

msg1:
	.byte 13,10
	.ascii "Loading system ..."
	.byte 13,10,13,10

.org 508
root_dev:
	.word ROOT_DEV
boot_flag:
	.word 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:
