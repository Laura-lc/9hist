#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

/*
 *  definitions from the old 9P.  we need these because sac files
 *  are encoded using the old definitions.
 */
#define NAMELEN		28

#define CHDIR		0x80000000	/* mode bit for directories */
#define CHAPPEND	0x40000000	/* mode bit for append only files */
#define CHEXCL		0x20000000	/* mode bit for exclusive use files */
#define CHMOUNT		0x10000000	/* mode bit for mounted channel */
#define CHREAD		0x4		/* mode bit for read permission */
#define CHWRITE		0x2		/* mode bit for write permission */
#define CHEXEC		0x1		/* mode bit for execute permission */

enum
{
	OPERM	= 0x3,		/* mask of all permission types in open mode */
	Nram	= 512,
	CacheSize = 20,
	OffsetSize = 4,		/* size in bytes of an offset */
};

typedef struct SacPath SacPath;
typedef struct Sac Sac;
typedef struct SacHeader SacHeader;
typedef struct SacDir SacDir;
typedef struct Cache Cache;

enum {
	Magic = 0x5acf5,
};

struct SacDir
{
	char	name[NAMELEN];
	char	uid[NAMELEN];
	char	gid[NAMELEN];
	uchar	qid[4];
	uchar	mode[4];
	uchar	atime[4];
	uchar	mtime[4];
	uchar	length[8];
	uchar	blocks[8];
};

struct SacHeader
{
	uchar	magic[4];
	uchar	length[8];
	uchar	blocksize[4];
	uchar	md5[16];
};


struct Sac
{
	SacDir;
	SacPath *path;
};

struct SacPath
{
	Ref;
	SacPath *up;
	long blocks;
	int entry;
	int nentry;
};

struct Cache
{
	long block;
	ulong age;
	uchar *data;
};

enum
{
	Pexec =		1,
	Pwrite = 	2,
	Pread = 	4,
	Pother = 	1,
	Pgroup = 	8,
	Powner =	64,
};

static char *sacfs = "fs.sac";
static uchar *data;
static int blocksize;
static Sac root;
static Cache cache[CacheSize];
static ulong cacheage;

static int	sacdir(Chan *, SacDir*, uchar*, int);
static ulong	getl(void *p);
static Sac	*saccpy(Sac *s);
static Sac	*saclookup(Sac *s, char *name);
static int	sacdirread(Chan *, uchar *p, long off, long cnt);
static void	loadblock(void *buf, uchar *offset, int blocksize);
static void	sacfree(Sac*);

static void
pathtoqid(ulong path, Qid *q)
{
	if(path & CHDIR)
		q->type = QTDIR;
	else
		q->type = QTFILE;
	q->path = path & ~(CHDIR|CHAPPEND|CHEXCL|CHMOUNT);
	q->vers = 0;
}

static void
omodetomode(ulong om, ulong *m)
{
	ulong nm;

	nm = om & ~(CHDIR|CHAPPEND|CHEXCL|CHMOUNT);
	if(om & CHDIR)
		nm |= DMDIR;
	*m = nm;
}

static void
sacinit(void)
{
	SacHeader *hdr;
	uchar *p;
	int i;

	p = (uchar*)Flash_tar;
	data = tarlookup(p, sacfs, &i);
	if(data == 0) {
		p = (uchar*)Flash_tar+4;
		data = tarlookup(p, sacfs, &i);
		if(data == 0) {
			print("devsac: could not find file: %s\n", sacfs);
			return;
		}
	}
	hdr = (SacHeader*)data;
	if(getl(hdr->magic) != Magic) {
		print("devsac: bad magic\n");
		return;
	}
	blocksize = getl(hdr->blocksize);
	root.SacDir = *(SacDir*)(data + sizeof(SacHeader));
	p = malloc(CacheSize*blocksize);
	if(p == nil)
		error("allocating cache");
	for(i=0; i<CacheSize; i++) {
		cache[i].data = p;
		p += blocksize;
	}
}

static Chan*
sacattach(char* spec)
{
	Chan *c;
	int dev;

	dev = atoi(spec);
	if(dev != 0)
		error("bad specification");

	// check if init found sac file system in memory
	if(blocksize == 0)
		error("devsac: bad magic");

	c = devattach('C', spec);
	pathtoqid(getl(root.qid), &c->qid);
	c->dev = dev;
	c->aux = saccpy(&root);
	return c;
}

static void listprint(char **name, int nname, int j)
{
	int i;

	print("%d ", j);
	for(i = 0; i< nname; i++)
		print("%s/", name[i]);
	print("\n");
}

static Walkqid*
sacwalk(Chan *c, Chan *nc, char **name, int nname)
{
	Sac *sac;
	int j, alloc;
	Walkqid *wq;
	char *n;

	if(nname > 0)
		isdir(c);

	alloc = 0;
	wq = smalloc(sizeof(Walkqid)+(nname-1)*sizeof(Qid));
	if(waserror()){
		if(alloc && wq->clone!=nil)
			cclose(wq->clone);
		free(wq);
		return nil;
	}
	if(nc == nil){
		nc = devclone(c);
		nc->aux = saccpy(c->aux);
		alloc = 1;
	}
	wq->clone = nc;

	for(j=0; j<nname; j++){
		isdir(nc);
		n = name[j];
		if(strcmp(n, ".") == 0){
			wq->qid[wq->nqid++] = nc->qid;
			continue;
		}
		sac = nc->aux;
		sac = saclookup(sac, n);
		if(sac == nil) {
			if(j == 0)
				error(Enonexist);
			kstrcpy(up->errstr, Enonexist, ERRMAX);
			break;
		}
		pathtoqid(getl(sac->qid), &nc->qid);
		wq->qid[wq->nqid++] = nc->qid;
	}
	poperror();
	if(wq->nqid < nname){
//		listprint(name, wq->nqid, j);
		if(alloc)
			cclose(wq->clone);
		wq->clone = nil;
	}
	return wq;
}

static Chan*
sacopen(Chan *c, int omode)
{
	ulong t, mode;
	Sac *sac;
	static int access[] = { 0400, 0200, 0600, 0100 };

	sac = c->aux;
	mode = getl(sac->mode);
	if(strcmp(up->user, sac->uid) == 0)
		mode = mode;
	else if(strcmp(up->user, sac->gid) == 0)
		mode = mode<<3;
	else
		mode = mode<<6;

	t = access[omode&3];
	if((t & mode) != t)
			error(Eperm);
	c->offset = 0;
	c->mode = openmode(omode);
	c->flag |= COPEN;
	return c;
}


static long
sacread(Chan *c, void *a, long n, vlong voff)
{
	Sac *sac;
	char *buf, *buf2;
	int nn, cnt, i, j;
	uchar *blocks;
	long length;
	long off = voff;

	buf = a;
	cnt = n;
	if(c->qid.type & QTDIR)
		return sacdirread(c, a, off, cnt);
	sac = c->aux;
	length = getl(sac->length);
	if(off >= length)
		return 0;
	if(cnt > length-off)
		cnt = length-off;
	if(cnt == 0)
		return 0;
	n = cnt;
	blocks = data + getl(sac->blocks);
	buf2 = malloc(blocksize);
	while(cnt > 0) {
		i = off/blocksize;
		nn = blocksize;
		if(nn > length-i*blocksize)
			nn = length-i*blocksize;
		loadblock(buf2, blocks+i*OffsetSize, nn);
		j = off-i*blocksize;
		nn -= j;
		if(nn > cnt)
			nn = cnt;
		memmove(buf, buf2+j, nn);
		cnt -= nn;
		off += nn;
		buf += nn;
	}
	free(buf2);
	return n;
}

static long
sacwrite(Chan *, void *, long, vlong)
{
	error(Eperm);
	return 0;
}

static void
sacclose(Chan* c)
{
	Sac *sac = c->aux;

	c->aux = nil;
	sacfree(sac);
}

static int
sacstat(Chan *c, uchar *db, int n)
{
	n = sacdir(c, c->aux, db, n);
	if(n == 0)
		error(Ebadarg);
	return n;
}

static Sac*
saccpy(Sac *s)
{
	Sac *ss;
	
	ss = malloc(sizeof(Sac));
	*ss = *s;
	if(ss->path)
		incref(ss->path);
	return ss;
}

static SacPath *
sacpathalloc(SacPath *p, long blocks, int entry, int nentry)
{
	SacPath *pp = malloc(sizeof(SacPath));
	pp->ref = 1;
	pp->blocks = blocks;
	pp->entry = entry;
	pp->nentry = nentry;
	pp->up = p;
	return pp;
}

static void
sacpathfree(SacPath *p)
{
	if(p == nil)
		return;
	if(decref(p) > 0)
		return;
	sacpathfree(p->up);
	free(p);
}


static void
sacfree(Sac *s)
{
	sacpathfree(s->path);
	free(s);
}

static int
sacdir(Chan *c, SacDir *s, uchar *db, int n)
{
	Dir dir;

	dir.name = s->name;
	dir.uid = s->uid;
	dir.gid = s->gid;
	dir.muid = s->uid;
	pathtoqid(getl(s->qid), &dir.qid);
	omodetomode(getl(s->mode), &dir.mode);
	dir.length = getl(s->length);
	dir.atime = getl(s->atime);
	dir.mtime = getl(s->mtime);
	dir.type = devtab[c->type]->dc;
	dir.dev = c->dev;
	return convD2M(&dir, db, n);
}

static void
loadblock(void *buf, uchar *offset, int blocksize)
{
	long block, n;
	ulong age;
	int i, j;

	block = getl(offset);
	if(block < 0) {
		block = -block;
		cacheage++;
		// age has wraped
		if(cacheage == 0) {
			for(i=0; i<CacheSize; i++)
				cache[i].age = 0;
		}
		j = 0;
		age = cache[0].age;
		for(i=0; i<CacheSize; i++) {
			if(cache[i].age < age) {
				age = cache[i].age;
				j = i;
			}
			if(cache[i].block != block)
				continue;
			memmove(buf, cache[i].data, blocksize);
			cache[i].age = cacheage;
			return;
		}

		n = getl(offset+OffsetSize);
		if(n < 0)
			n = -n;
		n -= block;
		if(unsac(buf, data+block, blocksize, n)<0)
			panic("unsac failed!");
		memmove(cache[j].data, buf, blocksize);
		cache[j].age = cacheage;
		cache[j].block = block;
	} else {
		memmove(buf, data+block, blocksize);
	}
}

static Sac*
sacparent(Sac *s)
{
	uchar *blocks;
	SacDir *buf;
	int per, i, n;
	SacPath *p;

	p = s->path;
	if(p == nil || p->up == nil) {
		sacpathfree(p);
		*s = root;
		return s;
	}
	p = p->up;

	blocks = data + p->blocks;
	per = blocksize/sizeof(SacDir);
	i = p->entry/per;
	n = per;
	if(n > p->nentry-i*per)
		n = p->nentry-i*per;
	buf = malloc(per*sizeof(SacDir));
	loadblock(buf, blocks + i*OffsetSize, n*sizeof(SacDir));
	s->SacDir = buf[p->entry-i*per];
	free(buf);
	incref(p);
	sacpathfree(s->path);
	s->path = p;
	return s;
}

/* n**2 alg to read a directory */
static int
sacdirread(Chan *c, uchar *p, long off, long cnt)
{
	uchar *blocks;
	SacDir *buf;
	int per, i, j, n, ndir;
	long sofar;
	Sac *s;

	s = c->aux;
	blocks = data + getl(s->blocks);
	per = blocksize/sizeof(SacDir);
	ndir = getl(s->length);

	buf = malloc(blocksize);
	sofar = 0;
	for(j = 0; j < ndir; j++) {
		i = j%per;
		if(i == 0) {
			n = per;
			if(n > ndir-j*per)
				n = ndir-j*per;
			loadblock(buf, blocks + j*OffsetSize, n*sizeof(SacDir));
		}
		n = sacdir(c, buf+i, p, cnt - sofar);
		if(n == 0)
			break;
		if(off > 0)
			off -= n;
		else {
			p += n;
			sofar += n;
		}
	}
	free(buf);

	return sofar;
}

static Sac*
saclookup(Sac *s, char *name)
{
	int ndir;
	int i, j, k, n, per;
	uchar *blocks;
	SacDir *buf;
	int iblock;
	SacDir *sd;
	
	if(strcmp(name, "..") == 0)
		return sacparent(s);
	blocks = data + getl(s->blocks);
	per = blocksize/sizeof(SacDir);
	ndir = getl(s->length);
	buf = malloc(per*sizeof(SacDir));
	iblock = -1;

	// linear search
	for(i=0; i<ndir; i++) {
		j = i/per;
		if(j != iblock) {
			n = per;
			if(n > ndir-j*per)
				n = ndir-j*per;
			loadblock(buf, blocks + j*OffsetSize, n*sizeof(SacDir));
			iblock = j;
		}
		j *= per;
		sd = buf+i-j;
		k = strcmp(name, sd->name);
		if(k == 0) {
			s->path = sacpathalloc(s->path, getl(s->blocks), i, ndir);
			s->SacDir = *sd;
			free(buf);
			return s;
		}
	}
	free(buf);
	return 0;
}

static ulong
getl(void *p)
{
	uchar *a = p;

	return (a[0]<<24) | (a[1]<<16) | (a[2]<<8) | a[3];
}

Dev sacdevtab = {
	'C',
	"sac",

	devreset,
	sacinit,
	devshutdown,
	sacattach,
	sacwalk,
	sacstat,
	sacopen,
	devcreate,
	sacclose,
	sacread,
	devbread,
	sacwrite,
	devbwrite,
	devremove,
	devwstat,
};
