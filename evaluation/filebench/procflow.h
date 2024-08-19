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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _FB_PROCFLOW_H
#define	_FB_PROCFLOW_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "filebench.h"

typedef struct procflow {
	char		pf_name[128];
	int		pf_instance;
	avd_t		pf_instances;
	int		pf_running;
	flag_t          pf_threads_defined_flag;
	struct procflow	*pf_next;
	pid_t		pf_pid;
	pthread_t	pf_tid;
	struct threadflow *pf_threads;
	int		pf_attrs;
	avd_t		pf_nice;
} procflow_t;

procflow_t *procflow_define(char *name, avd_t instances);
void proc_create(void);
void procflow_shutdown(void);
void	proc_shutdown(void);
int	procflow_exec(char *name, int instance);

#endif	/* _FB_PROCFLOW_H */
