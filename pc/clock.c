#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"

/*
 *  8253 timer
 */
enum
{
	T0cntr=	0x40,		/* counter ports */
	T1cntr=	0x41,		/* ... */
	T2cntr=	0x42,		/* ... */
	Tmode=	0x43,		/* mode port */

	/* commands */
	Latch0=	0x00,		/* latch counter 0's value */
	Load0=	0x30,		/* load counter 0 with 2 bytes */

	/* modes */
	Square=	0x36,		/* perioic square wave */

	Freq=	1193182,	/* Real clock frequency */
};

static ulong delayloop = 1000;

/*
 *  delay for l milliseconds more or less.  delayloop is set by
 *  clockinit() to match the actual CPU speed.
 */
void
delay(int l)
{
	ulong i;

	while(l-- > 0)
		for(i=0; i < delayloop; i++)
			;
}

void
clockinit(void)
{
	ulong x, y;	/* change in counter */

	/*
	 *  set vector for clock interrupts
	 */
	setvec(Clockvec, clock);

	/*
	 *  make clock output a square wave with a 1/HZ period
	 */
	outb(Tmode, Load0|Square);
	outb(T0cntr, (Freq/HZ));	/* low byte */
	outb(T0cntr, (Freq/HZ)>>8);	/* high byte */

	/*
	 *  measure time for delay(10) with current delayloop count
	 */
	outb(Tmode, Latch0);
	x = inb(T0cntr);
	x |= inb(T0cntr)<<8;
	delay(10);
	outb(Tmode, Latch0);
	y = inb(T0cntr);
	y |= inb(T0cntr)<<8;
	x -= y;

	/*
	 *  fix count
	 */
	delayloop = (delayloop*1193*10)/x;
	if(delayloop == 0)
		delayloop = 1;
}

void
clock(Ureg *ur)
{
	Proc *p;
	int nrun = 0;

	m->ticks++;

	checkalarms();
	mouseclock();

	/*
	 *  process time accounting
	 */
	p = m->proc;
	if(p){
		nrun = 1;
		p->pc = ur->pc;
		if (p->state==Running)
			p->time[p->insyscall]++;
	}
	nrun = (nrdy+nrun)*1000;
	MACHP(0)->load = (MACHP(0)->load*19+nrun)/20;

	if(up == 0 || (ur->cs&0xffff) != UESEL || up->state != Running)
		return;

	if(anyready())
		sched();

	/* user profiling clock */
	spllo();		/* in case we fault */
	(*(ulong*)(USTKTOP-BY2WD)) += TK2MS(1);
	splhi();
}
