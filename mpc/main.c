#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"init.h"
#include	"pool.h"

#define MAXCONF 1000

char *confname[MAXCONF];
char *confval[MAXCONF];
int nconf;

int	predawn = 1;

Conf	conf;

void
flash(void)
{
	int i;
	*(uchar*)(NIMMEM+0x2200) = 0;
	for(i=0; i<1000000; i++)
		;
	*(uchar*)(NIMMEM+0x2200) = 0x2;
	for(i=0; i<1000000; i++)
		;
}

void
main(void)
{
	machinit();
	clockinit();
	confinit();
	xinit();
	trapinit();
	printinit();
	cpminit();
	uartinstall();
	mmuinit();
if(0) {
	predawn = 0;
	print("spllo() = %ux\n", spllo());
	for(;;) {
		print("hello\n");
		delay(1000);
	}
}
	pageinit();
	procinit0();
	initseg();
	links();
	chandevreset();
	swapinit();
	userinit();
predawn = 0;
	schedinit();
}

void
machinit(void)
{
	IMM *io;
	int mf, osc;

	memset(m, 0, sizeof(*m));
	m->delayloop = 20000;
	m->cputype = getpvr()>>16;
	m->iomem = KADDR(INTMEM);

	io = m->iomem;
	osc = 5;
	mf = io->plprcr >> 20;
	m->oscclk = osc;
	m->speed = osc*(mf+1);
	m->cpuhz = m->speed*MHz;	/* general system clock (cycles) */
	m->clockgen = osc*MHz;		/* clock generator frequency (cycles) */
}

void
bootargs(ulong base)
{
print("bootargs = %ux\n", base);
	USED(base);
}

void
init0(void)
{
	int i;

	up->nerrlab = 0;

print("spllo = %ux\n", spllo());

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	up->dot = cclone(up->slash, 0);

	chandevinit();

	if(!waserror()){
		ksetenv("cputype", "power");
		if(cpuserver)
			ksetenv("service", "cpu");
		else
			ksetenv("service", "terminal");
		for(i = 0; i < nconf; i++)
			if(confname[i] && confname[i][0] != '*')
				ksetenv(confname[i], confval[i]);
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	print("to user!!\n");
	touser((void*)(USTKTOP-8));
}

void
userinit(void)
{
	Proc *p;
	Segment *s;
	KMap *k;
	Page *pg;

	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	strcpy(p->text, "*init*");
	strcpy(p->user, eve);
	p->fpstate = FPinit;

	/*
	 * Kernel Stack
	 *
	 * N.B. The -12 for the stack pointer is important.
	 *	4 bytes for gotolabel's return PC
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack+KSTACK-(1+MAXSYSARG)*BY2WD;

	/*
	 * User Stack
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	p->seg[SSEG] = s;
	pg = newpage(1, 0, USTKTOP-BY2PG);
	segpage(s, pg);
	k = kmap(pg);
	bootargs(VA(k));
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, 1);
	s->flushme++;
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memmove((ulong*)VA(k), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

void
exit(int ispanic)
{
	int ms, once;

	lock(&active);
	if(ispanic)
		active.ispanic = ispanic;
	else if(m->machno == 0 && (active.machs & (1<<m->machno)) == 0)
		active.ispanic = 0;
	once = active.machs & (1<<m->machno);
	active.machs &= ~(1<<m->machno);
	active.exiting = 1;
	unlock(&active);

	if(once)
		print("cpu%d: exiting\n", m->machno);
	spllo();
	for(ms = 5*1000; ms > 0; ms -= TK2MS(2)){
		delay(TK2MS(2));
		if(active.machs == 0 && consactive() == 0)
			break;
	}

	if(active.ispanic && m->machno == 0){
		if(cpuserver)
			delay(10000);
		else
			for(;;);
	}
	else
		delay(1000);

//	arch->reset();
}

/*
 *  set up floating point for a new process
 */
void
procsetup(Proc *p)
{
	USED(p);
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc *p)
{
	USED(p);
}

// print without using interrupts
int
iprint(char *fmt, ...)
{
	char buf[PRINTSIZE];
	int n;
	va_list arg;

	va_start(arg, fmt);
	n = doprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);

	putstrn(buf, n);
	uartwait();

	return n;
}

void
confinit(void)
{
	int nbytes;
	ulong pa;

	conf.nmach = 1;		/* processors */
	conf.nproc = 200;	/* processes */

	// hard wire for now
	pa = 0xff200000;		// leave 2 Meg for kernel
	nbytes = 10*1024*1024;	// leave room at the top as well
	
	conf.npage0 = nbytes/BY2PG;
	conf.base0 = pa;
	
	conf.npage1 = 0;
	conf.base1 = 0;

	conf.npage = conf.npage0 + conf.npage1;

	conf.upages = (conf.npage*50)/100;
	conf.ialloc = ((conf.npage-conf.upages)/2)*BY2PG;

	/* set up other configuration parameters */
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.copymode = 0;		/* copy on write */
}

int
isaconfig(char *class, int ctlrno, ISAConf *isa)
{
	char cc[NAMELEN], *p, *q, *r;
	int n;

	sprint(cc, "%s%d", class, ctlrno);
	for(n = 0; n < nconf; n++){
		if(strncmp(confname[n], cc, NAMELEN))
			continue;
		isa->nopt = 0;
		p = confval[n];
		while(*p){
			while(*p == ' ' || *p == '\t')
				p++;
			if(*p == '\0')
				break;
			if(strncmp(p, "type=", 5) == 0){
				p += 5;
				for(q = isa->type; q < &isa->type[NAMELEN-1]; q++){
					if(*p == '\0' || *p == ' ' || *p == '\t')
						break;
					*q = *p++;
				}
				*q = '\0';
			}
			else if(strncmp(p, "port=", 5) == 0)
				isa->port = strtoul(p+5, &p, 0);
			else if(strncmp(p, "irq=", 4) == 0)
				isa->irq = strtoul(p+4, &p, 0);
			else if(strncmp(p, "mem=", 4) == 0)
				isa->mem = strtoul(p+4, &p, 0);
			else if(strncmp(p, "size=", 5) == 0)
				isa->size = strtoul(p+5, &p, 0);
			else if(strncmp(p, "freq=", 5) == 0)
				isa->freq = strtoul(p+5, &p, 0);
			else if(strncmp(p, "dma=", 4) == 0)
				isa->dma = strtoul(p+4, &p, 0);
			else if(isa->nopt < NISAOPT){
				r = isa->opt[isa->nopt];
				while(*p && *p != ' ' && *p != '\t'){
					*r++ = *p++;
					if(r-isa->opt[isa->nopt] >= ISAOPTLEN-1)
						break;
				}
				*r = '\0';
				isa->nopt++;
			}
			while(*p && *p != ' ' && *p != '\t')
				p++;
		}
		return 1;
	}
	return 0;
}

int
cistrcmp(char *a, char *b)
{
	int ac, bc;

	for(;;){
		ac = *a++;
		bc = *b++;
	
		if(ac >= 'A' && ac <= 'Z')
			ac = 'a' + (ac - 'A');
		if(bc >= 'A' && bc <= 'Z')
			bc = 'a' + (bc - 'A');
		ac -= bc;
		if(ac)
			return ac;
		if(bc == 0)
			break;
	}
	return 0;
}