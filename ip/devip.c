#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"../ip/ip.h"

enum
{
	Qtopdir=	1,		/* top level directory */
	Qtopbase,
	Qarp=		Qtopbase,
	Qbootp,
	Qiproute,
	Qiprouter,
	Qipselftab,
	Qlog,

	Qprotodir,			/* directory for a protocol */
	Qprotobase,
	Qclone=		Qprotobase,
	Qstats,

	Qconvdir,			/* directory for a conversation */
	Qconvbase,
	Qctl=		Qconvbase,
	Qdata,
	Qerr,
	Qlisten,
	Qlocal,
	Qremote,
	Qstatus,

	Logtype=	5,
	Masktype=	(1<<Logtype)-1,
	Logconv=	12,
	Maskconv=	(1<<Logconv)-1,
	Shiftconv=	Logtype,
	Logproto=	8,
	Maskproto=	(1<<Logproto)-1,
	Shiftproto=	Logtype + Logconv,

	Nfs=		16,
};
#define TYPE(x) 	( (x).path & Masktype )
#define CONV(x) 	( ((x).path >> Shiftconv) & Maskconv )
#define PROTO(x) 	( ((x).path >> Shiftproto) & Maskproto )
#define QID(p, c, y) 	( ((p)<<(Shiftproto)) | ((c)<<Shiftconv) | (y) )

static char network[] = "network";

QLock	fslock;
Fs	*ipfs[Nfs];	/* attached fs's */
Queue	*qlog;

extern void nullmediumlink(void);
extern void pktmediumlink(void);

static int
ip3gen(Chan *c, int i, Dir *dp)
{
	Qid q;
	Conv *cv;
	char *p;

	cv = ipfs[c->dev]->p[PROTO(c->qid)]->conv[CONV(c->qid)];
	switch(i) {
	default:
		return -1;
	case Qctl:
		q = (Qid){QID(PROTO(c->qid), CONV(c->qid), Qctl), 0};
		devdir(c, q, "ctl", 0, cv->owner, cv->perm, dp);
		return 1;
	case Qdata:
		q = (Qid){QID(PROTO(c->qid), CONV(c->qid), Qdata), 0};
		devdir(c, q, "data", qlen(cv->rq), cv->owner, cv->perm, dp);
		return 1;
	case Qerr:
		q = (Qid){QID(PROTO(c->qid), CONV(c->qid), Qerr), 0};
		devdir(c, q, "err", qlen(cv->eq), cv->owner, cv->perm, dp);
		return 1;
	case Qlisten:
		p = "listen";
		q = (Qid){QID(PROTO(c->qid), CONV(c->qid), Qlisten), 0};
		break;
	case Qlocal:
		p = "local";
		q = (Qid){QID(PROTO(c->qid), CONV(c->qid), Qlocal), 0};
		break;
	case Qremote:
		p = "remote";
		q = (Qid){QID(PROTO(c->qid), CONV(c->qid), Qremote), 0};
		break;
	case Qstatus:
		p = "status";
		q = (Qid){QID(PROTO(c->qid), CONV(c->qid), Qstatus), 0};
		break;
	}
	devdir(c, q, p, 0, cv->owner, 0666, dp);
	return 1;
}

static int
ip2gen(Chan *c, int i, Dir *dp)
{
	Qid q;

	switch(i) {
	case Qclone:
		q = (Qid){QID(PROTO(c->qid), 0, Qclone), 0};
		devdir(c, q, "clone", 0, network, 0666, dp);
		return 1;
	case Qstats:
		q = (Qid){QID(PROTO(c->qid), 0, Qstats), 0};
		devdir(c, q, "stats", 0, network, 0444, dp);
		return 1;
	}	
	return -1;
}

static int
ip1gen(Chan *c, int i, Dir *dp)
{
	Qid q;
	char *p;
	int prot;

	prot = 0666;
	switch(i) {
	default:
		return -1;
	case Qarp:
		p = "arp";
		q = (Qid){QID(0, 0, Qarp), 0};
		break;
	case Qbootp:
		p = "bootp";
		q = (Qid){QID(0, 0, Qbootp), 0};
		break;
	case Qiproute:
		p = "iproute";
		q = (Qid){QID(0, 0, Qiproute), 0};
		break;
	case Qipselftab:
		p = "ipselftab";
		prot = 0444;
		q = (Qid){QID(0, 0, Qipselftab), 0};
		break;
	case Qiprouter:
		p = "iprouter";
		q = (Qid){QID(0, 0, Qiprouter), 0};
		break;
	case Qlog:
		p = "log";
		q = (Qid){QID(0, 0, Qlog), 0};
		break;
	}
	devdir(c, q, p, 0, network, prot, dp);
	return 1;
}

static int
ipgen(Chan *c, Dirtab*, int, int s, Dir *dp)
{
	Qid q;
	Conv *cv;
	char name[16];
	Fs *f;

	f = ipfs[c->dev];

	switch(TYPE(c->qid)) {
	case Qtopdir:
		if(s < f->np) {
			if(f->p[s]->connect == nil)
				return 0;	/* protocol with no user interface */
			q = (Qid){QID(s, 0, Qprotodir)|CHDIR, 0};
			devdir(c, q, f->p[s]->name, 0, network, CHDIR|0555, dp);
			return 1;
		}
		s -= f->np;
		return ip1gen(c, s+Qtopbase, dp);
	case Qarp:
	case Qbootp:
	case Qlog:
	case Qiproute:
	case Qiprouter:
	case Qipselftab:
		return ip1gen(c, TYPE(c->qid), dp);
	case Qprotodir:
		if(s < f->p[PROTO(c->qid)]->ac) {
			cv = f->p[PROTO(c->qid)]->conv[s];
			sprint(name, "%d", s);
			q = (Qid){QID(PROTO(c->qid), s, Qconvdir)|CHDIR, 0};
			devdir(c, q, name, 0, cv->owner, CHDIR|0555, dp);
			return 1;
		}
		s -= f->p[PROTO(c->qid)]->ac;
		return ip2gen(c, s+Qprotobase, dp);
	case Qclone:
	case Qstats:
		return ip2gen(c, TYPE(c->qid), dp);
	case Qconvdir:
		return ip3gen(c, s+Qconvbase, dp);
	case Qctl:
	case Qdata:
	case Qerr:
	case Qlisten:
	case Qlocal:
	case Qremote:
	case Qstatus:
		return ip3gen(c, TYPE(c->qid), dp);
	}
	return -1;
}

static void
ipreset(void)
{
	nullmediumlink();
	pktmediumlink();
}

static void
ipinit(void)
{
	fmtinstall('i', eipconv);
	fmtinstall('I', eipconv);
	fmtinstall('E', eipconv);
	fmtinstall('V', eipconv);
	fmtinstall('M', eipconv);
}

static Fs*
ipgetfs(int dev)
{
	extern void (*ipprotoinit[])(Fs*);
	Fs *f;
	int i;

	if(dev >= Nfs)
		return nil;

	qlock(&fslock);
	if(ipfs[dev] == nil){
		f = smalloc(sizeof(Fs));
		ip_init(f);
		arpinit(f);
		netloginit(f);
		for(i = 0; ipprotoinit[i]; i++)
			ipprotoinit[i](f);
		f->dev = dev;
		ipfs[dev] = f;
	}
	qunlock(&fslock);

	return ipfs[dev];
}

static Chan*
ipattach(char* spec)
{
	Chan *c;
	int dev;

	dev = atoi(spec);
	if(dev >= 16)
		error("bad specification");

	ipgetfs(dev);
	c = devattach('I', spec);
	c->qid = (Qid){QID(0, 0, Qtopdir)|CHDIR, 0};
	c->dev = dev;

	return c;
}

static Chan*
ipclone(Chan* c, Chan* nc)
{
	return devclone(c, nc);
}

static int
ipwalk(Chan* c, char* name)
{
	Path *op;

	if(strcmp(name, "..") != 0)
		return devwalk(c, name, nil, 0, ipgen);

	switch(TYPE(c->qid)){
	case Qtopdir:
	case Qprotodir:
		c->qid = (Qid){QID(0, 0, Qtopdir)|CHDIR, 0};
		break;
	case Qconvdir:
		c->qid = (Qid){QID(PROTO(c->qid), 0, Qprotodir)|CHDIR, 0};
		break;
	default:
		panic("ipwalk %lux", c->qid.path);
	}
	op = c->path;
	c->path = ptenter(&syspt, op, name);
	decref(op);
	return 1;
}

static void
ipstat(Chan* c, char* db)
{
	devstat(c, db, nil, 0, ipgen);
}

static int
incoming(void* arg)
{
	Conv *conv;

	conv = arg;
	return conv->incall != nil;
}

static int m2p[] = {
	[OREAD]		4,
	[OWRITE]	2,
	[ORDWR]		6
};

static Chan*
ipopen(Chan* c, int omode)
{
	Conv *cv, *nc;
	Proto *p;
	int perm;
	Fs *f;

	omode &= 3;
	perm = m2p[omode];

	f = ipfs[c->dev];

	switch(TYPE(c->qid)) {
	default:
		break;
	case Qlog:
		netlogopen(f);
		break;
	case Qiprouter:
		iprouteropen(f);
		break;
	case Qiproute:
		memmove(c->tag, "none", sizeof(c->tag));
		break;
	case Qtopdir:
	case Qprotodir:
	case Qconvdir:
	case Qstatus:
	case Qremote:
	case Qlocal:
	case Qstats:
	case Qbootp:
	case Qipselftab:
		if(omode != OREAD)
			error(Eperm);
		break;
	case Qclone:
		p = f->p[PROTO(c->qid)];
		qlock(p);
		if(waserror()){
			qunlock(p);
			nexterror();
		}
		cv = Fsprotoclone(p, commonuser());
		qunlock(p);
		poperror();
		if(cv == nil) {
			error(Enodev);
			break;
		}
		c->qid = (Qid){QID(p->x, cv->x, Qctl), 0};
		break;
	case Qdata:
	case Qctl:
	case Qerr:
		p = f->p[PROTO(c->qid)];
		qlock(p);
		cv = p->conv[CONV(c->qid)];
		qlock(cv);
		if(waserror()) {
			qunlock(cv);
			qunlock(p);
			nexterror();
		}
		if((perm & (cv->perm>>6)) != perm) {
			if(strcmp(commonuser(), cv->owner) != 0)
				error(Eperm);
		 	if((perm & cv->perm) != perm)
				error(Eperm); 

		}
		cv->inuse++;
		if(cv->inuse == 1){
			memmove(cv->owner, commonuser(), NAMELEN);
			cv->perm = 0660;
		}
		qunlock(cv);
		qunlock(p);
		poperror();
		break;
	case Qlisten:
		cv = f->p[PROTO(c->qid)]->conv[CONV(c->qid)];
		if(cv->state != Announced)
			error("not announced");

		nc = nil;
		while(nc == nil) {
			/* give up if we got a hangup */
			if(qisclosed(cv->rq))
				error("listen hungup");

			qlock(&cv->listenq);
			if(waserror()) {
				qunlock(&cv->listenq);
				nexterror();
			}

			/* wait for a connect */
			sleep(&cv->listenr, incoming, cv);

			qlock(cv);
			nc = cv->incall;
			if(nc != nil){
				cv->incall = nc->next;
				c->qid = (Qid){QID(PROTO(c->qid), nc->x, Qctl), 0};
				memmove(cv->owner, commonuser(), NAMELEN);
			}
			qunlock(cv);

			qunlock(&cv->listenq);
			poperror();
		}
		break;
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
ipcreate(Chan*, char*, int, ulong)
{
	error(Eperm);
}

static void
ipremove(Chan*)
{
	error(Eperm);
}

static void
ipwstat(Chan*, char*)
{
	error(Eperm);
}

static void
closeconv(Conv *cv)
{
	Conv *nc;
	Ipmulti *mp;

	qlock(cv);

	if(--cv->inuse > 0) {
		qunlock(cv);
		return;
	}

	/* close all incoming calls since no listen will ever happen */
	for(nc = cv->incall; nc; nc = cv->incall){
		cv->incall = nc->next;
		closeconv(nc);
	}
	cv->incall = nil;

	strcpy(cv->owner, network);
	cv->perm = 0660;

	while((mp = cv->multi) != nil)
		ipifcremmulti(cv, mp->ma, mp->ia);

	/* The close routine will unlock the conv */
	cv->p->close(cv);
}

static void
ipclose(Chan* c)
{
	Fs *f;

	f = ipfs[c->dev];
	switch(TYPE(c->qid)) {
	default:
		break;
	case Qlog:
		if(c->flag & COPEN)
			netlogclose(f);
		break;
	case Qiprouter:
		if(c->flag & COPEN)
			iprouterclose(f);
		break;
	case Qdata:
	case Qctl:
	case Qerr:
		if(c->flag & COPEN)
			closeconv(f->p[PROTO(c->qid)]->conv[CONV(c->qid)]);
	}
}

enum
{
	Statelen=	32*1024,
};

static long
ipread(Chan *ch, void *a, long n, vlong off)
{
	Conv *c;
	Proto *x;
	char *buf, *p;
	long rv;
	Fs *f;
	ulong offset = off;

	f = ipfs[ch->dev];

	p = a;
	switch(TYPE(ch->qid)) {
	default:
		error(Eperm);
	case Qtopdir:
	case Qprotodir:
	case Qconvdir:
		return devdirread(ch, a, n, 0, 0, ipgen);
	case Qarp:
		return arpread(f->arp, a, offset, n);
 	case Qbootp:
 		return bootpread(a, offset, n);
	case Qiproute:
		return routeread(f, a, offset, n);
	case Qipselftab:
		return ipselftabread(f, a, offset, n);
	case Qlog:
		return netlogread(f, a, offset, n);
	case Qctl:
		buf = smalloc(16);
		sprint(buf, "%lud", CONV(ch->qid));
		rv = readstr(offset, p, n, buf);
		free(buf);
		return rv;
	case Qremote:
		buf = smalloc(Statelen);
		c = f->p[PROTO(ch->qid)]->conv[CONV(ch->qid)];
		sprint(buf, "%I!%d\n", c->raddr, c->rport);
		rv = readstr(offset, p, n, buf);
		free(buf);
		return rv;
	case Qlocal:
		buf = smalloc(Statelen);
		x = f->p[PROTO(ch->qid)];
		c = x->conv[CONV(ch->qid)];
		if(x->local == nil) {
			sprint(buf, "%I!%d\n", c->laddr, c->lport);
		} else {
			(*x->local)(c, buf, Statelen-2);
		}
		rv = readstr(offset, p, n, buf);
		free(buf);
		return rv;
	case Qstatus:
		buf = smalloc(Statelen);
		x = f->p[PROTO(ch->qid)];
		c = x->conv[CONV(ch->qid)];
		(*x->state)(c, buf, Statelen-2);
		rv = readstr(offset, p, n, buf);
		free(buf);
		return rv;
	case Qdata:
		c = f->p[PROTO(ch->qid)]->conv[CONV(ch->qid)];
		return qread(c->rq, a, n);
	case Qerr:
		c = f->p[PROTO(ch->qid)]->conv[CONV(ch->qid)];
		return qread(c->eq, a, n);
	case Qstats:
		x = f->p[PROTO(ch->qid)];
		if(x->stats == nil)
			error("stats not implemented");
		buf = smalloc(Statelen);
		(*x->stats)(x, buf, Statelen);
		rv = readstr(offset, p, n, buf);
		free(buf);
		return rv;
	}
}

static Block*
ipbread(Chan* c, long n, ulong offset)
{
	return devbread(c, n, offset);
}

/*
 *  set local address to be that of the ifc closest to remote address
 */
static void
setladdr(Conv* c)
{
	findlocalip(c->p->f, c->laddr, c->raddr);
}

/*
 *  pick a local port and set it
 */
static void
setlport(Conv* c)
{
	Proto *p;
	ushort *pp;
	int x, found;

	p = c->p;
	if(c->restricted)
		pp = &p->nextrport;
	else
		pp = &p->nextport;
	qlock(p);
	for(;;(*pp)++){
		/*
		 * Fsproto initialises p->nextport to 0 and the restricted
		 * ports (p->nextrport) to 600.
		 * Restricted ports must lie between 600 and 1024.
		 * For the initial condition or if the unrestricted port number
		 * has wrapped round, select a random port between 5000 and 1<<15
		 * to start at.
		 */
		if(c->restricted){
			if(*pp >= 1024)
				*pp = 600;
		}
		else while(*pp < 5000)
			*pp = nrand(1<<15);

		found = 0;
		for(x = 0; x < p->nc; x++){
			if(p->conv[x] == nil)
				break;
			if(p->conv[x]->lport == *pp){
				found = 1;
				break;
			}
		}
		if(!found)
			break;
	}
	c->lport = (*pp)++;
	qunlock(p);
}

/*
 *  set a local address and port from a string of the form
 *	address!port
 *  if address is missing and not announcing, pick one
 *  if address is missing and announcing, leave address as zero (i.e. matches anything)
 *  if port is 0, pick a free one
 *  if port is '*', leave port as zero (i.e. matches anything)
 */
static void
setladdrport(Conv* c, char* str, int announcing)
{
	char *p;

	/*
	 *  ignore restricted part if it exists.  it's
	 *  meaningless on local ports.
	 */
	p = strchr(str, '!');
	if(p == nil || strcmp(p, "!r") == 0) {
		p = str;
		memset(c->laddr, 0, sizeof(c->laddr));
		if(!announcing)
			setladdr(c);
	} else {
		*p++ = 0;
		parseip(c->laddr, str);
	}

	c->lport = 0;
	if(*p != '*'){
		c->lport = atoi(p);
		if(c->lport == 0)
			setlport(c);
	}
}

static char*
setraddrport(Conv* c, char* str)
{
	char *p;

	p = strchr(str, '!');
	if(p == nil)
		return "malformed address";
	*p++ = 0;
	parseip(c->raddr, str);
	c->rport = atoi(p);
	p = strchr(p, '!');
	if(p){
		if(strcmp(p, "!r") == 0)
			c->restricted = 1;
	}
	return nil;
}

/*
 *  called by protocol connect routine to set addresses
 */
char*
Fsstdconnect(Conv *c, char *argv[], int argc)
{
	char *p;

	switch(argc) {
	default:
		return "bad args to connect";
	case 2:
		p = setraddrport(c, argv[1]);
		if(p != nil)
			return p;
		setladdr(c);
		setlport(c);
		break;
	case 3:
		p = setraddrport(c, argv[1]);
		if(p != nil)
			return p;
		setladdrport(c, argv[2], 0);
		break;
	}
	return nil;
}
/*
 *  initiate connection and sleep till its set up
 */
static int
connected(void* a)
{
	return ((Conv*)a)->state == Connected;
}
static void
connectctlmsg(Proto *x, Conv *c, Cmdbuf *cb)
{
	char *p;

	c->state = Connecting;
	c->cerr[0] = '\0';
	if(x->connect == nil)
		error("connect not supported");
	p = x->connect(c, cb->f, cb->nf);
	if(p != nil)
		error(p);
	sleep(&c->cr, connected, c);
	if(c->cerr[0] != '\0')
		error(c->cerr);
}

/*
 *  called by protocol announce routine to set addresses
 */
char*
Fsstdannounce(Conv* c, char* argv[], int argc)
{
	switch(argc){
	default:
		return "bad args to announce";
	case 2:
		setladdrport(c, argv[1], 1);
		break;
	}
	return nil;
}

/*
 *  initiate announcement and sleep till its set up
 */
static int
announced(void* a)
{
	return ((Conv*)a)->state == Announced;
}
static void
announcectlmsg(Proto *x, Conv *c, Cmdbuf *cb)
{
	char *p;

	c->state = Announcing;
	c->cerr[0] = '\0';
	if(x->announce == nil)
		error("announce not supported");
	p = x->announce(c, cb->f, cb->nf);
	if(p != nil)
		error(p);
	sleep(&c->cr, announced, c);
	if(c->cerr[0] != '\0')
		error(c->cerr);
}

/*
 *  called by protocol bide routine to set addresses
 */
char*
Fsstdbind(Conv* c, char* argv[], int argc)
{
	switch(argc){
	default:
		return "bad args to bind";
	case 2:
		setladdrport(c, argv[1], 0);
		break;
	}
	return nil;
}

static void
bindctlmsg(Proto *x, Conv *c, Cmdbuf *cb)
{
	char *p;

	if(x->bind == nil)
		p = Fsstdbind(c, cb->f, cb->nf);
	else
		p = x->bind(c, cb->f, cb->nf);
	if(p != nil)
		error(p);
}

static void
ttlctlmsg(Conv *c, Cmdbuf *cb)
{
	if(cb->nf < 2)
		c->ttl = MAXTTL;
	else
		c->ttl = atoi(cb->f[1]);
}

static long
ipwrite(Chan* ch, char* a, long n, vlong)
{
	Conv *c;
	Proto *x;
	char *p;
	Cmdbuf *cb;
	uchar ia[IPaddrlen], ma[IPaddrlen];
	Fs *f;

	f = ipfs[ch->dev];

	switch(TYPE(ch->qid)){
	default:
		error(Eperm);
	case Qdata:
		x = f->p[PROTO(ch->qid)];
		c = x->conv[CONV(ch->qid)];

		if(c->wq == nil)
			error(Eperm);

		qwrite(c->wq, a, n);
		x->kick(c, n);
		break;
	case Qarp:
		return arpwrite(f, a, n);
	case Qiproute:
		return routewrite(f, ch, a, n);
	case Qlog:
		p = netlogctl(f, a, n);
		if(p != nil)
			error(p);
		return n;
	case Qctl:
		x = f->p[PROTO(ch->qid)];
		c = x->conv[CONV(ch->qid)];
		cb = parsecmd(a, n);

		if(canqlock(&c->car) == 0){
			free(cb);
			error("connect/announce in progress");
		}
		if(waserror()) {
			qunlock(&c->car);
			free(cb);
			nexterror();
		}
		if(strcmp(cb->f[0], "connect") == 0)
			connectctlmsg(x, c, cb);
		else if(strcmp(cb->f[0], "announce") == 0)
			announcectlmsg(x, c, cb);
		else if(strcmp(cb->f[0], "bind") == 0)
			bindctlmsg(x, c, cb);
		else if(strcmp(cb->f[0], "ttl") == 0)
			ttlctlmsg(c, cb);
		else if(strcmp(cb->f[0], "addmulti") == 0){
			if(cb->nf < 2)
				error("addmulti needs interface address");
			if(cb->nf == 2){
				if(!ipismulticast(c->raddr))
					error("addmulti for a non multicast address");
				parseip(ia, cb->f[1]);
				ipifcaddmulti(c, c->raddr, ia);
			} else {
				parseip(ma, cb->f[2]);
				if(!ipismulticast(ma))
					error("addmulti for a non multicast address");
				parseip(ia, cb->f[1]);
				ipifcaddmulti(c, ma, ia);
			}
		} else if(strcmp(cb->f[0], "remmulti") == 0){
			if(cb->nf < 2)
				error("remmulti needs interface address");
			if(!ipismulticast(c->raddr))
				error("remmulti for a non multicast address");
			parseip(ia, cb->f[1]);
			ipifcremmulti(c, c->raddr, ia);
		} else if(x->ctl != nil) {
			p = x->ctl(c, cb->f, cb->nf);
			if(p != nil)
				error(p);
		} else
			error("unknown control request");
		qunlock(&c->car);
		free(cb);
		poperror();
	}
	return n;
}

static long
ipbwrite(Chan* c, Block* bp, ulong offset)
{
	return devbwrite(c, bp, offset);
}

Dev ipdevtab = {
	'I',
	"ip",

	ipreset,
	ipinit,
	ipattach,
	ipclone,
	ipwalk,
	ipstat,
	ipopen,
	ipcreate,
	ipclose,
	ipread,
	ipbread,
	ipwrite,
	ipbwrite,
	ipremove,
	ipwstat,
};

int
Fsproto(Fs *f, Proto *p)
{
	if(f->np >= Maxproto)
		return -1;

	p->f = f;

	if(p->ipproto > 0){
		if(f->t2p[p->ipproto] != nil)
			return -1;
		f->t2p[p->ipproto] = p;
	}

	p->qid.path = CHDIR|QID(f->np, 0, Qprotodir);
	p->conv = malloc(sizeof(Conv*)*(p->nc+1));
	if(p->conv == nil)
		panic("Fsproto");

	p->x = f->np;
	p->nextport = 0;
	p->nextrport = 600;
	f->p[f->np++] = p;

	return 0;
}

/*
 *  return true if this protocol is
 *  built in
 */
int
Fsbuiltinproto(Fs* f, uchar proto)
{
	return f->t2p[proto] != nil;
}

/*
 *  called with protocol locked
 */
Conv*
Fsprotoclone(Proto *p, char *user)
{
	Conv *c, **pp, **ep;

	c = nil;
	ep = &p->conv[p->nc];
	for(pp = p->conv; pp < ep; pp++) {
		c = *pp;
		if(c == nil){
			c = malloc(sizeof(Conv));
			if(c == nil)
				error(Enomem);
			qlock(c);
			c->p = p;
			c->x = pp - p->conv;
			c->ptcl = malloc(p->ptclsize);
			if(c->ptcl == nil) {
				free(c);
				error(Enomem);
			}
			*pp = c;
			p->ac++;
			c->eq = qopen(1024, 1, 0, 0);
			(*p->create)(c);
			break;
		}
		if(canqlock(c)){
			/*
			 *  make sure both processes and protocol
			 *  are done with this Conv
			 */
			if(c->inuse == 0 && (p->inuse == nil || (*p->inuse)(c) == 0))
				break;

			qunlock(c);
		}
	}
	if(pp >= ep) {
		return nil;
	}

	c->inuse = 1;
	strcpy(c->owner, user);
	c->perm = 0660;
	c->state = 0;
	ipmove(c->laddr, IPnoaddr);
	ipmove(c->raddr, IPnoaddr);
	c->lport = 0;
	c->rport = 0;
	c->restricted = 0;
	c->ttl = MAXTTL;
	qreopen(c->rq);
	qreopen(c->wq);
	qreopen(c->eq);

	qunlock(c);
	return c;
}

int
Fsconnected(Conv* c, char* msg)
{
	if(msg != nil && *msg != '\0')
		strncpy(c->cerr, msg, ERRLEN-1);

	switch(c->state){

	case Announcing:
		c->state = Announced;
		break;

	case Connecting:
		c->state = Connected;
		break;
	}

	wakeup(&c->cr);
	return 0;
}

Proto*
Fsrcvpcol(Fs* f, uchar proto)
{
	if(f->ipmux)
		return f->ipmux;
	else
		return f->t2p[proto];
}

Proto*
Fsrcvpcolx(Fs *f, uchar proto)
{
	return f->t2p[proto];
}

/*
 *  called with protocol locked
 */
Conv*
Fsnewcall(Conv *c, uchar *raddr, ushort rport, uchar *laddr, ushort lport)
{
	Conv *nc;
	Conv **l;
	int i;

	qlock(c);
	i = 0;
	for(l = &c->incall; *l; l = &(*l)->next)
		i++;
	if(i >= Maxincall) {
		qunlock(c);
		return nil;
	}

	/* find a free conversation */
	nc = Fsprotoclone(c->p, network);
	if(nc == nil) {
		qunlock(c);
		return nil;
	}
	ipmove(nc->raddr, raddr);
	nc->rport = rport;
	ipmove(nc->laddr, laddr);
	nc->lport = lport;
	nc->next = nil;
	*l = nc;
	qunlock(c);

	wakeup(&c->listenr);

	return nc;
}
