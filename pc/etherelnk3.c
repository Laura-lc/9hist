/*
 * Etherlink III and Fast EtherLink adapters.
 * To do:
 *	check robustness in the face of errors (e.g. busmaster & rxUnderrun);
 *	RxEarly and busmaster;
 *	autoSelect;
 *	PCI latency timer and master enable;
 *	errata list;
 *	rewrite all initialisation.
 *
 * Product ID:
 *	9150 ISA	3C509[B]
 *	9050 ISA	3C509[B]-TP
 *	9450 ISA	3C509[B]-COMBO
 *	9550 ISA	3C509[B]-TPO
 *
 *	9350 EISA	3C579
 *	9250 EISA	3C579-TP
 *
 *	5920 EISA	3C592-[TP|COMBO|TPO]
 *	5970 EISA	3C597-TX	Fast Etherlink 10BASE-T/100BASE-TX
 *	5971 EISA	3C597-T4	Fast Etherlink 10BASE-T/100BASE-T4
 *	5972 EISA	3C597-MII	Fast Etherlink 10BASE-T/MII
 *
 *	5900 PCI	3C590-[TP|COMBO|TPO]
 *	5950 PCI	3C595-TX	Fast Etherlink Shared 10BASE-T/100BASE-TX
 *	5951 PCI	3C595-T4	Fast Etherlink Shared 10BASE-T/100BASE-T4
 *	5952 PCI	3C595-MII	Fast Etherlink 10BASE-T/MII
 *
 *	9000 PCI	3C900-TPO	Etherlink III XL PCI 10BASE-T
 *	9001 PCI	3C900-COMBO	Etherlink III XL PCI 10BASE-T/10BASE-2/AUI
 *	9050 PCI	3C905-TX	Fast Etherlink XL Shared 10BASE-T/100BASE-TX
 *	9051 PCI	3C905-T4	Fast Etherlink Shared 10BASE-T/100BASE-T4
 *
 *	9058 PCMCIA	3C589[B]-[TP|COMBO]
 *
 *	627C MCA	3C529
 *	627D MCA	3C529-TP
 */
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
	IDport			= 0x0110,	/* anywhere between 0x0100 and 0x01F0 */
};

enum {						/* all windows */
	CommandR		= 0x000E,
	IntStatusR		= 0x000E,
};

enum {						/* Commands */
	GlobalReset		= 0x0000,
	SelectRegisterWindow	= 0x0001,
	EnableDcConverter	= 0x0002,
	RxDisable		= 0x0003,
	RxEnable		= 0x0004,
	RxReset			= 0x0005,
	Stall			= 0x0006,	/* 3C90x */
	TxDone			= 0x0007,
	RxDiscard		= 0x0008,
	TxEnable		= 0x0009,
	TxDisable		= 0x000A,
	TxReset			= 0x000B,
	RequestInterrupt	= 0x000C,
	AcknowledgeInterrupt	= 0x000D,
	SetInterruptEnable	= 0x000E,
	SetIndicationEnable	= 0x000F,	/* SetReadZeroMask */
	SetRxFilter		= 0x0010,
	SetRxEarlyThresh	= 0x0011,
	SetTxAvailableThresh	= 0x0012,
	SetTxStartThresh	= 0x0013,
	StartDma		= 0x0014,	/* initiate busmaster operation */
	StatisticsEnable	= 0x0015,
	StatisticsDisable	= 0x0016,
	DisableDcConverter	= 0x0017,
	SetTxReclaimThresh	= 0x0018,	/* PIO-only adapters */
	PowerUp			= 0x001B,	/* not all adapters */
	PowerDownFull		= 0x001C,	/* not all adapters */
	PowerAuto		= 0x001D,	/* not all adapters */
};

enum {						/* (Global|Rx|Tx)Reset command bits */
	tpAuiReset		= 0x0001,	/* 10BaseT and AUI transceivers */
	endecReset		= 0x0002,	/* internal Ethernet encoder/decoder */
	networkReset		= 0x0004,	/* network interface logic */
	fifoReset		= 0x0008,	/* FIFO control logic */
	aismReset		= 0x0010,	/* autoinitialise state-machine logic */
	hostReset		= 0x0020,	/* bus interface logic */
	dmaReset		= 0x0040,	/* bus master logic */
	vcoReset		= 0x0080,	/* on-board 10Mbps VCO */
	updnReset		= 0x0100,	/* upload/download (Rx/TX) logic */

	resetMask		= 0x01FF,
};

enum {						/* Stall command bits */
	upStall			= 0x0000,
	upUnStall		= 0x0001,
	dnStall			= 0x0002,
	dnUnStall		= 0x0003,
};

enum {						/* SetRxFilter command bits */
	receiveIndividual	= 0x0001,	/* match station address */
	receiveMulticast	= 0x0002,
	receiveBroadcast	= 0x0004,
	receiveAllFrames	= 0x0008,	/* promiscuous */
};

enum {						/* StartDma command bits */
	Upload			= 0x0000,	/* transfer data from adapter to memory */
	Download		= 0x0001,	/* transfer data from memory to adapter */
};

enum {						/* IntStatus bits */
	interruptLatch		= 0x0001,
	hostError		= 0x0002,	/* Adapter Failure */
	txComplete		= 0x0004,
	txAvailable		= 0x0008,
	rxComplete		= 0x0010,
	rxEarly			= 0x0020,
	intRequested		= 0x0040,
	updateStats		= 0x0080,
	transferInt		= 0x0100,	/* Bus Master Transfer Complete */
	dnComplete		= 0x0200,
	upComplete		= 0x0400,
	busMasterInProgress	= 0x0800,
	commandInProgress	= 0x1000,

	interruptMask		= 0x07FE,
};

#define COMMAND(port, cmd, a)	outs((port)+CommandR, ((cmd)<<11)|(a))
#define STATUS(port)		ins((port)+IntStatusR)

enum {						/* Window 0 - setup */
	Wsetup			= 0x0000,
						/* registers */
	ManufacturerID		= 0x0000,	/* 3C5[08]*, 3C59[27] */
	ProductID		= 0x0002,	/* 3C5[08]*, 3C59[27] */
	ConfigControl		= 0x0004,	/* 3C5[08]*, 3C59[27] */
	AddressConfig		= 0x0006,	/* 3C5[08]*, 3C59[27] */
	ResourceConfig		= 0x0008,	/* 3C5[08]*, 3C59[27] */
	EepromCommand		= 0x000A,
	EepromData		= 0x000C,
						/* AddressConfig Bits */
	autoSelect9		= 0x0080,
	xcvrMask9		= 0xC000,
						/* ConfigControl bits */
	Ena			= 0x0001,
	base10TAvailable9	= 0x0200,
	coaxAvailable9		= 0x1000,
	auiAvailable9		= 0x2000,
						/* EepromCommand bits */
	EepromReadRegister	= 0x0080,
	EepromBusy		= 0x8000,
};

#define EEPROMCMD(port, cmd, a)	outs((port)+EepromCommand, (cmd)|(a))
#define EEPROMBUSY(port)	(ins((port)+EepromCommand) & EepromBusy)
#define EEPROMDATA(port)	ins((port)+EepromData)

enum {						/* Window 1 - operating set */
	Wop			= 0x0001,
						/* registers */
	Fifo			= 0x0000,
	RxError			= 0x0004,	/* 3C59[0257] only */
	RxStatus		= 0x0008,
	Timer			= 0x000A,
	TxStatus		= 0x000B,
	TxFree			= 0x000C,
						/* RxError bits */
	rxOverrun		= 0x0001,
	runtFrame		= 0x0002,
	alignmentError		= 0x0004,	/* Framing */
	crcError		= 0x0008,
	oversizedFrame		= 0x0010,
	dribbleBits		= 0x0080,
						/* RxStatus bits */
	rxBytes			= 0x1FFF,	/* 3C59[0257] mask */
	rxBytes9		= 0x07FF,	/* 3C5[078]9 mask */
	rxError9		= 0x3800,	/* 3C5[078]9 error mask */
	rxOverrun9		= 0x0000,
	oversizedFrame9		= 0x0800,
	dribbleBits9		= 0x1000,
	runtFrame9		= 0x1800,
	alignmentError9		= 0x2000,	/* Framing */
	crcError9		= 0x2800,
	rxError			= 0x4000,
	rxIncomplete		= 0x8000,
						/* TxStatus Bits */
	txStatusOverflow	= 0x0004,
	maxCollisions		= 0x0008,
	txUnderrun		= 0x0010,
	txJabber		= 0x0020,
	interruptRequested	= 0x0040,
	txStatusComplete	= 0x0080,
};

enum {						/* Window 2 - station address */
	Wstation		= 0x0002,
};

enum {						/* Window 3 - FIFO management */
	Wfifo			= 0x0003,
						/* registers */
	InternalConfig		= 0x0000,	/* 3C509B, 3C589, 3C59[0257] */
	OtherInt		= 0x0004,	/* 3C59[0257] */
	RomControl		= 0x0006,	/* 3C509B, 3C59[27] */
	MacControl		= 0x0006,	/* 3C59[0257] */
	ResetOptions		= 0x0008,	/* 3C59[0257] */
	RxFree			= 0x000A,
						/* InternalConfig bits */
	disableBadSsdDetect	= 0x00000100,
	ramLocation		= 0x00000200,	/* 0 external, 1 internal */
	ramPartition5to3	= 0x00000000,
	ramPartition3to1	= 0x00010000,
	ramPartition1to1	= 0x00020000,
	ramPartition3to5	= 0x00030000,
	ramPartitionMask	= 0x00030000,
	xcvr10BaseT		= 0x00000000,
	xcvrAui			= 0x00100000,	/* 10BASE5 */
	xcvr10Base2		= 0x00300000,
	xcvr100BaseTX		= 0x00400000,
	xcvr100BaseFX		= 0x00500000,
	xcvrMii			= 0x00600000,
	xcvrMask		= 0x00700000,
	autoSelect		= 0x01000000,
						/* MacControl bits */
	deferExtendEnable	= 0x0001,
	deferTimerSelect	= 0x001E,	/* mask */
	fullDuplexEnable	= 0x0020,
	allowLargePackets	= 0x0040,
						/* ResetOptions bits */
	baseT4Available		= 0x0001,
	baseTXAvailable		= 0x0002,
	baseFXAvailable		= 0x0004,
	base10TAvailable	= 0x0008,
	coaxAvailable		= 0x0010,
	auiAvailable		= 0x0020,
	miiConnector		= 0x0040,
};

enum {						/* Window 4 - diagnostic */
	Wdiagnostic		= 0x0004,
						/* registers */
	VcoDiagnostic		= 0x0002,
	FifoDiagnostic		= 0x0004,
	NetworkDiagnostic	= 0x0006,
	PhysicalMgmt		= 0x0008,
	MediaStatus		= 0x000A,
	BadSSD			= 0x000C,
	UpperBytesOk		= 0x000D,
						/* FifoDiagnostic bits */
	txOverrun		= 0x0400,
	rxUnderrun		= 0x2000,
	receiving		= 0x8000,
						/* MediaStatus bits */
	dataRate100		= 0x0002,
	crcStripDisable		= 0x0004,
	enableSqeStats		= 0x0008,
	collisionDetect		= 0x0010,
	carrierSense		= 0x0020,
	jabberGuardEnable	= 0x0040,
	linkBeatEnable		= 0x0080,
	jabberDetect		= 0x0200,
	polarityReversed	= 0x0400,
	linkBeatDetect		= 0x0800,
	txInProg		= 0x1000,
	dcConverterEnabled	= 0x4000,
	auiDisable		= 0x8000,	/* 10BaseT transceiver selected */
};

enum {						/* Window 5 - internal state */
	Wstate			= 0x0005,
						/* registers */
	TxStartThresh		= 0x0000,
	TxAvailableThresh	= 0x0002,
	RxEarlyThresh		= 0x0006,
	RxFilter		= 0x0008,
	InterruptEnable		= 0x000A,
	IndicationEnable	= 0x000C,
};

enum {						/* Window 6 - statistics */
	Wstatistics		= 0x0006,
						/* registers */
	CarrierLost		= 0x0000,
	SqeErrors		= 0x0001,
	MultipleColls		= 0x0002,
	SingleCollFrames	= 0x0003,
	LateCollisions		= 0x0004,
	RxOverruns		= 0x0005,
	FramesXmittedOk		= 0x0006,
	FramesRcvdOk		= 0x0007,
	FramesDeferred		= 0x0008,
	UpperFramesOk		= 0x0009,
	BytesRcvdOk		= 0x000A,
	BytesXmittedOk		= 0x000C,
};

enum {						/* Window 7 - bus master operations */
	Wmaster			= 0x0007,
						/* registers */
	MasterAddress		= 0x0000,
	MasterLen		= 0x0006,
	MasterStatus		= 0x000C,
						/* MasterStatus bits */
	masterAbort		= 0x0001,
	targetAbort		= 0x0002,
	targetRetry		= 0x0004,
	targetDisc		= 0x0008,
	masterDownload		= 0x1000,
	masterUpload		= 0x4000,
	masterInProgress	= 0x8000,

	masterMask		= 0xD00F,
};

enum {						/* 3C90x extended register set */
	PktStatus		= 0x0020,	/* 32-bits */
	DnListPtr		= 0x0024,	/* 32-bits, 8-byte aligned */
	FragAddr		= 0x0028,	/* 32-bits */
	FragLen			= 0x002C,	/* 16-bits */
	ListOffset		= 0x002E,	/* 8-bits */
	TxFreeThresh		= 0x002F,	/* 8-bits */
	UpPktStatus		= 0x0030,	/* 32-bits */
	FreeTimer		= 0x0034,	/* 16-bits */
	UpListPtr		= 0x0038,	/* 32-bits, 8-byte aligned */

						/* PktStatus bits */
	fragLast		= 0x00000001,
	dnCmplReq		= 0x00000002,
	dnStalled		= 0x00000004,
	upCompleteX		= 0x00000008,
	dnCompleteX		= 0x00000010,
	upRxEarlyEnable		= 0x00000020,
	armCountdown		= 0x00000040,
	dnInProg		= 0x00000080,
	counterSpeed		= 0x00000010,	/* 0 3.2uS, 1 320nS */
	countdownMode		= 0x00000020,
						/* UpPktStatus bits (dpd->control) */
	upPktLenMask		= 0x00001FFF,
	upStalled		= 0x00002000,
	upError			= 0x00004000,
	upPktComplete		= 0x00008000,
	upOverrun		= 0x00010000,	/* RxError<<16 */
	upRuntFrame		= 0x00020000,
	upAlignmentError	= 0x00040000,
	upCRCError		= 0x00080000,
	upOversizedFrame	= 0x00100000,
	upDribbleBits		= 0x00800000,
	upOverflow		= 0x01000000,

	dnIndicate		= 0x80000000,	/* FrameStartHeader (dpd->control) */

	updnLastFrag		= 0x80000000,	/* (dpd->len) */

	Nup			= 32,
	Ndn			= 64,
};

/*
 * Up/Dn Packet Descriptors.
 * The hardware info (np, control, addr, len) must be 8-byte aligned
 * and this structure size must be a multiple of 8.
 */
typedef struct Pd Pd;
typedef struct Pd {
	ulong	np;				/* next pointer */
	ulong	control;			/* FSH or UpPktStatus */
	ulong	addr;
	ulong	len;

	Pd*	next;
	Block*	bp;
} Pd;

typedef struct {
	Lock	wlock;				/* window access */

	int	attached;
	int	busmaster;
	Block*	rbp;				/* receive buffer */

	Block*	txbp;				/* FIFO -based transmission */
	int	txthreshold;
	int	txbusy;

	int	nup;				/* full-busmaster -based reception */
	void*	upbase;
	Pd*	upr;
	Pd*	uphead;

	int	ndn;				/* full-busmaster -based transmission */
	void*	dnbase;
	Pd*	dnr;
	Pd*	dnhead;
	Pd*	dntail;
	int	dnq;

	long	interrupts;			/* statistics */
	long	timer;
	long	stats[BytesRcvdOk+3];

	int	upqmax;
	long	upinterrupts;
	long	upqueued;
	int	upstalls;
	int	dnqmax;
	long	dninterrupts;
	long	dnqueued;

	int	xcvr;				/* transceiver type */
	int	rxstatus9;			/* old-style RxStatus register */
	int	rxearly;			/* RxEarlyThreshold */
	int	ts;				/* threshold shift */
	int	upenabled;
	int	dnenabled;
} Ctlr;

static void
init905(Ctlr* ctlr)
{
	Block *bp;
	Pd *pd, *prev;

	/*
	 * Create rings for the receive and transmit sides.
	 * Take care with alignment:
	 *	make sure ring base is 8-byte aligned;
	 *	make sure each entry is 8-byte aligned.
	 */
	ctlr->upbase = malloc((ctlr->nup+1)*sizeof(Pd));
	ctlr->upr = (Pd*)ROUNDUP((ulong)ctlr->upbase, 8);

	prev = ctlr->upr;
	for(pd = &ctlr->upr[ctlr->nup-1]; pd >= ctlr->upr; pd--){
		pd->np = PADDR(&prev->np);
		pd->control = 0;
		bp = allocb(sizeof(Etherpkt));
		pd->addr = PADDR(bp->rp);
		pd->len = updnLastFrag|sizeof(Etherpkt);

		pd->next = prev;
		prev = pd;
		pd->bp = bp;
	}
	ctlr->uphead = ctlr->upr;

	ctlr->dnbase = malloc((ctlr->ndn+1)*sizeof(Pd));
	ctlr->dnr = (Pd*)ROUNDUP((ulong)ctlr->dnbase, 8);

	prev = ctlr->dnr;
	for(pd = &ctlr->dnr[ctlr->ndn-1]; pd >= ctlr->dnr; pd--){
		pd->next = prev;
		prev = pd;
	}
	ctlr->dnhead = ctlr->dnr;
	ctlr->dntail = ctlr->dnr;
	ctlr->dnq = 0;
}

static Block*
rbpalloc(Block* (*f)(int))
{
	Block *bp;
	ulong addr;

	/*
	 * The receive buffers must be on a 32-byte
	 * boundary for EISA busmastering.
	 */
	if(bp = f(ROUNDUP(sizeof(Etherpkt), 4) + 31)){
		addr = (ulong)bp->base;
		addr = ROUNDUP(addr, 32);
		bp->rp = (uchar*)addr;
	}

	return bp;
}

static uchar*
startdma(Ether* ether, ulong address)
{
	int port, status, w;
	uchar *wp;

	port = ether->port;

	w = (STATUS(port)>>13) & 0x07;
	COMMAND(port, SelectRegisterWindow, Wmaster);

	wp = KADDR(inl(port+MasterAddress));
	status = ins(port+MasterStatus);
	if(status & (masterInProgress|targetAbort|masterAbort))
		print("#l%d: BM status 0x%uX\n", ether->ctlrno, status);
	outs(port+MasterStatus, masterMask);
	outl(port+MasterAddress, address);
	outs(port+MasterLen, sizeof(Etherpkt));
	COMMAND(port, StartDma, Upload);

	COMMAND(port, SelectRegisterWindow, w);
	return wp;
}

static void
promiscuous(void* arg, int on)
{
	int filter, port;
	Ether *ether;

	ether = (Ether*)arg;
	port = ether->port;

	filter = receiveBroadcast|receiveIndividual;
	if(ether->nmaddr)
		filter |= receiveMulticast;
	if(on)
		filter |= receiveAllFrames;
	COMMAND(port, SetRxFilter, filter);
}

static void
multicast(void* arg, uchar *addr, int on)
{
	int filter, port;
	Ether *ether;

	USED(addr, on);

	ether = (Ether*)arg;
	port = ether->port;

	filter = receiveBroadcast|receiveIndividual;
	if(ether->nmaddr)
		filter |= receiveMulticast;
	if(ether->prom)
		filter |= receiveAllFrames;
	COMMAND(port, SetRxFilter, filter);
}

static void
attach(Ether* ether)
{
	int port, x;
	Ctlr *ctlr;

	ctlr = ether->ctlr;
	ilock(&ctlr->wlock);
	if(ctlr->attached){
		iunlock(&ctlr->wlock);
		return;
	}

	port = ether->port;

	/*
	 * Set the receiver packet filter for this and broadcast addresses,
	 * set the interrupt masks for all interrupts, enable the receiver
	 * and transmitter.
	 */
	promiscuous(ether, ether->prom);

	x = interruptMask;
	if(ctlr->busmaster == 1)
		x &= ~(rxEarly|rxComplete);
	else{
		if(ctlr->dnenabled)
			x &= ~transferInt;
		if(ctlr->upenabled)
			x &= ~(rxEarly|rxComplete);
	}
	COMMAND(port, SetIndicationEnable, x);
	COMMAND(port, SetInterruptEnable, x);

	COMMAND(port, RxEnable, 0);
	COMMAND(port, TxEnable, 0);

	/*
	 * Prime the busmaster channel for receiving directly into a
	 * receive packet buffer if necessary.
	 */
	if(ctlr->busmaster == 1)
		startdma(ether, PADDR(ctlr->rbp->rp));
	else{
		if(ctlr->upenabled)
			outl(port+UpListPtr, PADDR(&ctlr->uphead->np));
	}

	ctlr->attached = 1;
	iunlock(&ctlr->wlock);
}

static void
statistics(Ether* ether)
{
	int port, i, u, w;
	Ctlr *ctlr;

	port = ether->port;
	ctlr = ether->ctlr;

	/*
	 * 3C59[27] require a read between a PIO write and
	 * reading a statistics register.
	 */
	w = (STATUS(port)>>13) & 0x07;
	COMMAND(port, SelectRegisterWindow, Wstatistics);
	STATUS(port);

	for(i = 0; i < UpperFramesOk; i++)
		ctlr->stats[i] += inb(port+i) & 0xFF;
	u = inb(port+UpperFramesOk) & 0xFF;
	ctlr->stats[FramesXmittedOk] += (u & 0x30)<<4;
	ctlr->stats[FramesRcvdOk] += (u & 0x03)<<8;
	ctlr->stats[BytesRcvdOk] += ins(port+BytesRcvdOk) & 0xFFFF;
	ctlr->stats[BytesRcvdOk+1] += ins(port+BytesXmittedOk) & 0xFFFF;

	switch(ctlr->xcvr){

	case xcvrMii:
	case xcvr100BaseTX:
	case xcvr100BaseFX:
		COMMAND(port, SelectRegisterWindow, Wdiagnostic);
		STATUS(port);
		ctlr->stats[BytesRcvdOk+2] += inb(port+BadSSD);
		break;
	}

	COMMAND(port, SelectRegisterWindow, w);
}

static void
txstart(Ether* ether)
{
	int port, len;
	Ctlr *ctlr;
	Block *bp;

	port = ether->port;
	ctlr = ether->ctlr;

	/*
	 * Attempt to top-up the transmit FIFO. If there's room simply
	 * stuff in the packet length (unpadded to a dword boundary), the
	 * packet data (padded) and remove the packet from the queue.
	 * If there's no room post an interrupt for when there is.
	 * This routine is called both from the top level and from interrupt
	 * level and expects to be called with ctlr->wlock already locked
	 * and the correct register window (Wop) in place.
	 */
	for(;;){
		if(ctlr->txbp){
			bp = ctlr->txbp;
			ctlr->txbp = 0;
		}
		else{
			bp = qget(ether->oq);
			if(bp == nil)
				break;
		}

		len = ROUNDUP(BLEN(bp), 4);
		if(len+4 <= ins(port+TxFree)){
			outl(port+Fifo, BLEN(bp));
			outsl(port+Fifo, bp->rp, len/4);

			freeb(bp);

			ether->outpackets++;
		}
		else{
			ctlr->txbp = bp;
			if(ctlr->txbusy == 0){
				ctlr->txbusy = 1;
				COMMAND(port, SetTxAvailableThresh, len>>ctlr->ts);
			}
			break;
		}
	}
}

static void
txstart905(Ether* ether)
{
	Ctlr *ctlr;
	int port, stalled, timeo;
	Block *bp;
	Pd *pd;

	ctlr = ether->ctlr;
	port = ether->port;

	/*
	 * Free any completed packets.
	 */
	pd = ctlr->dntail;
	while(ctlr->dnq){
		if(PADDR(&pd->np) == inl(port+DnListPtr))
			break;
		if(pd->bp){
			freeb(pd->bp);
			pd->bp = nil;
		}
		ctlr->dnq--;
		pd = pd->next;
	}
	ctlr->dntail = pd;

	stalled = 0;
	while(ctlr->dnq < (ctlr->ndn-1)){
		bp = qget(ether->oq);
		if(bp == nil)
			break;

		pd = ctlr->dnhead->next;
		pd->np = 0;
		pd->control = dnIndicate|BLEN(bp);
		pd->addr = PADDR(bp->rp);
		pd->len = updnLastFrag|BLEN(bp);
		pd->bp = bp;

		if(stalled == 0 && ctlr->dnq && inl(port+DnListPtr)){
			COMMAND(port, Stall, dnStall);
			for(timeo = 100; (STATUS(port) & commandInProgress) && timeo; timeo--)
				;
			if(timeo == 0)
				print("#l%d: dnstall %d\n", ether->ctlrno, timeo);
			stalled = 1;
		}

		ctlr->dnhead->np = PADDR(&pd->np);
		ctlr->dnhead->control &= ~dnIndicate;
		ctlr->dnhead = pd;
		if(ctlr->dnq == 0)
			ctlr->dntail = pd;
		ctlr->dnq++;

		ctlr->dnqueued++;
	}

	if(ctlr->dnq > ctlr->dnqmax)
		ctlr->dnqmax = ctlr->dnq;

	/*
	 * If the adapter is not currently processing anything
	 * and there is something on the queue, start it processing.
	 */
	if(inl(port+DnListPtr) == 0 && ctlr->dnq)
		outl(port+DnListPtr, PADDR(&ctlr->dnhead->np));
	if(stalled)
		COMMAND(port, Stall, dnUnStall);
}

static void
transmit(Ether* ether)
{
	Ctlr *ctlr;
	int port, w;

	port = ether->port;
	ctlr = ether->ctlr;

	ilock(&ctlr->wlock);
	if(ctlr->dnenabled)
		txstart905(ether);
	else{
		w = (STATUS(port)>>13) & 0x07;
		COMMAND(port, SelectRegisterWindow, Wop);
		txstart(ether);
		COMMAND(port, SelectRegisterWindow, w);
	}
	iunlock(&ctlr->wlock);
}

static void
receive905(Ether* ether)
{
	Ctlr *ctlr;
	int len, port, q;
	Pd *pd;
	Block *bp;

	ctlr = ether->ctlr;
	port = ether->port;

	if(inl(port+UpPktStatus) & upStalled)
		ctlr->upstalls++;
	q = 0;
	for(pd = ctlr->uphead; pd->control & upPktComplete; pd = pd->next){
		if(pd->control & upError){
			if(pd->control & upOverrun)
				ether->overflows++;
			if(pd->control & (upOversizedFrame|upRuntFrame))
				ether->buffs++;
			if(pd->control & upAlignmentError)
				ether->frames++;
			if(pd->control & upCRCError)
				ether->crcs++;
		}
		else if(bp = iallocb(sizeof(Etherpkt)+4)){
			len = pd->control & rxBytes;
			pd->bp->wp = pd->bp->rp+len;
			etheriq(ether, pd->bp, 1);
			pd->bp = bp;
			pd->addr = PADDR(bp->rp);
		}

		pd->control = 0;
		COMMAND(port, Stall, upUnStall);

		q++;
	}
	ctlr->uphead = pd;

	ctlr->upqueued += q;
	if(q > ctlr->upqmax)
		ctlr->upqmax = q;
}

static void
receive(Ether* ether)
{
	int len, port, rxerror, rxstatus;
	Ctlr *ctlr;
	Block *bp;

	port = ether->port;
	ctlr = ether->ctlr;

	while(((rxstatus = ins(port+RxStatus)) & rxIncomplete) == 0){
		if(ctlr->busmaster == 1 && (STATUS(port) & busMasterInProgress))
			break;

		/*
		 * If there was an error, log it and continue.
		 * Unfortunately the 3C5[078]9 has the error info in the status register
		 * and the 3C59[0257] implement a separate RxError register.
		 */
		if(rxstatus & rxError){
			if(ctlr->rxstatus9){
				switch(rxstatus & rxError9){

				case rxOverrun9:
					ether->overflows++;
					break;

				case oversizedFrame9:
				case runtFrame9:
					ether->buffs++;
					break;

				case alignmentError9:
					ether->frames++;
					break;

				case crcError9:
					ether->crcs++;
					break;

				}
			}
			else{
				rxerror = inb(port+RxError);
				if(rxerror & rxOverrun)
					ether->overflows++;
				if(rxerror & (oversizedFrame|runtFrame))
					ether->buffs++;
				if(rxerror & alignmentError)
					ether->frames++;
				if(rxerror & crcError)
					ether->crcs++;
			}
		}

		/*
		 * If there was an error or a new receive buffer can't be
		 * allocated, discard the packet and go on to the next.
		 */
		if((rxstatus & rxError) || (bp = rbpalloc(iallocb)) == 0){
			COMMAND(port, RxDiscard, 0);
			while(STATUS(port) & commandInProgress)
				;

			if(ctlr->busmaster == 1)
				startdma(ether, PADDR(ctlr->rbp->rp));

			continue;
		}

		/*
		 * A valid receive packet awaits:
		 *	if using PIO, read it into the buffer;
		 *	discard the packet from the FIFO;
		 *	if using busmastering, start a new transfer for
		 *	  the next packet and as a side-effect get the
		 *	  end-pointer of the one just received;
		 *	pass the packet on to whoever wants it.
		 */
		if(ctlr->busmaster == 0 || ctlr->busmaster == 2){
			len = (rxstatus & rxBytes9);
			ctlr->rbp->wp = ctlr->rbp->rp + len;
			insl(port+Fifo, ctlr->rbp->rp, HOWMANY(len, 4));
		}

		COMMAND(port, RxDiscard, 0);
		while(STATUS(port) & commandInProgress)
			;

		if(ctlr->busmaster == 1)
			ctlr->rbp->wp = startdma(ether, PADDR(bp->rp));

		etheriq(ether, ctlr->rbp, 1);
		ctlr->rbp = bp;
	}
}

static void
interrupt(Ureg*, void* arg)
{
	Ether *ether;
	int port, status, s, w, x;
	Ctlr *ctlr;

	ether = arg;
	port = ether->port;
	ctlr = ether->ctlr;

	lock(&ctlr->wlock);
	w = (STATUS(port)>>13) & 0x07;
	COMMAND(port, SelectRegisterWindow, Wop);

	ctlr->interrupts++;
	ctlr->timer += inb(port+Timer) & 0xFF;
	while((status = STATUS(port)) & (interruptMask|interruptLatch)){
		if(status & hostError){
			/*
			 * Adapter failure, try to find out why, reset if
			 * necessary. What happens if Tx is active and a reset
			 * occurs, need to retransmit? This probably isn't right.
			 */
			COMMAND(port, SelectRegisterWindow, Wdiagnostic);
			x = ins(port+FifoDiagnostic);
			COMMAND(port, SelectRegisterWindow, Wop);
			print("#l%d: status 0x%uX, diag 0x%uX\n",
			    ether->ctlrno, status, x);

			if(x & txOverrun){
				if(ctlr->busmaster == 0)
					COMMAND(port, TxReset, 0);
				else
					COMMAND(port, TxReset, dmaReset);
				COMMAND(port, TxEnable, 0);
			}

			if(x & rxUnderrun){
				/*
				 * This shouldn't happen...
				 * Reset the receiver and restore the filter and RxEarly
				 * threshold before re-enabling.
				 * Need to restart any busmastering?
				 */
				COMMAND(port, SelectRegisterWindow, Wstate);
				s = (port+RxFilter) & 0x000F;
				COMMAND(port, SelectRegisterWindow, Wop);
				COMMAND(port, RxReset, 0);
				while(STATUS(port) & commandInProgress)
					;
				COMMAND(port, SetRxFilter, s);
				COMMAND(port, SetRxEarlyThresh, ctlr->rxearly>>ctlr->ts);
				COMMAND(port, RxEnable, 0);
			}

			status &= ~hostError;
		}

		if(status & (transferInt|rxComplete)){
			receive(ether);
			status &= ~(transferInt|rxComplete);
		}

		if(status & (upComplete)){
			COMMAND(port, AcknowledgeInterrupt, upComplete);
			receive905(ether);
			status &= ~upComplete;
			ctlr->upinterrupts++;
		}

		if(status & txComplete){
			/*
			 * Pop the TxStatus stack, accumulating errors.
			 * Adjust the TX start threshold if there was an underrun.
			 * If there was a Jabber or Underrun error, reset
			 * the transmitter, taking care not to reset the dma logic
			 * as a busmaster receive may be in progress.
			 * For all conditions enable the transmitter.
			 */
			s = 0;
			do{
				if(x = inb(port+TxStatus))
					outb(port+TxStatus, 0);
				s |= x;
			}while(STATUS(port) & txComplete);

			if(s & txUnderrun){
				if(ctlr->dnenabled){
					while(inl(port+PktStatus) & dnInProg)
						;
				}
				COMMAND(port, SelectRegisterWindow, Wdiagnostic);
				while(ins(port+MediaStatus) & txInProg)
					;
				COMMAND(port, SelectRegisterWindow, Wop);
				if(ctlr->txthreshold < ETHERMAXTU)
					ctlr->txthreshold += ETHERMINTU;
			}

			if(s & (txJabber|txUnderrun)){
				if(ctlr->busmaster == 0)
					COMMAND(port, TxReset, 0);
				else
					COMMAND(port, TxReset, (updnReset|dmaReset));
				while(STATUS(port) & commandInProgress)
					;
				COMMAND(port, SetTxStartThresh, ctlr->txthreshold>>ctlr->ts);
				if(ctlr->busmaster == 2)
					outl(port+TxFreeThresh, HOWMANY(ETHERMAXTU, 256));
				if(ctlr->dnenabled)
					status |= dnComplete;
			}

			print("#l%d: txstatus 0x%uX, threshold %d\n",
			    	ether->ctlrno, s, ctlr->txthreshold);
			COMMAND(port, TxEnable, 0);
			ether->oerrs++;
			status &= ~txComplete;
			status |= txAvailable;
		}

		if(status & txAvailable){
			COMMAND(port, AcknowledgeInterrupt, txAvailable);
			ctlr->txbusy = 0;
			txstart(ether);
			status &= ~txAvailable;
		}

		if(status & dnComplete){
			COMMAND(port, AcknowledgeInterrupt, dnComplete);
			txstart905(ether);
			status &= ~dnComplete;
			ctlr->dninterrupts++;
		}

		if(status & updateStats){
			statistics(ether);
			status &= ~updateStats;
		}

		/*
		 * Currently, this shouldn't happen.
		 */
		if(status & rxEarly){
			COMMAND(port, AcknowledgeInterrupt, rxEarly);
			status &= ~rxEarly;
		}

		/*
		 * Panic if there are any interrupts not dealt with.
		 */
		if(status & interruptMask)
			panic("#l%d: interrupt mask 0x%uX\n", ether->ctlrno, status);

		COMMAND(port, AcknowledgeInterrupt, interruptLatch);
	}

	COMMAND(port, SelectRegisterWindow, w);
	unlock(&ctlr->wlock);
}

static long
ifstat(Ether* ether, void* a, long n, ulong offset)
{
	char *p;
	int len;
	Ctlr *ctlr;

	if(n == 0)
		return 0;

	ctlr = ether->ctlr;

	ilock(&ctlr->wlock);
	statistics(ether);
	iunlock(&ctlr->wlock);

	p = malloc(READSTR);
	len = snprint(p, READSTR, "interrupts: %lud\n", ctlr->interrupts);
	len += snprint(p+len, READSTR-len, "timer: %lud\n", ctlr->timer);
	len += snprint(p+len, READSTR-len, "carrierlost: %lud\n", ctlr->stats[CarrierLost]);
	len += snprint(p+len, READSTR-len, "sqeerrors: %lud\n", ctlr->stats[SqeErrors]);
	len += snprint(p+len, READSTR-len, "multiplecolls: %lud\n", ctlr->stats[MultipleColls]);
	len += snprint(p+len, READSTR-len, "singlecollframes: %lud\n", ctlr->stats[SingleCollFrames]);
	len += snprint(p+len, READSTR-len, "latecollisions: %lud\n", ctlr->stats[LateCollisions]);
	len += snprint(p+len, READSTR-len, "rxoverruns: %lud\n", ctlr->stats[RxOverruns]);
	len += snprint(p+len, READSTR-len, "framesxmittedok: %lud\n", ctlr->stats[FramesXmittedOk]);
	len += snprint(p+len, READSTR-len, "framesrcvdok: %lud\n", ctlr->stats[FramesRcvdOk]);
	len += snprint(p+len, READSTR-len, "framesdeferred: %lud\n", ctlr->stats[FramesDeferred]);
	len += snprint(p+len, READSTR-len, "bytesrcvdok: %lud\n", ctlr->stats[BytesRcvdOk]);
	len += snprint(p+len, READSTR-len, "bytesxmittedok: %lud\n", ctlr->stats[BytesRcvdOk+1]);

	if(ctlr->upenabled){
		len += snprint(p+len, READSTR-len, "up: q %lud i %lud m %d s %lud\n",
			ctlr->upqueued, ctlr->upinterrupts, ctlr->upqmax, ctlr->upstalls);
		ctlr->upqmax = 0;
	}
	if(ctlr->dnenabled){
		len += snprint(p+len, READSTR-len, "dn: q %lud i %lud m %d\n",
			ctlr->dnqueued, ctlr->dninterrupts, ctlr->dnqmax);
		ctlr->dnqmax = 0;
	}

	snprint(p+len, READSTR-len, "badssd: %lud\n", ctlr->stats[BytesRcvdOk+2]);

	n = readstr(offset, a, n, p);
	free(p);

	return n;
}

typedef struct Adapter {
	int	port;
	int	irq;
	int	tbdf;
} Adapter;
static Block* adapter;

static void
tcmadapter(int port, int irq, int tbdf)
{
	Block *bp;
	Adapter *ap;

	bp = allocb(sizeof(Adapter));
	ap = (Adapter*)bp->rp;
	ap->port = port;
	ap->irq = irq;
	ap->tbdf = tbdf;

	bp->next = adapter;
	adapter = bp;
}

/*
 * Write two 0 bytes to identify the IDport and then reset the
 * ID sequence. Then send the ID sequence to the card to get
 * the card into command state.
 */
static void
idseq(void)
{
	int i;
	uchar al;
	static int reset, untag;

	/*
	 * One time only:
	 *	reset any adapters listening
	 */
	if(reset == 0){
		outb(IDport, 0);
		outb(IDport, 0);
		outb(IDport, 0xC0);
		delay(20);
		reset = 1;
	}

	outb(IDport, 0);
	outb(IDport, 0);
	for(al = 0xFF, i = 0; i < 255; i++){
		outb(IDport, al);
		if(al & 0x80){
			al <<= 1;
			al ^= 0xCF;
		}
		else
			al <<= 1;
	}

	/*
	 * One time only:
	 *	write ID sequence to get the attention of all adapters;
	 *	untag all adapters.
	 * If a global reset is done here on all adapters it will confuse
	 * any ISA cards configured for EISA mode.
	 */
	if(untag == 0){
		outb(IDport, 0xD0);
		untag = 1;
	}
}

static ulong
activate(void)
{
	int i;
	ushort x, acr;

	/*
	 * Do the little configuration dance:
	 *
	 * 2. write the ID sequence to get to command state.
	 */
	idseq();

	/*
	 * 3. Read the Manufacturer ID from the EEPROM.
	 *    This is done by writing the IDPort with 0x87 (0x80
	 *    is the 'read EEPROM' command, 0x07 is the offset of
	 *    the Manufacturer ID field in the EEPROM).
	 *    The data comes back 1 bit at a time.
	 *    A delay seems necessary between reading the bits.
	 *
	 * If the ID doesn't match, there are no more adapters.
	 */
	outb(IDport, 0x87);
	delay(20);
	for(x = 0, i = 0; i < 16; i++){
		delay(20);
		x <<= 1;
		x |= inb(IDport) & 0x01;
	}
	if(x != 0x6D50)
		return 0;

	/*
	 * 3. Read the Address Configuration from the EEPROM.
	 *    The Address Configuration field is at offset 0x08 in the EEPROM).
	 */
	outb(IDport, 0x88);
	for(acr = 0, i = 0; i < 16; i++){
		delay(20);
		acr <<= 1;
		acr |= inb(IDport) & 0x01;
	}

	return (acr & 0x1F)*0x10 + 0x200;
}

static void
tcm509isa(void)
{
	int irq, port;

	/*
	 * Attempt to activate all adapters. If adapter is set for
	 * EISA mode (0x3F0), tag it and ignore. Otherwise, activate
	 * it fully.
	 */
	while(port = activate()){
		/*
		 * 6. Tag the adapter so it won't respond in future.
		 */
		outb(IDport, 0xD1);
		if(port == 0x3F0)
			continue;

		/*
		 * 6. Activate the adapter by writing the Activate command
		 *    (0xFF).
		 */
		outb(IDport, 0xFF);
		delay(20);

		/*
		 * 8. Can now talk to the adapter's I/O base addresses.
		 *    Use the I/O base address from the acr just read.
		 *
		 *    Enable the adapter and clear out any lingering status
		 *    and interrupts.
		 */
		while(STATUS(port) & commandInProgress)
			;
		COMMAND(port, SelectRegisterWindow, Wsetup);
		outs(port+ConfigControl, Ena);

		COMMAND(port, TxReset, 0);
		COMMAND(port, RxReset, 0);
		COMMAND(port, AcknowledgeInterrupt, 0xFF);

		irq = (ins(port+ResourceConfig)>>12) & 0x0F;
		tcmadapter(port, irq, BUSUNKNOWN);
	}
}

static void
tcm5XXeisa(void)
{
	ushort x;
	int irq, port, slot;

	/*
	 * Check if this is an EISA machine.
	 * If not, nothing to do.
	 */
	if(strncmp((char*)KADDR(0xFFFD9), "EISA", 4))
		return;

	/*
	 * Continue through the EISA slots looking for a match on both
	 * 3COM as the manufacturer and 3C579-* or 3C59[27]-* as the product.
	 * If an adapter is found, select window 0, enable it and clear
	 * out any lingering status and interrupts.
	 */
	for(slot = 1; slot < MaxEISA; slot++){
		port = slot*0x1000;
		if(ins(port+0xC80+ManufacturerID) != 0x6D50)
			continue;
		x = ins(port+0xC80+ProductID);
		if((x & 0xF0FF) != 0x9050 && (x & 0xFF00) != 0x5900)
			continue;

		COMMAND(port, SelectRegisterWindow, Wsetup);
		outs(port+ConfigControl, Ena);

		COMMAND(port, TxReset, 0);
		COMMAND(port, RxReset, 0);
		COMMAND(port, AcknowledgeInterrupt, 0xFF);

		irq = (ins(port+ResourceConfig)>>12) & 0x0F;
		tcmadapter(port, irq, BUSUNKNOWN);
	}
}

static void
tcm59Xpci(void)
{
	Pcidev *p;
	int irq, port;

	p = nil;
	while(p = pcimatch(p, 0x10B7, 0)){
		port = p->mem[0].bar & ~0x01;
		irq = p->intl;
		COMMAND(port, GlobalReset, 0);
		while(STATUS(port) & commandInProgress)
			;

		tcmadapter(port, irq, p->tbdf);
	}
}

static int
tcm5XXpcmcia(Ether* ether)
{
	if(!cistrcmp(ether->type, "3C589") || !cistrcmp(ether->type, "3C562"))
		return ether->port;

	return 0;
}

static void
setxcvr(int port, int xcvr, int is9)
{
	int x;

	if(is9){
		COMMAND(port, SelectRegisterWindow, Wsetup);
		x = ins(port+AddressConfig) & ~xcvrMask9;
		x |= (xcvr>>20)<<14;
		outs(port+AddressConfig, x);
	}
	else{
		COMMAND(port, SelectRegisterWindow, Wfifo);
		x = inl(port+InternalConfig) & ~xcvrMask;
		x |= xcvr;
		outl(port+InternalConfig, x);
	}

	COMMAND(port, TxReset, 0);
	while(STATUS(port) & commandInProgress)
		;
	COMMAND(port, RxReset, 0);
	while(STATUS(port) & commandInProgress)
		;
}

#ifdef notdef
static struct xxx {
	int	available;
	int	next;
} xxx[8] = {
	{ base10TAvailable,	1, },		/* xcvr10BaseT	-> xcvrAui */
	{ auiAvailable,		3, },		/* xcvrAui	-> xcvr10Base2 */
	{ 0, -1, },
	{ coaxAvailable,	-1, },		/* xcvr10Base2	-> nowhere */
	{ baseTXAvailable,	5, },		/* xcvr100BaseTX-> xcvr100BaseFX */
	{ baseFXAvailable,	-1, },		/* xcvr100BaseFX-> nowhere */
	{ miiConnector,		-1, },		/* xcvrMii	-> nowhere */
	{ 0, -1, },
};
#endif /* notdef */

static int
autoselect(int port, int xcvr, int is9)
{
	int media, x;

	/*
	 * Pathetic attempt at automatic media selection.
	 * Really just to get the Fast Etherlink 10BASE-T/100BASE-TX
	 * cards operational.
	 * It's a bonus if it works for anything else.
	 */
	if(is9){
		COMMAND(port, SelectRegisterWindow, Wsetup);
		x = ins(port+ConfigControl);
		media = 0;
		if(x & base10TAvailable9)
			media |= base10TAvailable;
		if(x & coaxAvailable9)
			media |= coaxAvailable;
		if(x & auiAvailable9)
			media |= auiAvailable;
	}
	else{
		COMMAND(port, SelectRegisterWindow, Wfifo);
		media = ins(port+ResetOptions);
	}
//print("autoselect: media %uX\n", media);

	if(media & miiConnector)
		return xcvrMii;

//COMMAND(port, SelectRegisterWindow, Wdiagnostic);
//print("autoselect: media status %uX\n", ins(port+MediaStatus));

	if(media & baseTXAvailable){
		/*
		 * Must have InternalConfig register.
		 */
		setxcvr(port, xcvr100BaseTX, is9);

		COMMAND(port, SelectRegisterWindow, Wdiagnostic);
		x = ins(port+MediaStatus) & ~(dcConverterEnabled|jabberGuardEnable);
		outs(port+MediaStatus, linkBeatEnable|x);
		delay(10);

{ int i, v;
  for(i = 0; i < 2000; i++){
	v = ins(port+MediaStatus);
	if(v & linkBeatDetect){
		print("count %d v %uX\n", i, v);
		return xcvr100BaseTX;
	}
	delay(1);
  }
//print("count %d v %uX\n", i, ins(port+MediaStatus));
}

		if(ins(port+MediaStatus) & linkBeatDetect)
			return xcvr100BaseTX;
		outs(port+MediaStatus, x);
	}

	if(media & base10TAvailable){
		setxcvr(port, xcvr10BaseT, is9);

		COMMAND(port, SelectRegisterWindow, Wdiagnostic);
		x = ins(port+MediaStatus) & ~dcConverterEnabled;
		outs(port+MediaStatus, linkBeatEnable|jabberGuardEnable|x);
		delay(100);

		if(ins(port+MediaStatus) & linkBeatDetect)
			return xcvr10BaseT;
		outs(port+MediaStatus, x);
	}

	/*
	 * Botch.
	 */
	return autoSelect;
}

static int
eepromdata(int port, int offset)
{
	COMMAND(port, SelectRegisterWindow, Wsetup);
	while(EEPROMBUSY(port))
		;
	EEPROMCMD(port, EepromReadRegister, offset);
	while(EEPROMBUSY(port))
		;
	return EEPROMDATA(port);
}

int
etherelnk3reset(Ether* ether)
{
	int busmaster, did, i, port, rxearly, rxstatus9, x, xcvr;
	Block *bp, **bpp;
	Adapter *ap;
	uchar ea[Eaddrlen];
	Ctlr *ctlr;
	static int scandone;

	/*
	 * Scan for adapter on PCI, EISA and finally
	 * using the little ISA configuration dance.
	 */
	if(scandone == 0){
		tcm59Xpci();
		tcm5XXeisa();
		tcm509isa();
		scandone = 1;
	}

	/*
	 * Any adapter matches if no ether->port is supplied,
	 * otherwise the ports must match.
	 */
	port = 0;
	bpp = &adapter;
	for(bp = *bpp; bp; bp = bp->next){
		ap = (Adapter*)bp->rp;
		if(ether->port == 0 || ether->port == ap->port){
			port = ap->port;
			ether->irq = ap->irq;
			ether->tbdf = ap->tbdf;
			*bpp = bp->next;
			freeb(bp);
			break;
		}
		bpp = &bp->next;
	}
	if(port == 0 && (port = tcm5XXpcmcia(ether)) == 0)
		return -1;

	/*
	 * Read the DeviceID from the EEPROM, it's at offset 0x03,
	 * and do something depending on capabilities.
	 */
	switch(did = eepromdata(port, 0x03)){

	case 0x9000:
	case 0x9001:
	case 0x9050:
	case 0x9051:
		if(BUSTYPE(ether->tbdf) != BusPCI)
			goto buggery;
		busmaster = 2;
		goto vortex;

	case 0x5900:
	case 0x5920:
	case 0x5950:
	case 0x5951:
	case 0x5952:
	case 0x5970:
	case 0x5971:
	case 0x5972:
		busmaster = 1;
	vortex:
		COMMAND(port, SelectRegisterWindow, Wfifo);
		xcvr = inl(port+InternalConfig) & (autoSelect|xcvrMask);
		rxearly = 8188;
		rxstatus9 = 0;
		break;

	buggery:
	default:
		busmaster = 0;
		COMMAND(port, SelectRegisterWindow, Wsetup);
		x = ins(port+AddressConfig);
		xcvr = ((x & xcvrMask9)>>14)<<20;
		if(x & autoSelect9)
			xcvr |= autoSelect;
		rxearly = 2044;
		rxstatus9 = 1;
		break;
	}

	/*
	 * Check if the adapter's station address is to be overridden.
	 * If not, read it from the EEPROM and set in ether->ea prior to loading the
	 * station address in Wstation. The EEPROM returns 16-bits at a time.
	 */
	memset(ea, 0, Eaddrlen);
	if(memcmp(ea, ether->ea, Eaddrlen) == 0){
		for(i = 0; i < Eaddrlen/2; i++){
			x = eepromdata(port, i);
			ether->ea[2*i] = x>>8;
			ether->ea[2*i+1] = x;
		}
	}

	COMMAND(port, SelectRegisterWindow, Wstation);
	for(i = 0; i < Eaddrlen; i++)
		outb(port+i, ether->ea[i]);

	/*
	 * Enable the transceiver if necessary and determine whether
	 * busmastering can be used. Due to bugs in the first revision
	 * of the 3C59[05], don't use busmastering at 10Mbps.
	 */
//print("reset: xcvr %uX\n", xcvr);
	if(xcvr & autoSelect)
		xcvr = autoselect(port, xcvr, rxstatus9);
	switch(xcvr){

	case xcvrMii:
		/*
		 * Bug? the 3c905 always seems to have dataRate100 set.
		 */
		COMMAND(port, SelectRegisterWindow, Wdiagnostic);
		if(ins(port+MediaStatus) & dataRate100)
			ether->mbps = 100;
		break;

	case xcvr100BaseTX:
	case xcvr100BaseFX:
		COMMAND(port, SelectRegisterWindow, Wfifo);
		x = inl(port+InternalConfig) & ~ramPartitionMask;
		outl(port+InternalConfig, x|ramPartition1to1);

		COMMAND(port, SelectRegisterWindow, Wdiagnostic);
		x = ins(port+MediaStatus) & ~(dcConverterEnabled|jabberGuardEnable);
		x |= linkBeatEnable;
		outs(port+MediaStatus, x);

		if(x & dataRate100)
			ether->mbps = 100;
		break;

	case xcvr10BaseT:
		/*
		 * Enable Link Beat and Jabber to start the
		 * transceiver.
		 */
		COMMAND(port, SelectRegisterWindow, Wdiagnostic);
		x = ins(port+MediaStatus) & ~dcConverterEnabled;
		x |= linkBeatEnable|jabberGuardEnable;
		outs(port+MediaStatus, x);

		if((did & 0xFF00) == 0x5900)
			busmaster = 0;
		break;

	case xcvr10Base2:
		COMMAND(port, SelectRegisterWindow, Wdiagnostic);
		x = ins(port+MediaStatus) & ~(linkBeatEnable|jabberGuardEnable);
		outs(port+MediaStatus, x);

		/*
		 * Start the DC-DC converter.
		 * Wait > 800 microseconds.
		 */
		COMMAND(port, EnableDcConverter, 0);
		delay(1);
		break;
	}

	/*
	 * Wop is the normal operating register set.
	 * The 3C59[0257] adapters allow access to more than one register window
	 * at a time, but there are situations where switching still needs to be
	 * done, so just do it.
	 * Clear out any lingering Tx status.
	 */
	COMMAND(port, SelectRegisterWindow, Wop);
	while(inb(port+TxStatus))
		outb(port+TxStatus, 0);

	/*
	 * Allocate a controller structure, clear out the
	 * adapter statistics, clear the statistics logged into ctlr
	 * and enable statistics collection. Xcvr is needed in order
	 * to collect the BadSSD statistics.
	 */
	ether->ctlr = malloc(sizeof(Ctlr));
	ctlr = ether->ctlr;

	ilock(&ctlr->wlock);
	ctlr->xcvr = xcvr;
	statistics(ether);
	memset(ctlr->stats, 0, sizeof(ctlr->stats));

	ctlr->busmaster = busmaster;
	ctlr->xcvr = xcvr;
	ctlr->rxstatus9 = rxstatus9;
	ctlr->rxearly = rxearly;
	if(rxearly >= 2048)
		ctlr->ts = 2;

	COMMAND(port, StatisticsEnable, 0);

	/*
	 * Allocate any receive buffers.
	 */
	switch(ctlr->busmaster){

	case 2:
		ctlr->dnenabled = 1;

		/*
		 * 10MUpldBug.
		 * Disabling is too severe, can use receive busmastering at
		 * 100Mbps OK, but how to tell which rate is actually being used -
		 * the 3c905 always seems to have dataRate100 set?
		 * Believe the bug doesn't apply if upRxEarlyEnable is set
		 * and the threshold is set such that uploads won't start
		 * until the whole packet has been received.
		 */
		ctlr->upenabled = 1;
		x = eepromdata(port, 0x0F);
		//print("software info 2: %uX\n", x);
		if(!(x & 0x01))
			outl(port+PktStatus, upRxEarlyEnable);

		if(ctlr->upenabled || ctlr->dnenabled){
			ctlr->nup = Nup;
			ctlr->ndn = Ndn;
			init905(ctlr);
		}
		else
			ctlr->rbp = rbpalloc(allocb);
		outl(port+TxFreeThresh, HOWMANY(ETHERMAXTU, 256));
		break;

	default:
		ctlr->rbp = rbpalloc(allocb);
		break;
	}

	/*
	 * Set a base TxStartThresh which will be incremented
	 * if any txUnderrun errors occur and ensure no RxEarly
	 * interrupts happen.
	 */
	ctlr->txthreshold = ETHERMAXTU/2;
	COMMAND(port, SetTxStartThresh, ctlr->txthreshold>>ctlr->ts);
	COMMAND(port, SetRxEarlyThresh, rxearly>>ctlr->ts);

	iunlock(&ctlr->wlock);

	/*
	 * Linkage to the generic ethernet driver.
	 */
	ether->port = port;
	ether->attach = attach;
	ether->transmit = transmit;
	ether->interrupt = interrupt;
	ether->ifstat = ifstat;

	ether->promiscuous = promiscuous;
	ether->multicast = multicast;
	ether->arg = ether;

	return 0;
}

void
etherelnk3link(void)
{
	addethercard("elnk3",  etherelnk3reset);
	addethercard("3C509",  etherelnk3reset);
}
