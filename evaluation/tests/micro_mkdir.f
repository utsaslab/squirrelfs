#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Creates a directory with $ndirs potential leaf directories, than mkdir's them
#
set $dir=/mnt/pmem
set $ndirs=1000000
set $meandirwidth=100
set $nthreads=24
#set $runtime=10

set mode quit firstdone

define fileset name=mkdirfileset,path=$dir,size=0,leafdirs=$ndirs,dirwidth=$meandirwidth

define process name=mkdir,instances=1
{
  thread name=mkdirthread,memsize=10m,instances=$nthreads
  {
    flowop makedir name=mkdir1,filesetname=mkdirfileset
  }
}

echo  "MakeDirs Version 1.0 personality successfully loaded"

run 