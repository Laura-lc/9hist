#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include "screen.h"

static int
cyber938xpageset(VGAscr*, int page)
{
	int opage;

	opage = inb(0x3D8);

	outb(0x3D8, page);
	outb(0x3D9, page);

	return opage;
}

static void
cyber938xpage(VGAscr* scr, int page)
{
	lock(&scr->devlock);
	cyber938xpageset(scr, page);
	unlock(&scr->devlock);
}

static ulong
cyber938xlinear(VGAscr* scr, int* size, int* align)
{
	ulong aperture, oaperture;
	int oapsize, wasupamem;
	Pcidev *p;

	oaperture = scr->aperture;
	oapsize = scr->apsize;
	wasupamem = scr->isupamem;
	if(wasupamem)
		upafree(oaperture, oapsize);
	scr->isupamem = 0;

	if(p = pcimatch(nil, 0x1023, 0)){
		aperture = p->mem[0].bar & ~0x0F;
		*size = p->mem[0].size;
	}
	else
		aperture = 0;

	aperture = upamalloc(aperture, *size, *align);
	if(aperture == 0){
		if(wasupamem && upamalloc(oaperture, oapsize, 0))
			scr->isupamem = 1;
	}
	else
		scr->isupamem = 1;

	return aperture;
}

static void
cyber938xcurdisable(VGAscr*)
{
	vgaxo(Crtx, 0x50, 0);
}

static void
cyber938xcurenable(VGAscr* scr)
{
	int i;
	ulong storage;

	cyber938xcurdisable(scr);

	/*
	 * Cursor colours.
	 */
	for(i = 0x48; i < 0x4C; i++)
		vgaxo(Crtx, i, Pwhite);
	for(i = 0x4C; i < 0x50; i++)
		vgaxo(Crtx, i, Pblack);

	/*
	 * Find a place for the cursor data in display memory.
	 */
	storage = ((scr->gscreen->width*BY2WD*scr->gscreen->r.max.y+1023)/1024)*2;
	vgaxo(Crtx, 0x44, storage & 0xFF);
	vgaxo(Crtx, 0x45, (storage>>8) & 0xFF);
	storage *= 512;
	scr->storage = storage;

	/*
	 * Enable the 32x32 cursor.
	 * (64x64 is bit 0, X11 format is bit 6 and
	 * cursor enable is bit 7).
	 */
	vgaxo(Crtx, 0x50, 0xC0);
}

static void
cyber938xcurload(VGAscr* scr, Cursor* curs)
{
	uchar *p;
	int islinear, opage, y;

	cyber938xcurdisable(scr);

	opage = 0;
	p = KADDR(scr->aperture);
	islinear = vgaxi(Crtx, 0x21) & 0x20;
	if(!islinear){
		lock(&scr->devlock);
		opage = cyber938xpageset(scr, scr->storage>>16);
		p += (scr->storage & 0xFFFF);
	}
	else
		p += scr->storage;

	for(y = 0; y < 16; y++){
		*p++ = curs->set[2*y]|curs->clr[2*y];
		*p++ = curs->set[2*y + 1]|curs->clr[2*y + 1];
		*p++ = 0x00;
		*p++ = 0x00;
	}
	while(y < 32){
		*p++ = 0x00;
		*p++ = 0x00;
		*p++ = 0x00;
		*p++ = 0x00;
		y++;
	}

	/*
	 * This is clearly not what's supposed to be done, but
	 * without a proper datasheet this is what binary search
	 * through the display memory gives as the place for the
	 * pattern.
	 * Note also that is seems the cursor image offset (CRT44
	 * and CRT45) cannot be set above 512KB.
	 * This will do for now as the ThinkPad 560E has 1MB and
	 * can only manage 800x600x8.
	 */
	p += 512*1024 - 128;
	for(y = 0; y < 16; y++){
		*p++ = curs->set[2*y];
		*p++ = curs->set[2*y + 1];
		*p++ = 0x00;
		*p++ = 0x00;
	}
	while(y < 32){
		*p++ = 0x00;
		*p++ = 0x00;
		*p++ = 0x00;
		*p++ = 0x00;
		y++;
	}

	if(!islinear){
		cyber938xpageset(scr, opage);
		unlock(&scr->devlock);
	}

	/*
	 * Save the cursor hotpoint and enable the cursor.
	 */
	scr->offset = curs->offset;
	vgaxo(Crtx, 0x50, 0xC0);
}

static int
cyber938xcurmove(VGAscr* scr, Point p)
{
	int x, xo, y, yo;

	/*
	 * Mustn't position the cursor offscreen even partially,
	 * or it might disappear. Therefore, if x or y is -ve, adjust the
	 * cursor origins instead.
	 */
	if((x = p.x+scr->offset.x) < 0){
		xo = -x;
		x = 0;
	}
	else
		xo = 0;
	if((y = p.y+scr->offset.y) < 0){
		yo = -y;
		y = 0;
	}
	else
		yo = 0;

	/*
	 * Load the new values.
	 */
	vgaxo(Crtx, 0x46, xo);
	vgaxo(Crtx, 0x47, yo);
	vgaxo(Crtx, 0x40, x & 0xFF);
	vgaxo(Crtx, 0x41, (x>>8) & 0xFF);
	vgaxo(Crtx, 0x42, y & 0xFF);
	vgaxo(Crtx, 0x43, (y>>8) & 0xFF);

	return 0;
}

VGAdev vgacyber938xdev = {
	"cyber938x",

	0,				/* enable */
	0,				/* disable */
	cyber938xpage,			/* page */
	cyber938xlinear,		/* linear */
};

VGAcur vgacyber938xcur = {
	"cyber938xhwgc",

	cyber938xcurenable,		/* enable */
	cyber938xcurdisable,		/* disable */
	cyber938xcurload,		/* load */
	cyber938xcurmove,		/* move */
};
