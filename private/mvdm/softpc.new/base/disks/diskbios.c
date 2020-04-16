#include "insignia.h"
#include "host_def.h"

/*	
*		diskbios.c
*
*	The fast disk bios & disk post routine plus some disk debugging routines
*
*	This file combine the old diskbios.c & fast_dbios.c it is designed to
*	sit alongside fdisk.c but doesn't interact with it.
*
*	Post & debugging from the old diskbios by Jerry Kramskoy
*	Fast disk bios by Ade Brownlow
*
*	NB: This file does not comply with all of Insignias standards.
*/

#ifdef SCCSID
static char	SccsID[] = "@(#)diskbios.c	1.36 04/12/95 Copyright Insignia Solutions Inc.";
#endif

#ifdef SEGMENTATION
/*
 * The following #include specifies the code segment into which this
 * module will by placed by the MPW C compiler on the Mac II running
 * MultiFinder.
 */
#include "AT_STUFF.seg"
#endif

/* includes */
#include <stdio.h>
#include TypesH
#include "xt.h"
#include CpuH
#include "sas.h"
#include "ios.h"
#include "gmi.h"
#include "trace.h"
#include "fdisk.h"
#include "cmos.h"
#include "ica.h"
#include "error.h"
#include "config.h"
#include "dsktrace.h"
#include "idetect.h"
#include "debug.h"

/* disk status BIOS variable location */
#define DISK_STATUS1 0x474

/* id mismatch return */
#define IDMISMATCH 	1

/* status byte values */
#define STATUS_RECORD_NOT_FOUND 0x04
#define STATUS_BAD_COMMAND 0x01
#define STATUS_NO_ERROR 0xe0
#define STATUS_DMA_BOUNDRY 0x09
#define STATUS_ERROR 0x01
#define STATUS_CLEAR 0x00
#define STATUS_INIT_FAIL 0x07

/* error conditions */
#define ERROR_NO_ID 0x10
#define ERROR_COMMAND_ABORT 0x04
#define ERROR_CLEAR 0x00
#define ERROR_READ_ONLY_MEDIA 0x03 /* this is really only for floppies but it has the desired effect on DOS */

/* command parameters structure */
/* use ints to allow the compiler to chose the fastest type */
typedef struct _com
{
	int drive;
	int sectors;
	int head;
	int cylinder_low;
	int cylinder_high;
	int start_sector;
	int xfersegment;
	int xferoffset;
} command_params;

/* this structure holds information about each drive */
typedef struct _dt
{
	int connected;
} drivetable;

/* disk parameter block ...... */
typedef struct _dpb
{
	unsigned short cyls;
	UTINY heads;
	UTINY sectors;
} dpb_block;

/********************************************************/
/* local globals */
LOCAL drivetable drivetab[2];
LOCAL command_params com;
LOCAL dpb_block dpb[2];

/* again allow compiler choice of types 16 bit ints are large enough */
LOCAL int maxsectors, maxoffset;
LOCAL int tfstatus;

#ifndef PROD
/*
* disk trace control variable; global only for use by Yoda.
*/
GLOBAL IU32 disktraceinfo;
#endif

/*
* non-zero if drive 1 does not exist
*/
LOCAL int drive1notwiredup;


/********************************************************/
/* support functionallity */

void disk_post IPT0();
LOCAL void hd_reset IPT2(int, drive, int, diag);

LOCAL UTINY rerror IPT0();
LOCAL void werror IPT1(UTINY, error);
LOCAL int check_drive IPT1(int, drive);
LOCAL long dosearchid IPT0();
LOCAL int checkdatalimit IPT0();
LOCAL void getdpb IPT1(int, drive);
LOCAL void wstatus IPT1(UTINY, value);
LOCAL UTINY rcmos IPT1(int, index);
LOCAL void wcmos IPT2(int, index, UTINY, value);
LOCAL UTINY rstatus IPT0();
LOCAL void disk_reset IPT0();
LOCAL void return_status IPT0();
LOCAL void disk_read IPT0();
LOCAL void disk_write IPT0();
LOCAL void disk_verify IPT0();
LOCAL void format IPT0();
LOCAL void badcmd IPT0();
LOCAL void get_drive_params IPT0();
LOCAL void init_drive IPT0();
LOCAL void disk_seek IPT0();
LOCAL void test_ready IPT0();
LOCAL void recalibrate IPT0();
LOCAL void diagnostics IPT0();
LOCAL void read_dasd IPT0();
LOCAL void enable_disk_interrupts IPT0();

LOCAL void hd_reset IPT2(int, drive, int, diag);

/********************************************************/
/* POST & DEBUG defines*/

/*
 * define the disk interrupt vector generated by the PIC
 */
#define	IVTDISKINTRUPT		0x1d8	/* 4 * 0x76 */

 /*
  * value to send to fixed disk register (3f6) to enable head select 3 (thus
  * enabling head addresses of 8-0xf)
  */
#define	ENABLE_HD_SEL_3		8	/* enable head 3, + enable fixed disk * interrupts */

 /*
  * BIOS variables Interrupt vectors
  */
#define	IVT13			0x4c	/* 4 * 0x13 */
#define	IVT40			0x100	/* 4 * 0x40 */
#define	IVT41			0x104	/* 4 * 0x41 */
#define	IVT46			0x118	/* 4 * 0x46 */

 /*
  * BIOS data area (segment 40h)
  */
#define	HF_NUM			0x475
#define	HF_STATUS		0x48c
#define	HF_INT_FLAG		0x48e

/*
PROCEDURE	  : 	disk_post()

PURPOSE		  : 	called during POST to establish the number
			of drives available, and to set the disk
			subsystem up prior to booting DOS. Should be
			called after configuration has been processed
			for hard disk(s) (if any)

PARAMETERS	  :	none

GLOBALS		  :

RETURNED VALUE	  : 	none

DESCRIPTION	  : 	reads the CMOS to see if any disks.
			sets up IVT entries so that INT 13h
			routes to hard disk BIOS rather than floppy
			disk BIOS. sets INT 40h to route to floppy.
			sets INT 'DISKINTRUPT' to route to disk interrupt service
			routine. sets INT 41h to point to disk parameter
			block (dpb) for fixed drive 0, and INT 46h to
			point to the dpb for fixed drive 1 (or to same as
			INT 41h if no drive 1). sets drive parameters for
			the drive(s).

*/

void disk_post IFN0()
{
	UTINY diag;
	UTINY disks;
	UTINY ndisks;
#if !(defined(NTVDM) && defined(MONITOR))
	USHORT diskette_offset;
	USHORT diskette_seg;
#endif /* !(NTVDM && MONITOR) */
	int i;

	/*
	 * read diagnostic byte of CMOS
	 */
	 diag = rcmos (CMOS_DIAG);

#if !(defined(NTVDM) && defined(MONITOR))
	/* We don't want this section if running on NTVDM MONITOR */

	 drive1notwiredup = ~0;

	/*
	 * set up IVT entries
	 */

	 dt0 (DBIOS | CALL, 0, "disk_post() stealing IVT's\n")

	 diskette_offset = sas_w_at(IVT13);
	 diskette_seg = sas_w_at(IVT13 + 2);
	 sas_storew (IVT13, DISKIO_OFFSET);
	 sas_storew (IVT13 + 2, SYSROM_SEG);
	 sas_storew (IVT40, diskette_offset);
	 sas_storew (IVT40 + 2, diskette_seg);
	 sas_storew (IVT41, DPB0_OFFSET);
	 sas_storew (IVT41 + 2, SYSROMORG_SEG);
	 sas_storew (IVT46, DPB0_OFFSET);
	 sas_storew (IVT46 + 2, SYSROMORG_SEG);
	 sas_storew (IVTDISKINTRUPT, DISKISR_OFFSET);
	 sas_storew (IVTDISKINTRUPT + 2, SYSROM_SEG);

	 enable_disk_interrupts ();

	/*
	 * clear BIOS status variable
	 */
	 wstatus (0);

	/*
	 * if CMOS is bad, go no further
	 */
	if (diag & (BAD_BAT + BAD_CKSUM))
		 return;

	/*
	 * initially show ok to IPL off drive C
	 */
	 wcmos (CMOS_DIAG, diag & ~HF_FAIL);

#endif  /* !(NTVDM && MONITOR) */

#ifdef NTVDM	/* True for all NT VDMs */
	/*
	 * Microsoft NT VDM specfic change:
	 * Fake up no hard disks in CMOS and consequently BIOS RAM
	 * because we don't want apps accessing the real hard disk
	 * under NT.
	 *
	 */
	wcmos(CMOS_DISK, (half_word) (NO_HARD_C | NO_HARD_D));

#endif	/* NTVDM */

	/*
	 * read cmos disk byte for configured drives. Note we don't
	 * support extended drive types. We always use types 1 and 2
	 * (or 0 if no drive) for drives C and D respectively
	 */
	 disks = rcmos (CMOS_DISK);

	if (disks & 0xf0)
	{
		ndisks = 1;
		if (disks & 0xf)
		{

			/*
			 * BIOS happy to accept drive 1 if CMOS
			 * indicates it
			 */
			drive1notwiredup = 0;
			ndisks++;
			sas_storew (IVT46, DPB1_OFFSET);
			sas_storew (IVT46 + 2, SYSROMORG_SEG);
		}

		/*
		 * set BIOS variable indicating the number of drives
		 * configured according to the CMOS
		 */
		 sas_store (HF_NUM, ndisks);

		/*
		 * reset the drive(s)
		 */
		for (i = 0; i < ndisks; i++)
			hd_reset (i, diag);
	}
	else
	{
		sas_store (HF_NUM, 0);
		wcmos (CMOS_DIAG, diag | HF_FAIL);
	}
}


/*
FUNCTION	:	hd_reset(drive)
PURPOSE		:	set drive parameters for drive, and
			recalibrate it. If the drive is not
			attached to the disk controller, then
			an error will be returned by the controller
			(Drive not ready). If an error occurs on
			drive 0, then set the CMOS to forbid IPL
			from the hard disk.
EXTERNAL OBJECTS:
RETURN VALUE	:
INPUT  PARAMS	:	drive	-	0	(drive C)
				   or	1	(drive D)
*/
LOCAL void hd_reset IFN2(int, drive, int, diag)
{

	/*
	 * set up drive id
	 */
	setDL (0x80 + drive);

	/*
	 * set the drive parameters
	 */
	setAH (0x9);
	disk_io ();

	/*
	 * recalibrate the drive
	 */
	setAH (0x11);
	disk_io ();

	if (getCF () && !drive)
		wcmos (CMOS_DIAG, diag | HF_FAIL);
}

#ifndef PROD
/*
FUNCTION	:	relevant()
PURPOSE		:	filter out unwanted tracing as requested by the
			disk trace flag.
INPUT PARAMS	:
	i		encoded flag indicating type of tracing information
			asking to be output
*/
LOCAL BOOL relevant IFN1(int, i)
{
	if ((i & disktraceinfo) == i)
		return ~0;
	else
		return 0;
}

LOCAL BOOL storing_trace = FALSE;
LOCAL int stored_trace_hndl, stored_trace_count = 0;

#ifdef macintosh
GLOBAL	int	*stored_trace_data;
#else
LOCAL int stored_trace_data[1024];
#endif
LOCAL int stored_tr_dptr = 0;
LOCAL int stored_infoid;
LOCAL char stored_tr_string[80];

void unload_stored_trace IFN1(int, hndl)
{
	int i,linecount;

	if (stored_infoid & DBIOS)
		fprintf (trace_file, "DBIOS:");

	if (stored_infoid & DHW)
		fprintf (trace_file, "DHW  :");

	switch (hndl)
	{
	case INW_TRACE_HNDL:
	case OUTW_TRACE_HNDL:
		if (stored_trace_count == 1)
		{
			fprintf (trace_file, "%s : 0x%04x\n",stored_tr_string,stored_trace_data[0]);
		}
		else
		{
			fprintf (trace_file, "%d lots of %s : \n",
				stored_trace_count,stored_tr_string);
			linecount = 8;
			for (i=0;i<stored_trace_count;i++)
			{
				if (linecount >= 8)
				{
					fprintf(trace_file, "\n");
					if (stored_infoid & DBIOS)
						fprintf (trace_file, "DBIOS:");
				
					if (stored_infoid & DHW)
						fprintf (trace_file, "DHW  :");
					linecount = 0;
				}
				fprintf(trace_file, "0x%04x ",stored_trace_data[i]);
				linecount++;
			}
			fprintf(trace_file, "\n");
		}
		break;
	default:
		/* unknown handle */
		break;
	}
	stored_tr_dptr = 0;
	stored_trace_count = 0;
}

void add_to_stored_trace IFN1(int, data)
{
	stored_trace_data[stored_tr_dptr++]=data;
}

#ifdef ANSI
void disktrace (int infoid, int nargs, int hndl, char *fmt, unsigned long a1,
		unsigned long a2, unsigned long a3, unsigned long a4,
		unsigned long a5)
#else
void disktrace (infoid, nargs, hndl, fmt, a1, a2, a3, a4, a5)
int infoid;
int nargs;
int hndl;
char *fmt;
unsigned long a1;
unsigned long a2;
unsigned long a3;
unsigned long a4;
unsigned long a5;
#endif	/* ANSI */
{


	if (relevant (infoid))
	{
		switch(hndl)
		{
		case INW_TRACE_HNDL:
		case OUTW_TRACE_HNDL:
			if (storing_trace && (stored_trace_hndl != hndl))
				unload_stored_trace(stored_trace_hndl);
			if (!storing_trace)
			{	
				storing_trace = TRUE;
				stored_trace_hndl = hndl;
				strcpy(stored_tr_string, fmt);
				stored_infoid = infoid;
			}
			add_to_stored_trace(a1);
			stored_trace_count++;
			return;
		default:
		case 0:
			if (storing_trace)
				unload_stored_trace(stored_trace_hndl);
			storing_trace = FALSE;
			break;
		}			
				
		if (infoid & DBIOS)
			fprintf (trace_file, "DBIOS:");

		if (infoid & DHW)
			fprintf (trace_file, "DHW  :");

		switch (nargs)
		{
			case 0:
				fprintf (trace_file, fmt);
				break;
			case 1:
				fprintf (trace_file, fmt, a1);
				break;
			case 2:
				fprintf (trace_file, fmt, a1, a2);
				break;
			case 3:
				fprintf (trace_file, fmt, a1, a2, a3);
				break;
			case 4:
				fprintf (trace_file, fmt, a1, a2, a3, a4);
				break;
			case 5:
				fprintf (trace_file, fmt, a1, a2, a3, a4, a5);
				break;
		}
	}
}


void setdisktrace IFN0()
{
	char l[30];
	int value;

/* make the compiler happy */
#if defined(NTVDM) && defined(MONITOR)
	value = 0;
#endif
	printf ("select disk trace mask\n");
	printf ("\tcmnd info\t%x\n",CMDINFO);
	printf ("\texec info\t%x\n",XINFO);
	printf ("\texec status\t%x\n",XSTAT);
	printf ("\tphys.att\t%x\n",PAD);
	printf ("\tio-att\t\t%x\n",IOAD);
	printf ("\tportio\t\t%x\n",PORTIO);
	printf ("\tints\t\t%x\n",INTRUPT);
	printf ("\thw xinfo\t%x\n",HWXINFO);
	printf ("\tdata dump\t%x\n",DDATA);
	printf ("\tPhys IO\t\t%x\n",PHYSIO);
	printf ("\thardware\t\t%x\n",DHW);
	printf ("\tbios\t\t%x\n",DBIOS);
#ifdef WDCTRL_BOP
	printf ("\twdctrl\t\t%x\n",WDCTRL);
#endif /* WDCTRL_BOP
	printf (" .. ? ");
	 gets (l);
	 sscanf (l, "%x", &value);

	/*
	 * automatically select BIOS entry,exit tracing if BIOS
	 * tracing selected
	 */
	if (value & DBIOS)
		 value |= CALL;
	 disktraceinfo = value;
}
#endif				/* nPROD */


/********************************************************/
/* BIOS mainline and functions */

#ifdef ANSI
LOCAL void (*disk_func[]) (void) =
#else
LOCAL void (*disk_func[]) () =
#endif
{
	disk_reset,		/* 0x00 */
	return_status,		/* 0x01 */
	disk_read,		/* 0x02 */
	disk_write,		/* 0x03 */
	disk_verify,		/* 0x04 */
	format,			/* 0x05 */
	badcmd,			/* 0x06 */
	badcmd,			/* 0x07 */
	get_drive_params,	/* 0x08 */
	init_drive,		/* 0x09 */
	disk_read,		/* 0x0a */
	disk_write,		/* 0x0b */
	disk_seek,		/* 0x0c */
	disk_reset,		/* 0x0d */
	badcmd,			/* 0x0e */
	badcmd,			/* 0x0f */
	test_ready,		/* 0x10 */
	recalibrate,		/* 0x11 */
	badcmd,			/* 0x12 */
	badcmd,			/* 0x13 */
	diagnostics,		/* 0x14 */
	read_dasd,		/* 0x15 */
	/* 0x16 - 0x19 are all floppy commands */
};

#ifndef PROD
static char *BIOSnames[] =
{
        "reset disk (AH=0)",
        "read last status (AH=1)",
        "read sectors (AH=2)",
        "write sectors (AH=3)",
        "verify sectors (AH=4)",
        "format track (AH=5)",
        "unused (AH=6)",
        "unused (AH=7)",
        "return current drive parameters (AH=8)",
        "set drive geometry for controller (AH=9)",
        "read long (AH=0xa)",
        "write long (AH=0xb)",
        "seek (AH=0xc)",
        "alternate disk reset (AH=0xd)",
        "unused (AH=0xe)",
        "unused (AH=0xf)",
        "test drive ready (AH=0x10)",
        "recalibrate (AH=0x11)",
        "unused (AH=0x12)",
        "unused (AH=0x13)",
        "diagnostics (AH=0x14)",
        "read dasd type (AH=0x15)"
};
#endif /* nPROD */

/* Fixed disk BIOS mainline */
void disk_io IFN0()
{
	register int BIOS_command;
#ifndef PROD
	int ax,bx,cx,dx,es;
#endif


	IDLE_disk();


	/* what function to perform ? */
	/* what command ?? */
	BIOS_command = getAH ();

#ifndef PROD

        ax = getAX();
        bx = getBX();
        cx = getCX();
        dx = getDX();
        es = getES();

        dt5(DBIOS, 0, "<ax %x bx %x cx %x dx %x es %x\n", ax,bx,cx,dx,es)

        if (BIOS_command > 0x15)
                dt1(DBIOS|CALL, 0, "bad BIOS call (AH=%x)\n", BIOS_command)
        else
                dt1(DBIOS|CALL, 0, "BIOS call = %s\n",
				(unsigned long)BIOSnames[BIOS_command])

#endif /* PROD */

	/* clear up the status before we kick off */
	/* but only if this is NOT a read status command */
	if (BIOS_command != 0x01)
		wstatus (STATUS_CLEAR);

	/* setup our command structure from the remaining registers */
	com.drive = getDL () & 0x7f;
	com.sectors = getAL ();
	com.head = getDH () & 0xf;
	com.cylinder_low = getCH ();
	com.cylinder_high = (getCL () & 0xc0) >> 6;
	com.start_sector = getCL () & 0x3f;
	com.xfersegment = getES () + ((getBX ()) >> 4);
	com.xferoffset = getBX () & 0x0f;

	/* DMA boundary check */
	if (BIOS_command == 0x0a || BIOS_command == 0x0b)
	{
		maxsectors = 127;
		maxoffset = 4;
	}
	else
	{
		maxsectors = 128;
		maxoffset = 0;
	}

	/* call our function if sensible ie the drive requested exists */
	/* and the BIOS command is in a sensible range */
	if (check_drive (com.drive)
	    && BIOS_command < 0x16 && BIOS_command >= 0x00)
	{
		/* call it */
		(*disk_func[BIOS_command]) ();
	}
	else
	{
		badcmd ();
	}

	/* setup the applications return */
	if (BIOS_command != 0x15)
		setAH (rstatus ());
	if (rstatus ())
	{
		/*
		 * Command has failed - set the carry flag.  We also
		 * do some tracing if the command is not 'status'.  The
		 * latter is excluded as it is sometimes used as a 'poll'
		 * of a device.
		 */
#ifndef PROD
		if (BIOS_command != 8) {
			assert0 (NO,"FAST DISK COMMAND FAILED \n");
			assert1 (NO,"STATUS %x   ", rstatus ());
			assert1 (NO,"BIOS_command %x \n", BIOS_command);
		}
#endif /* PROD */
		setCF (1);	/* cmd fails */
	}
	else
		setCF (0);	/* cmd ok */
        dt2(DBIOS|CALL, 0, "CF=%c, status=%x(hex)\n",
		getCF()?'T':'F',(unsigned)rstatus())
}

/*0x?? badcommand catch all */
LOCAL void badcmd IFN0()
{
	wstatus (STATUS_BAD_COMMAND);
}

/*0x00 reset the hard disk system */
LOCAL void disk_reset IFN0()
{
	register int i;

	/* interupts on */
	enable_disk_interrupts ();

	/* do reset for the attached drives */
	for (i = 0; i < 2; i++)
	{
		if (drivetab[i].connected)
		{
			host_fdisk_seek0 (i);

			com.drive = i;
			init_drive ();
		}
	}
	wstatus (STATUS_CLEAR);
}

/*0x01 send back the status byte */
LOCAL void return_status IFN0()
{
	setAL (rstatus ());
        dt1(DBIOS|CMDINFO, 0, "\treturned status = %x\n", (unsigned)rstatus())
	wstatus (STATUS_CLEAR);
}

#ifdef	ERROR
#undef	ERROR
#endif

#define ERROR()		{\
						werror (ERROR_NO_ID | ERROR_COMMAND_ABORT); \
						wstatus (STATUS_ERROR); \
						return;\
					}

/*0x02 disk read sector(s) */
LOCAL void disk_read IFN0()
{
	long offset; 
	host_addr inbuf;
	sys_addr pdata;

	/* find offset into the hd file */
	if ((offset = dosearchid ()) == IDMISMATCH)
		ERROR();

	dt3(DBIOS|CMDINFO, 0, "\t%d sectors to read, \n\tbuffer at [%x:%x]\n", com.sectors, (unsigned)getES(), (unsigned) getBX())

	if (checkdatalimit ())
	{
	    sas_store(HF_INT_FLAG, 0);

		/* read to where ? */
		pdata = effective_addr (getES (), getBX ());

		if (!(inbuf = (host_addr)sas_transbuf_address (pdata, com.sectors*512L)))
		{
			assert0 (NO,"No BUFFER in disk_read");
			ERROR();
		}

		dt1(DBIOS|XINFO,0, "\t\trd buffer from card -> memory (offset %x)\n", pdata );

		if (!host_fdisk_rd (com.drive, offset, com.sectors,(char *) inbuf))
			ERROR();

		/* now store what we read */
		sas_stores_from_transbuf (pdata, inbuf, com.sectors*512L);
	}
	else
	{
		/* status set by function */
		ERROR();
	}
	wstatus (STATUS_CLEAR);
	setAL ((unsigned char) com.sectors);
}

/*0x03 disk write sector(s) */
LOCAL void disk_write IFN0()
{
	long offset;
	host_addr outbuf;
	sys_addr pdata;

	/* check for read_only disk */
	if (!config_get_active(C_HARD_DISK1_NAME + com.drive))
	{
		werror (ERROR_READ_ONLY_MEDIA);	/* floppy write prot */
		wstatus (STATUS_ERROR);
		return;
	}

	/* find offset into the hd file */
	if ((offset = dosearchid ()) == IDMISMATCH)
		ERROR();

	/* check for segment wrap around */
	if (checkdatalimit ())
	{
	        sas_store(HF_INT_FLAG, 0);

		/* write from where */
		pdata = effective_addr (getES (), getBX ());

		/* get the transfer buffer */
		if (!(outbuf = (host_addr)sas_transbuf_address (pdata, com.sectors*512L)))
		{
			assert0 (NO,"No BUFFER in disk_write\n");
			ERROR();
		}

		/* load our stuff to the transfer buffer */
		sas_loads_to_transbuf (pdata, outbuf, com.sectors*512L);

		if (!host_fdisk_wt (com.drive, offset, com.sectors, (char *)outbuf))
			ERROR();
	}
	else
	{
		ERROR();
	}
	wstatus (STATUS_CLEAR);
	setAL ((unsigned char) com.sectors);
}

/*0x04 disk verify sector */
LOCAL void disk_verify IFN0()
{
	/* this is a dummy really */
	wstatus (STATUS_CLEAR);
}

/*0x05 format track  is this EVER used ?? */
LOCAL void format IFN0()
{
	register int i = 0;

	/* 17 sectors/track */
	while (i < 17)
	{
		/* one sector at a time */
		com.sectors = 1;

		/* start at this sector */
		com.start_sector = i;

		/* write to disk */
		disk_write ();

		if (rstatus ())
		{
			/* we have failed */
			return;
		}

		i++;		/* next sector */
	}
}

/*0x06-0x07 bad commands */

/*0x08 get drive parameters */
LOCAL void get_drive_params IFN0()
{
	/* valid drive or what ?? */
	if (check_drive (com.drive))
	{
		long maxcylinder = 0;

		getdpb (com.drive);

		/* how many drives are there */
		if (drivetab[1].connected)
			setDL (2);
		else
			setDL (1);

		/* number of heads 0 - max */
		setDH (dpb[com.drive].heads - 1);

		/* number of cylinders is max addressable - 0th - diagnostic */
		maxcylinder = dpb[com.drive].cyls - 2;
		setCH (maxcylinder & 0xff);
		setCL (dpb[com.drive].sectors | ((maxcylinder >> 8) << 6));

		/* we are happy */
		wstatus (STATUS_CLEAR);
		setAX (0);
	}
	else
	{
		/* oooooooops */
		wstatus (STATUS_INIT_FAIL);
		setAX (STATUS_INIT_FAIL);
		setDX (0);
		setCX (0);
	}
}

/*0x09 initialise drives */
LOCAL void init_drive IFN0()
{
	/* another dummy really */
	getdpb (com.drive);
	if (drivetab[com.drive].connected)
		wstatus (STATUS_CLEAR);
	else
		wstatus (STATUS_INIT_FAIL);
}

/*0x0a read long mapped to 0x02 */
/*0x0b write long mapped to 0x03 */
/*0x0c seek */
LOCAL void disk_seek IFN0()
{
	/* don't do anything physical here just do a search and set result */

	com.sectors = 1;	/* Awful frig to stop false fails */

	if (dosearchid () == IDMISMATCH)
		ERROR();

	/* no problem */
	wstatus (STATUS_CLEAR);
}

/*0x0d reset disk system mapped to 0x00*/
/*0x0e-0x0f bad commands */
/*0x10 give drive status */
LOCAL void test_ready IFN0()
{
	register half_word status;

	/* read our status, check for a fault if there is a fault */
	/* then fix the status and tell them we failed */
	/* the only fault possible is an error state */
	status = rstatus ();
	if (status & STATUS_ERROR)
	{
		/* read the error status */
		status = rerror ();
		if (status & ERROR_NO_ID)
		{
			wstatus (STATUS_RECORD_NOT_FOUND);
		}
		else
		{
			if (status & ERROR_COMMAND_ABORT)
			{
				wstatus (STATUS_BAD_COMMAND);
		                dt1(DBIOS|CMDINFO, 0, "\tdrive %d not ready\n", com.drive)
			}
			else
			{
				wstatus (STATUS_NO_ERROR);
                		dt1(DBIOS|CMDINFO, 0, "\tdrive %d ready\n", com.drive)
			}
		}
	}
	else
	{
		wstatus (STATUS_CLEAR);
                dt1(DBIOS|CMDINFO, 0, "\tdrive %d ready\n", com.drive)
	}
}

/*0x11 recalibrate */
LOCAL void recalibrate IFN0()
{
	if (!drivetab[com.drive].connected)
		wstatus (STATUS_ERROR);
	else
		wstatus (STATUS_CLEAR);
	host_fdisk_seek0 (com.drive);
}

/*0x12-0x13 bad commands */
/*0x14 controller internal diagnostic */
LOCAL void diagnostics IFN0()
{
	/* return the controller ok  - what controller */
	setAH (0);
	wstatus (STATUS_CLEAR);
        dt0(DBIOS|CMDINFO, 0, "\tcontroller diags.ok\n")
}

/*0x15 get disk type */
LOCAL void read_dasd IFN0()
{
	register int blocks;

	wstatus (STATUS_CLEAR);
	if ((!drivetab[com.drive].connected) && com.drive > 0)
	{
		/* drive not availiable */
		setAX (0);

		/* no blocks */
		setCX (0);
		setDX (0);
		setCF (0);
                dt0(DBIOS|CMDINFO, 0, "\tdrive 1 not available\n")
		return;
	}

	/* get physical address of disk param block for drive */
	getdpb (com.drive);

	/* set the number of blocks */
	blocks = (dpb[com.drive].cyls - 1) * dpb[com.drive].heads * dpb[com.drive].sectors;
	setCX (blocks / 256);
	setDX (blocks % 256);

	/* fixed disk exists */
	setAH (3);
	setCF (0);
        dt2(DBIOS|CMDINFO, 0, "\tdrive (%d) has %d blocks available\n", com.drive, blocks)
}

/*0x16 - 0x19 handled by floppy controller */

/********************************************************/
/* Support functions */

/* read the status register */
LOCAL UTINY rstatus IFN0()
{
	UTINY disk_stat;

	/* read the BIOS variable */
	disk_stat = sas_hw_at(DISK_STATUS1);
	return (disk_stat);
}

/* write the status register */
LOCAL void wstatus IFN1(UTINY,value)
{
	/* set BIOS var */
	sas_store (DISK_STATUS1, value);
}

LOCAL UTINY rcmos IFN1(int, index)
{
	UTINY value;

	cmos_outb (CMOS_PORT, index);
	cmos_inb (CMOS_DATA, &value);
	return value;
}

LOCAL void wcmos IFN2(int,index,UTINY,value)
{
	cmos_outb (CMOS_PORT, index);
	cmos_outb (CMOS_DATA, value);
}

/* get the disk parameter block for a given drive */
LOCAL void getdpb IFN1(int, drive)
{
	sys_addr ivt;
	sys_addr pdpb;
	unsigned short offset;
	unsigned short segment;

	/* choose apropriate vector */
	if (!drive)
		ivt = IVT41;
	else
		ivt = IVT46;

	/* read IVT to get address of disk parameter block */
	offset = sas_w_at(ivt);
	segment = sas_w_at(ivt + 2);
	pdpb = effective_addr ((unsigned long) segment, offset);

	/* read the relevant params */
	dpb[drive].cyls = sas_w_at(pdpb);
	dpb[drive].heads = sas_hw_at(pdpb + 2);
	dpb[drive].sectors = sas_hw_at(pdpb + 14);
}

/* detect segment overflow when reading/writing disk */
LOCAL int checkdatalimit IFN0()
{
	if (com.sectors > maxsectors)
	{
		wstatus (STATUS_DMA_BOUNDRY);
                dt1(DBIOS|XINFO, 0, "\t\ttoo many sectors (%d(dec))to transfer\n",
com.sectors)
		return (0);
	}
	else
	{
		if (com.sectors == maxsectors)
		{
			if (com.xferoffset > maxoffset)
			{
				dt2(DBIOS|XINFO, 0, "\t\tat max.sectors(%d(dec)), bad offset(%x(hex)) for transfer\n", com.sectors, com.xferoffset)
				wstatus (STATUS_DMA_BOUNDRY);
				return (0);
			}
		}
	}
	return (1);
}

/* simulate search on hard disk for cyl,hd,sec id field */
LOCAL long dosearchid IFN0()
{
	long maxhead, cylinder, bytes_per_cyl, bytes_per_track;

	maxhead = (dpb[com.drive].heads-1) & 0xf;

	/* head ok? (heads numbered from 0 - maxhead) */
	if (com.head > maxhead)
	{
		return (IDMISMATCH);
	}

	/*
	 * sector ok? (assumes all tracks have been formatted with sector ids
	 * 1 - nsecspertrack which is DOS standard)
	 */
	if (com.start_sector == 0 || 
		com.start_sector > dpb[com.drive].sectors || 
		com.sectors <= 0)
	{
		return (IDMISMATCH);
	}

	/* set up the correct cylinder */
	cylinder = (((unsigned long) com.cylinder_high) << 8) +
	    (unsigned long) com.cylinder_low;

	/*
	 * cylinder ok? (we've imposed an artificial limit on the maximum
	 * cylinder number based upon the file size)
	 */
	if (cylinder >= dpb[com.drive].cyls)
	{
		return (IDMISMATCH);
	}

	bytes_per_track = dpb[com.drive].sectors * 512L;
	bytes_per_cyl = bytes_per_track * (maxhead + 1);

	return (cylinder * bytes_per_cyl + com.head *
	    bytes_per_track + (com.start_sector - 1L) * 512L);
}

/* check the drive is valid for the command */
LOCAL int check_drive IFN1(int, drive)
{
	if (!drive)
		return (1);
	if ((drive > 1) || (drive == 1 && !drivetab[1].connected))
	{
		badcmd ();
		return (0);
	}
	return (1);
}

/* write to the error flag */
LOCAL UTINY error_register;
LOCAL void werror IFN1(UTINY,error)
{
	error_register = error;
}

/* read the error register */
LOCAL UTINY rerror IFN0()
{
	return (error_register);
}

#define INTB01	(io_addr)0xa1
#define INTA01	(io_addr)0x21
LOCAL void enable_disk_interrupts IFN0()
{
	 UTINY value;

	 inb (INTB01, &value);
	 value &= 0xbf;
	 outb (INTB01, value);
	 inb (INTA01, &value);
	 value &= 0xfb;
	 outb (INTA01, value);
}

/********************************************************/
/* the following are called by fdisk_physattach and detach and just initialise this */
/* cut down BIOS */

/* tell the turbo bios a drive is attached */
GLOBAL void fast_disk_bios_attach IFN1(int, drive)
{
	drivetab[drive].connected = 1;
}

/* tell the turbo bios a drive is unattached */
GLOBAL void fast_disk_bios_detach IFN1(int, drive)
{
	drivetab[drive].connected = 0;
}


#ifdef WDCTRL_BOP

/* ===================================== wdctrl_bop ===========================
 * PURPOSE:
 *		BOP to allow fast (32-bit) disk access in Windows
 *
 *		based on disk_read and disk_write
 *
 * INPUT:
 *		Intel registers set up as:
 *			EAX	Start sector
 *			ECX	Number of sectors
 *			DS:EBX	intel buffer address (just EBX in flat mode)
 *			DL	drive (80h or 81h)
 *			DH	command (BDC_READ or BDC_WRITE)
 *
 *
 * OUTPUT:
 *		CF	set if error, clear on success
 * ============================================================================
 */

#define BDC_READ	0	/* read n sectors, called in flat memory model */
#define BDC_WRITE	1	/* write n sectors, called in flat memory model */
#define WDCTRL_TEST	0xff	/* read n sectors, called during real mode init */

GLOBAL void wdctrl_bop IFN0()
{

	IUM16	command;

	IDLE_disk();

	/* what command ?? */
	command = getDH();

#ifndef PROD
	/* #ifndef PROD so the tests don't occur in PROD version */
	{
		IUM32	cs = getCS();
		IUM32	eip = getEIP();
		
		if (command == BDC_READ)
		{
			dt2(WDCTRL, 0, "WDCTRL READ: called from %04x:%08x\n",
	                                cs, eip);
	        }
	        else if (command == BDC_WRITE)
		{
			dt2(WDCTRL, 0, "WDCTRL WRITE: called from %04x:%08x\n",
					cs, eip);
		}
		else
		{
			dt3(WDCTRL, 0, "WDCTRL command %d: called from %04x:%08x\n",
					command, cs, eip);
		}
	}
#endif

	/* clear up the status before we kick off */
	wstatus (STATUS_CLEAR);

	/* setup our command structure from the remaining registers */
	com.drive = getDL () & 0x7f;
	com.sectors = getECX ();
	com.start_sector = getEAX ();
	com.xferoffset = getEBX();

	dt4(WDCTRL, 0, "WDCTRL: drive=%d   start_sector=%d   num sectors=%d   xfer addr %08x\n",
			com.drive, com.start_sector, com.sectors,
			com.xferoffset);

	maxsectors = 128;
	maxoffset = 0;

	/* call our function if sensible ie the drive requested exists */
	/* and the BIOS command is in a sensible range */
	if (check_drive (com.drive) &&
		command >= BDC_READ && command <= BDC_WRITE)
	{
		long offset; 
		host_addr buf;
		IU8	*phys_addr;
		
		/* check for segment overflow and that we aren't writing to
		 * a read-only disk
		 */
		if (checkdatalimit () &&
			((command == BDC_READ) ||
				config_get_active(C_HARD_DISK1_NAME + com.drive)))
		{
			sas_store(HF_INT_FLAG, 0);

			/* Convert sector to offset in file */
			offset = com.start_sector * 512L;

			if (!(buf = (host_addr)sas_transbuf_address (com.xferoffset, com.sectors*512L)))
			{
				assert0 (NO,"No BUFFER in wdctrl_bop");
				ERROR();
			}
	
			if (command == BDC_READ)
			{
				if (!host_fdisk_rd (com.drive, offset, com.sectors, (char *) buf))
				{
					ERROR();
				}
				/* now store what we read into intel memory */
				sas_stores_from_transbuf(com.xferoffset, buf, com.sectors*512L);
			}
			else /* BDC_WRITE */
			{
				/* load our stuff from intel memory to the transfer buffer */
				sas_loads_to_transbuf (com.xferoffset, buf, com.sectors*512L);
				if (!host_fdisk_wt (com.drive, offset, com.sectors, (char *)buf))
				{
					ERROR();
				}
			}
		}
		else /* checkdatalimit failed */
		{
			/* status set by function */
			ERROR();
		}
		wstatus (STATUS_CLEAR);
		setAL ((unsigned char) com.sectors);
	}
	else if (command == WDCTRL_TEST)
	{
		/* This path used by wdrminit.asm to check bop is there and is
		 * reading data OK.
		 */
		long offset; 
		host_addr buf;
		sys_addr pdata;

		/* check for segment overflow and that we aren't writing to
		 * a read-only disk
		 */
		if (checkdatalimit ())
		{
			sas_store(HF_INT_FLAG, 0);

			/* read/write where ? */
			com.xfersegment = getDS();
			dt2(WDCTRL, 0, "WDCTRL: addr %04:%08x\n",
					com.xfersegment, com.xferoffset);

			pdata = effective_addr (com.xfersegment, com.xferoffset);

			/* Convert sector to offset in file */
			offset = com.start_sector * 512L;

			if (!(buf = (host_addr)sas_transbuf_address (pdata, com.sectors*512L)))
			{
				assert0 (NO,"No BUFFER in wdctrl_bop");
				ERROR();
			}
	
			if (!host_fdisk_rd (com.drive, offset, com.sectors, (char *) buf))
			{
				ERROR();
			}
			/* now store what we read */
			sas_stores_from_transbuf (pdata, buf, com.sectors*512L);
		}
		else /* checkdatalimit failed */
		{
			/* status set by function */
			ERROR();
		}
		wstatus (STATUS_CLEAR);
		setAL ((unsigned char) com.sectors);
	}
#ifndef PROD
/* These are for debugging scatter gather - I never had one and don't know
 * if the intel side is correct...
 */
else if (command == 4)
{
	/* Just to let me know we've got a scatter/gather loop */
	dt0(WDCTRL, 0, "wdctrl_bop: scatter gather detected ********\n");
	force_yoda();
	return;
}
else if (command == 5)
{
	/* Just to let me know we've got a scatter/gather loop */
	dt0(WDCTRL, 0, "wdctrl_bop: scatter gather processing ********\n");
	force_yoda();
	return;
}
/*
 * end of debugging commands
 */
#endif	/* PROD */
	else
	{
		badcmd ();
	}

	setAH (rstatus ());
	
	if (rstatus ())
	{
		/*
		 * Command has failed - set the carry flag.  We also
		 * do some tracing if the command is not 'status'.  The
		 * latter is excluded as it is sometimes used as a 'poll'
		 * of a device.
		 */
#ifndef PROD
		assert0 (NO,"FAST DISK COMMAND FAILED \n");
		assert1 (NO,"STATUS %x   ", rstatus ());
		assert1 (NO,"command %x \n", command);
#endif /* PROD */
		setCF (1);	/* cmd fails */
	}
	else
	{
		setCF (0);	/* cmd ok */
	}

	dt2(WDCTRL, 0, "CF=%c, status=%x(hex)\n",
		getCF()?'1':'0',(unsigned)rstatus());
}
#endif	/* WDCTRL_BOP */
