/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_SEM_H
#define _LINUX_SEM_H

#include <linux/ipc.h>

#define SEM_UNDO        0x1000

#define GETPID  11
#define GETVAL  12
#define GETALL  13
#define GETNCNT 14
#define GETZCNT 15
#define SETVAL  16
#define SETALL  17

#define SEM_STAT 18
#define SEM_INFO 19
#define SEM_STAT_ANY 20

struct semid_ds {
	struct ipc_perm	sem_perm;
	__kernel_old_time_t sem_otime;
	__kernel_old_time_t sem_ctime;
	struct sem	*sem_base;
	struct sem_queue *sem_pending;
	struct sem_queue **sem_pending_last;
	struct sem_undo	*undo;
	unsigned short	sem_nsems;
};

#include <asm/sembuf.h>

struct sembuf {
	unsigned short  sem_num;
	short		sem_op;
	short		sem_flg;
};

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
	struct seminfo *__buf;
	void *__pad;
};

struct  seminfo {
	int semmap;
	int semmni;
	int semmns;
	int semmnu;
	int semmsl;
	int semopm;
	int semume;
	int semusz;
	int semvmx;
	int semaem;
};

#define SEMMNI  32000
#define SEMMSL  32000
#define SEMMNS  (SEMMNI*SEMMSL)
#define SEMOPM  500
#define SEMVMX  32767
#define SEMAEM  SEMVMX

#define SEMUME  SEMOPM
#define SEMMNU  SEMMNS
#define SEMMAP  SEMMNS
#define SEMUSZ  20

#endif
