/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Portions Copyright 2008 Denis Cheng
 */

/*
 * The event generator in this module is the producer half of a metering system
 * which blocks flowops using consumer routines in the flowop_library.c module.
 * Four routines in that module can limit rates by event rate
 * (flowoplib_eventlimit), by I/O operations rate (flowoplib_iopslimit()), by
 * operations rate (flowoplib_opslimit), or by I/O bandwidth limit
 * (flowoplib_bwlimit). By setting appropriate event generation rates, required
 * calls per second, I/O ops per second, file system ops per second, or I/O
 * bandwidth per second limits can be set. Note, the generated events are
 * shared with all consumer flowops, of which their will be one for each
 * process / thread instance which has a consumer flowop defined in it.
 */

#include <sys/time.h>

#include "filebench.h"
#include "vars.h"
#include "eventgen.h"
#include "flowop.h"
#include "ipc.h"

#define	FB_SEC2NSEC	1000000000UL

/*
 * The producer side of the event system.  Once eventgen_hz has been set by
 * eventgen_setrate(), the routine sends eventgen_hz events per second until
 * the program terminates. Events are posted by incrementing
 * filebench_shm->shm_eventgen_q by the number of generated events then
 * signalling the condition variable filebench_shm->shm_eventgen_cv to indicate
 * to event consumers that more events are available.
 *
 * Eventgen_thread attempts to sleep for 10 event periods, then, once awakened,
 * determines how many periods actually passed since sleeping, and issues a set
 * of events equal to the number of periods that it slept, thus keeping the
 * average rate at the requested rate.
 */
static void
eventgen_thread(void)
{
	hrtime_t last;

	last = gethrtime();
	filebench_shm->shm_eventgen_enabled = FALSE;

	while (1) {
		struct timespec sleeptime;
		hrtime_t delta;
		int count, rate;

		if (filebench_shm->shm_eventgen_hz == NULL) {
			(void) sleep(1);
			continue;
		} else {
			rate = avd_get_int(filebench_shm->shm_eventgen_hz);
			if (rate > 0)
				filebench_shm->shm_eventgen_enabled = TRUE;
			else
				continue;
		}

		/* Sleep for [10 x period] */
		sleeptime.tv_sec = 0;
		sleeptime.tv_nsec = FB_SEC2NSEC / rate;

		sleeptime.tv_nsec *= 10;
		if (sleeptime.tv_nsec < 1000UL)
			sleeptime.tv_nsec = 1000UL;

		sleeptime.tv_sec = sleeptime.tv_nsec / FB_SEC2NSEC;
		if (sleeptime.tv_sec > 0)
			sleeptime.tv_nsec -= (sleeptime.tv_sec * FB_SEC2NSEC);

		(void)nanosleep(&sleeptime, NULL);

		delta = gethrtime() - last;
		last = gethrtime();

		count = (rate * delta) / FB_SEC2NSEC;

		filebench_log(LOG_DEBUG_SCRIPT, "delta %lluns count %d",
			(u_longlong_t)delta, count);

		/* Send 'count' events */
		(void)ipc_mutex_lock(&filebench_shm->shm_eventgen_lock);

		/*
		 * Keep the producer with a max of 5 second depth.
		 * Events accumulate in shm_eventgen_cv even before
		 * worker threads are created. But eventgen_reset()
		 * drops shm_eventgen_q to zero after the worker threads
		 * are created.
		 */
		if (filebench_shm->shm_eventgen_q < (5 * rate))
			filebench_shm->shm_eventgen_q += count;

		(void)pthread_cond_signal(&filebench_shm->shm_eventgen_cv);

		(void)ipc_mutex_unlock(&filebench_shm->shm_eventgen_lock);
	}
}

/*
 * Creates a thread to run the event generator eventgen_thread routine.
 */
void
eventgen_init(void)
{
	pthread_t tid;

	if (pthread_create(&tid, NULL,
	    (void *(*)(void *))eventgen_thread, 0) != 0) {
		filebench_log(LOG_ERROR, "create timer thread failed: %s",
		    strerror(errno));
		exit(1);
	}
}

/*
 * Sets the event generator rate to that supplied by
 * var_t *rate.
 */
void
eventgen_setrate(avd_t rate)
{
	filebench_shm->shm_eventgen_hz = rate;
	if (rate == NULL) {
		filebench_log(LOG_ERROR,
		    "eventgen_setrate() called without a rate");
		return;
	}
}

/*
 * Clears the event queue so we have a clean start
 */
void
eventgen_reset(void)
{
	filebench_shm->shm_eventgen_q = 0;
}
