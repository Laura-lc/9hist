#include <u.h>
#include <libc.h>
#include <../boot/boot.h>

int
plumb(char *dir, char *dest, int *efd, char *here)
{
	char buf[128];
	char name[128];
	int n;

	sprint(name, "%s/clone", dir);
	efd[0] = open(name, ORDWR);
	if(efd[0] < 0)
		return -1;
	n = read(efd[0], buf, sizeof(buf)-1);
	if(n < 0){
		close(efd[0]);
		return -1;
	}
	buf[n] = 0;
	sprint(name, "%s/%s/data", dir, buf);
	if(here){
		sprint(buf, "announce %s", here);
		if(sendmsg(efd[0], buf) < 0){
			close(efd[0]);
			return -1;
		}
	}
	sprint(buf, "connect %s", dest);
	if(sendmsg(efd[0], buf) < 0){
		close(efd[0]);
		return -1;
	}
	efd[1] = open(name, ORDWR);
	if(efd[1] < 0){
		close(efd[0]);
		return -1;
	}
	return efd[1];
}

int
sendmsg(int fd, char *msg)
{
	int n;

	n = strlen(msg);
	if(write(fd, msg, n) != n)
		return -1;
	return 0;
}

void
warning(char *s)
{
	char buf[ERRLEN];

	errstr(buf);
	fprint(2, "boot: %s: %s\n", s, buf);
}

void
fatal(char *s)
{
	char buf[ERRLEN];

	errstr(buf);
	fprint(2, "boot: %s: %s\n", s, buf);
	exits(0);
}

int
readenv(char *name, char *buf, int len)
{
	int f, n;
	char ename[2*NAMELEN];

	sprint(ename, "#e/%s", name);
	buf[0] = 0;
	f = open(ename, OREAD);
	if(f < 0)
		return -1;
	n = read(f, buf, len-1);
	if(n >= 0)
		buf[n] = 0;
	close(f);
	return 0;
}

void
setenv(char *name, char *val)
{
	int f;
	char ename[2*NAMELEN];

	sprint(ename, "#e/%s", name);
	f = create(ename, 1, 0666);
	if(f < 0)
		return;
	write(f, val, strlen(val));
	close(f);
}

void
srvcreate(char *name, int fd)
{
	char *srvname;
	int f;
	char buf[2*NAMELEN];

	srvname = strrchr(name, '/');
	if(srvname)
		srvname++;
	else
		srvname = name;

	sprint(buf, "#s/%s", srvname);
	f = create(buf, 1, 0666);
	if(f < 0)
		fatal(buf);
	sprint(buf, "%d", fd);
	if(write(f, buf, strlen(buf)) != strlen(buf))
		fatal("write");
	close(f);
}

int
outin(char *prompt, char *def, int len)
{
	int n;
	char buf[256];

	do{
		print("%s[%s]: ", prompt, *def ? def : "no default");
		n = read(0, buf, len);
	}while(n==0);
	if(n < 0)
		fatal("can't read #c/cons; please reboot");
	if(n != 1){
		buf[n-1] = 0;
		strcpy(def, buf);
	}
	return n;
}
