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
 */

#ifndef	_FB_PARSERTYPES_H
#define	_FB_PARSERTYPES_H

#include "filebench.h"

#define	DOFILE_FALSE 0
#define	DOFILE_TRUE 1

#define	FSE_SYSTEM 1

typedef struct list {
	struct list	*list_next;
	avd_t		 list_string;
	avd_t		 list_integer;
} list_t;

typedef struct attr {
	int		 attr_name;
	struct attr	*attr_next;
	avd_t		 attr_avd;
	void		*attr_obj;
} attr_t;

typedef struct cmd {
	void (*cmd)(struct cmd *);
	char		*cmd_name;
	char		*cmd_tgt1;
	char		*cmd_tgt2;
	char		*cmd_tgt3;
	char		*thread_name;
	int		cmd_subtype;
	uint64_t	cmd_qty;
	int64_t		cmd_qty1;
	struct cmd	*cmd_list;
	struct cmd	*cmd_next;
	attr_t		*cmd_attr_list;
	list_t		*cmd_param_list;
	list_t		*cmd_param_list2;
} cmd_t;

typedef union {
	int64_t i;
	unsigned char b;
	char *s;
} fs_u;

typedef struct pidlist {
	struct pidlist	*pl_next;
	int		pl_fd;
	pid_t		pl_pid;
} pidlist_t;

typedef void (*cmdfunc)(cmd_t *);
int yy_switchfileparent(FILE *file);
int yy_switchfilescript(FILE *file);

#endif	/* _FB_PARSERTYPES_H */
