#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"errno.h"

#include	<libg.h>
#include	<gnot.h>

#define	MINX	8

extern	GFont	defont0;
GFont		*defont;

struct{
	Point	pos;
	int	bwid;
}out;

void	duartinit(void);
int	duartacr;
int	duartimr;

void	(*kprofp)(ulong);

GBitmap	gscreen =
{
	(ulong*)((4*1024*1024-256*1024)|KZERO),	/* BUG */
	0,
	64,
	0,
	0, 0, 1024, 1024,
	0
};

void
screeninit(void)
{
	duartinit();
	/*
	 * Read HEX switch to set ldepth
	 */
	if(*(uchar*)MOUSE & (1<<4))
		gscreen.ldepth = 1;
	defont = &defont0;	/* save space; let bitblt do the conversion work */
	gbitblt(&gscreen, Pt(0, 0), &gscreen, gscreen.r, 0);
	out.pos.x = MINX;
	out.pos.y = 0;
	out.bwid = defont0.info[' '].width;
}

void
screenputc(int c)
{
	char buf[2];
	int nx;

	if(c == '\n'){
		out.pos.x = MINX;
		out.pos.y += defont0.height;
		if(out.pos.y > gscreen.r.max.y-defont0.height)
			out.pos.y = gscreen.r.min.y;
		gbitblt(&gscreen, Pt(0, out.pos.y), &gscreen,
		    Rect(0, out.pos.y, gscreen.r.max.x, out.pos.y+2*defont0.height), 0);
	}else if(c == '\t'){
		out.pos.x += (8-((out.pos.x-MINX)/out.bwid&7))*out.bwid;
		if(out.pos.x >= gscreen.r.max.x)
			screenputc('\n');
	}else if(c == '\b'){
		if(out.pos.x >= out.bwid+MINX){
			out.pos.x -= out.bwid;
			screenputc(' ');
			out.pos.x -= out.bwid;
		}
	}else{
		if(out.pos.x >= gscreen.r.max.x-out.bwid)
			screenputc('\n');
		buf[0] = c&0x7F;
		buf[1] = 0;
		out.pos = gbitbltstring(&gscreen, out.pos, defont, buf, S);
	}
}

/*
 * Register set for half the duart.  There are really two sets.
 */
struct Duart{
	uchar	mr1_2;		/* Mode Register Channels 1 & 2 */
	uchar	sr_csr;		/* Status Register/Clock Select Register */
	uchar	cmnd;		/* Command Register */
	uchar	data;		/* RX Holding / TX Holding Register */
	uchar	ipc_acr;	/* Input Port Change/Aux. Control Register */
#define	ivr	ivr		/* Interrupt Vector Register */
	uchar	is_imr;		/* Interrupt Status/Interrupt Mask Register */
#define	ip_opcr	is_imr		/* Input Port/Output Port Configuration Register */
	uchar	ctur;		/* Counter/Timer Upper Register */
#define	scc_sopbc ctur		/* Start Counter Command/Set Output Port Bits Command */
	uchar	ctlr;		/* Counter/Timer Lower Register */
#define	scc_ropbc ctlr		/* Stop Counter Command/Reset Output Port Bits Command */
};

enum{
	CHAR_ERR	=0x00,	/* MR1x - Mode Register 1 */
	PAR_ENB		=0x00,
	EVEN_PAR	=0x00,
	ODD_PAR		=0x04,
	NO_PAR		=0x10,
	CBITS8		=0x03,
	CBITS7		=0x02,
	CBITS6		=0x01,
	CBITS5		=0x00,
	NORM_OP		=0x00,	/* MR2x - Mode Register 2 */
	TWOSTOPB	=0x0F,
	ONESTOPB	=0x07,
	ENB_RX		=0x01,	/* CRx - Command Register */
	DIS_RX		=0x02,
	ENB_TX		=0x04,
	DIS_TX		=0x08,
	RESET_MR 	=0x10,
	RESET_RCV  	=0x20,
	RESET_TRANS  	=0x30,
	RESET_ERR  	=0x40,
	RESET_BCH	=0x50,
	STRT_BRK	=0x60,
	STOP_BRK	=0x70,
	RCV_RDY		=0x01,	/* SRx - Channel Status Register */
	FIFOFULL	=0x02,
	XMT_RDY		=0x04,
	XMT_EMT		=0x08,
	OVR_ERR		=0x10,
	PAR_ERR		=0x20,
	FRM_ERR		=0x40,
	RCVD_BRK	=0x80,
	BD38400		=0xCC|0x0000,
	BD19200		=0xCC|0x0100,
	BD9600		=0xBB|0x0000,
	BD4800		=0x99|0x0000,
	BD2400		=0x88|0x0000,
	BD1200		=0x66|0x0000,
	BD300		=0x44|0x0000,
	IM_IPC		=0x80,	/* IMRx/ISRx - Interrupt Mask/Interrupt Status */
	IM_DBB		=0x40,
	IM_RRDYB	=0x20,
	IM_XRDYB	=0x10,
	IM_CRDY		=0x08,
	IM_DBA		=0x04,
	IM_RRDYA	=0x02,
	IM_XRDYA	=0x01,
};

uchar keymap[]={
/*80*/	0x58,	0x58,	0x58,	0x58,	0x58,	0x58,	0x58,	0x58,
	0x58,	0x58,	0x58,	0x58,	0x58,	0x58,	0x8e,	0x58,
/*90*/	0x90,	0x91,	0x92,	0x93,	0x94,	0x95,	0x96,	0x97,
	0x98,	0x99,	0x9a,	0x9b,	0x58,	0x58,	0x58,	0x58,
/*A0*/	0x58,	0xa1,	0xa2,	0xa3,	0xa4,	0xa5,	0xa6,	0xa7,
	0x58,	0x58,	0x58,	0x58,	0x58,	0x58,	0xae,	0xaf,
/*B0*/	0xb0,	0xb1,	0xb2,	0xb3,	0xb4,	0xb5,	0x80,	0xb7,
	0xb8,	0xb9,	0x00,	0xbb,	0x1e,	0xbd,	0x60,	0x1f,
/*C0*/	0xc0,	0xc1,	0xc2,	0xc3,	0xc4,	0x58,	0xc6,	0x0a,
	0xc8,	0xc9,	0xca,	0xcb,	0xcc,	0xcd,	0xce,	0xcf,
/*D0*/	0x09,	0x08,	0xd2,	0xd3,	0xd4,	0xd5,	0xd6,	0xd7,
	0x58,	0x58,	0x58,	0x58,	0x58,	0x58,	0x7f,	0x58,
/*E0*/	0x58,	0x58,	0xe2,	0x1b,	0x0d,	0xe5,	0x58,	0x0a,
	0xe8,	0xe9,	0xea,	0xeb,	0xec,	0xed,	0xee,	0xef,
/*F0*/	0x09,	0x08,	0xb2,	0x1b,	0x0d,	0xf5,	0x81,	0x58,
	0x58,	0x58,	0x58,	0x58,	0x58,	0x58,	0x7f,	0xb2,
};

void
duartinit(void)
{
	Duart *duart;

	duart  =  DUARTREG;

	/*
	 * Keyboard
	 */
	duart[0].cmnd = RESET_RCV|DIS_TX|DIS_RX;
	duart[0].cmnd = RESET_TRANS;
	duart[0].cmnd = RESET_ERR;
	duart[0].cmnd = RESET_MR;
	duart[0].mr1_2 = CHAR_ERR|PAR_ENB|EVEN_PAR|CBITS8;
	duart[0].mr1_2 = NORM_OP|ONESTOPB;
	duart[0].sr_csr = BD4800;

	/*
	 * RS232
	 */
	duart[1].cmnd = RESET_RCV|DIS_TX|DIS_RX;
	duart[1].cmnd = RESET_TRANS;
	duart[1].cmnd = RESET_ERR;
	duart[1].cmnd = RESET_MR;
	duart[1].mr1_2 = CHAR_ERR|NO_PAR|CBITS8;
	duart[1].mr1_2 = NORM_OP|ONESTOPB;
	duart[1].sr_csr = BD9600;

	/*
	 * Output port
	 */
	duart[0].ipc_acr = duartacr = 0xB7;	/* allow change	of state interrupt */
	duart[1].ip_opcr = 0x00;
	duart[1].scc_ropbc = 0xFF;	/* make sure the port is reset first */
	duart[1].scc_sopbc = 0x04;	/* dtr = 1, pp = 01 */
	duart[0].is_imr = duartimr = IM_IPC|IM_RRDYB|IM_XRDYB|IM_RRDYA|IM_XRDYA;
	duart[0].cmnd = ENB_TX|ENB_RX;	/* enable TX and RX last */
	duart[1].cmnd = ENB_TX|ENB_RX;

	/*
	 * Initialize keyboard
	 */
	while (!(duart[0].sr_csr & (XMT_EMT|XMT_RDY)))
		;
	duart[0].data = 0x02;
}

int
duartinputport(void)
{
	Duart *duart = DUARTREG;
	return duart[1].ip_opcr;
}
void
duartbaud(int b)
{
	int x;
	Duart *duart = DUARTREG;

	x = 0;		/* set */
	switch(b){
	case 38400:
		x = BD38400;
		break;
	case 19200:
		x = BD19200;
		break;
	case 9600:
		x = BD9600;
		break;
	case 4800:
		x = BD4800;
		break;
	case 2400:
		x = BD2400;
		break;
	case 1200:
		x = BD1200;
		break;
	case 300:
		x = BD300;
		break;
	default:
		error(Ebadarg);
	}
	if(x & 0x0100)
		duart[0].ipc_acr = duartacr |= 0x80;
	else
		duart[0].ipc_acr = duartacr &= ~0x80;
	duart[1].sr_csr = x;
}

void
duartdtr(int val)
{
	Duart *duart = DUARTREG;
	if (val)
		duart[1].scc_ropbc=0x01;
	else
		duart[1].scc_sopbc=0x01;
}

void
duartbreak(int ms)
{
	static QLock brk;
	Duart *duart = DUARTREG;
	if (ms<=0 || ms >20000)
		error(Ebadarg);
	qlock(&brk);
	duart[0].is_imr = duartimr &= ~IM_XRDYB;
	duart[1].cmnd = STRT_BRK|ENB_TX;
	tsleep(&u->p->sleep, return0, 0, ms);
	duart[1].cmnd = STOP_BRK|ENB_TX;
	duart[0].is_imr = duartimr |= IM_XRDYB;
	qunlock(&brk);
}

enum{
	Kptime=200
};
void
duartstarttimer(void)
{
	Duart *duart;
	char x;

	duart = DUARTREG;
	duart[0].ctur = (Kptime)>>8;
	duart[0].ctlr = (Kptime)&255;
	duart[0].is_imr = duartimr |= IM_CRDY;
	x = duart[1].scc_sopbc;
}

void
duartstoptimer(void)
{
	Duart *duart;
	char x;

	duart = DUARTREG;
	x = duart[1].scc_ropbc;
	duart[0].is_imr = duartimr &= ~IM_CRDY;
}

void
duartrs232intr(void)
{
	int c;
	Duart *duart;

	duart = DUARTREG;
	c = getrs232o();
	if(c == -1)
		duart[1].cmnd = DIS_TX;
	else
		duart[1].data = c;
}

void
duartstartrs232o(void)
{
	DUARTREG[1].cmnd = ENB_TX;
	duartrs232intr();
}

struct latin
{
	uchar	l;
	char	c[2];
}latintab[] = {
	'',	"!!",	/* spanish initial ! */
	'',	"c|",	/* cent */
	'',	"c$",	/* cent */
	'',	"l$",	/* pound sterling */
	'',	"g$",	/* general currency */
	'',	"y$",	/* yen */
	'',	"j$",	/* yen */
	'',	"||",	/* broken vertical bar */
	'',	"SS",	/* section symbol */
	'',	"\"\"",	/* dieresis */
	'',	"cr",	/* copyright */
	'',	"cO",	/* copyright */
	'',	"sa",	/* super a, feminine ordinal */
	'',	"<<",	/* left angle quotation */
	'',	"no",	/* not sign, hooked overbar */
	'',	"--",	/* soft hyphen */
	'',	"rg",	/* registered trademark */
	'',	"__",	/* macron */
	'',	"s0",	/* degree (sup o) */
	'',	"+-",	/* plus-minus */
	'',	"s2",	/* sup 2 */
	'',	"s3",	/* sup 3 */
	'',	"''",	/* grave accent */
	'',	"mu",	/* mu */
	'',	"pg",	/* paragraph (pilcrow) */
	'',	"..",	/* centered . */
	'',	",,",	/* cedilla */
	'',	"s1",	/* sup 1 */
	'',	"so",	/* sup o */
	'',	">>",	/* right angle quotation */
	'',	"14",	/* 1/4 */
	'',	"12",	/* 1/2 */
	'',	"34",	/* 3/4 */
	'',	"??",	/* spanish initial ? */
	'',	"A`",	/* A grave */
	'',	"A'",	/* A acute */
	'',	"A^",	/* A circumflex */
	'',	"A~",	/* A tilde */
	'',	"A\"",	/* A dieresis */
	'',	"A:",	/* A dieresis */
	'',	"Ao",	/* A circle */
	'',	"AO",	/* A circle */
	'',	"Ae",	/* AE ligature */
	'',	"AE",	/* AE ligature */
	'',	"C,",	/* C cedilla */
	'',	"E`",	/* E grave */
	'',	"E'",	/* E acute */
	'',	"E^",	/* E circumflex */
	'',	"E\"",	/* E dieresis */
	'',	"E:",	/* E dieresis */
	'',	"I`",	/* I grave */
	'',	"I'",	/* I acute */
	'',	"I^",	/* I circumflex */
	'',	"I\"",	/* I dieresis */
	'',	"I:",	/* I dieresis */
	'',	"D-",	/* Eth */
	'',	"N~",	/* N tilde */
	'',	"O`",	/* O grave */
	'',	"O'",	/* O acute */
	'',	"O^",	/* O circumflex */
	'',	"O~",	/* O tilde */
	'',	"O\"",	/* O dieresis */
	'',	"O:",	/* O dieresis */
	'',	"OE",	/* O dieresis */
	'',	"Oe",	/* O dieresis */
	'',	"xx",	/* times sign */
	'',	"O/",	/* O slash */
	'',	"U`",	/* U grave */
	'',	"U'",	/* U acute */
	'',	"U^",	/* U circumflex */
	'',	"U\"",	/* U dieresis */
	'',	"U:",	/* U dieresis */
	'',	"UE",	/* U dieresis */
	'',	"Ue",	/* U dieresis */
	'',	"Y'",	/* Y acute */
	'',	"P|",	/* Thorn */
	'',	"Th",	/* Thorn */
	'',	"TH",	/* Thorn */
	'',	"ss",	/* sharp s */
	'',	"a`",	/* a grave */
	'',	"a'",	/* a acute */
	'',	"a^",	/* a circumflex */
	'',	"a~",	/* a tilde */
	'',	"a\"",	/* a dieresis */
	'',	"a:",	/* a dieresis */
	'',	"ao",	/* a circle */
	'',	"ae",	/* ae ligature */
	'',	"c,",	/* c cedilla */
	'',	"e`",	/* e grave */
	'',	"e'",	/* e acute */
	'',	"e^",	/* e circumflex */
	'',	"e\"",	/* e dieresis */
	'',	"e:",	/* e dieresis */
	'',	"i`",	/* i grave */
	'',	"i'",	/* i acute */
	'',	"i^",	/* i circumflex */
	'',	"i\"",	/* i dieresis */
	'',	"i:",	/* i dieresis */
	'',	"d-",	/* eth */
	'',	"n~",	/* n tilde */
	'',	"o`",	/* o grave */
	'',	"o'",	/* o acute */
	'',	"o^",	/* o circumflex */
	'',	"o~",	/* o tilde */
	'',	"o\"",	/* o dieresis */
	'',	"o:",	/* o dieresis */
	'',	"oe",	/* o dieresis */
	'',	"-:",	/* divide sign */
	'',	"o/",	/* o slash */
	'',	"u`",	/* u grave */
	'',	"u'",	/* u acute */
	'',	"u^",	/* u circumflex */
	'',	"u\"",	/* u dieresis */
	'',	"u:",	/* u dieresis */
	'',	"ue",	/* u dieresis */
	'',	"y'",	/* y acute */
	'',	"th",	/* thorn */
	'',	"p|",	/* thorn */
	'',	"y\"",	/* y dieresis */
	'',	"y:",	/* y dieresis */
	0,	0,
};

int
latin1(int k1, int k2)
{
	int i;
	struct latin *l;

	for(l=latintab; l->l; l++)
		if(k1==l->c[0] && k2==l->c[1])
			return l->l;
	return 0;
}

void
duartintr(Ureg *ur)
{
	int cause, status, c;
	Duart *duart;
	static int kbdstate, k1, k2;

	duart = DUARTREG;
	cause = duart->is_imr;
	/*
	 * I can guess your interrupt.
	 */
	/*
	 * Is it 0?
	 */
	if(cause & IM_CRDY){
		if(kprofp)
			(*kprofp)(ur->pc);
		c = duart[1].scc_ropbc;
		duart[0].ctur = (Kptime)>>8;
		duart[0].ctlr = (Kptime)&255;
		c = duart[1].scc_sopbc;
		return;
	}
	/*
	 * Is it 1?
	 */
	if(cause & IM_RRDYA){		/* keyboard input */
		status = duart->sr_csr;
		c = duart->data;
		if(status & (FRM_ERR|OVR_ERR|PAR_ERR))
			duart->cmnd = RESET_ERR;
		if(status & PAR_ERR) /* control word: caps lock (0x4) or repeat (0x10) */
			kbdrepeat((c&0x10) == 0);
		else{
			if(c == 0x7F)	/* VIEW key (bizarre) */
				c = 0xFF;
			if(c == 0xB6)	/* NUM PAD */
				kbdstate = 1;
			else{
				if(c & 0x80)
					c = keymap[c&0x7F];
				switch(kbdstate){
				case 1:
					k1 = c;
					kbdstate = 2;
					break;
				case 2:
					k2 = c;
					c = latin1(k1, k2);
					if(c == 0){
						kbdchar(k1);
						c = k2;
					}
					/* fall through */
				default:
					kbdstate = 0;
					kbdchar(c);
				}
			}
		}
	}
	/*
	 * Is it 2?
	 */
	if(cause & IM_RRDYB){		/* rs232 input */
		status = duart[1].sr_csr;
		c = duart[1].data;
		if(status & (FRM_ERR|OVR_ERR|PAR_ERR))
			duart[1].cmnd = RESET_ERR;
		else
			rs232ichar(c);
	}
	/*
	 * Is it 3?
	 */
	if(cause & IM_XRDYB)		/* rs232 output */
		duartrs232intr();
	/*
	 * Is it 4?
	 */
	if(cause & IM_XRDYA)
		duart[0].cmnd = DIS_TX;
	/*
	 * Is it 5?
	 */
	if(cause & IM_IPC)
		mousebuttons((~duart[0].ipc_acr) & 7);
}
