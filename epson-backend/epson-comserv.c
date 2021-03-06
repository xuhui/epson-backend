/*
* Copyright (C) Seiko Epson Corporation 2014.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <WinSock2.h>
#include <Windows.h>

#include "epson.h"
#include "epson-thread.h"
#include "epson-wrapper.h"

#ifndef _CRT_NO_TIME_T
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#endif

#pragma comment(lib, "Ws2_32.lib")

#define SOCK_ACCSESS_WAIT_MAX 20
/* command packet key word */
static const char COMMAND_PACKET_KEY[] = "pcp";

/* length of packet key word */
#define PACKET_KEY_LEN 3

#define COM_BUF_SIZE  256	/* size of command buffer */
#define PAC_HEADER_SIZE 5	/* size of packet header */
#define REP_BUF_SIZE  256	/* maximum size of reply buffer */

static char file_to_prt[COM_BUF_SIZE];
static int _comserv_first_flag;

enum _ERROR_PACKET_NUMBERS {
	ERRPKT_NOREPLY = 0,	/* no error (nothing reply) */
	ERRPKT_UNKNOWN_PACKET,	/* receive indistinct packet */
	ERRPKT_PRINTER_NO_CONNECT, /* cannot communicate with a printer */
	ERRPKT_MEMORY		/* memory shortage */
};


/* open a socket */
static int servsock_open(int port)
{
	WSADATA wsa;     
	SOCKET fd;
	int opt = 1;
	struct sockaddr_in addr;

    if (WSAStartup(MAKEWORD(2,2),&wsa) != 0) {
        printf("Failed. Error Code : %d",WSAGetLastError());
        return 1;
    }

	/* 0 means IPPROTO_IP  dummy for IP */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) 
		return -1;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(int));
	listen(fd, 5);

	return fd;
}

/* read it from a socket */
static int sock_read(int fd, char* buf, int read_size)
{
	int i;
	int size;

	for (i = 0; i < SOCK_ACCSESS_WAIT_MAX; i++)
	{
		size = recv(fd, buf, read_size, 0);
		if (size == read_size) {
			return 0;
		}
		else if (size > 0) {
			read_size -= size;
			buf += size;
			usleep (1000);
		}
		else
			break;
	}
	return 1;
}

/* write it to a socket */
static int sock_write(int fd, char* buf, int write_size)
{
	int i;
	int size;

	if (fd < 0 || buf == NULL || write_size <= 0)
		return 1;

	for (i = 0; i < SOCK_ACCSESS_WAIT_MAX; i++)
	{
		size = send(fd, buf, write_size, 0);
		if (size == write_size) {
			/* todo: windows has no fsync, maybe use fflush instead */
			//fsync(fd);
			//fflush(fd);
			return 0;
		}
		else if (size > 0) {
			write_size -= size;
			buf += size;
			usleep (1000);
		}
		else
			break;
	}
	return 1;
}

/* transmit packet */
static void reply_send(int fd, char* buf, int size)
{
	char packet[REP_BUF_SIZE + PAC_HEADER_SIZE];
	char header[] = { 'p', 'c', 'p', 0x00, 0x00 };

	header[3] = (char)(size >> 8);
	header[4] = (char)(size & 0xff);

	memcpy(packet, header, PAC_HEADER_SIZE);
	memcpy(packet + PAC_HEADER_SIZE, buf, size);
	sock_write(fd, packet, size + PAC_HEADER_SIZE);
	return;
}


int SendCommand(char* pCmd, int nCmdSize)
{
	return 0;
}

/* take out data division minute from packet */
static int command_recv(int fd, char* cbuf, int* csize)
{

	if (sock_read(fd, cbuf, PAC_HEADER_SIZE))
		return 1;

	if (strncmp(cbuf, COMMAND_PACKET_KEY, PACKET_KEY_LEN))
		return 1;

	*csize = ((int)cbuf[3] << 8) + (int)cbuf[4];

	if (sock_read(fd, cbuf, *csize))
		return 1;
	return 0;
}

/* transmit error packet */
static int error_recept(int fd, int err_code)
{
	char err_packet[] = { 'p', 'c', 'p', 0x00, 0x00, 0x00 };
	char buf[100];

	err_packet[5] = (char)err_code;

	sock_read(fd, buf, sizeof(buf));
	sock_write(fd, err_packet, sizeof(err_packet));
	return 0;
}

/* received a print file command */
static int prt_file_recept(P_CBTD_INFO p_info, char *cbuf, int csize, int fd) {
	const char prt_file_header[] = { 'p', 'r', 't', 'f', 'i', 'l', 'e' };
	int header_size = sizeof(prt_file_header);

	for(int i = 0; i < csize - header_size; i++) {
		file_to_prt[i] = cbuf[i + header_size];
	}

	p_info->file_path = (char *)malloc(sizeof(char));
	p_info->file_path = file_to_prt;
	set_sysflags(p_info, ST_JOB_RECV);
	
	return 0;
}

/* received a printer status get command */
static int prt_status_recept(P_CBTD_INFO p_info, int fd) {
	if (p_info->prt_status_len == 0)
		return 1;

	char rbuf[2] = { 0 };

	rbuf[0] = (char)p_info->status->printerStatus;
	rbuf[1] = (char)p_info->prt_state;

	reply_send(fd, rbuf, sizeof(rbuf));
	printf("send a prt_status_buf from sock, size: %d\n", sizeof(rbuf));
	return 0;
}

/* received a job status get command */
static int job_status_recept(P_CBTD_INFO p_info, int fd) {
	char rbuf[1] = { 0 };
	if (p_info->prt_job_status_len == 0) {
		reply_send(fd, rbuf, sizeof(rbuf));
		printf("no prt job status, reply a job status command: %d\n", sizeof(rbuf));
		//return 1;
	}
	else {
		reply_send(fd, (char *)p_info->prt_job_status, p_info->prt_job_status_len);
		printf("reply a job status command: %d\n", p_info->prt_job_status_len);
	}

	return 0;
}

/* received a material status get command */
static int material_status_recept(P_CBTD_INFO p_info, int fd) {
	char rbuf[256];
	int rsize = 0;

	if (p_info->status->ink_num <= 0)
		return 1;

	int nums = p_info->status->ink_num;
	rbuf[0] = (char)nums;

	for (int i = 0; i < p_info->status->ink_num; i++) {
		rbuf[i + 1] = (char)p_info->status->colors[i];
		rbuf[i + 1 + nums] = (char)p_info->status->inklevel[i];
	}

	reply_send(fd, rbuf, sizeof(rbuf));
	printf("reply a meterial status, size: %d\n", sizeof(rbuf));
	return 0;
}

/* received a job cancel command */
static int job_cancel_recept(P_CBTD_INFO p_info, int fd) {
	BOOL ret = cancel_prt_job(p_info->printer_handle);
	if (ret) {
		return 0;
	}
	return 1;
}

/* received a nozzle check command */
static int nozzlecheck_recept(P_CBTD_INFO p_info, int fd)
{
	char buffer[256];
	int bufsize, err;
	epsMakeMainteCmd(EPS_MNT_NOZZLE, buffer, &bufsize);

	open_port_channel(p_info, SID_DATA);
	enter_critical(p_info->ecbt_accsess_critical);
	err = write_to_prt(p_info, SID_DATA, buffer, &bufsize);
	leave_critical(p_info->ecbt_accsess_critical);
	close_port_channel(p_info, SID_DATA);

	err == 0 ? ERRPKT_NOREPLY : err;

	error_recept(fd, err);

	return 0;
}

/* received a head cleaning command */
static int headcleaning_recept(P_CBTD_INFO p_info, int fd)
{
	char buffer[256];
	int bufsize, err;
	epsMakeMainteCmd(EPS_MNT_CLEANING, buffer, &bufsize);

	open_port_channel(p_info, SID_DATA);
	enter_critical(p_info->ecbt_accsess_critical);
	err = write_to_prt(p_info, SID_DATA, buffer, &bufsize);
	leave_critical(p_info->ecbt_accsess_critical);
	close_port_channel(p_info, SID_DATA);

	err == 0 ? ERRPKT_NOREPLY : err;

	error_recept(fd, err);

	return 0;
}

/* received a get device_id command */
static int getdeviceid_recept(P_CBTD_INFO p_info, int fd)
{
	char device_id[256];
	/* todo: can't get device id here */
	//get_device_id(device_id);
	strcpy(device_id, "MFG:EPSON;CMD:ESCPL2,BDC,D4,D4PX;MDL:Epson Stylus Photo R330;CLS:PRINTER;DES:EPSON Epson Stylus Photo R330;CID:EpsonStd2;");

	reply_send(fd, device_id, strlen(device_id));

	return 0;
}

/* usual command reception */
static int default_recept(P_CBTD_INFO p_info, int fd, char* cbuf, int csize)
{
	char rbuf[REP_BUF_SIZE];
	int rsize;

	rsize = REP_BUF_SIZE;
	if (write_prt_command(p_info, cbuf, csize, rbuf, &rsize)) {
		return 1;
	}


	reply_send(fd, rbuf, rsize);
	return 0;
}

/* handle the data which received */
static int comserv_work(P_CBTD_INFO p_info, int fd)
{
	const char prt_file_command[] = { 'p', 'r', 't', 'f', 'i', 'l', 'e' };
	const char prt_status_command[] = { 'p', 'r', 't', 's', 't' };
	const char job_status_command[] = { 'j', 'o', 'b', 's', 't' };
	const char material_command[] = { 'm', 'a', 't', 'e', 'r', 'i', 'a', 'l' };
	const char job_cancel_command[] = { 'j', 'o', 'b', 'c', 'a', 'n', 'c', 'e', 'l' };
	const char nozzlecheck_command[] = { 'n', 'o', 'z', 'z', 'l', 'e', 'c', 'h', 'e', 'c', 'k' };
	const char headcleaning_command[] = { 'h', 'e', 'a', 'd', 'c', 'l', 'e', 'a', 'n', 'i', 'n', 'g' };
	const char getdeviceid_command[] = { 'g', 'e', 't', 'd', 'e', 'v', 'i', 'c', 'e', 'i', 'd' };
	char cbuf[COM_BUF_SIZE];
	unsigned int csize, err = 0;

	assert(p_info);

	/* wait till it is connected to a printer */
	if (!is_sysflags(p_info, ST_PRT_CONNECT))
		return error_recept(fd, ERRPKT_PRINTER_NO_CONNECT);

	if (command_recv(fd, cbuf, &csize)) {
		/* received indistinct packet */
		return error_recept(fd, ERRPKT_UNKNOWN_PACKET);
	}

	/* acquire print a file */
	if (memcmp(cbuf, prt_file_command, sizeof(prt_file_command)) == 0) {
		printf("recv a print file command\n");
		/* clear global file path data */
		memset(file_to_prt, 0, sizeof(file_to_prt));
		if (prt_file_recept(p_info, cbuf, csize, fd))
			err = 1;
	}

	/* acquire printer status */
	if (memcmp(cbuf, prt_status_command, sizeof(prt_status_command)) == 0) {
		if (prt_status_recept(p_info, fd))
			err = 1;
	}

	/* acquire winspool print job status */
	else if (memcmp(cbuf, job_status_command, sizeof(job_status_command)) == 0) {
		if (job_status_recept(p_info, fd))
			err = 1;
	}

	/* acquire printer material status */
	else if (memcmp(cbuf, material_command, sizeof(material_command)) == 0) {
		if (material_status_recept(p_info, fd))
			err = 1;
	}

	/* acquire to stop print job */
	else if (memcmp(cbuf, job_cancel_command, sizeof(job_cancel_command)) == 0) {
		if (job_cancel_recept(p_info, fd))
			err = 1;
	}

	/* acquire nozzle check */
	else if (memcmp(cbuf, nozzlecheck_command, sizeof(nozzlecheck_command)) == 0) {
		if (nozzlecheck_recept(p_info, fd))
			err = 1;
	}

	/* acquire head cleaning */
	else if (memcmp(cbuf, headcleaning_command, sizeof(headcleaning_command)) == 0) {
		if (headcleaning_recept(p_info, fd))
			err = 1;
	}

	/* acquire device info */
	else if (memcmp(cbuf, getdeviceid_command, sizeof(getdeviceid_command)) == 0) {
		if (getdeviceid_recept(p_info, fd))
			err = 1;
	}

	/* others */
	else {
		if (default_recept(p_info, fd, cbuf, csize))
			err = 1;
	}

	if (err) {
		error_recept(fd, ERRPKT_PRINTER_NO_CONNECT);
		return 1;
	}
	return 0;
}



/* end of thread */
static void comserv_cleanup(void* data)
{
	P_CARGS p_cargs = (P_CARGS)data;
	int fd;

	for (fd = 0; fd < *(p_cargs->p_max) + 1; fd++) {
		if (FD_ISSET(fd, p_cargs->p_fds)) {
			shutdown(fd, 2);
			FD_CLR(fd, p_cargs->p_fds);
		}
	}

	if (!is_sysflags(p_cargs->p_info, ST_SYS_DOWN))
		set_sysflags(p_cargs->p_info, ST_SYS_DOWN);

	p_cargs->p_info->comserv_thread_status = THREAD_DOWN;
	printf("Command server thread ...down\n");
	return;
}


/* communication thread */
void comserv_thread(P_CBTD_INFO p_info)
{
	int server_fd, client_fd;
	struct sockaddr_in client_addr;
	fd_set sock_fds;
	struct timeval tv;
	int maxval, nclient;
	CARGS cargs;

	_comserv_first_flag = 1;

	FD_ZERO (&sock_fds);
	maxval = nclient = 0;

	cargs.p_info = p_info;
	cargs.p_fds = &sock_fds;
	cargs.p_max = &maxval;
	pthread_cleanup_push (comserv_cleanup, (void *)&cargs);

	server_fd = servsock_open (p_info->comsock_port);
	if (server_fd < 0) {
		char sock_num[10];

		sprintf (sock_num, "%d", p_info->comsock_port);
		p_info->comserv_thread_status = THREAD_DOWN;
		perror (sock_num);
		return;
	}

	FD_SET (server_fd, &sock_fds);
	maxval = server_fd;

	for (;;) {
		int fd;
		int addr_len = sizeof(client_addr);
		fd_set watch_fds = sock_fds;

		tv.tv_sec = 2;
		tv.tv_usec = 0;

		if (select (maxval + 1, &watch_fds, NULL, NULL, &tv) < 0) {
			perror ("cs select ()");
			continue;
		}

		/* Is daemon in the middle of process for end ? */
		if (is_sysflags (p_info, ST_SYS_DOWN))
			break;

		for (fd = 0; fd < maxval + 1; fd++) {
			if ( FD_ISSET(fd, &watch_fds)) {
				if (fd == server_fd) {
					/* connecting */
					client_fd = accept (server_fd,
							    (struct sockaddr *)&client_addr,
							    (int *)&addr_len);
					if (client_fd < 0)
						continue;

					printf("connect client fd = %d\n", client_fd);
					FD_SET (client_fd, &sock_fds);

					if (maxval < client_fd)
						maxval = client_fd;

					nclient ++;
					set_sysflags (p_info, ST_CLIENT_CONNECT);
					if (!is_sysflags (p_info, ST_PRT_CONNECT))
						Sleep (1000);
				} 
				else {
					int nread;
					/* windows has no ioctl in user space, ioctlsocket instead */
					//ioctl (fd, FIONREAD, &nread);
					ioctlsocket(fd, FIONREAD, &nread);
					if (nread == 0) {
						/* disconnecting */
						printf("deconnect client fd = %d\n", fd);
						shutdown (fd, 2);
						FD_CLR (fd, &sock_fds);						

						nclient--;
						if (nclient == 0)
							reset_sysflags (p_info, ST_CLIENT_CONNECT);
					}
					else {
						/* receive a message */						
						if (comserv_work (p_info, fd))	
							reset_sysflags (p_info, ST_PRT_CONNECT);
					}
				}
			}
		}
	}

	WSACleanup();
	pthread_cleanup_pop (1);
	return;
}

