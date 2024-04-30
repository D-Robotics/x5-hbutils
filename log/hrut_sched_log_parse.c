/*
 * Horizon Robotics
 *
 *  Copyright (C) 2023 Horizon Robotics Inc.
 *  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/sysinfo.h>

/* files are not retained persistent_ram_buffer */
#define PSTORE_RESERVER_SIZE 12
#define CACHE_ALIGN 64
#define TASK_COMM_LEN 16
#define NS_TO_SEC 1000000000
#define NS_TO_US 1000

typedef uint64_t u64;
typedef int32_t s32;
typedef int16_t s16;
typedef uint8_t u8;

#define TASK_STAT_BUF_LEN 4
/* fits into a single 64-byte cache line.*/
struct hobot_sched_log {
	u64 ts;
	s16 cpuid;
	u8 prev_prio;
	u8 next_prio;
	s32 prev_pid;
	s32 prev_tgid;
	s32 next_pid;
	s32 next_tgid;
	char prev_stat[TASK_STAT_BUF_LEN];
	char prev_comm[TASK_COMM_LEN];
	char next_comm[TASK_COMM_LEN];
};

int main(int argc, char *argv[])
{
	FILE *fp;
	struct hobot_sched_log rec;
	int cpus_num = get_nprocs_conf();
	int sched_total_size = 0;
	int sched_percpu_size = 0;
	int rec_num_percpu = 0;
	int offset, cpu, i;
	int reserved = CACHE_ALIGN - PSTORE_RESERVER_SIZE;

	if (cpus_num < 1) {
		printf("cpu num is %d by get_nprocs_conf\n", cpus_num);
		return -1;
	}

	if (argc < 2 || strncmp(argv[1], "-h", 2) == 0 || strncmp(argv[1], "--help", 6) == 0) {
		printf("This program parse dump sched log pstore sched-ramoops-0\n");
		printf("Usage:\n");
		printf("      mount -t pstore pstore /mnt/\n");
		printf("      %s /mnt/sched-ramoops-0 | sort \n", argv[0]);
		return -1;
	}

	fp = fopen(argv[1], "rb");
	if (fp == 0) {
		printf("Cannot open file %s\n", argv[1]);
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	sched_total_size = ftell(fp) + PSTORE_RESERVER_SIZE;
	sched_percpu_size = sched_total_size / cpus_num - CACHE_ALIGN;
	rec_num_percpu = sched_percpu_size / sizeof(rec);

	for (cpu = 0; cpu < cpus_num; cpu++) {
		offset = reserved + sched_percpu_size * cpu;
		if (fseek(fp, offset, SEEK_SET) != 0) {
			printf("fseek failed\n");
			break;
		}

		for (i = 0; i < rec_num_percpu; i++) {
			if (fread(&rec, sizeof(rec), 1, fp) == 1) {
				if (rec.ts == 0)
					continue;
				printf("[%5lu.%06lu] [%d] : %s(%d:%d:%d:%s) -> %s(%d:%d:%d)\n",
				       rec.ts / NS_TO_SEC, (rec.ts % NS_TO_SEC) / NS_TO_US, rec.cpuid,
				       rec.prev_comm, rec.prev_tgid, rec.prev_pid, rec.prev_prio,
				       rec.prev_stat, rec.next_comm, rec.next_tgid, rec.next_pid,
				       rec.next_prio);
			}
		}
	}

	printf("\nsched_total_size:%d cpus_num:%d sched_percpu_size:%d rec_num_percpu:%d\n", sched_total_size,
	       cpus_num, sched_percpu_size, rec_num_percpu);

	fclose(fp);
	return 0;
}
