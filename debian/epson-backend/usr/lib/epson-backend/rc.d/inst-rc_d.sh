#!/bin/sh
#
## Copyright (C) Seiko Epson Corporation 2015.
##  This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

sbindir=/usr/sbin
pkgrcddir=/usr/lib/epson-backend/rc.d

PATH=$PATH:/usr/local/sbin:/usr/sbin:/sbin:$sbindir

SYSCONFDIR=/etc
SCRIPTDIR=$pkgrcddir
INSTALL="install -m 755"
LN="ln -fs"
RM="rm -f"
SNO=11
KNO=89


#
# Check init scripts directory
#

if [ -d $SYSCONFDIR/init.d ]; then
    funcdir=$SYSCONFDIR/init.d
    if [ -d $funcdir/rc1.d ]; then
	# for suse
	rcdir=$funcdir
    else
	rcdir=$SYSCONFDIR
    fi
elif [ -d $SYSCONFDIR/rc.d ]; then
    rcdir=$SYSCONFDIR/rc.d
    if [ -d $rcdir/init.d ]; then
	funcdir=$rcdir/init.d
    else
	funcdir=$rcdir
    fi
else
    echo Error : Unknow linux distribution
    exit 1
fi


case "$1" in
    #
    # Install script
    #
    install)
	$INSTALL $SCRIPTDIR/ecbd $funcdir/ecbd
	if [ ! -x /usr/lib/lsb/install_initd ] ; then
            # LSB standard
	    /usr/lib/lsb/install_initd $funcdir/ecbd > /dev/null 2>&1
	elif type update-rc.d > /dev/null 2>&1 ; then
	    # Debian compatible
	    update-rc.d ecbd defaults $SNO $KNO > /dev/null 2>&1
	elif type chkconfig > /dev/null 2>&1 ; then
            # RedHat compatible
	    chkconfig --add ecbd > /dev/null 2>&1
	else
	    # legacy system
	    for loop in 2 3 4 5 S M ; do
		if [ -d $rcdir/rc$loop.d ]; then
		    $LN $funcdir/ecbd $rcdir/rc$loop.d/S${SNO}ecbd

		elif [ -d $rcdir/rc.$loop ]; then
		    $LN $funcdir/ecbd $rcdir/rc.$loop/S${SNO}ecbd

		fi
	    done
	    for loop in 0 1 6 K ; do
		if [ -d $rcdir/rc$loop.d ]; then
		    $LN $funcdir/ecbd $rcdir/rc$loop.d/K${KNO}ecbd

		elif [ -d $rcdir/rc.$loop ]; then
		    $LN $funcdir/ecbd $rcdir/rc.$loop/K${KNO}ecbd

		fi
	    done
	fi
	;;

    deinstall)
	if [ ! -x /usr/lib/lsb/remove_initd ] ; then
            # LSB standard
	    /usr/lib/lsb/remove_initd $funcdir/ecbd > /dev/null 2>&1
	elif type update-rc.d > /dev/null 2>&1 ; then
	    # Debian compatible
	    update-rc.d -f ecbd remove > /dev/null 2>&1
	elif type chkconfig > /dev/null 2>&1 ; then
            # RedHat compatible
	    chkconfig --del ecbd > /dev/null 2>&1
	else
	    # legacy system
	    for loop in 2 3 4 5 S M ; do
		if [ -s $rcdir/rc$loop.d/S${SNO}ecbd ]; then
		    $RM $rcdir/rc$loop.d/S${SNO}ecbd

		elif [ -s $rcdir/rc.$loop/S${SNO}ecbd ]; then
		    $RM $rcdir/rc.$loop/S${SNO}ecbd

		fi
	    done
	    for loop in 0 1 6 K ; do
		if [ -s $rcdir/rc$loop.d/K${KNO}ecbd ]; then
		    $RM $rcdir/rc$loop.d/K${KNO}ecbd

		elif [ -s $rcdir/rc.$loop/K${KNO}ecbd ]; then
		    $RM $rcdir/rc.$loop/K${KNO}ecbd

		fi
	    done
	fi

	if [ -s $funcdir/ecbd ]; then
	    $RM $funcdir/ecbd
	fi

	;;

    *)
	echo "Usage: install-rc_d.sh { install | deinstall }" >&2
	exit 1
	;;
esac

exit 0;
