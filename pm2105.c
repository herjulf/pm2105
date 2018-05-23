/*

 tty_talker sends command on serial port and prints response on stdout

 Robert Olsson  <robert@herjulf.se>  most code taken from:


 file is part of the minicom communications package,
 *		Copyright 1991-1995 Miquel van Smoorenburg.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.

 and

 * Based on.... serial port tester
 * Doug Hughes - Auburn University College of Engineering
 * 9600 baud by default, settable via -19200 or -9600 flags
 * first non-baud argument is tty (e.g. /dev/term/a)
 * second argument is file name (e.g. /etc/hosts)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <unistd.h>
#include <termio.h>
#include <sys/fcntl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "devtag-allinone.h"

#define VERSION "1.8 110628"
#define END_OF_FILE 26
#define CTRLD  4
#define P_LOCK "/var/lock"

char lockfile[128]; /* UUCP lock file of terminal */
char dial_tty[128];
char username[16];
int pid;
int retry = 6;


void usage(void)
{
  printf("\ntty_talk version %s\n", VERSION);
  
  printf("\ntty_talk sends query to terminal device and waits for it's response\n");
  printf("A response is teminated with EOF (0x4)\n");
  printf("tty_talk [-BAUDRATE] device command\n");
  printf(" Valid baudrates 4800, 9600 (Default), 19200, 38400 bps\n");
  printf("tty_talk can handle devtag\n");

  exit(-1);
}

/*
 * Find out name to use for lockfile when locking tty.
 */

char *mbasename(char *s, char *res, int reslen)
{
  char *p;
  
  if (strncmp(s, "/dev/", 5) == 0) {
    /* In /dev */
    strncpy(res, s + 5, reslen - 1);
    res[reslen-1] = 0;
    for (p = res; *p; p++)
      if (*p == '/')
        *p = '_';
  } else {
    /* Outside of /dev. Do something sensible. */
    if ((p = strrchr(s, '/')) == NULL)
      p = s;
    else
      p++;
    strncpy(res, p, reslen - 1);
    res[reslen-1] = 0;
  }
  return res;
}

int lockfile_create(void)
{
  int fd, n;
  char buf[81];

  n = umask(022);
  /* Create lockfile compatible with UUCP-1.2  and minicom */
  if ((fd = open(lockfile, O_WRONLY | O_CREAT | O_EXCL, 0666)) < 0) {
    return 0;
  } else {
    snprintf(buf, sizeof(buf),  "%05d tty_talk %.20s\n", (int) getpid(), 
	     username);

    write(fd, buf, strlen(buf));
    close(fd);
  }
  umask(n);
  return 1;
}

void lockfile_remove(void)
{
  if (lockfile[0])
    unlink(lockfile);
}

int have_lock_dir(void)
{
 struct stat stt;
  char buf[128];

  if ( stat(P_LOCK, &stt) == 0) {

    snprintf(lockfile, sizeof(lockfile),
                       "%s/LCK..%s",
                       P_LOCK, mbasename(dial_tty, buf, sizeof(buf)));
  }
  else {
    printf("Lock directory %s does not exist\n", P_LOCK);
	exit(-1);
  }
  return 1;
}

int get_lock()
{
  char buf[128];
  int fd, n = 0;

  have_lock_dir();

  if((fd = open(lockfile, O_RDONLY)) >= 0) {
    n = read(fd, buf, 127);
    close(fd);
    if (n > 0) {
      pid = -1;
      if (n == 4)
        /* Kermit-style lockfile. */
        pid = *(int *)buf;
      else {
        /* Ascii lockfile. */
        buf[n] = 0;
        sscanf(buf, "%d", &pid);
      }
      if (pid > 0 && kill((pid_t)pid, 0) < 0 &&
          errno == ESRCH) {
        printf("Lockfile is stale. Overriding it..\n");
        sleep(1);
        unlink(lockfile);
      } else
        n = 0;
    }
    if (n == 0) {
      if(retry == 1) /* Last retry */
	printf("Device %s is locked.\n", dial_tty);
      return 0;
    }
  }
  lockfile_create();
  return 1;
}

struct {
  unsigned char head;
  unsigned char len;
  unsigned char df[60];
} mes;

unsigned char csum(unsigned char *in, unsigned char len) 
{
  int i;
  unsigned char cs = 0;
  for(i=0; i<len; i++) {
    cs += in[i];
  }
  cs = 256 - cs;
  return cs;
}

int main(int ac, char *av[]) 
{
	struct termios tp, old;
	int fd;
	char io[BUFSIZ];
	int res;
	long baud;
	int i, len, idx;
	uint32_t x1, x2, x3, pm[3];

	if(ac == 1) 
	  usage();

	if (strcmp(av[1], "-4800") == 0) {
		baud = B4800;
		av++; ac--;
	} else if (strcmp(av[1], "-9600") == 0) {
		baud = B9600;
		av++; ac--;
	} else if (strcmp(av[1], "-19200") == 0) {
		baud = B19200;
		av++; ac--;
	} else if (strcmp(av[1], "-38400") == 0) {
		baud = B38400;
		av++; ac--;
	} else
		baud = B9600;

	if(ac < 3) 
	  usage();

	strncpy(dial_tty, devtag_get(av[1]), sizeof(dial_tty));

	while (! get_lock()) {
	    if(--retry == 0)
	      exit(-1);
	    sleep(1);
	}

	if ((fd = open(devtag_get(av[1]), O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0) {
	  perror("bad terminal device, try another");
	  exit(-1);
	}
	
	fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, O_RDWR);

	if (tcgetattr(fd, &tp) < 0) {
		perror("Couldn't get term attributes");
		exit(-1);
	}
	old = tp;

/*
SANE is a composite flag that sets the following parameters from termio(M):

CREAD BRKINT IGNPAR ISTRIP ICRNL IXON ISIG ICANON
ECHO ECHOK OPOST ONLCR

SANE also clears the following modes:

CLOCAL
IGNBRK PARMRK INPCK INLCR IUCLC IXOFF
XCASE ECHOE ECHONL NOFLSH
OLCUC OCRNL ONOCR ONLRET OFILL OFDEL NLDLY CRDLY
TABDLY BSDLY VTDLY FFDLY 

*/

	tp.c_cc[VINTR] = 3;	/* C-c */
	tp.c_cc[VQUIT] = 28;	/* C-\ */
	tp.c_cc[VERASE] = 127;	/* C-? */
	tp.c_cc[VKILL] = 21;	/* C-u */
	tp.c_cc[VEOF] = 4;	/* C-d */
	tp.c_cc[VSTART] = 17;	/* C-q */
	tp.c_cc[VSTOP] = 19;	/* C-s */
	tp.c_cc[VSUSP] = 26;	/* C-z */

	/* ignore CR, ignore parity */
	tp.c_iflag = ~(IGNBRK|PARMRK|INPCK|INLCR|IUCLC|IXOFF) |
	  BRKINT|ISTRIP|IGNPAR|ICRNL|IXON|ISIG|ICANON;

	/* Found Linux BUG */
	tp.c_iflag &= ~ISTRIP;

	/* Make it be sane */
	tp.c_cflag &= (CBAUD | CBAUDEX | CSIZE | CSTOPB | PARENB | PARODD);
	tp.c_cflag |= (CS8 | CREAD | HUPCL | CLOCAL);

	tp.c_oflag = 0; /* Raw Input */
	tp.c_lflag = 0; /* No canonical */

	tcflush(fd, TCIFLUSH);

	/* set output and input baud rates */

	cfsetospeed(&tp, baud);
	cfsetispeed(&tp, baud);

	if (tcsetattr(fd, TCSANOW, &tp) < 0) {
		perror("Couldn't set term attributes");
		goto error;
	}

	for(idx = 0, i = 2; i < ac; i++) {
	  len = strlen(av[i]);
	  strncpy(&io[idx], av[i], len);
	  idx += len;
	  io[idx++] = '\r';
	}

	while(1) {

	/* Set measuring open */
	if(0)  {
	  io[0] = 0x11;
	  io[1] = 0x01;
	  io[2] = 0x0C;
	  io[3] = 0x02;
	  io[4] = 0x1E;
	  io[5] = 0xC0;
	  res = write(fd, io, 6);
	}

	/* Read SW version */
	if(0)  {
	  io[0] = 0x11;
	  io[1] = 0x01;
	  io[2] = 0x1E;
	  io[3] = 0xD0;
	  res = write(fd, io, 4);
	}

	/* Read particle measurement */
	if(1)  {
	  io[0] = 0x11;
	  io[1] = 0x02;
	  io[2] = 0x0B;
	  io[3] = 0x07;	
	  io[4] = 0xDB;
	  res = write(fd, io, 5);
	}

	if(res < 0 ) {
	  perror("write faild");
	  goto error;
	}

	/* 50 ms */
        sleep(2);

	if ((res = read(fd, &mes, 60)) == len) {
        }

	if(mes.head != 0x16 )  {
	  printf("Frame read Err %02x\n", mes.head);
	  continue;
	}

	len = mes.len;

#if DEBUG
	printf("Frame read res=%d len=%d\n", res, mes.len);

	for(i=0; i < len+3; i++)  {
	  printf("%d %02X\n", i, mes.df[i]);
	}
	printf("CS len=%d frame-cs=%02x csum=%02x\n", mes.len, mes.df[len], csum((unsigned char*)&mes, mes.len+2));


	CSUM 30 42 2d
#endif

	x1 = 256;
	x2 = 256*256;
	x3 = 256*256*256;

	if(mes.df[len] == csum((unsigned char*)&mes, mes.len+2)) {
	  pm[0] = mes.df[1]*x3 + mes.df[2]*x2 + mes.df[3]*x1 + mes.df[4];
	  pm[1] = mes.df[5]*x3 + mes.df[6]*x2 + mes.df[7]*x1 + mes.df[8];
	  pm[2] = mes.df[9]*x3 + mes.df[10]*x2 + mes.df[11]*x1 + mes.df[12];

	  pm[3] = mes.df[13]*x3 + mes.df[14]*x2 + mes.df[15]*x1 + mes.df[16];
	  pm[4] = mes.df[17]*x3 + mes.df[18]*x2 + mes.df[19]*x1 + mes.df[20];
	  pm[5] = mes.df[21]*x3 + mes.df[22]*x2 + mes.df[23]*x1 + mes.df[24];

	  pm[6] = mes.df[25]*x3 + mes.df[26]*x2 + mes.df[27]*x1 + mes.df[28];
	  pm[7] = mes.df[29]*x3 + mes.df[30]*x2 + mes.df[31]*x1 + mes.df[32];
	  pm[8] = mes.df[33]*x3 + mes.df[34]*x2 + mes.df[35]*x1 + mes.df[36];
	  pm[9] = mes.df[37]*x3 + mes.df[38]*x2 + mes.df[39]*x1 + mes.df[40];
	  pm[10] = mes.df[41]*x3 + mes.df[42]*x2 + mes.df[43]*x1 + mes.df[44];
	  pm[11] = mes.df[45]*x3 + mes.df[46]*x2 + mes.df[47]*x1 + mes.df[48];

	  printf("GRIMM:");
	  for(i=0; i<3; i++) {
	    printf(" %6u", pm[i]);
	  }
	  printf(" TSI:  ");
	  for(i=3; i<6; i++) {
	    printf(" %6u", pm[i]);
	  }
	  printf(" DB:");
	  for(i=6; i<12; i++) {
	    printf(" %6u", pm[i]);
	  }
	  printf("\n");
	  
	}
	else
	  printf("CS len=%d frame-cs=%02x csum=%02x\n", mes.len, mes.df[len], csum((unsigned char*)&mes, mes.len+2));
	sleep(36);

	}

	if (tcsetattr(fd, TCSANOW, &old) < 0) {
		perror("Couldn't restore term attributes");
		exit(-1);
	}

	lockfile_remove();
	exit(0);
error:
	if (tcsetattr(fd, TCSANOW, &old) < 0) {
		perror("Couldn't restore term attributes");
	}
	exit(-1);
}
