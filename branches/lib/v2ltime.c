/****************************************************************************
 * Copyright (C) 2006 v2lin Team <http://v2lin.sf.net>
 * 
 * This file is part of the v2lin Library.
 * VxWorks is a registered trademark of Wind River Systems, Inc.
 * 
 * Contributed by Constantine Shulyupin, conan.sh@gmail.com, 2006.
 * 
 * The v2lin library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * The v2lin Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 ****************************************************************************/

#include "tickLib.h"
#include "v2ldebug.h"
#include <time.h>
#include <stdio.h>

/*****************************************************************************/
/* v2pthread emulated tick                                                   */
/*****************************************************************************/
static int v2pt_tick = 0;

/*****************************************************************************/
/* Emulated clock rate in v2pthread environment - ticks per second           */
/*****************************************************************************/
static int v2pt_sysClkRate = 0;

STATUS v2pt_sysClkRateSet(int ticksPerSecond)
{
    /*************************************************************************/
    /* Make sure we're going to set something reasonable                     */
    /*************************************************************************/
	if (ticksPerSecond <= 0) {
		printf("\n\rv2pt_sysClkRateSet: bad rate value %d\n\r", ticksPerSecond);
		return ERROR;
	}
	
	/*************************************************************************/
    /* Get the value of the emulated tick in milliseconds - this is the      */
    /* main unit for v2lin API                                               */
    /*************************************************************************/
    v2pt_tick = 1000 / ticksPerSecond;
    
    /*************************************************************************/
    /* Update the value of the emulated clock rate.                          */
    /*                                                                       */
    /* This is required when the requested virtual rate cannot be translated */
    /* into the whole number of milliseconds in tick.                        */
    /*                                                                       */
    /* For example:                                                          */
    /*                                                                       */
    /*      requested ticksPerSeconds = 60 (Hz)                              */
    /*      v2pt_tick = 1000 / ticksPerSecond = 1000 / 60 = 16               */
    /*      v2pt_sysClkRate = 1000 / v2pt_tick = 1000 / 16 = 62              */
    /*                                                                       */
    /*      That is, the real rate of our virtual clock is slightly higher   */
    /*      than 62 Hz: 62 * 16 = 992                                        */
    /*                                                                       */
    /*************************************************************************/
    v2pt_sysClkRate = 1000 / v2pt_tick;
    return OK;
}

int sysClkRateGet(void)
{
    return v2pt_sysClkRate;
}

int v2pt_Tick(void)
{
    return v2pt_tick;
}

int sysRealClkRateGet(void)
{
    struct timespec res;
    clock_getres(CLOCK_REALTIME, &res);
    TRACEV("%i", res.tv_nsec);
    return 1E9 / res.tv_nsec;
}

void tickSet(unsigned long ticks)
{
    v2pt_tickSet(ticks);
}

unsigned long tickGet(void)
{
    return v2pt_tickGet();
}
