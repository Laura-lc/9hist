#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/netif.h"
#include "../port/error.h"

#include	<libcrypt.h>

typedef struct Sdp		Sdp;
typedef struct Conv 	Conv;
typedef struct OneWay OneWay;

enum
{
	Qtopdir=	1,		/* top level directory */

	Qsdpdir,			/* sdp directory */
	Qclone,
	Qstats,
	Qlog,

	Qconvdir,			/* directory per conversation */
	Qctl,
	Qdata,				/* reliable control channel */
	Qpacket,			/* unreliable packet channel */
	Qstatus,

	MaxQ,

	Maxconv=	256,		// power of 2
	Nfs=		4,			// number of file systems
};

#define TYPE(x) 	((x).path & 0xff)
#define CONV(x) 	(((x).path >> 8)&(Maxconv-1))
#define QID(x, y) 	(((x)<<8) | (y))

struct OneWay
{
	ulong	seqwrap;	// number of wraps of the sequence number
	ulong	seq;
	ulong	window;		// for replay attacks

	char	*calg;
	void	*cstate;	// state cipher
	int		civlen;		// in bytes
	int		(*cipher)(OneWay*, uchar *buf, int len);

	char	*aalg;
	void	*astate;	// auth state
	int		alen;		// auth data length in bytes
	int		(*auth)(OneWay*, uchar *buf, int len, uchar *hash);
};

struct Conv {
	QLock;

	int	id;
	Sdp	*sdp;

	Chan chan;	// packet channel

	char	user[NAMELEN];		/* protections */
	int	perm;
	int	inuse;
	int	length;
	int	state;

	OneWay	in;
	OneWay	out;
};

struct Sdp {
	QLock;
	Log;
	int	nconv;
	Conv	*conv[Maxconv];
};

static Dirtab sdpdirtab[]={
	"stats",	{Qstats},	0,	0444,
	"log",		{Qlog},		0,	0666,
	"clone",	{Qclone},		0,	0666,
};

static Dirtab convdirtab[]={
	"ctl",		{Qctl},	0,	0666,
	"data",		{Qdata},	0,	0666,
	"packet",	{Qpacket},	0,	0666,
	"status",	{Qstatus},	0,	0444,
};

static int m2p[] = {
	[OREAD]		4,
	[OWRITE]	2,
	[ORDWR]		6
};

enum {
	Logcompress=	(1<<0),
	Logauth=	(1<<1),
	Loghmac=	(1<<2),
};

static Logflag logflags[] =
{
	{ "compress",	Logcompress, },
	{ "auth",	Logauth, },
	{ "hmac",	Loghmac, },
	{ nil,		0, },
};

static Dirtab	*dirtab[MaxQ];
static Sdp sdptab[Nfs];

static int sdpgen(Chan *c, Dirtab*, int, int s, Dir *dp);
static Conv *sdpclone(Sdp *sdp);

static void
sdpinit(void)
{
	int i;
	Dirtab *dt;

	// setup dirtab with non directory entries
	for(i=0; i<nelem(sdpdirtab); i++) {
		dt = sdpdirtab + i;
		dirtab[TYPE(dt->qid)] = dt;
	}

	for(i=0; i<nelem(convdirtab); i++) {
		dt = convdirtab + i;
		dirtab[TYPE(dt->qid)] = dt;
	}
}

static Chan*
sdpattach(char* spec)
{
	Chan *c;
	int dev;

	dev = atoi(spec);
	if(dev<0 || dev >= Nfs)
		error("bad specification");

	c = devattach('T', spec);
	c->qid = (Qid){QID(0, Qtopdir)|CHDIR, 0};
	c->dev = dev;

	return c;
}

static int
sdpwalk(Chan *c, char *name)
{
	if(strcmp(name, "..") == 0){
		switch(TYPE(c->qid)){
		case Qtopdir:
		case Qsdpdir:
			c->qid = (Qid){CHDIR|Qtopdir, 0};
			break;
		case Qconvdir:
			c->qid = (Qid){CHDIR|Qsdpdir, 0};
			break;
		default:
			panic("sdpwalk %lux", c->qid.path);
		}
		return 1;
	}

	return devwalk(c, name, 0, 0, sdpgen);
}

static void
sdpstat(Chan* c, char* db)
{
	devstat(c, db, nil, 0, sdpgen);
}

static Chan*
sdpopen(Chan* ch, int omode)
{
	int perm;
	Sdp *sdp;
	Conv *c;

	omode &= 3;
	perm = m2p[omode];
	USED(perm);

	sdp = sdptab + ch->dev;

	switch(TYPE(ch->qid)) {
	default:
		break;
	case Qtopdir:
	case Qsdpdir:
	case Qconvdir:
	case Qstatus:
	case Qstats:
		if(omode != OREAD)
			error(Eperm);
		break;
	case Qlog:
		logopen(sdp);
		break;
	case Qclone:
		c = sdpclone(sdp);
		if(c == nil)
			error(Enodev);
		ch->qid.path = QID(c->id, Qctl);
		break;
	}
	ch->mode = openmode(omode);
	ch->flag |= COPEN;
	ch->offset = 0;
	return ch;
}

static void
sdpclose(Chan* c)
{
	Sdp *sdp  = sdptab + c->dev;

	switch(TYPE(c->qid)) {
	case Qlog:
		if(c->flag & COPEN)
			logclose(sdp);
		break;
	}
}

static long
sdpread(Chan *ch, void *a, long n, vlong off)
{
	char buf[256];
	Sdp *sdp = sdptab + ch->dev;
	Conv *c;

	USED(off);
	switch(TYPE(ch->qid)) {
	default:
		error(Eperm);
	case Qtopdir:
	case Qsdpdir:
	case Qconvdir:
		return devdirread(ch, a, n, 0, 0, sdpgen);
	case Qlog:
		return logread(sdp, a, off, n);
	case Qstatus:
		qlock(sdp);
		c = sdp->conv[CONV(ch->qid)];
		if(c == 0)
			strcpy(buf, "unbound\n");
		else {
		}
		n = readstr(off, a, n, buf);
		qunlock(sdp);
		return n;
	case Qctl:
		sprint(buf, "%lud", CONV(ch->qid));
		return readstr(off, a, n, buf);
	}
}

static long
sdpwrite(Chan *ch, void *a, long n, vlong off)
{
	Sdp *sdp = sdptab + ch->dev;
	Cmdbuf *cb;
	char *arg0;
	char *p;
	
	USED(off);
	switch(TYPE(ch->qid)) {
	default:
		error(Eperm);
	case Qctl:
		cb = parsecmd(a, n);
		qlock(sdp);
		if(waserror()) {
			qunlock(sdp);
			free(cb);
			nexterror();
		}
		if(cb->nf == 0)
			error("short write");
		arg0 = cb->f[0];
print("cmd = %s\n", arg0);
		if(strcmp(arg0, "xxx") == 0) {
			print("xxx\n");
		} else
			error("unknown control request");
		poperror();
		qunlock(sdp);
		free(cb);
		return n;
	case Qlog:
		cb = parsecmd(a, n);
		p = logctl(sdp, cb->nf, cb->f, logflags);
		free(cb);
		if(p != nil)
			error(p);
		return n;
	}
}

static int
sdpgen(Chan *c, Dirtab*, int, int s, Dir *dp)
{
	Sdp *sdp = sdptab + c->dev;
	int type = TYPE(c->qid);
	char buf[32];
	Dirtab *dt;
	Qid qid;

	switch(type) {
	default:
		// non directory entries end up here
		if(c->qid.path & CHDIR)
			panic("sdpgen: unexpected directory");	
		if(s != 0)
			return -1;
		dt = dirtab[TYPE(c->qid)];
		if(dt == nil)
			panic("sdpgen: unknown type: %d", TYPE(c->qid));
		devdir(c, c->qid, dt->name, dt->length, eve, dt->perm, dp);
		return 1;
	case Qtopdir:
		if(s != 0)
			return -1;
		devdir(c, (Qid){QID(0,Qsdpdir)|CHDIR,0}, "sdp", 0, eve, 0555, dp);
		return 1;
	case Qsdpdir:
		if(s<nelem(sdpdirtab)) {
			dt = sdpdirtab+s;
			devdir(c, dt->qid, dt->name, dt->length, eve, dt->perm, dp);
			return 1;
		}
		s -= nelem(sdpdirtab);
		if(s >= sdp->nconv)
			return -1;
		qid = (Qid){QID(s,Qconvdir)|CHDIR, 0};
		snprint(buf, sizeof(buf), "%d", s);
		devdir(c, qid, buf, 0, eve, 0555, dp);
		return 1;
	case Qconvdir:
		if(s>=nelem(convdirtab))
			return -1;
		dt = convdirtab+s;
		qid = (Qid){QID(CONV(c->qid),TYPE(dt->qid)),0};
		devdir(c, qid, dt->name, dt->length, eve, dt->perm, dp);
		return 1;
	}
}

static Conv*
sdpclone(Sdp *sdp)
{
	Conv *c, **pp, **ep;

	c = nil;
	ep = sdp->conv + nelem(sdp->conv);
	for(pp = sdp->conv; pp < ep; pp++) {
		c = *pp;
		if(c == nil){
			c = malloc(sizeof(Conv));
			if(c == nil)
				error(Enomem);
			qlock(c);
			c->sdp = sdp;
			c->id = pp - sdp->conv;
			*pp = c;
			sdp->nconv++;
			break;
		}
		if(canqlock(c)){
			if(c->inuse == 0)
				break;

			qunlock(c);
		}
	}

	if(pp >= ep) {
		return nil;
	}

	c->inuse = 1;
	strncpy(c->user, up->user, sizeof(c->user));
	c->perm = 0660;
	c->state = 0;

	qunlock(c);
	return c;
}


Dev sdpdevtab = {
	'T',
	"sdp",

	devreset,
	sdpinit,
	sdpattach,
	devclone,
	sdpwalk,
	sdpstat,
	sdpopen,
	devcreate,
	sdpclose,
	sdpread,
	devbread,
	sdpwrite,
	devbwrite,
	devremove,
	devwstat,
};
