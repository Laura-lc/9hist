#include "mem.h"

#define OP16	BYTE	$0x66
#define NOP	XCHGL	AX,AX

/*
 *	We assume that b.com got us into 32 bit mode already.  We are now
 *	running with a PC == origin & ~KZERO.
 */
TEXT	origin(SB),$0

	CLI

	/*
	 * Clear BSS
	 */
	LEAL	edata-KZERO(SB),SI
	MOVL	SI,DI
	ADDL	$4,DI
	MOVL	$0,AX
	MOVL	AX,(SI)
	LEAL	end-KZERO(SB),CX
	SUBL	DI,CX
	SHRL	$2,CX
	CLD; REP; MOVSL

	/*
	 *  make a bottom level page table page that maps the first
	 *  16 meg of physical memory
	 */
	LEAL	tpt-KZERO(SB),AX	/* get phys addr of temporary page table */
	ADDL	$(BY2PG-1),AX		/* must be page alligned */
	ANDL	$(~(BY2PG-1)),AX	/* ... */
	MOVL	$(1024),CX		/* pte's per page */
	MOVL	$((((1024)-1)<<PGSHIFT)|PTEVALID|PTEKERNEL|PTEWRITE),BX
setpte:
	MOVL	BX,-4(AX)(CX*4)
	SUBL	$(1<<PGSHIFT),BX
	LOOP	setpte

	/*
	 *  make a top level page table page that maps the first
	 *  16 meg of memory to 0 thru 16meg and to KZERO thru KZERO+16meg
	 */
	MOVL	AX,BX
	ADDL	$(BY2PG),AX
	ADDL	$(PTEVALID|PTEKERNEL|PTEWRITE),BX
	MOVL	BX,0(AX)
	MOVL	BX,((((KZERO>>1)&0x7FFFFFFF)>>(2*PGSHIFT-1-4))+0)(AX)

	/*
	 *  point processor to top level page & turn on paging
	 */
	MOVL	AX,CR3
	MOVL	CR0,AX
	ORL	$0X80000000,AX
	ANDL	$~(0x8|0x2),AX	/* TS=0, MP=0 */
	MOVL	AX,CR0

	/*
	 *  use a jump to an absolute location to get the PC into
	 *  KZERO.
	 */
	LEAL	tokzero(SB),AX
	JMP*	AX

TEXT	tokzero(SB),$0

	/*
	 *  stack and mach
	 */
	MOVL	$mach0(SB),SP
	MOVL	SP,m(SB)
	MOVL	$0,0(SP)
	ADDL	$(MACHSIZE-4),SP	/* start stack under machine struct */
	MOVL	$0, up(SB)

	/*
	 *  clear flags
	 */
	MOVL	$0,AX
	PUSHL	AX
	POPFL

	CALL	main(SB)

loop:
	JMP	loop

GLOBL	mach0+0(SB), $MACHSIZE
GLOBL	up(SB), $4
GLOBL	m(SB), $4
GLOBL	tpt(SB), $(BY2PG*3)

/*
 *  gdt to get us to 32-bit/segmented/unpaged mode
 */
TEXT	tgdt(SB),$0

	/* null descriptor */
	LONG	$0
	LONG	$0

	/* data segment descriptor for 4 gigabytes (PL 0) */
	LONG	$(0xFFFF)
	LONG	$(SEGG|SEGB|(0xF<<16)|SEGP|SEGPL(0)|SEGDATA|SEGW)

	/* exec segment descriptor for 4 gigabytes (PL 0) */
	LONG	$(0xFFFF)
	LONG	$(SEGG|SEGD|(0xF<<16)|SEGP|SEGPL(0)|SEGEXEC|SEGR)

/*
 *  pointer to initial gdt
 */
TEXT	tgdtptr(SB),$0

	WORD	$(3*8)
	LONG	$tgdt-KZERO(SB)

/*
 *  input a byte
 */
TEXT	inb(SB),$0

	MOVL	p+0(FP),DX
	XORL	AX,AX
	INB
	RET

/*
 *  input a string of bytes from a port
 */
TEXT	insb(SB),$0

	MOVL	p+0(FP),DX
	MOVL	a+4(FP),DI
	MOVL	c+8(FP),CX
	CLD; REP; INSB
	RET

/*
 *  output a byte
 */
TEXT	outb(SB),$0

	MOVL	p+0(FP),DX
	MOVL	b+4(FP),AX
	OUTB
	RET

/*
 *  output a string of bytes to a port
 */
TEXT	outsb(SB),$0

	MOVL	p+0(FP),DX
	MOVL	a+4(FP),SI
	MOVL	c+8(FP),CX
	CLD; REP; OUTSB
	RET

/*
 * input a short from a port
 */
TEXT	ins(SB), $0

	MOVL	p+0(FP), DX
	XORL	AX, AX
	OP16; INL
	RET

/*
 *  input a string of shorts from a port
 */
TEXT	inss(SB),$0

	MOVL	p+0(FP),DX
	MOVL	a+4(FP),DI
	MOVL	c+8(FP),CX
	CLD; REP; OP16; INSL
	RET

/*
 * input a long from a port
 */
TEXT	inl(SB), $0

	MOVL	p+0(FP), DX
	XORL	AX, AX
	INL
	RET

/*
 *  input a string of longs from a port
 */
TEXT	insl(SB),$0

	MOVL	p+0(FP),DX
	MOVL	a+4(FP),DI
	MOVL	c+8(FP),CX
	CLD; REP; INSL
	RET

/*
 * output a short to a port
 */
TEXT	outs(SB), $0
	MOVL	p+0(FP), DX
	MOVL	s+4(FP), AX
	OP16; OUTL
	RET

/*
 *  output a string of shorts to a port
 */
TEXT	outss(SB),$0

	MOVL	p+0(FP),DX
	MOVL	a+4(FP),SI
	MOVL	c+8(FP),CX
	CLD; REP; OP16; OUTSL
	RET

/*
 * output a long to a port
 */
TEXT	outl(SB), $0
	MOVL	p+0(FP), DX
	MOVL	s+4(FP), AX
	OUTL
	RET

/*
 *  output a string of longs to a port
 */
TEXT	outsl(SB),$0

	MOVL	p+0(FP),DX
	MOVL	a+4(FP),SI
	MOVL	c+8(FP),CX
	CLD; REP; OUTSL
	RET

/*
 *  test and set
 */
TEXT	tas(SB),$0
	MOVL	$0xdeadead,AX
	MOVL	l+0(FP),BX
	XCHGL	AX,(BX)
	RET

/*
 *  routines to load/read various system registers
 */
GLOBL	idtptr(SB),$6
TEXT	putidt(SB),$0		/* interrupt descriptor table */
	MOVL	t+0(FP),AX
	MOVL	AX,idtptr+2(SB)
	MOVL	l+4(FP),AX
	MOVW	AX,idtptr(SB)
	MOVL	idtptr(SB),IDTR
	RET

GLOBL	gdtptr(SB),$6
TEXT	putgdt(SB),$0		/* global descriptor table */
	MOVL	t+0(FP),AX
	MOVL	AX,gdtptr+2(SB)
	MOVL	l+4(FP),AX
	MOVW	AX,gdtptr(SB)
	MOVL	gdtptr(SB),GDTR
	RET

TEXT	putcr3(SB),$0		/* top level page table pointer */
	MOVL	t+0(FP),AX
	MOVL	AX,CR3
	RET

TEXT	puttr(SB),$0		/* task register */
	MOVL	t+0(FP),AX
	MOVW	AX,TASK
	RET

TEXT	getcr0(SB),$0		/* coprocessor bits */
	MOVL	CR0,AX
	RET

TEXT	getcr2(SB),$0		/* fault address */
	MOVL	CR2,AX
	RET

#define	FPOFF\
	WAIT;\
	MOVL	CR0,AX;\
	ORL	$0x24,AX	/* EM=1, NE=1 */;\
	MOVL	AX,CR0

#define	FPON\
	MOVL	CR0,AX;\
	ANDL	$~0x4,AX	/* EM=0 */;\
	MOVL	AX,CR0
	
TEXT	fpoff(SB),$0		/* turn off floating point */
	FPOFF
	RET

TEXT	fpinit(SB),$0		/* turn on & init the floating point */
	FPON
	FINIT
	WAIT
	PUSHW	$0x033E
	FLDCW	0(SP)		/* ignore underflow/precision, signal others */
	POPW	AX
	WAIT
	RET

TEXT	fpsave(SB),$0		/* save floating point state and turn off */
	MOVL	p+0(FP),AX
	WAIT
	FSAVE	0(AX)
	FPOFF
	RET

TEXT	fprestore(SB),$0	/* turn on floating point and restore regs */
	FPON
	MOVL	p+0(FP),AX
	FRSTOR	0(AX)
	WAIT
	RET

TEXT	fpstatus(SB),$0		/* get floating point status */
	FSTSW	AX
	RET

TEXT	fpenv(SB),$0		/* save floating point environment without waiting */
	MOVL	p+0(FP),AX
	FSTENV	0(AX)
	RET

/*
 *  special traps
 */
TEXT	intr0(SB),$0
	PUSHL	$0
	PUSHL	$0
	JMP	intrcommon
TEXT	intr1(SB),$0
	PUSHL	$0
	PUSHL	$1
	JMP	intrcommon
TEXT	intr2(SB),$0
	PUSHL	$0
	PUSHL	$2
	JMP	intrcommon
TEXT	intr3(SB),$0
	PUSHL	$0
	PUSHL	$3
	JMP	intrcommon
TEXT	intr4(SB),$0
	PUSHL	$0
	PUSHL	$4
	JMP	intrcommon
TEXT	intr5(SB),$0
	PUSHL	$0
	PUSHL	$5
	JMP	intrcommon
TEXT	intr6(SB),$0
	PUSHL	$0
	PUSHL	$6
	JMP	intrcommon
TEXT	intr7(SB),$0
	PUSHL	$0
	PUSHL	$7
	JMP	intrcommon
TEXT	intr8(SB),$0
	PUSHL	$8
	JMP	intrscommon
TEXT	intr9(SB),$0
	PUSHL	$0
	PUSHL	$9
	JMP	intrcommon
TEXT	intr10(SB),$0
	PUSHL	$10
	JMP	intrscommon
TEXT	intr11(SB),$0
	PUSHL	$11
	JMP	intrscommon
TEXT	intr12(SB),$0
	PUSHL	$12
	JMP	intrscommon
TEXT	intr13(SB),$0
	PUSHL	$13
	JMP	intrscommon
TEXT	intr14(SB),$0
	PUSHL	$14
	JMP	intrscommon
TEXT	intr15(SB),$0
	PUSHL	$0
	PUSHL	$15
	JMP	intrcommon
TEXT	intr16(SB),$0
	PUSHL	$0
	PUSHL	$16
	JMP	intrcommon
TEXT	intr24(SB),$0
	PUSHL	$0
	PUSHL	$24
	JMP	intrcommon
TEXT	intr25(SB),$0
	PUSHL	$0
	PUSHL	$25
	JMP	intrcommon
TEXT	intr26(SB),$0
	PUSHL	$0
	PUSHL	$26
	JMP	intrcommon
TEXT	intr27(SB),$0
	PUSHL	$0
	PUSHL	$27
	JMP	intrcommon
TEXT	intr28(SB),$0
	PUSHL	$0
	PUSHL	$28
	JMP	intrcommon
TEXT	intr29(SB),$0
	PUSHL	$0
	PUSHL	$29
	JMP	intrcommon
TEXT	intr30(SB),$0
	PUSHL	$0
	PUSHL	$30
	JMP	intrcommon
TEXT	intr31(SB),$0
	PUSHL	$0
	PUSHL	$31
	JMP	intrcommon
TEXT	intr32(SB),$0
	PUSHL	$0
	PUSHL	$32
	JMP	intrcommon
TEXT	intr33(SB),$0
	PUSHL	$0
	PUSHL	$33
	JMP	intrcommon
TEXT	intr34(SB),$0
	PUSHL	$0
	PUSHL	$34
	JMP	intrcommon
TEXT	intr35(SB),$0
	PUSHL	$0
	PUSHL	$35
	JMP	intrcommon
TEXT	intr36(SB),$0
	PUSHL	$0
	PUSHL	$36
	JMP	intrcommon
TEXT	intr37(SB),$0
	PUSHL	$0
	PUSHL	$37
	JMP	intrcommon
TEXT	intr38(SB),$0
	PUSHL	$0
	PUSHL	$38
	JMP	intrcommon
TEXT	intr39(SB),$0
	PUSHL	$0
	PUSHL	$39
	JMP	intrcommon
TEXT	intr64(SB),$0
	PUSHL	$0
	PUSHL	$64
	JMP	intrcommon
TEXT	intrbad(SB),$0
	PUSHL	$0
	PUSHL	$0x1ff
	JMP	intrcommon

intrcommon:
	PUSHL	DS
	PUSHL	ES
	PUSHL	FS
	PUSHL	GS
	PUSHAL
	MOVL	$(KDSEL),AX
	MOVW	AX,DS
	MOVW	AX,ES
	LEAL	0(SP),AX
	PUSHL	AX
	CALL	trap(SB)
	POPL	AX
	POPAL
	NOP
	POPL	GS
	POPL	FS
	POPL	ES
	POPL	DS
	NOP
	ADDL	$8,SP	/* error code and trap type */
	IRETL

TEXT forkret(SB), $0
	POPL	AX
	POPAL
	NOP
	POPL	GS
	POPL	FS
	POPL	ES
	POPL	DS
	NOP
	ADDL	$8,SP	/* error code and trap type */
	IRETL

intrscommon:
	PUSHL	DS
	PUSHL	ES
	PUSHL	FS
	PUSHL	GS
	PUSHAL
	MOVL	$(KDSEL),AX
	MOVW	AX,DS
	MOVW	AX,ES
	LEAL	0(SP),AX
	PUSHL	AX
	CALL	trap(SB)
	POPL	AX
	POPAL
	NOP
	POPL	GS
	POPL	FS
	POPL	ES
	POPL	DS
	NOP
	ADDL	$8,SP	/* error code and trap type */
	IRETL

/*
 *  interrupt level is interrupts on or off
 */
TEXT	spllo(SB),$0
	PUSHFL
	POPL	AX
	STI
	RET

TEXT	splhi(SB),$0
	PUSHFL
	POPL	AX
	CLI
	RET

TEXT	splx(SB),$0
	MOVL	s+0(FP),AX
	PUSHL	AX
	POPFL
	RET

TEXT	getstatus(SB),$0
	PUSHFL
	POPL	AX
	RET

/*
 *  do nothing whatsoever till interrupt happens
 */
TEXT	idle(SB),$0
	HLT
	RET

/*
 *  label consists of a stack pointer and a PC
 */
TEXT	gotolabel(SB),$0
	MOVL	l+0(FP),AX
	MOVL	0(AX),SP	/* restore sp */
	MOVL	4(AX),AX	/* put return pc on the stack */
	MOVL	AX,0(SP)
	MOVL	$1,AX		/* return 1 */
	RET

TEXT	setlabel(SB),$0
	MOVL	l+0(FP),AX
	MOVL	SP,0(AX)	/* store sp */
	MOVL	0(SP),BX	/* store return pc */
	MOVL	BX,4(AX)
	MOVL	$0,AX		/* return 0 */
	RET

/*
 *  Used to get to the first process.
 *  Set up an interrupt return frame and IRET to user level.
 */
TEXT	touser(SB),$0
	PUSHL	$(UDSEL)		/* old ss */
	MOVL	sp+0(FP),AX		/* old sp */
	PUSHL	AX
	PUSHFL				/* old flags */
	PUSHL	$(UESEL)		/* old cs */
	PUSHL	$(UTZERO+32)		/* old pc */
	MOVL	$(UDSEL),AX
	MOVW	AX,DS
	MOVW	AX,ES
	MOVW	AX,GS
	MOVW	AX,FS
	IRETL

/*
 *  set configuration register
 */
TEXT	config(SB),$0
	MOVL	l+0(FP),AX
	MOVL	$0x3F3,DX
	OUTB
	OUTB
	RET

#define SRX	0x3C4		/* index to sequence registers */
#define	SR	0x3C5		/* sequence registers */
#define Smmask	0x02		/*  map mask */

/*
 *  invert the ldepth 0 bitmap
 */
TEXT	l0update(SB),$0
	MOVL	from+4(FP),SI
	MOVL	to+0(FP),DI
	MOVL	len+8(FP),BX

	MOVL	BX,CX
	SHRL	$2,CX
	JZ	l01
l00:
	MOVL	-4(SI)(CX*4),AX
	NOTL	AX
	MOVL	AX,-4(DI)(CX*4)
	LOOP	l00
l01:
	MOVL	BX,CX
	ANDL	$(~3),BX
	ANDL	$3,CX
	JZ	l03
	ADDL	BX,SI
	ADDL	BX,DI
l02:
	MOVB	-1(SI)(CX*1),AL
	NOTL	AX
	MOVB	AL,-1(DI)(CX*1)
	LOOP	l02
l03:
	RET

/*
 *  separate ldepth 1 bitmap into 2 bit planes
 */
TEXT	l1update(SB),$0
	XORL	AX,AX
	MOVL	from+4(FP),SI
	MOVL	to+0(FP),DI
	MOVL	len+8(FP),CX
	MOVB	$(Smmask),AL
	MOVW	$(SRX),DX
	OUTB
l10:
	MOVL	-4(SI)(CX*2),DX
	MOVB	DL,AL
	MOVL	l1septab(SB)(AX*4),BX
	SHLL	$4,BX
	RORL	$8,DX
	MOVB	DL,AL
	ORL	l1septab(SB)(AX*4),BX
	RORL	$12,BX
	RORL	$8,DX
	MOVB	DL,AL
	ORL	l1septab(SB)(AX*4),BX
	SHLL	$4,BX
	RORL	$8,DX
	MOVB	DL,AL
	ORL	l1septab(SB)(AX*4),BX
	ROLL	$8,BX
	MOVW	$(SR),DX
	MOVB	$0x5,AL			/* write lo order bits to bit planes 1 & 3 */
	OUTB
	MOVW	BX,-2(DI)(CX*1)
	SHRL	$16,BX			/* write hi order bits to bit planes 0 & 2 */
	MOVB	$0xA,AL
	OUTB
	MOVW	BX,-2(DI)(CX*1)
	LOOP	l10
	RET

/*
 *  separate ldepth 2 bitmap into 4 bit planes
 */
TEXT	l2update(SB),$0
	XORL	AX,AX
	MOVL	from+4(FP),SI
	MOVL	to+0(FP),DI
	MOVL	len+8(FP),CX
	MOVB	$(Smmask),AL
	MOVW	$(SRX),DX
	OUTB
l20:
	MOVL	-4(SI)(CX*4),DX
	MOVB	DL,AL
	MOVL	l2septab(SB)(AX*4),BX
	SHLL	$2,BX
	SHRL	$8,DX
	MOVB	DL,AL
	ORL	l2septab(SB)(AX*4),BX
	SHLL	$2,BX
	SHRL	$8,DX
	MOVB	DL,AL
	ORL	l2septab(SB)(AX*4),BX
	SHLL	$2,BX
	SHRL	$8,DX
	MOVB	DL,AL
	ORL	l2septab(SB)(AX*4),BX
	MOVW	$(SR),DX
	MOVB	$0x1,AL			/* plane 3 */
	OUTB
	MOVB	BX,-1(DI)(CX*1)
	MOVB	$0x2,AL			/* plane 2 */
	OUTB
	SHRL	$8,BX
	MOVB	BX,-1(DI)(CX*1)
	MOVB	$0x4,AL			/* plane 1 */
	OUTB
	SHRL	$8,BX
	MOVB	BX,-1(DI)(CX*1)
	MOVB	$0x8,AL			/* plane 0*/
	OUTB
	SHRL	$8,BX
	MOVB	BX,-1(DI)(CX*1)
	LOOP	l20
	RET

/*
 * The DP8390 ethernet chip needs some time between
 * successive chip selects, so we force a jump into
 * the instruction stream to break the pipeline.
 */
TEXT dp8390inb(SB), $0
	MOVL	p+0(FP),DX
	XORL	AX,AX				/* CF = 0 */
	INB

	JCC	_dp8390inb0			/* always true */
	MOVL	AX,AX

_dp8390inb0:
	RET

TEXT dp8390outb(SB), $0
	MOVL	p+0(FP),DX
	MOVL	b+4(FP),AX
	OUTB

	CLC					/* CF = 0 */
	JCC	_dp8390outb0			/* always true */
	MOVL	AX,AX

_dp8390outb0:
	RET

/*
 * dsp outb string called from devdsp.c
 */
	TEXT	dspoutb+0(SB), $0

	MOVL	a+4(FP), BX
	MOVL	n+8(FP), CX

	MOVL	base+0(FP), DX
	ADDL	$2, DX			/* Pcontrol */

	MOVL	c2+12(FP), DI
	MOVL	c3+16(FP), SI

dsploop:
	MOVL	DI, AX			/* normal */
	OUTB

	SUBL	$1, CX
	CMPL	CX, $0
	JLT	dspout

	SUBL	$2, DX			/* Pdata */
	MOVB	(BX), AX
	ADDL	$1, BX
	OUTB

	ADDL	$2, DX			/* Pcontrol */
	MOVL	SI, AX			/* strobe */
	OUTB

	JMP	dsploop

dspout:
	RET
