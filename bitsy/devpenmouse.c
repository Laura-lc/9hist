#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#define	Image	IMAGE
#include	<draw.h>
#include	<memdraw.h>
#include	<cursor.h>
#include	"screen.h"

typedef struct Mouseinfo	Mouseinfo;
typedef struct Mousestate	Mousestate;
typedef struct Calibration	Calibration;

struct Calibration
{
	long	scalex;
	long	scaley;
	long	transx;
	long	transy;
} calibration = {
	-16435,
	23275,
	253,
	-23
};

struct Mousestate
{
	Point	xy;			/* mouse.xy */
	int		buttons;	/* mouse.buttons */
	ulong	counter;	/* increments every update */
	ulong	msec;		/* time of last event */
};

struct Mouseinfo
{
	Mousestate;
	ulong	lastcounter;	/* value when /dev/mouse read */
	Rendez	r;
	Ref;
	QLock;
	int	open;
	int	inopen;
	Mousestate 	queue[16];	/* circular buffer of click events */
	int	ri;	/* read index into queue */
	int	wi;	/* write index into queue */
	uchar	qfull;	/* queue is full */
};

Mouseinfo	mouse;
int		mouseshifted;

int	penmousechanged(void*);
static void	penmousetrack(int b, int x, int y);

enum{
	Qdir,
	Qmouse,
	Qmousein,
	Qmousectl,
};

static Dirtab mousedir[]={
	"mouse",	{Qmouse},		0,			0666,
	"mousein",	{Qmousein},		0,			0220,
	"mousectl",	{Qmousectl},	0,			0220,
};

static uchar buttonmap[8] = {
	0, 1, 2, 3, 4, 5, 6, 7,
};
static int mouseswap;

extern	Memimage*	gscreen;

void
penbutton(int up, int b) {
	if (up)
		mouse.buttons &= ~b;
	else
		mouse.buttons |= b;
	penmousetrack(mouse.buttons, -1, -1);
}

void
pentrackxy(int x, int y) {
	if (x == -1) {
		/* pen up. associate with button 1, 2, 3 up */
		mouse.buttons &= ~0x7;
	} else {
		x = ((x*calibration.scalex)>>16) + calibration.transx;
		y = ((y*calibration.scaley)>>16) + calibration.transy;
		if ((mouse.buttons & 0x7) == 0)
			mouse.buttons |= 0x1;
	}
	penmousetrack(mouse.buttons, x, y);
}

static void
penmousereset(void)
{
	if(!conf.monitor)
		return;
}

static void
penmouseinit(void)
{
	if(!conf.monitor)
		return;
}

static Chan*
penmouseattach(char *spec)
{
	if(!conf.monitor)
		error(Egreg);
	return devattach('m', spec);
}

static Chan*
penmouseclone(Chan *c, Chan *nc)
{
	nc = devclone(c, nc);
	if(c->qid.path != CHDIR)
		incref(&mouse);
	return nc;
}

static int
penmousewalk(Chan *c, char *name)
{
	return devwalk(c, name, mousedir, nelem(mousedir), devgen);
}

static void
penmousestat(Chan *c, char *db)
{
	devstat(c, db, mousedir, nelem(mousedir), devgen);
}

static Chan*
penmouseopen(Chan *c, int omode)
{
	switch(c->qid.path){
	case CHDIR:
		if(omode != OREAD)
			error(Eperm);
		break;
	case Qmouse:
		lock(&mouse);
		if(mouse.open){
			unlock(&mouse);
			error(Einuse);
		}
		mouse.open = 1;
		mouse.ref++;
		unlock(&mouse);
		break;
	case Qmousein:
	/*	error("disabled");	*/
		lock(&mouse);
		if(mouse.inopen){
			unlock(&mouse);
			error(Einuse);
		}
		mouse.inopen = 1;
		unlock(&mouse);
		break;
	default:
		incref(&mouse);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
penmousecreate(Chan*, char*, int, ulong)
{
	if(!conf.monitor)
		error(Egreg);
	error(Eperm);
}

static void
penmouseclose(Chan *c)
{
	if(c->qid.path!=CHDIR && (c->flag&COPEN)){
		lock(&mouse);
		if(c->qid.path == Qmouse)
			mouse.open = 0;
		else if(c->qid.path == Qmousein){
			mouse.inopen = 0;
			unlock(&mouse);
			return;
		}
		--mouse.ref;
		unlock(&mouse);
	}
}

static long
penmouseread(Chan *c, void *va, long n, vlong)
{
	char buf[4*12+1];
	static int map[8] = {0, 4, 2, 6, 1, 5, 3, 7 };
	Mousestate m;

	switch(c->qid.path){
	case CHDIR:
		return devdirread(c, va, n, mousedir, nelem(mousedir), devgen);

	case Qmouse:
		while(penmousechanged(0) == 0)
			sleep(&mouse.r, penmousechanged, 0);

		mouse.qfull = 0;

		/*
		 * No lock of the indicies is necessary here, because ri is only
		 * updated by us, and there is only one mouse reader
		 * at a time.  I suppose that more than one process
		 * could try to read the fd at one time, but such behavior
		 * is degenerate and already violates the calling
		 * conventions for sleep above.
		 */
		if(mouse.ri != mouse.wi) {
			m = mouse.queue[mouse.ri];
			if(++mouse.ri == nelem(mouse.queue))
				mouse.ri = 0;
		} else {
			m = mouse.Mousestate;
		}

		sprint(buf, "m%11d %11d %11d %11lud",
			m.xy.x, m.xy.y,
			m.buttons,
			m.msec);
		mouse.lastcounter = m.counter;
		if(n > 1+4*12)
			n = 1+4*12;
		memmove(va, buf, n);
		return n;
	}
	return 0;
}

static void
setbuttonmap(char* map)
{
	int i, x, one, two, three;

	one = two = three = 0;
	for(i = 0; i < 3; i++){
		if(map[i] == 0)
			error(Ebadarg);
		if(map[i] == '1'){
			if(one)
				error(Ebadarg);
			one = 1<<i;
		}
		else if(map[i] == '2'){
			if(two)
				error(Ebadarg);
			two = 1<<i;
		}
		else if(map[i] == '3'){
			if(three)
				error(Ebadarg);
			three = 1<<i;
		}
		else
			error(Ebadarg);
	}
	if(map[i])
		error(Ebadarg);

	memset(buttonmap, 0, 8);
	for(i = 0; i < 8; i++){
		x = 0;
		if(i & 1)
			x |= one;
		if(i & 2)
			x |= two;
		if(i & 4)
			x |= three;
		buttonmap[x] = i;
	}
}

static long
penmousewrite(Chan *c, void *va, long n, vlong)
{
	char *p;
	Point pt;
	char buf[64], *field[5];
	int nf, b;

	p = va;
	switch(c->qid.path){
	case CHDIR:
		error(Eisdir);

	case Qmousectl:
		if(n >= sizeof(buf))
			n = sizeof(buf)-1;
		strncpy(buf, va, n);
		if(buf[n - 1] == '\n')
			buf[n-1] = 0;
		else
			buf[n] = 0;
		nf = getfields(buf, field, 5, 1, " ");
		if(strcmp(field[0], "swap") == 0){
			if(mouseswap)
				setbuttonmap("123");
			else
				setbuttonmap("321");
			mouseswap ^= 1;
		}
		else if(strcmp(field[0], "calibrate") == 0){
			if (nf == 1) {
				calibration.scalex = 1<<16;
				calibration.scaley = 1<<16;
				calibration.transx = 0;
				calibration.transy = 0;
			} else if (nf == 5) {
				calibration.scalex = strtol(field[1], nil, 0);
				calibration.scaley = strtol(field[2], nil, 0);
				calibration.transx = strtol(field[3], nil, 0);
				calibration.transy = strtol(field[4], nil, 0);
			} else
				print("calibrate %d fields\n", nf);
		}
		return n;

	case Qmousein:
		if(n > sizeof buf-1)
			n = sizeof buf -1;
		memmove(buf, va, n);
		buf[n] = 0;
		p = 0;
		pt.x = strtol(buf+1, &p, 0);
		if(p == 0)
			error(Eshort);
		pt.y = strtol(p, &p, 0);
		if(p == 0)
			error(Eshort);
		b = strtol(p, &p, 0);
		penmousetrack(b, pt.x, pt.y);
		return n;
		
	case Qmouse:
		if(n > sizeof buf-1)
			n = sizeof buf -1;
		memmove(buf, va, n);
		buf[n] = 0;
		p = 0;
		pt.x = strtoul(buf+1, &p, 0);
		if(p == 0)
			error(Eshort);
		pt.y = strtoul(p, 0, 0);
		qlock(&mouse);
		if(ptinrect(pt, gscreen->r))
			penmousetrack(mouse.buttons, pt.x, pt.y);
		qunlock(&mouse);
		return n;
	}

	error(Egreg);
	return -1;
}

Dev penmousedevtab = {
	'm',
	"penmouse",

	penmousereset,
	penmouseinit,
	penmouseattach,
	penmouseclone,
	penmousewalk,
	penmousestat,
	penmouseopen,
	penmousecreate,
	penmouseclose,
	penmouseread,
	devbread,
	penmousewrite,
	devbwrite,
	devremove,
	devwstat,
};

/*
 *  called at interrupt level to update the structure and
 *  awaken any waiting procs.
 */
static void
penmousetrack(int b, int x, int y)
{
	int lastb;

	if (x >= 0)
		mouse.xy = Pt(x, y);
	lastb = mouse.buttons;
	mouse.buttons = b;
	mouse.counter++;
	mouse.msec = TK2MS(MACHP(0)->ticks);

	/*
	 * if the queue fills, we discard the entire queue and don't
	 * queue any more events until a reader polls the mouse.
	 */
	if(!mouse.qfull && lastb != b) {	/* add to ring */
		mouse.queue[mouse.wi] = mouse.Mousestate;
		if(++mouse.wi == nelem(mouse.queue))
			mouse.wi = 0;
		if(mouse.wi == mouse.ri)
			mouse.qfull = 1;
	}
	wakeup(&mouse.r);
}

int
penmousechanged(void*)
{
	return mouse.lastcounter != mouse.counter;
}

Point
penmousexy(void)
{
	return mouse.xy;
}
