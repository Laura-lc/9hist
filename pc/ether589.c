#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "etherif.h"

enum {
	GlobalReset	= 0x00,		/* Global Reset */
	SelectWindow	= 0x01,		/* SelectWindow command */

	Command		= 0x0E,		/* all windows */
	Status		= 0x0E,

	XcvrTypeMask	= 0xC000,	/* Transceiver Type Select */
	Xcvr10BaseT	= 0x0000,
	XcvrAUI		= 0x4000,
	XcvrBNC		= 0xC000,

	ManufacturerID	= 0x00,		/* window 0 */
	ProductID	= 0x02,
	ConfigControl	= 0x04,
	AddressConfig	= 0x06,
	ResourceConfig	= 0x08,
	EEPROMcmd	= 0x0A,
	EEPROMdata	= 0x0C,

	FIFOdiag	= 0x04,		/* window 4 */
	MediaStatus	= 0x0A,

					/* MediaStatus bits */
	JabberEna	= 0x0040,	/* Jabber Enabled (writeable) */
	LinkBeatEna	= 0x0080,	/* Link Beat Enabled (writeable) */
	LinkBeatOk	= 0x0800,	/* Valid link beat detected (ro) */
};

#define COMMAND(port, cmd, a)	outs(port+Command, ((cmd)<<11)|(a))

extern int ether509reset(Ether*);

static int
configASIC(Ether *ether, int port, int xcvr)
{
	ushort x;

	/* set Window 0 configuration registers */
	COMMAND(port, SelectWindow, 0);

	/* ROM size & base - must be set before we can access ROM */
	/* transceiver type is 2 for 'figure it out'  */
	x = ins(port + AddressConfig);
	outs(port + AddressConfig, (x & 0xf0) | xcvr);

	/* IRQ must be 3 on 3C589 */
	x = ins(port + ResourceConfig);
	outs(port + ResourceConfig, 0x3f00 | (x&0xff));

	/* move product ID to register */
	while(ins(port+EEPROMcmd) & 0x8000)
		;
	outs(port+EEPROMcmd, (2<<6)|3);
	while(ins(port+EEPROMcmd) & 0x8000)
		;
	x = ins(port+EEPROMdata);
	outs(port + ProductID, x);

	return ether509reset(ether);
}

static int
reset(Ether *ether)
{
	int slot;
	int port;

	if(ether->irq == 0)
		ether->irq = 10;
	if(ether->port == 0)
		ether->port = 0x240;
	port = ether->port;

	slot = pcmspecial("3C589", ether);
	if(slot < 0)
		return -1;

	/* try configuring as a 10baseT */
	if(configASIC(ether, port, Xcvr10BaseT) < 0){
		pcmspecialclose(slot);
		return -1;
	}
	delay(100);
	COMMAND(port, SelectWindow, 4);
	if(ins(port+MediaStatus) & LinkBeatOk){
		/* reselect window 1 for normal operation */
		COMMAND(port, SelectWindow, 1);
		print("10baseT 3C589\n");
		return 0;
	}

	/* try configuring as a 10base2 */
	COMMAND(port, GlobalReset, 0);
	if(configASIC(ether, port, XcvrBNC) < 0){
		pcmspecialclose(slot);
		return -1;
	}
	print("BNC 3C589\n");
	return 0;
}

void
ether589link(void)
{
	addethercard("3C589", reset);
}
