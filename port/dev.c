#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"errno.h"

#define	DEVTAB
#include	"devtab.h"

#include	"fcall.h"

int
devno(int c, int user)
{
	char *s;

	s = strchr(devchar, c);
	if(s==0 || c==0){
		if(user)
			return -1;
		panic("devno %c 0x%ux", c, c);
	}
	return s - devchar;
}

void
devdir(Chan *c, long qid, char *n, long length, long perm, Dir *db)
{
	strcpy(db->name, n);
	db->qid = qid;
	db->type = devchar[c->type];
	db->dev = c->dev;
	if(qid & CHDIR)
		db->mode = CHDIR|perm;
	else
		db->mode = perm;
	db->atime = seconds();
	db->mtime = db->atime;
	db->hlength = 0;
	db->length = length;
	db->uid = 0;
	db->gid = 0;
}

int
devgen(Chan *c, Dirtab *tab, int ntab, int i, Dir *dp)
{
	if(tab==0 || i>=ntab)
		return -1;
	tab += i;
	devdir(c, tab->qid, tab->name, tab->length, tab->perm, dp);
	return 1;
}

Chan *
devattach(int tc, char *spec)
{
	Chan *c;

	c = newchan();
	c->qid = CHDIR;
	c->type = devno(tc, 0);
	return c;
}

Chan *
devclone(Chan *c, Chan *nc)
{
	if(nc == 0)
		nc = newchan();
	nc->type = c->type;
	nc->mode = c->mode;
	nc->qid = c->qid;
	nc->offset = c->offset;
	nc->dev = c->dev;
	nc->flag = c->flag;
	nc->mnt = c->mnt;
	nc->mchan = c->mchan;
	nc->mqid = c->mqid;
	return nc;
}

int
devwalk(Chan *c, char *name, Dirtab *tab, int ntab, Devgen *gen)
{
	long i;
	Dir dir;

	isdir(c);
	if(name[0]=='.' && name[1]==0)
		return 1;
	for(i=0;; i++)
		switch((*gen)(c, tab, ntab, i, &dir)){
		case -1:
			u->error.type = 0;
			u->error.dev = 0;
			u->error.code = Enonexist;
			return 0;
		case 0:
			continue;
		case 1:
			if(strcmp(name, dir.name) == 0){
				c->qid = dir.qid;
				return 1;
			}
			continue;
		}
}

void
devstat(Chan *c, char *db, Dirtab *tab, int ntab, Devgen *gen)
{
	int i;
	Dir dir;

	for(i=0;; i++)
		switch((*gen)(c, tab, ntab, i, &dir)){
		case -1:
			/*
			 * devices with interesting directories usually don't get
			 * here, which is good because we've lost the name by now.
			 */
			if(c->qid & CHDIR){
				devdir(c, c->qid, ".", 0L, CHDIR|0700, &dir);
				convD2M(&dir, db);
				return;
			}
			panic("devstat");
		case 0:
			break;
		case 1:
			if(c->qid == dir.qid){
				convD2M(&dir, db);
				return;
			}
			break;
		}
}

long
devdirread(Chan *c, char *d, long n, Dirtab *tab, int ntab, Devgen *gen)
{
	long k, l, m;
	Dir dir;

	k = c->offset/DIRLEN;
	l = (c->offset+n)/DIRLEN;
	n = 0;
	for(m=k; m<l; k++)
		switch((*gen)(c, tab, ntab, k, &dir)){
		case -1:
			return n;

		case 0:
			c->offset += DIRLEN;
			break;

		case 1:
			convD2M(&dir, d);
			n += DIRLEN;
			d += DIRLEN;
			m++;
			break;
		}
	return n;
}

Chan *
devopen(Chan *c, int omode, Dirtab *tab, int ntab, Devgen *gen)
{
	int i;
	Dir dir;
	static int access[] = { 0400, 0200, 0600, 0100 };

	for(i=0;; i++)
		switch((*gen)(c, tab, ntab, i, &dir)){
		case -1:
			/* Deal with union directories? */
			goto Return;
		case 0:
			break;
		case 1:
			if(c->qid == dir.qid){
				if((access[omode&3] & dir.mode) == access[omode&3])
					goto Return;
				error(0, Eperm);
			}
			break;
		}
    Return:
	c->offset = 0;
	if((c->qid&CHDIR) && omode!=OREAD)
		error(0, Eperm);
	c->mode = openmode(omode);
	c->flag |= COPEN;
	return c;
}
