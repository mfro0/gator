/*********************************************************************
 *
 * $Id: theater_out.c,v 1.1.1.1.2.4 2003/09/28 15:11:57 fede Exp $
 *
 * Main file for tv output handling of ATI Rage Theater chip 
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
 * $Log: theater_out.c,v $
 * Revision 1.1.1.1.2.4  2003/09/28 15:11:57  fede
 * Some minor aesthetic changes
 *
 * Revision 1.1.1.1.2.2  2003/08/31 13:35:58  fede
 * Adapted for XFree86
 *
 * Revision 1.1.1.1.2.1  2003/08/09 23:20:46  fede
 * Switched to memory mapped IO
 * Some magic numbers turned into named constants
 *
 * Revision 1.1.1.1  2003/07/24 15:37:27  fede
 * Initial version
 *
 *
 *********************************************************************/

#include "xf86.h"
#include "generic_bus.h"
#include "theatre_reg.h"
#include "radeon_reg.h"
#include "radeon_macros.h"
#include "theater_out.h"

#undef read
#undef write
#undef ioctl

#if RADEON_DEBUG
#define RTTRACE(x)							\
do {									\
    ErrorF("(**) %s(%d): ", RADEON_NAME, 0);		\
    ErrorF x;								\
} while (0);
#else
#define RTTRACE(x)
#endif

/**********************************************************************
 *
 * MASK_N_BIT
 *
 **********************************************************************/

#define MASK_N_BIT(n)	(1UL << (n))

/**********************************************************************
 *
 * Constants
 *
 **********************************************************************/

/*
 * Reference frequency
 * FIXME: This should be extracted from BIOS data
 */
#define REF_FREQUENCY		27000

/*
 * Values for TV PLL for PAL color burst (nominally 4433618.75 Hz)
 * These values apply to 27 MHz reference freq.
 */
#define TV_PLL_N_PAL_27000	668
#define TV_PLL_M_PAL_27000	113
#define TV_PLL_PD_PAL_27000	3
#define TV_PLL_FINE_PAL_27000	0x10000000

/*
 * VIP_TV_PLL_CNTL
 */
#define VIP_TV_PLL_CNTL_M_SHIFT		0
#define VIP_TV_PLL_CNTL_NLO		0x1ff
#define VIP_TV_PLL_CNTL_NLO_SHIFT	8
#define VIP_TV_PLL_CNTL_NHI		0x600
#define VIP_TV_PLL_CNTL_NHI_SHIFT	(21-9)
#define VIP_TV_PLL_CNTL_P_SHIFT		24

/*
 * VIP_CRT_PLL_CNTL
 */
#define VIP_CRT_PLL_CNTL_M		0xff
#define VIP_CRT_PLL_CNTL_M_SHIFT	0
#define VIP_CRT_PLL_CNTL_NLO		0x1ff
#define VIP_CRT_PLL_CNTL_NLO_SHIFT	8
#define VIP_CRT_PLL_CNTL_NHI		0x600
#define VIP_CRT_PLL_CNTL_NHI_SHIFT	(21-9)
#define VIP_CRT_PLL_CNTL_CLKBY2		MASK_N_BIT(25)

/*
 * Value for VIP_PLL_CNTL0
 */
#define VIP_PLL_CNTL0_INI		0x00acac18
#define VIP_PLL_CNTL0_TVSLEEPB		MASK_N_BIT(3)
#define VIP_PLL_CNTL0_CRTSLEEPB		MASK_N_BIT(4)

/*
 * Value for VIP_PLL_TEST_CNTL
 */
#define VIP_PLL_TEST_CNTL_INI		0

/*
 * VIP_CLOCK_SEL_CNTL
 */
#define VIP_CLOCK_SEL_CNTL_INI		0x33
#define VIP_CLOCK_SEL_CNTL_BYTCLK_SHIFT	2
#define VIP_CLOCK_SEL_CNTL_BYTCLK	0xc
#define VIP_CLOCK_SEL_CNTL_REGCLK	MASK_N_BIT(5)
#define VIP_CLOCK_SEL_CNTL_BYTCLKD_SHIFT 8

/*
 * Value for VIP_CLKOUT_CNTL
 */
#define VIP_CLKOUT_CNTL_INI		0x29

/*
 * Value for VIP_SYNC_LOCK_CNTL
 */
#define VIP_SYNC_LOCK_CNTL_INI		0x01000000

/*
 * Value for VIP_TVO_SYNC_PAT_EXPECT
 */
#define VIP_TVO_SYNC_PAT_EXPECT_INI	0x00000001

/*
 * VIP_RGB_CNTL
 */
#define VIP_RGB_CNTL_RGB_IS_888_PACK	MASK_N_BIT(0)

/*
 * Value for VIP_VFTOTAL
 */
#define VIP_VFTOTAL_INI			3

/*
 * Value for VIP_VSCALER_CNTL2
 */
#define VIP_VSCALER_CNTL2_INI		0x10000000

/*
 * Value for VIP_Y_FALL_CNTL
 */
/* #define VIP_Y_FALL_CNTL_INI		0x00010200 */
#define VIP_Y_FALL_CNTL_INI		0x80030400

/*
 * VIP_UV_ADR
 */
#define VIP_UV_ADR_INI			0xc8
#define VIP_UV_ADR_HCODE_TABLE_SEL	0x06000000
#define VIP_UV_ADR_HCODE_TABLE_SEL_SHIFT 25
#define VIP_UV_ADR_VCODE_TABLE_SEL	0x18000000
#define VIP_UV_ADR_VCODE_TABLE_SEL_SHIFT 27
#define VIP_UV_ADR_MAX_UV_ADR		0x000000ff
#define VIP_UV_ADR_MAX_UV_ADR_SHIFT	0
#define VIP_UV_ADR_TABLE1_BOT_ADR	0x0000ff00
#define VIP_UV_ADR_TABLE1_BOT_ADR_SHIFT	8
#define VIP_UV_ADR_TABLE3_TOP_ADR	0x00ff0000
#define VIP_UV_ADR_TABLE3_TOP_ADR_SHIFT	16
#define MAX_FIFO_ADDR			0x1a7

/*
 * VIP_HOST_RD_WT_CNTL
 */
#define VIP_HOST_RD_WT_CNTL_RD		MASK_N_BIT(12)
#define VIP_HOST_RD_WT_CNTL_RD_ACK	MASK_N_BIT(13)
#define VIP_HOST_RD_WT_CNTL_WT		MASK_N_BIT(14)
#define VIP_HOST_RD_WT_CNTL_WT_ACK	MASK_N_BIT(15)

/*
 * Value for VIP_SYNC_CNTL
 */
#define VIP_SYNC_CNTL_INI		0x28

/*
 * VIP_VSCALER_CNTL1
 */
#define VIP_VSCALER_CNTL1_UV_INC	0xffff
#define VIP_VSCALER_CNTL1_UV_INC_SHIFT	0

/*
 * VIP_TIMING_CNTL
 */
#define VIP_TIMING_CNTL_UV_OUT_POST_SCALE_SHIFT	24
#define VIP_TIMING_CNTL_INI		0x000b0000
#define VIP_TIMING_CNTL_H_INC_SHIFT	0

/*
 * Value for VIP_MODULATOR_CNTL1
 */
#define VIP_MODULATOR_CNTL1_INI		0x60bb3bcc

/*
 * Value for VIP_MODULATOR_CNTL2
 */
#define VIP_MODULATOR_CNTL2_INI		0x003e01b2

/*
 * Value for VIP_PRE_DAC_MUX_CNTL
 */
#define VIP_PRE_DAC_MUX_CNTL_INI	0x0000000f

/*
 * Value for VIP_TV_DAC_CNTL
 */
#define VIP_TV_DAC_CNTL_ON_INI		0x00000013
#define VIP_TV_DAC_CNTL_OFF_INI		0x0000004a
#define VIP_TV_DAC_CNTL_NBLANK		MASK_N_BIT(0)
#define VIP_TV_DAC_CNTL_DASLEEP		MASK_N_BIT(3)
#define VIP_TV_DAC_CNTL_BGSLEEP		MASK_N_BIT(6)

/*
 * Value for VIP_FRAME_LOCK_CNTL
 */
#define VIP_FRAME_LOCK_CNTL_INI		0x0000000f

/*
 * Value for VIP_HW_DEBUG
 */
#define VIP_HW_DEBUG_INI 		0x00000200

/*
 * VIP_MASTER_CNTL
 */
#define VIP_MASTER_CNTL_TV_ASYNC_RST	MASK_N_BIT(0)
#define VIP_MASTER_CNTL_CRT_ASYNC_RST	MASK_N_BIT(1)
#define VIP_MASTER_CNTL_RESTART_PHASE_FIX	MASK_N_BIT(3)
#define VIP_MASTER_CNTL_TV_FIFO_ASYNC_RST	MASK_N_BIT(4)
#define VIP_MASTER_CNTL_VIN_ASYNC_RST	MASK_N_BIT(5)
#define VIP_MASTER_CNTL_AUD_ASYNC_RST	MASK_N_BIT(6)
#define VIP_MASTER_CNTL_DVS_ASYNC_RST	MASK_N_BIT(7)
#define VIP_MASTER_CNTL_CRT_FIFO_CE_EN	MASK_N_BIT(9)
#define VIP_MASTER_CNTL_TV_FIFO_CE_EN	MASK_N_BIT(10)
#define VIP_MASTER_CNTL_ON_INI		(VIP_MASTER_CNTL_RESTART_PHASE_FIX | \
					 VIP_MASTER_CNTL_VIN_ASYNC_RST | \
					 VIP_MASTER_CNTL_AUD_ASYNC_RST | \
					 VIP_MASTER_CNTL_DVS_ASYNC_RST | \
					 VIP_MASTER_CNTL_CRT_FIFO_CE_EN | \
					 VIP_MASTER_CNTL_TV_FIFO_CE_EN)
#define VIP_MASTER_CNTL_OFF_INI		(VIP_MASTER_CNTL_TV_ASYNC_RST | \
					 VIP_MASTER_CNTL_CRT_ASYNC_RST | \
					 VIP_MASTER_CNTL_RESTART_PHASE_FIX | \
					 VIP_MASTER_CNTL_TV_FIFO_ASYNC_RST | \
					 VIP_MASTER_CNTL_VIN_ASYNC_RST | \
					 VIP_MASTER_CNTL_AUD_ASYNC_RST | \
					 VIP_MASTER_CNTL_DVS_ASYNC_RST)

/*
 * Value for VIP_LINEAR_GAIN_SETTINGS
 */
#define VIP_LINEAR_GAIN_SETTINGS_INI	0x01000100

/*
 * Value for VIP_GAIN_LIMIT_SETTINGS_INI
 */
#define VIP_GAIN_LIMIT_SETTINGS_INI	0x017f05ff

/*
 * Value for VIP_UPSAMP_AND_GAIN_CNTL 
 */
#define VIP_UPSAMP_AND_GAIN_CNTL_INI	0x00000005

/*
 * RADEON_VCLK_ECP_CNTL
 */
#define RADEON_VCLK_ECP_CNTL_BYTECLK_POSTDIV	0x00030000
#define RADEON_VCLK_ECP_CNTL_BYTECLK_NODIV	0x00000000

/*
 * RADEON_PLL_TEST_CNTL
 */
#define RADEON_PLL_TEST_CNTL_PLL_MASK_READ_B	MASK_N_BIT(9)

/*
 * RADEON_DAC_CNTL
 */
#define RADEON_DAC_CNTL_DAC_TVO_EN	MASK_N_BIT(10)

#define RADEON_PPLL_POST3_DIV_BY_2	0x10000
#define RADEON_PPLL_POST3_DIV_BY_3	0x40000

/*
 * Constant upsampler coefficients
 */
static
const
CARD32 upsamplerCoeffs[] =
{
  0x3f010000,
  0x7b008002,
  0x00003f01,
  0x341b7405,
  0x7f3a7617,
  0x00003d04,
  0x2d296c0a,
  0x0e316c2c,
  0x00003e7d,
  0x2d1f7503,
  0x2927643b,
  0x0000056f,
  0x29257205,
  0x25295050,
  0x00000572
};
#define N_UPSAMPLER_COEFFS	(sizeof(upsamplerCoeffs) / sizeof(upsamplerCoeffs[ 0 ]))

/*
 * Maximum length of horizontal/vertical code timing tables for state storage
 */
#define MAX_H_CODE_TIMING_LEN	32
#define MAX_V_CODE_TIMING_LEN	32

/*
 * Type of VIP bus
 */
#define VIP_TYPE	"ATI VIP BUS"

/**********************************************************************
 *
 * TimingTableEl
 *
 * Elements of H/V code timing tables
 *
 **********************************************************************/

typedef CARD16 TimingTableEl;	/* Bits 0 to 13 only are actually used */

/**********************************************************************
 *
 * ModeConstants
 *
 * Storage of constants related to a single video mode
 *
 **********************************************************************/

typedef struct
{
  CARD16 horResolution;	
  CARD16 verResolution;
  TVStd  standard;
  CARD16 horTotal;
  CARD16 verTotal;
  CARD16 horStart;
  CARD16 dFRestart;
  CARD16 dHRestart;
  CARD16 dVRestart;
  CARD32 vScalerCntl1;
  CARD32 yRiseCntl;
  CARD32 ySawtoothCntl;
  CARD16 hInc;
  CARD16 crtcPLL_N;
  CARD8  crtcPLL_M;
  Bool   crtcPLL_divBy2;
  CARD8  crtcPLL_byteClkDiv;
  Bool   use888RGB;		/* False: RGB data is 565 packed (2 bytes/pixel) */
				/* True : RGB data is 888 packed (3 bytes/pixel) */
  CARD8	 byteClkDelay;
  CARD32 tvoDataDelayA;
  CARD32 tvoDataDelayB;
  const TimingTableEl *horTimingTable;
  const TimingTableEl *verTimingTable;
} ModeConstants;

/**********************************************************************
 *
 * TheaterState
 *
 * Storage of RT state
 *
 **********************************************************************/

typedef struct
{
  CARD32 clkout_cntl;
  CARD32 clock_sel_cntl;
  CARD32 crc_cntl;
  CARD32 crt_pll_cntl;
  CARD32 dfrestart;
  CARD32 dhrestart;
  CARD32 dvrestart;
  CARD32 frame_lock_cntl;
  CARD32 gain_limit_settings;	/* TODO */
  CARD32 hdisp;
  CARD32 hstart;
  CARD32 htotal;
  CARD32 hw_debug;
  CARD32 linear_gain_settings;	/* TODO */
  CARD32 master_cntl;
  CARD32 modulator_cntl1;
  CARD32 modulator_cntl2;
  CARD32 pll_cntl0;
  CARD32 pll_test_cntl;
  CARD32 pre_dac_mux_cntl;
  CARD32 rgb_cntl;
  CARD32 sync_cntl;
  CARD32 sync_lock_cntl;
  CARD32 sync_size;
  CARD32 timing_cntl;
  CARD32 tvo_data_delay_a;
  CARD32 tvo_data_delay_b;
  CARD32 tvo_sync_pat_expect;
  CARD32 tvo_sync_threshold;
  CARD32 tv_dac_cntl;
  CARD32 tv_pll_cntl;
  CARD32 tv_pll_fine_cntl;
  CARD32 upsamp_and_gain_cntl;
  CARD32 upsamp_coeffs[ N_UPSAMPLER_COEFFS ];
  CARD32 uv_adr;
  CARD32 vdisp;
  CARD32 vftotal;
  CARD32 vscaler_cntl1;
  CARD32 vscaler_cntl2;
  CARD32 vtotal;
  CARD32 y_fall_cntl;
  CARD32 y_rise_cntl;
  CARD32 y_saw_tooth_cntl;

  TimingTableEl h_code_timing[ MAX_H_CODE_TIMING_LEN ];
  TimingTableEl v_code_timing[ MAX_V_CODE_TIMING_LEN ];
} TheaterState , *TheaterStatePtr;

/**********************************************************************
 *
 * TheaterOutRec , TheaterOutPtr
 *
 * Global state of module
 *
 **********************************************************************/

typedef struct TheaterOut
{
  GENERIC_BUS_Ptr VIP;
  int theatre_num;
  
  TVStd standard;
  Bool compatibleMode;
  Bool enabled;
  
  TheaterState savedState;
  TheaterState modeState;
} TheaterOutRec;

/**********************************************************************
 *
 * availableModes
 *
 * Table of all allowed modes for tv output
 *
 **********************************************************************/

static
const
TimingTableEl horCodeTimingBIOS[] =
  {
    0x0007,
    0x0058,
    0x027c,
    0x0a31,
    0x2a77,
    0x0a95,
    0x124f,
    0x1bfe,
    0x1b22,
    0x1ef9,
    0x387c,
    0x1bfe,
    0x1bfe,
    0x1b31,
    0x1eb5,
    0x0e43,
    0x201b,
    0
  };

static
const
TimingTableEl verCodeTimingBIOS[] =
  {
    0x2001,
    0x200c,
    0x1005,
    0x0c05,
    0x1005,
    0x1401,
    0x1821,
    0x2240,
    0x1005,
    0x0c05,
    0x1005,
    0x1401,
    0x1822,
    0x2230,
    0x0002,
    0
  };

static
const
ModeConstants availableModes[] =
  {
    { 
      800,		/* horResolution */
      600,		/* verResolution */
      TV_STD_PAL,	/* standard */
      1144,		/* horTotal */
      706,		/* verTotal */
      812,		/* horStart */
      0,		/* dFRestart */
      4,		/* dHRestart */
      609,		/* dVRestart */
      0x09009097,	/* vScalerCntl1 */
      0x000007da,	/* yRiseCntl */
      0x10002426,	/* ySawtoothCntl */
      1267,		/* hInc */
      1382,		/* crtcPLL_N */
      231,		/* crtcPLL_M */
      TRUE,		/* crtcPLL_divBy2 */
      0,		/* crtcPLL_byteClkDiv */
      FALSE,		/* use888RGB */
      1,		/* byteClkDelay */
      0x0a0b0907,	/* tvoDataDelayA */
      0x060a090a,	/* tvoDataDelayB */
      horCodeTimingBIOS,/* horTimingTable */
      verCodeTimingBIOS	/* verTimingTable */
    }
  };

#define N_AVAILABLE_MODES	(sizeof(availableModes) / sizeof(availableModes[ 0 ]))

/**********************************************************************
 *
 * theatre_read
 *
 * Read from a RT register
 *
 **********************************************************************/

static 
Bool 
theatre_read(
	     TheaterOutPtr t,
	     CARD32 reg,
	     CARD32 *data
	     )
{
  if (t->theatre_num < 0)
    return FALSE;
  
  return t->VIP->read(t->VIP, ((t->theatre_num & 0x3) << 14) | reg , 4 , (CARD8*)data);
}

/**********************************************************************
 *
 * theatre_write
 *
 * Write to a RT register
 *
 **********************************************************************/

static
Bool 
theatre_write(
	      TheaterOutPtr t,
	      CARD32 reg,
	      CARD32 data
	      )
{
  if (t->theatre_num < 0)
    return FALSE;

  return t->VIP->write(t->VIP , ((t->theatre_num & 0x03) << 14) | reg , 4 , (CARD8*)&data);
}

/**********************************************************************
 *
 * waitPLL_lock
 *
 * Wait for PLLs to lock
 *
 **********************************************************************/

static
void
waitPLL_lock(
	     TheaterOutPtr t,
	     ScrnInfoPtr pScrn,
	     unsigned nTests
	     )
{
  RADEONInfoPtr  info       = RADEONPTR(pScrn);
  unsigned char *RADEONMMIO = info->MMIO;
  CARD32 savePLLTest;
  unsigned i;
  unsigned j;

  OUTREG(RADEON_TEST_DEBUG_MUX , (INREG(RADEON_TEST_DEBUG_MUX) & 0xffff60ff) | 0x100);

  savePLLTest = INPLL(pScrn , RADEON_PLL_TEST_CNTL);

  OUTPLL(RADEON_PLL_TEST_CNTL , savePLLTest & ~RADEON_PLL_TEST_CNTL_PLL_MASK_READ_B);

  OUTREG8(RADEON_CLOCK_CNTL_INDEX , RADEON_PLL_TEST_CNTL);

  for (i = 0; i < nTests; i++)
    {
      OUTREG8(RADEON_CLOCK_CNTL_DATA + 3 , 0);
      
      for (j = 0; j < 160; j++)
	if (INREG8(RADEON_CLOCK_CNTL_DATA + 3) >= 27)
	  break;
    }

  OUTPLL(RADEON_PLL_TEST_CNTL , savePLLTest);

  OUTREG(RADEON_TEST_DEBUG_MUX , INREG(RADEON_TEST_DEBUG_MUX) & 0xffffe0ff);
}

/**********************************************************************
 *
 * restorePLL
 *
 * Set PLLs for CRTC pixel clock & TV color burst generation
 *
 **********************************************************************/

static
void
restorePLL(
	   TheaterOutPtr t,
	   ScrnInfoPtr pScrn,
	   TheaterStatePtr restore
	   )
{
  unsigned i;

  /*
   * Set TV PLL
   */
  RTTRACE(("restorePLL: TV_PLL_CNTL = %08x\n" , restore->tv_pll_cntl));
  theatre_write(t , VIP_TV_PLL_CNTL , restore->tv_pll_cntl);
  theatre_write(t , VIP_TV_PLL_FINE_CNTL , restore->tv_pll_fine_cntl);

  /*
   * Set CRT PLL (for byte and pixel clock generation)
   */
  RTTRACE(("restorePLL: CRT_PLL_CNTL = %08x\n" , restore->crt_pll_cntl));
  theatre_write(t , VIP_CRT_PLL_CNTL , restore->crt_pll_cntl);
  
  theatre_write(t , VIP_PLL_CNTL0 , restore->pll_cntl0);

  theatre_write(t , VIP_PLL_TEST_CNTL , restore->pll_test_cntl);

  /*
   * Set coefficients for upsampler
   */
  for (i = 0; i < N_UPSAMPLER_COEFFS; i++)
    theatre_write(t , VIP_UPSAMP_COEFF0_0 + i * 4 , restore->upsamp_coeffs[ i ]);

  /*
   * Waiting for PLLs to settle is skipped when restoring a state with stopped PLLs
   */
  if ((~restore->pll_cntl0 & (VIP_PLL_CNTL0_TVSLEEPB | VIP_PLL_CNTL0_CRTSLEEPB)) == 0)
    {
      waitPLL_lock(t , pScrn , 3000);

      theatre_write(t , VIP_CLOCK_SEL_CNTL , restore->clock_sel_cntl & ~VIP_CLOCK_SEL_CNTL_REGCLK);

      waitPLL_lock(t , pScrn , 3000);
    }

  RTTRACE(("restorePLL: CLOCK_SEL_CNTL = %08x\n" , restore->clock_sel_cntl));
  theatre_write(t , VIP_CLOCK_SEL_CNTL , restore->clock_sel_cntl);

  theatre_write(t , VIP_CLKOUT_CNTL , restore->clkout_cntl);
}

/**********************************************************************
 *
 * restoreTVO_SYNC
 *
 * Set TVO_SYNC_* registers
 *
 **********************************************************************/

static
void
restoreTVO_SYNC(
		TheaterOutPtr t,
		TheaterStatePtr restore
		)
{
  theatre_write(t , VIP_SYNC_LOCK_CNTL , restore->sync_lock_cntl);
  theatre_write(t , VIP_TVO_SYNC_THRESHOLD , restore->tvo_sync_threshold);
  theatre_write(t , VIP_TVO_SYNC_PAT_EXPECT , restore->tvo_sync_pat_expect);
}

/**********************************************************************
 *
 * restoreTVO_DataDelay
 *
 * Set TVO_DATA_DELAY_* registers
 *
 **********************************************************************/

static
void
restoreTVO_DataDelay(
		     TheaterOutPtr t,
		     TheaterStatePtr restore
		     )
{
  theatre_write(t , VIP_TVO_DATA_DELAY_A , restore->tvo_data_delay_a);
  theatre_write(t , VIP_TVO_DATA_DELAY_B , restore->tvo_data_delay_b);
}

/**********************************************************************
 *
 * restoreRT_HV
 *
 * Set RT horizontal/vertical settings
 *
 **********************************************************************/

static
void
restoreRT_HV(
	     TheaterOutPtr t,
	     TheaterStatePtr restore
	     )
{
  theatre_write(t , VIP_RGB_CNTL , restore->rgb_cntl);

  theatre_write(t , VIP_HTOTAL , restore->htotal);
  theatre_write(t , VIP_HDISP  , restore->hdisp);
  theatre_write(t , VIP_HSTART , restore->hstart);
  
  theatre_write(t , VIP_VTOTAL , restore->vtotal);
  theatre_write(t , VIP_VDISP  , restore->vdisp);
  
  theatre_write(t , VIP_VFTOTAL , restore->vftotal);

  theatre_write(t , VIP_SYNC_SIZE , restore->sync_size);

  theatre_write(t , VIP_VSCALER_CNTL1 , restore->vscaler_cntl1);
  theatre_write(t , VIP_VSCALER_CNTL2 , restore->vscaler_cntl2);
  
  theatre_write(t , VIP_Y_FALL_CNTL , restore->y_fall_cntl);
  theatre_write(t , VIP_Y_RISE_CNTL , restore->y_rise_cntl);
  theatre_write(t , VIP_Y_SAW_TOOTH_CNTL , restore->y_saw_tooth_cntl);
}

/**********************************************************************
 *
 * restoreRestarts
 *
 * Set RT D*RESTART registers
 *
 **********************************************************************/

static
void
restoreRestarts(	 
		TheaterOutPtr t,
		TheaterStatePtr restore
		)
{
  theatre_write(t , VIP_DFRESTART , restore->dfrestart);
  theatre_write(t , VIP_DHRESTART , restore->dhrestart);
  theatre_write(t , VIP_DVRESTART , restore->dvrestart);
}

/**********************************************************************
 *
 * writeFIFO
 *
 * Write to RT FIFO RAM
 *
 **********************************************************************/

static
void
writeFIFO(
	  TheaterOutPtr t,
	  CARD16 addr,
	  CARD32 value
	  )
{
  CARD32 tmp;

  theatre_write(t , VIP_HOST_WRITE_DATA , value);

  theatre_write(t , VIP_HOST_RD_WT_CNTL , addr | VIP_HOST_RD_WT_CNTL_WT);

  do
    {
      if (!theatre_read(t , VIP_HOST_RD_WT_CNTL , &tmp))
	break;
    } 
  while ((tmp & VIP_HOST_RD_WT_CNTL_WT_ACK) == 0);

  theatre_write(t , VIP_HOST_RD_WT_CNTL , 0);
}

/**********************************************************************
 *
 * readFIFO
 *
 * Read from RT FIFO RAM
 *
 **********************************************************************/

static
void
readFIFO(
	 TheaterOutPtr t,
	 CARD16 addr,
	 CARD32 *value
	 )
{
  CARD32 tmp;

  theatre_write(t , VIP_HOST_RD_WT_CNTL , addr | VIP_HOST_RD_WT_CNTL_RD);

  do
    {
      if (!theatre_read(t , VIP_HOST_RD_WT_CNTL , &tmp))
	break;
    } 
  while ((tmp & VIP_HOST_RD_WT_CNTL_RD_ACK) == 0);

  theatre_write(t , VIP_HOST_RD_WT_CNTL , 0);

  theatre_read(t , VIP_HOST_READ_DATA , value);
}

/**********************************************************************
 *
 * getTimingTablesAddr
 *
 * Get FIFO addresses of horizontal & vertical code timing tables from
 * settings of uv_adr register.
 *
 **********************************************************************/

static
void
getTimingTablesAddr(
		    CARD32 uv_adr,
		    CARD16 *hTable,
		    CARD16 *vTable
		    )
{
  switch ((uv_adr & VIP_UV_ADR_HCODE_TABLE_SEL) >> VIP_UV_ADR_HCODE_TABLE_SEL_SHIFT)
    {
    case 0:
      *hTable = MAX_FIFO_ADDR;
      break;

    case 1:
      *hTable = ((uv_adr & VIP_UV_ADR_TABLE1_BOT_ADR) >> VIP_UV_ADR_TABLE1_BOT_ADR_SHIFT) * 2;
      break;

    case 2:
      *hTable = ((uv_adr & VIP_UV_ADR_TABLE3_TOP_ADR) >> VIP_UV_ADR_TABLE3_TOP_ADR_SHIFT) * 2;
      break;

    default:
      /*
       * Of course, this should never happen
       */
      *hTable = 0;
      break;
    }

  switch ((uv_adr & VIP_UV_ADR_VCODE_TABLE_SEL) >> VIP_UV_ADR_VCODE_TABLE_SEL_SHIFT)
    {
    case 0:
      *vTable = ((uv_adr & VIP_UV_ADR_MAX_UV_ADR) >> VIP_UV_ADR_MAX_UV_ADR_SHIFT) * 2 + 1;
      break;

    case 1:
      *vTable = ((uv_adr & VIP_UV_ADR_TABLE1_BOT_ADR) >> VIP_UV_ADR_TABLE1_BOT_ADR_SHIFT) * 2 + 1;
      break;

    case 2:
      *vTable = ((uv_adr & VIP_UV_ADR_TABLE3_TOP_ADR) >> VIP_UV_ADR_TABLE3_TOP_ADR_SHIFT) * 2 + 1;
      break;

    default:
      /*
       * Of course, this should never happen
       */
      *vTable = 0;
      break;
    }
}

/**********************************************************************
 *
 * restoreTimingTables
 *
 * Load horizontal/vertical timing code tables
 *
 **********************************************************************/

static
void
restoreTimingTables(
		    TheaterOutPtr t,
		    TheaterStatePtr restore
		    )
{
  CARD16 hTable;
  CARD16 vTable;
  CARD32 tmp;
  unsigned i;

  theatre_write(t , VIP_UV_ADR , restore->uv_adr);

  getTimingTablesAddr(restore->uv_adr , &hTable , &vTable);

  for (i = 0; i < MAX_H_CODE_TIMING_LEN; i += 2 , hTable--)
    {
      tmp = ((CARD32)restore->h_code_timing[ i ] << 14) | ((CARD32)restore->h_code_timing[ i + 1 ]);
      writeFIFO(t , hTable , tmp);
      if (restore->h_code_timing[ i ] == 0 || restore->h_code_timing[ i + 1 ] == 0)
	break;
    }

  for (i = 0; i < MAX_V_CODE_TIMING_LEN; i += 2 , vTable++)
    {
      tmp = ((CARD32)restore->v_code_timing[ i + 1 ] << 14) | ((CARD32)restore->v_code_timing[ i ]);
      writeFIFO(t , vTable , tmp);
      if (restore->v_code_timing[ i ] == 0 || restore->v_code_timing[ i + 1 ] == 0)
	break;
    }
}

/**********************************************************************
 *
 * restoreOutputStd
 *
 * Set tv standard & output muxes
 *
 **********************************************************************/

static
void
restoreOutputStd(
		 TheaterOutPtr t,
		 TheaterStatePtr restore
		 )
{
  theatre_write(t , VIP_SYNC_CNTL , restore->sync_cntl);
  
  theatre_write(t , VIP_TIMING_CNTL , restore->timing_cntl);

  theatre_write(t , VIP_MODULATOR_CNTL1 , restore->modulator_cntl1);
  theatre_write(t , VIP_MODULATOR_CNTL2 , restore->modulator_cntl2);
 
  theatre_write(t , VIP_PRE_DAC_MUX_CNTL , restore->pre_dac_mux_cntl);

  theatre_write(t , VIP_CRC_CNTL , restore->crc_cntl);

  theatre_write(t , VIP_FRAME_LOCK_CNTL , restore->frame_lock_cntl);

  theatre_write(t , VIP_HW_DEBUG , restore->hw_debug);
}

/**********************************************************************
 *
 * enableTV_DAC
 *
 * Enable/disable tv output DAC
 *
 **********************************************************************/

static
void
enableTV_DAC(
	     TheaterOutPtr t,
	     Bool enable
	     )
{
  CARD32 tmp;

  theatre_read(t , VIP_TV_DAC_CNTL , &tmp);

  if (enable)
    {
      tmp |= VIP_TV_DAC_CNTL_NBLANK;
      tmp &= ~VIP_TV_DAC_CNTL_DASLEEP;
      tmp &= ~VIP_TV_DAC_CNTL_BGSLEEP;
    }
  else
    {
      tmp &= ~VIP_TV_DAC_CNTL_NBLANK;
      tmp |= VIP_TV_DAC_CNTL_DASLEEP;
      tmp |= VIP_TV_DAC_CNTL_BGSLEEP;
    }

  theatre_write(t , VIP_TV_DAC_CNTL , tmp);
}

/**********************************************************************
 *
 * RT_Restore
 *
 * Restore state of RT
 *
 **********************************************************************/

static
void
RT_Restore(
	   TheaterOutPtr t,
	   ScrnInfoPtr pScrn,
	   TheaterStatePtr restore
	   )
{
  RTTRACE(("Entering RT_Restore\n"));

  theatre_write(t , 
		VIP_MASTER_CNTL , 
		restore->master_cntl | 
		VIP_MASTER_CNTL_TV_ASYNC_RST |
		VIP_MASTER_CNTL_CRT_ASYNC_RST |
		VIP_MASTER_CNTL_TV_FIFO_ASYNC_RST);

  /*
   * Temporarily turn the TV DAC off
   */
  theatre_write(t ,
		VIP_TV_DAC_CNTL ,
		(restore->tv_dac_cntl & ~VIP_TV_DAC_CNTL_NBLANK) |
		VIP_TV_DAC_CNTL_DASLEEP | 
		VIP_TV_DAC_CNTL_BGSLEEP);

  RTTRACE(("RT_Restore: checkpoint 1\n"));
  restoreTVO_SYNC(t , restore);

  RTTRACE(("RT_Restore: checkpoint 2\n"));
  restorePLL(t , pScrn , restore);

  RTTRACE(("RT_Restore: checkpoint 3\n"));
  restoreTVO_DataDelay(t , restore);

  RTTRACE(("RT_Restore: checkpoint 4\n"));
  restoreRT_HV(t , restore);
  
  theatre_write(t , 
		VIP_MASTER_CNTL , 
		restore->master_cntl |
		VIP_MASTER_CNTL_TV_ASYNC_RST |
		VIP_MASTER_CNTL_CRT_ASYNC_RST);

  RTTRACE(("RT_Restore: checkpoint 5\n"));
  restoreRestarts(t , restore);

  RTTRACE(("RT_Restore: checkpoint 6\n"));

  /*
   * Timing tables are restored when tv output is active
   */
  if ((restore->tv_dac_cntl & (VIP_TV_DAC_CNTL_DASLEEP | VIP_TV_DAC_CNTL_BGSLEEP)) == 0)
    restoreTimingTables(t , restore);
  
  theatre_write(t , 
		VIP_MASTER_CNTL , 
		restore->master_cntl |
		VIP_MASTER_CNTL_TV_ASYNC_RST);

  RTTRACE(("RT_Restore: checkpoint 7\n"));
  restoreOutputStd(t , restore);

  theatre_write(t , 
		VIP_MASTER_CNTL , 
		restore->master_cntl);

  theatre_write(t , VIP_TV_DAC_CNTL , restore->tv_dac_cntl);

  RTTRACE(("Leaving RT_Restore\n"));
}

/**********************************************************************
 *
 * RT_Save
 *
 * Save state of RT
 *
 **********************************************************************/

static
void
RT_Save(
	TheaterOutPtr t,
	TheaterStatePtr save
	)
{
  unsigned i;
  CARD16 hTable;
  CARD16 vTable;
  CARD32 tmp;

  RTTRACE(("Entering RT_Save\n"));

  theatre_read(t , VIP_CLKOUT_CNTL         , &save->clkout_cntl);
  theatre_read(t , VIP_CLOCK_SEL_CNTL      , &save->clock_sel_cntl);
  theatre_read(t , VIP_CRC_CNTL            , &save->crc_cntl);
  theatre_read(t , VIP_CRT_PLL_CNTL        , &save->crt_pll_cntl);
  theatre_read(t , VIP_DFRESTART           , &save->dfrestart);
  theatre_read(t , VIP_DHRESTART           , &save->dhrestart);
  theatre_read(t , VIP_DVRESTART           , &save->dvrestart);
  theatre_read(t , VIP_FRAME_LOCK_CNTL     , &save->frame_lock_cntl);
  theatre_read(t , VIP_GAIN_LIMIT_SETTINGS , &save->gain_limit_settings);
  theatre_read(t , VIP_HDISP               , &save->hdisp);
  theatre_read(t , VIP_HSTART              , &save->hstart);
  theatre_read(t , VIP_HTOTAL              , &save->htotal);
  theatre_read(t , VIP_HW_DEBUG            , &save->hw_debug);
  theatre_read(t , VIP_LINEAR_GAIN_SETTINGS, &save->linear_gain_settings);
  theatre_read(t , VIP_MASTER_CNTL	   , &save->master_cntl);
  theatre_read(t , VIP_MODULATOR_CNTL1     , &save->modulator_cntl1);
  theatre_read(t , VIP_MODULATOR_CNTL2     , &save->modulator_cntl2);
  theatre_read(t , VIP_PLL_CNTL0           , &save->pll_cntl0);
  theatre_read(t , VIP_PLL_TEST_CNTL       , &save->pll_test_cntl);
  theatre_read(t , VIP_PRE_DAC_MUX_CNTL    , &save->pre_dac_mux_cntl);
  theatre_read(t , VIP_RGB_CNTL            , &save->rgb_cntl);
  theatre_read(t , VIP_SYNC_CNTL           , &save->sync_cntl);
  theatre_read(t , VIP_SYNC_LOCK_CNTL      , &save->sync_lock_cntl);
  theatre_read(t , VIP_SYNC_SIZE           , &save->sync_size);
  theatre_read(t , VIP_TIMING_CNTL         , &save->timing_cntl);
  theatre_read(t , VIP_TVO_DATA_DELAY_A    , &save->tvo_data_delay_a);
  theatre_read(t , VIP_TVO_DATA_DELAY_B    , &save->tvo_data_delay_b);
  theatre_read(t , VIP_TVO_SYNC_PAT_EXPECT , &save->tvo_sync_pat_expect);
  theatre_read(t , VIP_TVO_SYNC_THRESHOLD  , &save->tvo_sync_threshold);
  theatre_read(t , VIP_TV_DAC_CNTL         , &save->tv_dac_cntl);
  theatre_read(t , VIP_TV_PLL_CNTL         , &save->tv_pll_cntl);
  theatre_read(t , VIP_TV_PLL_FINE_CNTL    , &save->tv_pll_fine_cntl);
  theatre_read(t , VIP_UPSAMP_AND_GAIN_CNTL, &save->upsamp_and_gain_cntl);
  theatre_read(t , VIP_UV_ADR              , &save->uv_adr);
  theatre_read(t , VIP_VDISP               , &save->vdisp);
  theatre_read(t , VIP_VFTOTAL             , &save->vftotal);
  theatre_read(t , VIP_VSCALER_CNTL1       , &save->vscaler_cntl1);
  theatre_read(t , VIP_VSCALER_CNTL2       , &save->vscaler_cntl2);
  theatre_read(t , VIP_VTOTAL              , &save->vtotal);
  theatre_read(t , VIP_Y_FALL_CNTL         , &save->y_fall_cntl);
  theatre_read(t , VIP_Y_RISE_CNTL         , &save->y_rise_cntl);
  theatre_read(t , VIP_Y_SAW_TOOTH_CNTL    , &save->y_saw_tooth_cntl);

  for (i = 0; i < N_UPSAMPLER_COEFFS; i++)
    theatre_read(t , VIP_UPSAMP_COEFF0_0 + i * 4 , &save->upsamp_coeffs[ i ]);

  /*
   * Read H/V code timing tables (current tables only are saved)
   * This step is skipped when tv output is disabled in current RT state
   * (see RT_Restore)
   */
  if ((save->tv_dac_cntl & (VIP_TV_DAC_CNTL_DASLEEP | VIP_TV_DAC_CNTL_BGSLEEP)) == 0)
    {
      getTimingTablesAddr(save->uv_adr , &hTable , &vTable);

      /*
       * Reset FIFO arbiter in order to be able to access FIFO RAM
       */
      RTTRACE(("RT_Save: MASTER_CNTL = %08x\n" , save->master_cntl));
      theatre_write(t , VIP_MASTER_CNTL , 
		    save->master_cntl | 
		    VIP_MASTER_CNTL_CRT_ASYNC_RST |
		    VIP_MASTER_CNTL_TV_FIFO_ASYNC_RST |
		    VIP_MASTER_CNTL_TV_ASYNC_RST);
      theatre_write(t , 
		    VIP_MASTER_CNTL , 
		    save->master_cntl |
		    VIP_MASTER_CNTL_TV_ASYNC_RST |
		    VIP_MASTER_CNTL_CRT_ASYNC_RST);

      RTTRACE(("RT_Save: reading timing tables\n"));

      for (i = 0; i < MAX_H_CODE_TIMING_LEN; i += 2)
	{
	  readFIFO(t , hTable-- , &tmp);
	  save->h_code_timing[ i     ] = (CARD16)((tmp >> 14) & 0x3fff);
	  save->h_code_timing[ i + 1 ] = (CARD16)(tmp & 0x3fff);

	  if (save->h_code_timing[ i ] == 0 || save->h_code_timing[ i + 1 ] == 0)
	    break;
	}

      for (i = 0; i < MAX_V_CODE_TIMING_LEN; i += 2)
	{
	  readFIFO(t , vTable++ , &tmp);
	  save->v_code_timing[ i     ] = (CARD16)(tmp & 0x3fff);
	  save->v_code_timing[ i + 1 ] = (CARD16)((tmp >> 14) & 0x3fff);

	  if (save->v_code_timing[ i ] == 0 || save->v_code_timing[ i + 1 ] == 0)
	    break;
	}
    }

  RTTRACE(("RT_Save returning\n"));
}

/**********************************************************************
 *
 * RT_Init
 *
 * Define RT state for a given standard/resolution combination
 *
 **********************************************************************/

static
void
RT_Init(
	const ModeConstants *constPtr,
	Bool enable,
	TheaterStatePtr save
	)
{
  unsigned i;
  CARD32 tmp;

  save->clkout_cntl = VIP_CLKOUT_CNTL_INI;

  save->clock_sel_cntl = VIP_CLOCK_SEL_CNTL_INI |  
    (constPtr->crtcPLL_byteClkDiv << VIP_CLOCK_SEL_CNTL_BYTCLK_SHIFT) |
    (constPtr->byteClkDelay << VIP_CLOCK_SEL_CNTL_BYTCLKD_SHIFT);

  save->crc_cntl = 0;

  tmp = ((CARD32)constPtr->crtcPLL_M << VIP_CRT_PLL_CNTL_M_SHIFT) |
    (((CARD32)constPtr->crtcPLL_N & VIP_CRT_PLL_CNTL_NLO) << VIP_CRT_PLL_CNTL_NLO_SHIFT) |
    (((CARD32)constPtr->crtcPLL_N & VIP_CRT_PLL_CNTL_NHI) << VIP_CRT_PLL_CNTL_NHI_SHIFT);
  if (constPtr->crtcPLL_divBy2)
    tmp |= VIP_CRT_PLL_CNTL_CLKBY2;
  save->crt_pll_cntl = tmp;

  save->dfrestart = constPtr->dFRestart;
  save->dhrestart = constPtr->dHRestart;
  save->dvrestart = constPtr->dVRestart;

  save->frame_lock_cntl = VIP_FRAME_LOCK_CNTL_INI;

  save->gain_limit_settings = VIP_GAIN_LIMIT_SETTINGS_INI;

  save->hdisp = constPtr->horResolution - 1;
  save->hstart = constPtr->horStart;
  save->htotal = constPtr->horTotal - 1;

  save->hw_debug = VIP_HW_DEBUG_INI;

  save->linear_gain_settings = VIP_LINEAR_GAIN_SETTINGS_INI;

  /*
   * TEST   TEST   TEST   TEST   TEST   TEST   TEST   TEST   TEST   
   */
  save->master_cntl = VIP_MASTER_CNTL_ON_INI;
/*   save->master_cntl = enable ? VIP_MASTER_CNTL_ON_INI : VIP_MASTER_CNTL_OFF_INI; */

  save->modulator_cntl1 = VIP_MODULATOR_CNTL1_INI;
  save->modulator_cntl2 = VIP_MODULATOR_CNTL2_INI;

  save->pll_cntl0 = VIP_PLL_CNTL0_INI;
  save->pll_test_cntl = VIP_PLL_TEST_CNTL_INI;

  save->pre_dac_mux_cntl = VIP_PRE_DAC_MUX_CNTL_INI;

  /*
   * Instruct RT to accept either 565 or 888 packed pixels
   */
  save->rgb_cntl = constPtr->use888RGB ? VIP_RGB_CNTL_RGB_IS_888_PACK : 0;

  save->sync_cntl = VIP_SYNC_CNTL_INI;

  save->sync_lock_cntl = VIP_SYNC_LOCK_CNTL_INI;

  save->sync_size = constPtr->horResolution + 8;
  
  tmp = (constPtr->vScalerCntl1 >> VIP_VSCALER_CNTL1_UV_INC_SHIFT) & VIP_VSCALER_CNTL1_UV_INC;
  tmp = ((16384 * 256 * 10) / tmp + 5) / 10;
  tmp = (tmp << VIP_TIMING_CNTL_UV_OUT_POST_SCALE_SHIFT) | 
    VIP_TIMING_CNTL_INI |
    (constPtr->hInc << VIP_TIMING_CNTL_H_INC_SHIFT);
  save->timing_cntl = tmp;

  save->tvo_data_delay_a = constPtr->tvoDataDelayA;
  save->tvo_data_delay_b = constPtr->tvoDataDelayB;

  save->tvo_sync_pat_expect = VIP_TVO_SYNC_PAT_EXPECT_INI;

  if (constPtr->use888RGB)
    save->tvo_sync_threshold = constPtr->horResolution + constPtr->horResolution / 2;
  else
    save->tvo_sync_threshold = constPtr->horResolution;

  save->tv_dac_cntl = enable ? VIP_TV_DAC_CNTL_ON_INI : VIP_TV_DAC_CNTL_OFF_INI;

  tmp = ((CARD32)TV_PLL_M_PAL_27000 << VIP_TV_PLL_CNTL_M_SHIFT) |
    (((CARD32)TV_PLL_N_PAL_27000 & VIP_TV_PLL_CNTL_NLO) << VIP_TV_PLL_CNTL_NLO_SHIFT) |
    (((CARD32)TV_PLL_N_PAL_27000 & VIP_TV_PLL_CNTL_NHI) << VIP_TV_PLL_CNTL_NHI_SHIFT) |
    ((CARD32)TV_PLL_PD_PAL_27000 << VIP_TV_PLL_CNTL_P_SHIFT);
  save->tv_pll_cntl = tmp;
  save->tv_pll_fine_cntl = TV_PLL_FINE_PAL_27000;

  save->upsamp_and_gain_cntl = VIP_UPSAMP_AND_GAIN_CNTL_INI;

  memcpy(&save->upsamp_coeffs[ 0 ] , upsamplerCoeffs , sizeof(save->upsamp_coeffs));

  save->uv_adr = VIP_UV_ADR_INI;

  save->vdisp = constPtr->verResolution - 1;
  save->vftotal = VIP_VFTOTAL_INI;

  save->vscaler_cntl1 = constPtr->vScalerCntl1;
  save->vscaler_cntl2 = VIP_VSCALER_CNTL2_INI;

  save->vtotal = constPtr->verTotal - 1;

  save->y_fall_cntl = VIP_Y_FALL_CNTL_INI;
  save->y_rise_cntl = constPtr->yRiseCntl;
  save->y_saw_tooth_cntl = constPtr->ySawtoothCntl;

  for (i = 0; i < MAX_H_CODE_TIMING_LEN; i++)
    {
      if ((save->h_code_timing[ i ] = constPtr->horTimingTable[ i ]) == 0)
	break;
    }

  for (i = 0; i < MAX_V_CODE_TIMING_LEN; i++)
    {
      if ((save->v_code_timing[ i ] = constPtr->verTimingTable[ i ]) == 0)
	break;
    }
}

/**********************************************************************
 *
 * RT_InitCRTC
 *
 **********************************************************************/
static
void
RT_InitCRTC(
	    const ModeConstants *constPtr,
	    RADEONSavePtr save
	    )
{
  save->crtc_h_total_disp = (((constPtr->horResolution / 8) - 1) << RADEON_CRTC_H_DISP_SHIFT) |
    (((constPtr->horTotal / 8) - 1) << RADEON_CRTC_H_TOTAL_SHIFT);
  
  save->crtc_v_total_disp = ((constPtr->verResolution - 1) << RADEON_CRTC_V_DISP_SHIFT) |
    ((constPtr->verTotal - 1) << RADEON_CRTC_V_TOTAL_SHIFT);

  save->htotal_cntl = (constPtr->horTotal & 7) | 0x10000000;

  /*
   * Instruct Radeon to output either 565 or 888 packed pixels
   */
  save->disp_output_cntl &= 0xf900ffff;
  save->disp_output_cntl |= (constPtr->use888RGB ? 0x080000 : 0x0a0000);

  save->crtc_ext_cntl |= RADEON_CRTC_VGA_XOVERSCAN;
  save->dac_cntl |= RADEON_DAC_CNTL_DAC_TVO_EN;

  /*
   * Set Radeon to be clocked from RT
   */
  save->vclk_ecp_cntl &= ~RADEON_VCLK_SRC_SEL_MASK;
  save->vclk_ecp_cntl |= RADEON_VCLK_SRC_SEL_BYTECLK;

  save->vclk_ecp_cntl &= ~RADEON_VCLK_ECP_CNTL_BYTECLK_POSTDIV;
  save->vclk_ecp_cntl |= RADEON_VCLK_ECP_CNTL_BYTECLK_NODIV;

  save->ppll_div_3 &= ~RADEON_PPLL_POST3_DIV_MASK;
  save->ppll_div_3 |= (constPtr->use888RGB ? RADEON_PPLL_POST3_DIV_BY_3 : RADEON_PPLL_POST3_DIV_BY_2);

}

/**********************************************************************
 *
 * detectTheaterOut
 *
 * Detect presence of a RT chip
 *
 **********************************************************************/

TheaterOutPtr 
detectTheaterOut(
		 GENERIC_BUS_Ptr b
		 )
{
  TheaterOutPtr t;  
  int i;
  CARD32 val;
  char s[20];
   
  b->ioctl(b , GB_IOCTL_GET_TYPE , 20 , s);
  if (strcmp(VIP_TYPE , s))
    {
      xf86DrvMsg(b->scrnIndex , X_ERROR , "detectTheaterOut must be called with bus of type \"%s\", not \"%s\"\n",
		 VIP_TYPE , s);
      return NULL;
    }
   
  t = xcalloc(1 , sizeof(TheaterOutRec));
  t->VIP = b;
  t->theatre_num = -1;
  
  /*
   * Is this really needed?
   */ 
  b->read(b , VIP_VIP_VENDOR_DEVICE_ID , 4 , (CARD8*)&val);

  for (i = 0; i < 4; i++)
    {
      if(b->read(b , ((i & 0x03) << 14) | VIP_VIP_VENDOR_DEVICE_ID , 4 , (CARD8*)&val))
        {
	  if (val)
	    xf86DrvMsg(b->scrnIndex , X_INFO , "Device %d on VIP bus ids as 0x%08x\n" , i , val);
	  if (t->theatre_num >= 0)
	    continue;	/* already found one instance */
	  if (val == RT100_ATI_ID)
	    t->theatre_num = i;
	} 
      else 
	{
	  xf86DrvMsg(b->scrnIndex , X_INFO , "No response from device %d on VIP bus\n" , i);	
	}
    }

  if (t->theatre_num >= 0)
    xf86DrvMsg(b->scrnIndex , X_INFO , 
	       "Detected Rage Theatre as device %d on VIP bus\n" , t->theatre_num);

  if (t->theatre_num < 0)
    {
      xfree(t);
      return NULL;
    }

  theatre_read(t , VIP_VIP_REVISION_ID , &val);
  xf86DrvMsg(b->scrnIndex , X_INFO , "Detected Rage Theatre revision %8.8X\n" , val);

  return t;
}

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

void
initTheaterOut(
	       TheaterOutPtr t,
	       const char *s
	       )
{
  RTTRACE(("Entering initTheaterOut, s = %s\n" , s));
  /*
   * TEST   TEST   TEST   TEST   TEST   TEST   TEST   TEST   TEST   
   */
  if (xf86NameCmp(s , "NTSC") == 0)
    t->standard = TV_STD_NTSC;
  else if (xf86NameCmp(s , "PAL") == 0)
    t->standard = TV_STD_PAL;
  else if (xf86NameCmp(s , "PAL-M") == 0)
    t->standard = TV_STD_PAL_M;
  else if (xf86NameCmp(s , "PAL-60") == 0)
    t->standard = TV_STD_PAL_60;
  else if (xf86NameCmp(s , "NTSC-J") == 0)
    t->standard = TV_STD_NTSC_J;
  else if (xf86NameCmp(s , "PAL-CN") == 0)
    t->standard = TV_STD_PAL_CN;
  else if (xf86NameCmp(s , "PAL-N") == 0)
    t->standard = TV_STD_PAL_N;
  else
    {
      xf86DrvMsg(0 , X_WARNING , "Unrecognized TV standard in TVOutput option (%s), defaulting to PAL\n" , s);
      t->standard = TV_STD_PAL;
    }
  t->compatibleMode = FALSE;
  t->enabled = TRUE;
}

/**********************************************************************
 *
 * theaterOutSave
 *
 * Save current state of RT as initial state (the one that is restored
 * when switching back to text mode)
 *
 **********************************************************************/

void
theaterOutSave(
	       TheaterOutPtr t
	       )
{
  RTTRACE(("Entering theaterOutSave\n"));
  RT_Save(t , &t->savedState);
}

/**********************************************************************
 *
 * theaterOutRestore
 *
 * Restore state of RT from initial state (the one saved through 
 * theaterOutSave)
 *
 **********************************************************************/

void
theaterOutRestore(
		  TheaterOutPtr t,
		  ScrnInfoPtr pScrn
		  )
{
  RTTRACE(("Entering theaterOutRestore\n"));
  RT_Restore(t , pScrn , &t->savedState);
}
		  
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

Bool
theaterOutInit(
	       TheaterOutPtr t,
	       DisplayModePtr mode,
	       RADEONSavePtr save
	       )
{
  const ModeConstants *p;

  RTTRACE(("Entering theaterOutInit: en=%d h=%u v=%u\n" , t->enabled , mode->HDisplay , mode->VDisplay));

  t->compatibleMode = FALSE;

  /*
   * Search mode among available ones
   */
  for (p = availableModes; p < (availableModes + N_AVAILABLE_MODES); p++)
    {
      if (p->horResolution == mode->HDisplay &&
	  p->verResolution == mode->VDisplay &&
	  p->standard == t->standard)
	{
	  /*
	   * Match found
	   */
	  t->compatibleMode = TRUE;

	  if (t->enabled)
	    {
	      RT_Init(p , TRUE , &t->modeState);
	      RT_InitCRTC(p , save);

	      return TRUE;
	    }
	  else
	    break;
	}
    }

  /*
   * Match not found or tv output disabled
   * First parameter is dummy when setting for no tv output, so any mode
   * will do (here first mode in table is passed).
   */
  RT_Init(availableModes , FALSE , &t->modeState);
  return FALSE;
}
	       
/**********************************************************************
 *
 * theaterOutRestoreMode
 *
 * Set state of RT to the one defined by last call to theaterOutInit
 *
 **********************************************************************/

void
theaterOutRestoreMode(
		      TheaterOutPtr t,
		      ScrnInfoPtr pScrn
		      )
{
  RTTRACE(("Entering theaterOutRestoreMode\n"));
  RT_Restore(t , pScrn , &t->modeState);
}

/**********************************************************************
 *
 * theaterOutSetStandard
 *
 * Set TV output standard
 *
 * Return value is TRUE when video mode should be set again
 *
 **********************************************************************/
Bool
theaterOutSetStandard(
		      TheaterOutPtr t,
		      TVStd std
		      )
{
  Bool changed = t->standard != std;

  RTTRACE(("Entering theaterOutSetStandard\n"));
  t->standard = std;

  /*
   * TEST   TEST   TEST   TEST   TEST   TEST   TEST   TEST   TEST   
   */
  return changed && t->enabled && t->compatibleMode;
}
		      
/**********************************************************************
 *
 * theaterOutSetOnOff
 *
 * Set module on/off
 *
 * Return value is TRUE when video mode should be set again
 *
 **********************************************************************/

Bool
theaterOutSetOnOff(
		   TheaterOutPtr t,
		   Bool on
		   )
{
  Bool changed = on != t->enabled;

  RTTRACE(("Entering theaterOutSetOnOff (on = %d)\n" , on));
  t->enabled = on;

  /*
   * TEST   TEST   TEST   TEST   TEST   TEST   TEST   TEST   TEST   
   */
  return changed && t->compatibleMode;
}

/**********************************************************************
 *
 * theaterOutGetStandard
 *
 * Get current TV output standard
 *
 **********************************************************************/
TVStd
theaterOutGetStandard(
		      TheaterOutPtr t
		      )
{
  return t->standard;
}

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
Bool
theaterOutGetOnOff(
		   TheaterOutPtr t
		   )
{
  return t->enabled;
}

/**********************************************************************
 *
 * theaterOutGetCompatMode
 *
 * Return whether the current mode is compatible with tv output or not
 *
 **********************************************************************/
Bool
theaterOutGetCompatMode(
			TheaterOutPtr t
			)
{
  return t->compatibleMode;
}

