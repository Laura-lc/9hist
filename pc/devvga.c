#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

#include	<libg.h>
#include	<gnot.h>
#include	"devtab.h"
#include	"vga.h"

/*
 *  Driver for various VGA cards
 */

char	monitor[NAMELEN];	/* monitor name and type */
char	vgacard[NAMELEN];	/* vga card type */
struct screeninfo {
	int	maxx, maxy;	/* current bits per screen */
	int	packed;		/* 0=planar, 1=packed */
	int	interlaced;	/* != 0 if interlaced */
} screeninfo;

enum {
	Qdir=		0,
	Qvgamonitor=	1,
	Qvgasize=	2,
	Qvgatype=	3,
	Qvgaport=	4,
	Nvga=		4,
};

Dirtab vgadir[]={
	"vgamonitor",	{Qvgamonitor},	0,		0666,
	"vgatype",	{Qvgatype},	0,		0666,
	"vgasize",	{Qvgasize},	0,		0666,
	"vgaport",	{Qvgaport},	0,		0666,
};

/* a routine from ../port/devcons.c */
extern	int readstr(ulong, char *, ulong, char *);

void
vgasetup(void) {
}

void
vgareset(void) {
	strcpy(monitor, "generic");
	strcpy(vgacard, "generic");
	screeninfo.maxx = 640;
	screeninfo.maxy = 480;
	screeninfo.packed = 0;
	screeninfo.interlaced = 0;
}

void
vgainit(void)
{
}

Chan*
vgaattach(char *upec)
{
	return devattach('v', upec);
}

Chan*
vgaclone(Chan *c, Chan *nc)
{
	return devclone(c, nc);
}

int
vgawalk(Chan *c, char *name)
{
	return devwalk(c, name, vgadir, Nvga, devgen);
}

void
vgastat(Chan *c, char *dp)
{
	switch(c->qid.path){
	default:
		devstat(c, dp, vgadir, Nvga, devgen);
		break;
	}
}

Chan*
vgaopen(Chan *c, int omode)
{
	switch(c->qid.path){
	case Qvgamonitor:
	case Qvgatype:
	case Qvgasize:
	case Qvgaport:
		break;
	}

	return devopen(c, omode, vgadir, Nvga, devgen);
}

void
vgacreate(Chan *c, char *name, int omode, ulong perm)
{
	error(Eperm);
}

void
vgaclose(Chan *c)
{
}

long
vgaread(Chan *c, void *buf, long n, ulong offset)
{
	char obuf[60];
	int port, i;
	uchar *cp = buf;
	void *outfunc(int, int);

	switch(c->qid.path&~CHDIR){
	case Qdir:
		return devdirread(c, buf, n, vgadir, Nvga, devgen);
	case Qvgamonitor:
		return readstr(offset, buf, n, monitor);
	case Qvgatype:
		return readstr(offset, buf, n, vgacard);
	case Qvgasize:
		sprint(obuf, "%d %d",
			gscreen.r.max.x, gscreen.r.max.y);
		return readstr(offset, buf, n, obuf);
	case Qvgaport:
		if (offset + n >= 0x8000)
			error(Ebadarg);
		for (port=offset; port<offset+n; port++) {
			*cp++ = inb(port);
		}
		return n;
	}
}

long
vgawrite(Chan *c, void *buf, long n, ulong offset)
{
	char cbuf[20], *cp;
	int port, i, maxx, maxy;

	switch(c->qid.path&~CHDIR){
	case Qdir:
		error(Eperm);
	case Qvgamonitor:
		if(offset != 0)
			error(Ebadarg);
		error(Eperm);
	case Qvgatype:
		if(offset != 0)
			error(Ebadarg);
		error(Eperm);
	case Qvgasize:
		if(offset != 0)
			error(Ebadarg);
		if(n >= sizeof cbuf)
			n = sizeof cbuf - 1;
		memmove(cbuf, buf, n);
		cbuf[n-1] = 0;
		cp = cbuf;
		maxx = strtoul(cp, &cp, 0);
		maxy = strtoul(cp, &cp, 0);
		if (maxx == 0 || maxy == 0 ||
		    maxx > 1280 || maxy > 1024)
			error(Ebadarg);
		setscreen(maxx, maxy, 1);
		return n;
	case Qvgaport:
		cp = buf;
		if (offset + n >= 0x8000)
			error(Ebadarg);
		for (port=offset; port<offset+n; port++) {
			outb(port, *cp++);
		}
		return n;
	}
}

void
vgaremove(Chan *c)
{
	error(Eperm);
}

void
vgawstat(Chan *c, char *dp)
{
	error(Eperm);
}
