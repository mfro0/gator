/*********************************************************************
 *
 * $Id: theater_out.h,v 1.1.2.3 2003/09/28 15:26:09 fede Exp $
 *
 * Interface file for theater_out module
 *
 * Copyright (C) 2003 Federico Ulivi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * AUTHORS: F.Ulivi
 * NOTES:
 * $Log: theater_out.h,v $
 * Revision 1.1.2.3  2003/09/28 15:26:09  fede
 * Minor aesthetic changes
 *
 * Revision 1.1.2.1  2003/08/31 13:36:35  fede
 * *** empty log message ***
 *
 *
 *********************************************************************/

#ifndef _THEATER_OUT_H
#define _THEATER_OUT_H

#include "radeon.h"

/**********************************************************************
 *
 * TheaterOutPtr
 *
 * Pointer to TheaterOut struct. Actual definition is in theater_out.c
 *
 **********************************************************************/
typedef struct TheaterOut *TheaterOutPtr;

/**********************************************************************
 *
 * TVStd
 *
 * Tv standard
 *
 **********************************************************************/
typedef enum
  {
    TV_STD_NTSC,
    TV_STD_PAL,
    TV_STD_PAL_M,
    TV_STD_PAL_60,
    TV_STD_NTSC_J,
    TV_STD_PAL_CN,
    TV_STD_PAL_N,
  } TVStd;

/**********************************************************************
 *
 * detectTheaterOut
 *
 * Detect presence of a RT chip
 *
 **********************************************************************/

extern
TheaterOutPtr 
detectTheaterOut(
		 GENERIC_BUS_Ptr b
		 );

/**********************************************************************
 *
 * initTheaterOut
 *
 * Initialization of module
 *
 * 's' should be the value of TV_Output option (i.e. the initial TV
 * standard)
 *
 **********************************************************************/

extern
void
initTheaterOut(
	       TheaterOutPtr t,
	       const char *s
	       );

/**********************************************************************
 *
 * theaterOutSave
 *
 * Save current state of RT as initial state (the one that is restored
 * when switching back to text mode)
 *
 **********************************************************************/

extern
void
theaterOutSave(
	       TheaterOutPtr t
	       );

/**********************************************************************
 *
 * theaterOutRestore
 *
 * Restore state of RT from initial state (the one saved through 
 * theaterOutSave)
 *
 **********************************************************************/

extern
void
theaterOutRestore(
		  TheaterOutPtr t,
		  ScrnInfoPtr pScrn
		  );
		  
/**********************************************************************
 *
 * theaterOutInit
 *
 * Define state for cloning current CRTC mode on TV output
 * It works in this way:
 * 1. It checks if resolution in "mode" parameter is one of those 
 *    allowing tv output
 * 2. If resolution is OK, define RT state according to resolution and
 *    and current settings (tv standard etc.)
 *    If resolution is not ok, define RT state to turn tv output off
 * 3. If resolution is OK, modify Radeon state to make it correct for 
 *    tv output (this is needed,e.g., to set vertical frequency to 50/60 Hz)
 *
 * Return value is TRUE when mode is OK for cloning on tv and tv output
 * is enabled, FALSE otherwise
 *
 **********************************************************************/

extern
Bool
theaterOutInit(
	       TheaterOutPtr t,
	       DisplayModePtr mode,
	       RADEONSavePtr save
	       );
	       
/**********************************************************************
 *
 * theaterOutRestoreMode
 *
 * Set state of RT to the one defined by last call to theaterOutInit
 *
 **********************************************************************/

extern
void
theaterOutRestoreMode(
		      TheaterOutPtr t,
		      ScrnInfoPtr pScrn
		      );

/**********************************************************************
 *
 * theaterOutSetStandard
 *
 * Set TV output standard
 *
 * Return value is TRUE when video mode should be set again
 *
 **********************************************************************/

extern
Bool
theaterOutSetStandard(
		      TheaterOutPtr t,
		      TVStd std
		      );
		      
/**********************************************************************
 *
 * theaterOutSetOnOff
 *
 * Set module on/off
 *
 * Return value is TRUE when video mode should be set again
 *
 **********************************************************************/

extern
Bool
theaterOutSetOnOff(
		   TheaterOutPtr t,
		   Bool on
		   );

/**********************************************************************
 *
 * theaterOutGetStandard
 *
 * Get current TV output standard
 *
 **********************************************************************/
extern
TVStd
theaterOutGetStandard(
		      TheaterOutPtr t
		      );

/**********************************************************************
 *
 * theaterOutGetOnOff
 *
 * Return whether module is enabled or not
 * WARNING: TRUE does not necessarily mean that tv output is enabled.
 * That information comes from logical AND between theaterOutGetOnOff and
 * theaterOutGetCompatMode.
 *
 **********************************************************************/
extern
Bool
theaterOutGetOnOff(
		   TheaterOutPtr t
		   );

/**********************************************************************
 *
 * theaterOutGetCompatMode
 *
 * Return whether the current mode is compatible with tv output or not
 *
 **********************************************************************/
extern
Bool
theaterOutGetCompatMode(
			TheaterOutPtr t
			);

/**********************************************************************
 *
 * THEATER_OUT_SYMBOLS
 *
 * Symbol list for module loading
 *
 **********************************************************************/

#define THEATER_OUT_SYMBOLS	"detectTheaterOut", \
				"initTheaterOut", \
				"theaterOutSave", \
				"theaterOutRestore", \
				"theaterOutInit", \
				"theaterOutRestoreMode", \
				"theaterOutSetStandard", \
				"theaterOutSetOnOff", \
				"theaterOutGetStandard", \
				"theaterOutGetOnOff" , \
				"theaterOutGetCompatMode"

/**********************************************************************
 *
 * External access to module functions
 *
 **********************************************************************/

#ifdef XFree86LOADER

#define xf86_detectTheaterOut	((TheaterOutPtr (*)(GENERIC_BUS_Ptr))LoaderSymbol("detectTheaterOut"))
#define xf86_initTheaterOut	((void (*)(TheaterOutPtr , const char*))LoaderSymbol("initTheaterOut"))
#define xf86_theaterOutSave	((void (*)(TheaterOutPtr))LoaderSymbol("theaterOutSave"))
#define xf86_theaterOutRestore	((void (*)(TheaterOutPtr , ScrnInfoPtr))LoaderSymbol("theaterOutRestore"))
#define xf86_theaterOutInit	((Bool (*)(TheaterOutPtr , DisplayModePtr , RADEONSavePtr))LoaderSymbol("theaterOutInit"))
#define xf86_theaterOutRestoreMode	((void (*)(TheaterOutPtr , ScrnInfoPtr))LoaderSymbol("theaterOutRestoreMode"))
#define xf86_theaterOutSetStandard	((Bool (*)(TheaterOutPtr , TVStd))LoaderSymbol("theaterOutSetStandard"))
#define xf86_theaterOutSetOnOff	((Bool (*)(TheaterOutPtr , Bool))LoaderSymbol("theaterOutSetOnOff"))
#define xf86_theaterOutGetStandard	((TVStd (*)(TheaterOutPtr))LoaderSymbol("theaterOutGetStandard"))
#define xf86_theaterOutGetOnOff	((Bool (*)(TheaterOutPtr))LoaderSymbol("theaterOutGetOnOff"))
#define xf86_theaterOutGetCompatMode	((Bool (*)(TheaterOutPtr))LoaderSymbol("theaterOutGetCompatMode"))

#else

#define xf86_detectTheaterOut	detectTheaterOut
#define xf86_initTheaterOut	initTheaterOut
#define xf86_theaterOutSave	theaterOutSave
#define xf86_theaterOutRestore	theaterOutRestore
#define xf86_theaterOutInit	theaterOutInit
#define xf86_theaterOutRestoreMode	theaterOutRestoreMode
#define xf86_theaterOutSetStandard	theaterOutSetStandard
#define xf86_theaterOutSetOnOff	theaterOutSetOnOff
#define xf86_theaterOutGetStandard	theaterOutGetStandard
#define xf86_theaterOutGetOnOff	theaterOutGetOnOff
#define xf86_theaterOutGetCompatMode	theaterOutGetCompatMode

#endif	/* XFree86LOADER */

#endif  /* _THEATER_OUT_H */
