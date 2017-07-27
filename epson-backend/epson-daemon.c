/*
* Copyright (C) Seiko Epson Corporation 2014.
*
* This file is part of the `ecbd' program.
*
*  This program is free software; you can redistribute it and/or modify
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
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
* As a special exception, AVASYS CORPORATION gives permission
* to link the code of this program with the `cbt' library and
* distribute linked combinations including the two.  You must obey
* the GNU General Public License in all respects for all of the
* code used other then `cbt'.
*/

/*
* Restrictions
*      Cannot be connected to more than one printer at a time.
*/

/*
* Communication packet specification between client programs.
*
* [ input packet ]
*      header (3byte) : 'pcp' length data
*      length (2byte) : Size of data
*      data           : Data (command)
*
* [ output packet ]
*      header (3byte) : 'pcp' length data
*      length (2byte) : Size of data. When it was 0, show that error
*                       occurred with the server side. In that event
*                       data size is always 1 (error code).
*
*      error code
*           0x00 No error (Nothing reply)
*           0x01 Receive indistinct packet
*           0x02 Cannot communicate with a printer
*           0x03 Memory shortage
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
//#include <sys/file.h>
#include <fcntl.h>
//#include <unistd.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "epson-daemon.h"
#include "libepson.h"
#include "epson-thread.h"
#include "epson-typedefs.h"


#define ECBD_VERSION "* ecbd is a part of " PACKAGE_STRING

#define ECBD_USAGE "Usage: $ ecbd [-p pidfile]\n\
    -p pidfile\n\
        Use specified file as pid file"

int pid_fd = -1;

/* connect a printer */
/*
 * todo: open /dev/usb/lp0 in linux
 * not the same with linux , maybe libusb is a
 * good way to do this
 * 所有操作应该转移到 epson-setup.c里面去做
 */
static int prt_connect(P_CBTD_INFO p_info)
{
	int *fd = &p_info->devfd;
	int i;
	int error = 1;

	if (is_sysflags(p_info, ST_SYS_DOWN))
		return -1;

	for (i = 0; i < 5; i++)
	{
		error = parameter_update(p_info);

		if (error == 0) {
			*fd = open(p_info->devprt_path, O_RDWR);
		}
		else {
			*fd = (-1);
		}

		if (*fd < 0)
		{
			usleep(50000);
		}
		else
		{
			_DEBUG_MESSAGE("connect printer\n");
			return *fd;
		}
	}

	//_DEBUG_FUNC(perror(devprt_path));
	_DEBUG_MESSAGE("waiting.....\n");
	sleep(3);
	return *fd;
}

/* initialize CBT */
static int init_epson_cbt(P_CBTD_INFO p_info)
{
	start_ecbt_engine();
	return open_port_driver(p_info);
}

/* end of CBT */
int end_epson_cbt(P_CBTD_INFO p_info)
{
	int err = 0;

	err = close_port_driver(p_info);
	end_ecbt_engine();

	return err;
}

/* initialize process */
static void init_cbtd(P_CBTD_INFO p_info)
{
	memset(p_info, 0, sizeof(CBTD_INFO));

	/* default setup */
	/* todo: windows has no port and fifo path */
	strcpy(p_info->devprt_path, DEVICE_PATH);
	strcpy(p_info->infifo_path, FIFO_PATH);
	
	p_info->comsock_port = DAEMON_PORT;

	p_info->devfd = -1;

	p_info->sysflags = 0;
	p_info->sysflags_critical = init_critical();

	p_info->ecbt_accsess_critical = init_critical();
	assert(p_info->sysflags_critical != NULL
		&& p_info->ecbt_accsess_critical != NULL);

	p_info->datatrans_thread_status = THREAD_RUN;
	p_info->comserv_thread_status = THREAD_RUN;

	p_info->datatrans_thread
		= init_thread(CBTD_THREAD_STACK_SIZE,
		(void *)datatrans_thread,
			(void *)p_info);

	p_info->comserv_thread
		= init_thread(CBTD_THREAD_STACK_SIZE,
		(void *)comserv_thread,
			(void *)p_info);

	assert(p_info->datatrans_thread != NULL
		&& p_info->comserv_thread != NULL);

	return;
}

/* end of process */
void end_cbtd(P_CBTD_INFO p_info)
{
	if (p_info->datatrans_thread)
		delete_thread(p_info->datatrans_thread);

	if (p_info->comserv_thread)
		delete_thread(p_info->comserv_thread);


	if (p_info->sysflags_critical)
		delete_critical(p_info->sysflags_critical);

	if (p_info->ecbt_accsess_critical)
		delete_critical(p_info->ecbt_accsess_critical);

	return;
}

/* main thread */
static void cbtd_control(void)
{
	CBTD_INFO info;
	int set_flags, reset_flags;

	init_cbtd(&info);
	/* todo: no need to set singal, no pid found since we don't get pid first*/
	//sig_set();

	while (!is_sysflags(&info, ST_SYS_DOWN))
	{
		/* turn into the main loop */
		for (;;)
		{
			set_flags = 0;
			reset_flags = ST_SYS_DOWN | ST_CLIENT_CONNECT | ST_JOB_PRINTING | ST_JOB_CANCEL;
			if (wait_sysflags(&info, set_flags, reset_flags, 5, WAIT_SYS_AND) == 0)
				break;

			if (is_sysflags(&info, ST_DAEMON_WAKEUP))
				reset_sysflags(&info, ST_DAEMON_WAKEUP);
		}

		set_sysflags(&info, ST_DAEMON_WAKEUP);
		/* connect a printer */
		if (prt_connect(&info) < 0)
			continue;

		/* initialize communication */
		if (init_epson_cbt(&info) == 0)
		{
			/* thread starting */
			set_sysflags(&info, ST_PRT_CONNECT);

			/* check status */
			for (;;)
			{
				set_flags = ST_CLIENT_CONNECT | ST_JOB_PRINTING | ST_JOB_CANCEL;
				reset_flags = 0;

				if (wait_sysflags(&info, set_flags, reset_flags, 2, WAIT_SYS_OR) == 0)
					break;

				set_flags = ST_PRT_CONNECT;
				reset_flags = ST_SYS_DOWN;

				if (wait_sysflags(&info, set_flags, reset_flags, 2, WAIT_SYS_AND) == 0)
					break;
			}
		}
		end_epson_cbt(&info);

		if (info.devfd >= 0)
		{
			close(info.devfd);
			_DEBUG_MESSAGE("deconnect printer\n");
			info.devfd = -1;

			if (!is_sysflags(&info, ST_SYS_DOWN))
				sleep(2);
		}
	}

	/* wait for end of other thread */
	while (info.datatrans_thread_status != THREAD_DOWN
		|| info.comserv_thread_status != THREAD_DOWN)
		sleep(1);

	end_cbtd(&info);
	return;
}

int main()
{
	cbtd_control();
	return 0;
}