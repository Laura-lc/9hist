#include "../port/portfns.h"

void	archinit(void);
int	brgalloc(void);
void	brgfree(int);
ulong	baudgen(int, int);
int	cistrcmp(char*, char*);
int	cistrncmp(char*, char*, int);
void	clockcheck(void);
void	clockinit(void);
void	clockintr(Ureg*);
void	clrfptrap(void);
#define coherence()		// not need on single processor machines
void	cpminit(void);
int	cpuidentify(void);
void	cpuidprint(void);
void	dcflush(void*, ulong);
void	delay(int);
ulong	draminit(ulong*);
void	dtlbmiss(void);
void	dumpregs(Ureg*);
//void	eieio(void);
#define	eieio()
void	evenaddr(ulong);
void	faultpower(Ureg*, ulong addr, int read);
void	firmware(int);
void	fpinit(void);
int	fpipower(Ureg*);
void	fpoff(void);
ulong	fpstatus(void);
char*	getconf(char*);
ulong	getdar(void);
ulong	getdec(void);
ulong	getdepn(void);
ulong	getdsisr(void);
ulong	getimmr(void);
ulong	getmsr(void);
ulong	getpvr(void);
ulong	gettbl(void);
ulong	gettbu(void);
void	gotopc(ulong);
void	icflush(void*, ulong);
void	idle(void);
int	iprint(char*, ...);
void	intr(Ureg*);
void	intrenable(int, void (*)(Ureg*, void*), void*, int);
int	intrstats(char*, int);
void	intrvec(void);
void	itlbmiss(void);
int	isaconfig(char*, int, ISAConf*);
void	kbdinit(void);
void	kbdreset(void);
void	kernelmmu(void);
void	links(void);
void	mapfree(RMap*, ulong, int);
void	mapinit(RMap*, Map*, int);
void	mathinit(void);
void	mmuinit(void);
ulong*	mmuwalk(ulong*, ulong, int);
#define	procrestore(p)
void	procsave(Proc*);
void	procsetup(Proc*);
void	putdec(ulong);
void	putmsr(ulong);
ulong	rmapalloc(RMap*, ulong, int, int);
void	screeninit(void);
void	setpanic(void);
int	screenprint(char*, ...);			/* debugging */
#define screenputs(x, y)		// no screen
ulong	sdraminit(ulong*);
int	segflush(void*, ulong);
void	spireset(void);
long	spioutin(void*, long, void*);
int	tas(void*);
void	touser(void*);
void	trapinit(void);
void	trapvec(void);
void	uartinstall(void);
void	uartwait(void);	/* debugging */
void	wbflush(void);

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
ulong	getcallerpc(void*);

// identity map between kernel physical and virtual addresses
#define KADDR(a)	((void*)(a))
#define PADDR(a)	((ulong)(a))

/* IBM bit field order */
#define	IBIT(b)		((ulong)1<<(31-(b)))
#define	SIBIT(n)	((ushort)1<<(15-(n)))

/* compatibility with inf2.1 */
#define	bpenumenv(n)	((char*)0)

#define IOREGS(x, T)	((T*)((char*)m->iomem+(x)))