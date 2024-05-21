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

#define LOG_TAG "chiplog"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <asm/types.h>
#include <pthread.h>
#include <getopt.h>
#include <semaphore.h>
#include <dirent.h>
#include <limits.h>
#include <float.h>
#include <sys/syscall.h>
#include <sched.h>
#include <stdbool.h>
#include <sys/sysinfo.h>
#include <logging.h>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* soc config */
#define SOC_ID_FILE "/sys/class/socinfo/soc_uid"
#define BOARD_ID_FILE "/sys/class/socinfo/board_id"
#define SOC_THERMAL_FILE "/sys/class/thermal/thermal_zone1/temp"

/* cpu config */
#define CPU_LOAD_FILE "/proc/stat"
#define CPU_NUM_MAX 256

/* bpu config */
#define BPU_LOAD_FILE_DIR "/sys/devices/system/bpu/"
#define BPU_LOAD_FILE_PREFIX "bpu"
#define BPU_LOAD_FILE_SUFFIX "/ratio"
#define BPU_NUM_MAX 16

/* ddr config, DDR_BAND_GET_CMD may change */
#define DDR_PORT_NUM_MAX 16
#define DDR_BAND_GET_CMD "hrut_ddr -t all -p"
#define DDR_BAND_NUMS_FLAG "Perf"
#define DDR_BAND_READ_FLAG "Read"
#define DDR_BAND_WRITE_FLAG "Write"

/* asynchronous queue buffer config */
#define BUF_SIZE 65536
#define QUEUE_MSG_SIZE (BUF_SIZE)
#define QUEUE_NUMS 10

/* default config */
#define CHIP_LOG_PATH_LEN 128
#define DEFAULT_FILE_MAX_NUMS 100
#define DEFAULT_FILE_MAX_LEN 2048
#define DEFAULT_TEMP_THRESHOLD 115
#define DEFAULT_SLEEP_PERIOD_MS 1000
#define DEFAULT_QUERY_LOAD_PERIOD_MS DEFAULT_SLEEP_PERIOD_MS
#define DEFAULT_SAVE_LOAD_PERIOD_MS 10000
#define VOLUME_PRODUCTION_OVER_TEMP_SAVE_CNT 10

typedef struct {
	uint32_t msg_type;
	uint32_t msg_len;
	char data[QUEUE_MSG_SIZE]; /* save the generated logs as an asynchronous buffer */
} msg_t;

typedef struct {
	int head;
	int tail;
	int count;
	sem_t sem;
	msg_t *msg;
} queue_t;

typedef struct {
	char cpu[8];
	uint64_t all;
	uint64_t user;
	uint64_t nice;
	uint64_t sys;
	uint64_t idle;
	uint64_t iowait;
	uint64_t irq;
	uint64_t softirq;
	uint64_t steal; /* virtual */
	uint64_t guest;
	uint64_t gnice;
} cpu_load_t;

typedef struct {
	float user;
	float nice;
	float sys;
	float idle;
	float iowait;
	float irq;
	float softirq;
	float steal;
	float guest;
	float gnice;
} cpu_usage_t;

typedef struct {
	char name[8];
	uint32_t r_cur;
	uint32_t r_max;
	uint32_t r_min;
	uint32_t r_avg;
	uint32_t w_cur;
	uint32_t w_max;
	uint32_t w_min;
	uint32_t w_avg;
	int record_num;
} ddr_band_t;

typedef struct module_load {
	int fd;
	int record_num;
	float max;
	float min;
	float avg;
	float cur;
} module_load_t;

typedef struct {
	char *path;
	int max_nums;
	int max_len;
	int fd;
	int fd_oflag;
	int len;
} file_config_t;

typedef struct {
	int cpu_nums;
	FILE *cpu_load_fp;
	int query_cpu_load_period_ms;
	int save_cpu_load_period_ms;
	module_load_t cpu_load;

	int bpu_nums;
	int query_bpu_load_period_ms;
	int save_bpu_load_period_ms;
	module_load_t bpu_load[BPU_NUM_MAX];

	int query_temp_period_ms;
	int save_temp_period_ms;
	float temp_threshold;
	module_load_t temp;

	int ddr_band_nums;
	FILE *ddr_band_pipe;
	int query_ddr_band_period_ms;
	int save_ddr_band_period_ms;
	ddr_band_t ddr_band[DDR_PORT_NUM_MAX];

	file_config_t file;
	pthread_t save_file_thread_id;

	queue_t queue;
	int sleep_period_ms;
	int is_exit;
	int is_volume;
	int priority;
	int sched_police;
} global_config_t;

static global_config_t g_cnf = { 0 };
static char id_buf[512] = { 0 };
static cpu_load_t *prev_cpu_load = NULL;
static cpu_load_t *curr_cpu_load = NULL;
static cpu_usage_t *curr_cpu_usage = NULL;
static char *all_buf = NULL;

static int save_log_file(char *buf, int len);

static int match_file_nums(const char *path, const char *prefix)
{
	DIR *dir;
	struct dirent *entry;
	int count = 0;

	dir = opendir(path);
	if (dir == NULL) {
		pr_err("opendir %s failed\n", path);
		return 0;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0)
			count++;
	}

	closedir(dir);
	return count;
}

static int basic_info_init()
{
	int size = 0;
	int i = 0;
	char path[CHIP_LOG_PATH_LEN];

	g_cnf.cpu_nums = get_nprocs_conf();
	if (g_cnf.cpu_nums < 1) {
		pr_err("cpu num is %d by get_nprocs_conf\n", g_cnf.cpu_nums);
		return -1;
	}
	if (g_cnf.cpu_nums > CPU_NUM_MAX) {
		g_cnf.cpu_nums = CPU_NUM_MAX;
		pr_info("cpu num is %d, change to %d\n", g_cnf.cpu_nums, CPU_NUM_MAX);
	}

	g_cnf.bpu_nums = match_file_nums(BPU_LOAD_FILE_DIR, BPU_LOAD_FILE_PREFIX);
	if (g_cnf.bpu_nums > BPU_NUM_MAX) {
		g_cnf.bpu_nums = BPU_NUM_MAX;
		pr_info("bpu num is %d, change to %d\n", g_cnf.bpu_nums, BPU_NUM_MAX);
	}

	/* init bpu fd */
	for (i = 0; i < g_cnf.bpu_nums; i++) {
		memset(path, 0, CHIP_LOG_PATH_LEN);
		snprintf(path, CHIP_LOG_PATH_LEN, "%s%s%d%s", BPU_LOAD_FILE_DIR, BPU_LOAD_FILE_PREFIX, i,
			 BPU_LOAD_FILE_SUFFIX);
		g_cnf.bpu_load[i].fd = open(path, O_RDONLY);
		if (g_cnf.bpu_load[i].fd < 0) {
			pr_err("open %s failed\n", path);
		}
	}

	/* init temp fd */
	g_cnf.temp.fd = open(SOC_THERMAL_FILE, O_RDONLY);
	if (g_cnf.temp.fd < 0) {
		pr_err("open %s failed\n", SOC_THERMAL_FILE);
	}

	/* malloc about cpu */
	size = sizeof(cpu_load_t) * (g_cnf.cpu_nums + 1) * 2 + sizeof(cpu_usage_t) * (g_cnf.cpu_nums + 1);
	prev_cpu_load = malloc(size);
	if (!prev_cpu_load) {
		pr_err("malloc prev_cpu_load failed\n");
		return -1;
	}
	memset(prev_cpu_load, 0, size);
	curr_cpu_load = prev_cpu_load + (g_cnf.cpu_nums + 1);
	curr_cpu_usage = (cpu_usage_t *)(curr_cpu_load + (g_cnf.cpu_nums + 1));

	/* malloc generated data buf */
	all_buf = malloc(BUF_SIZE);
	if (!all_buf) {
		pr_err("malloc all_buf failed\n");
		free(prev_cpu_load);
		prev_cpu_load = NULL;
		return -1;
	}
	memset(all_buf, 0, BUF_SIZE);

	return 0;
}

static void basic_info_exit()
{
	int i = 0;

	free(all_buf);
	all_buf = NULL;
	free(prev_cpu_load);
	prev_cpu_load = NULL;
	curr_cpu_load = NULL;
	curr_cpu_usage = NULL;

	if (g_cnf.temp.fd > 0)
		close(g_cnf.temp.fd);
	for (i = 0; i < g_cnf.bpu_nums; i++) {
		if (g_cnf.bpu_load[i].fd > 0)
			close(g_cnf.bpu_load[i].fd);
	}
}

static inline float calc_average_float(float last_avg, float next_val, int num)
{
	float avg_val = (last_avg * (num - 1) + next_val) / num;
	return avg_val;
}

static inline uint32_t calc_average_uint32(uint64_t last_avg, uint32_t next_val, int num)
{
	uint32_t avg_val = (last_avg * (num - 1) + next_val) / num;
	return avg_val;
}

static int gener_time_buf(char *buf)
{
	time_t time;
	struct tm *tm_ptr;
	char time_buf[128] = { 0 };
	struct timespec mono_ts;
	struct timespec real_ts;
	int ret = 0;

	clock_gettime(CLOCK_MONOTONIC, &mono_ts);
	clock_gettime(CLOCK_REALTIME, &real_ts);

	time = real_ts.tv_sec;
	tm_ptr = localtime(&time);
	strftime(time_buf, 120, "%Y-%m-%d %H:%M:%S", tm_ptr);

	ret = sprintf(buf, "%s realtime:[%ld,%ld] monotime:[%ld,%ld]\n", time_buf, real_ts.tv_sec, real_ts.tv_nsec,
		      mono_ts.tv_sec, mono_ts.tv_nsec);
	return ret;
}

static int msg_queue_init(queue_t *queue, int msg_count)
{
	if (!queue) {
		pr_err("invalid queue\n");
		return -1;
	}
	queue->tail = 0;
	queue->head = 0;
	queue->count = msg_count;

	queue->msg = malloc(sizeof(msg_t) * msg_count);
	if (queue->msg == NULL) {
		pr_err("malloc error\n");
		return -1;
	}

	sem_init(&queue->sem, 0, 1);

	return 0;
}

static void msg_queue_exit(queue_t *queue)
{
	if (queue && queue->msg)
		free(queue->msg);
}

static int msg_dequeue(queue_t *queue)
{
	msg_t *msg = NULL;

	if (!queue) {
		pr_err("invalid queue\n");
		return -1;
	}

	/* only one consumer, no need to lock head */
	if (queue->tail == queue->head) {
		return -2;
	}

	msg = &queue->msg[queue->head];
	save_log_file(msg->data, msg->msg_len);
	queue->head = (queue->head + 1) % queue->count;
	return 0;
}

static int msg_enqueue(queue_t *queue, uint32_t msg_type, uint32_t msg_len, char *buf)
{
	int offset = 0;
	if (!queue || !buf) {
		pr_err("invalid queue buf\n");
		return -1;
	}

	if (queue->head == (queue->tail + 1) % queue->count) {
		pr_err("full queue\n");
		return -1;
	}

	sem_wait(&queue->sem);

	/* add time information at the beginning of msg */
	offset = gener_time_buf(queue->msg[queue->tail].data);
	queue->msg[queue->tail].msg_type = msg_type;
	queue->msg[queue->tail].msg_len = msg_len + offset;
	if (unlikely(queue->msg[queue->tail].msg_len >= QUEUE_MSG_SIZE)) {
		msg_len = QUEUE_MSG_SIZE - offset - 1;
		queue->msg[queue->tail].msg_len = QUEUE_MSG_SIZE;
	}
	memset((queue->msg[queue->tail]).data + offset, 0, msg_len + 1);
	memcpy(queue->msg[queue->tail].data + offset, buf, msg_len);
	queue->tail = (queue->tail + 1) % queue->count;

	sem_post(&queue->sem);
	return 0;
}

static int read_file_by_fd(int fd, char *buf, int size)
{
	int ret = 0;

	if (unlikely(!buf || fd < 0)) {
		pr_err("buf is NULL or fd %d\n", fd);
		return -1;
	}

	ret = lseek(fd, 0, SEEK_SET);
	if (unlikely(ret < 0)) {
		pr_err("lseek fd=%d failed ret=%d\n", fd, ret);
		return -1;
	}

	ret = read(fd, buf, size);
	if (unlikely(ret <= 0)) {
		pr_err("read fd=%d failed ret=%d\n", fd, ret);
		return -1;
	}

	return ret;
}

static int read_file_by_path(char *path, char *buf, int size)
{
	int ret = 0;
	int fd = 0;

	if (!buf || !path) {
		pr_err("path or buf is NULL\n");
		return -1;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		pr_err("open file %s failed ret %d\n", path, fd);
		return -1;
	}

	ret = read(fd, buf, size);
	if (ret <= 0) {
		pr_err("read file %s failed ret %d\n", path, ret);
	}

	close(fd);

	return ret;
}

static void get_id()
{
	int ret = 0;
	int buf_len = 128;
	char soc_id_buf[128] = { 0 };
	char board_id_buf[128] = { 0 };

	ret = read_file_by_path(SOC_ID_FILE, soc_id_buf, buf_len - 1);
	if (ret <= 0)
		pr_err("get soc id %s failed\n", SOC_ID_FILE);

	ret = read_file_by_path(BOARD_ID_FILE, board_id_buf, buf_len - 1);
	if (ret <= 0)
		pr_err("get board id %s failed\n", BOARD_ID_FILE);

	memset(id_buf, 0, sizeof(id_buf));
	sprintf(id_buf, "chip id: %sboard id: %s", soc_id_buf, board_id_buf);
}

static int get_cpu_load()
{
	int i = 0;
	char tmpbuf[256];
	float one_jiffies_percentage = 0;

	/* using fgets you must reopen the device node */
	g_cnf.cpu_load_fp = fopen(CPU_LOAD_FILE, "r");
	if (unlikely(!g_cnf.cpu_load_fp)) {
		pr_err("open cpu_load_fp failed");
		return -1;
	}

	for (i = 0; i <= g_cnf.cpu_nums; i++) {
		memset(tmpbuf, 0, sizeof(tmpbuf));
		fgets(tmpbuf, sizeof(tmpbuf), g_cnf.cpu_load_fp);
		sscanf(tmpbuf, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu", curr_cpu_load[i].cpu,
		       &curr_cpu_load[i].user, &curr_cpu_load[i].nice, &curr_cpu_load[i].sys, &curr_cpu_load[i].idle,
		       &curr_cpu_load[i].iowait, &curr_cpu_load[i].irq, &curr_cpu_load[i].softirq,
		       &curr_cpu_load[i].steal, &curr_cpu_load[i].guest, &curr_cpu_load[i].gnice);

		curr_cpu_load[i].all = curr_cpu_load[i].user + curr_cpu_load[i].nice + curr_cpu_load[i].sys +
				       curr_cpu_load[i].idle + curr_cpu_load[i].iowait + curr_cpu_load[i].irq +
				       curr_cpu_load[i].softirq + curr_cpu_load[i].steal + curr_cpu_load[i].guest +
				       curr_cpu_load[i].gnice;
	}

	/* first measurement does not calculate cpu usage */
	if (likely(prev_cpu_load[0].all != 0)) {
		for (i = 0; i <= g_cnf.cpu_nums; i++) {
			one_jiffies_percentage = 1.0 / (curr_cpu_load[i].all - prev_cpu_load[i].all) * 100;
			curr_cpu_usage[i].user =
				(curr_cpu_load[i].user - prev_cpu_load[i].user) * one_jiffies_percentage;
			curr_cpu_usage[i].nice =
				(curr_cpu_load[i].nice - prev_cpu_load[i].nice) * one_jiffies_percentage;
			curr_cpu_usage[i].sys = (curr_cpu_load[i].sys - prev_cpu_load[i].sys) * one_jiffies_percentage;
			curr_cpu_usage[i].idle =
				(curr_cpu_load[i].idle - prev_cpu_load[i].idle) * one_jiffies_percentage;
			curr_cpu_usage[i].iowait =
				(curr_cpu_load[i].iowait - prev_cpu_load[i].iowait) * one_jiffies_percentage;
			curr_cpu_usage[i].irq = (curr_cpu_load[i].irq - prev_cpu_load[i].irq) * one_jiffies_percentage;
			curr_cpu_usage[i].softirq =
				(curr_cpu_load[i].softirq - prev_cpu_load[i].softirq) * one_jiffies_percentage;
			curr_cpu_usage[i].steal =
				(curr_cpu_load[i].steal - prev_cpu_load[i].steal) * one_jiffies_percentage;
			curr_cpu_usage[i].guest =
				(curr_cpu_load[i].guest - prev_cpu_load[i].guest) * one_jiffies_percentage;
			curr_cpu_usage[i].gnice =
				(curr_cpu_load[i].gnice - prev_cpu_load[i].gnice) * one_jiffies_percentage;
		}

		g_cnf.cpu_load.cur = curr_cpu_usage[0].user + curr_cpu_usage[0].nice + curr_cpu_usage[0].sys +
				     curr_cpu_usage[0].iowait + curr_cpu_usage[0].irq + curr_cpu_usage[0].softirq +
				     curr_cpu_usage[0].steal + curr_cpu_usage[0].guest + curr_cpu_usage[0].gnice;

		if (g_cnf.cpu_load.cur > g_cnf.cpu_load.max)
			g_cnf.cpu_load.max = g_cnf.cpu_load.cur;

		if (g_cnf.cpu_load.cur < g_cnf.cpu_load.min)
			g_cnf.cpu_load.min = g_cnf.cpu_load.cur;

		g_cnf.cpu_load.record_num++;
		g_cnf.cpu_load.avg =
			calc_average_float(g_cnf.cpu_load.avg, g_cnf.cpu_load.cur, g_cnf.cpu_load.record_num);
	}

	memcpy(prev_cpu_load, curr_cpu_load, (sizeof(cpu_load_t) * (g_cnf.cpu_nums + 1)));
	fclose(g_cnf.cpu_load_fp);
	return 0;
}

static int gener_cpu_load_buf(char *buf, int offset)
{
	int i = 0;
	int ret = 0;

	ret = sprintf(buf + offset, "CPU  Load: cur:%-6.2f max:%-6.2f min:%-6.2f avg:%-6.2f\n", g_cnf.cpu_load.cur,
		      g_cnf.cpu_load.max, g_cnf.cpu_load.min, g_cnf.cpu_load.avg);

	for (i = 0; i <= g_cnf.cpu_nums; i++) {
		ret += sprintf(
			buf + offset + ret,
			"%6s usr %-6.2f sys %-6.2f nice %-6.2f idle %-6.2f iowait %-6.2f irq %-6.2f softirq %-6.2f steal %-6.2f guest %-6.2f gnice %-6.2f\n",
			curr_cpu_load[i].cpu, curr_cpu_usage[i].user, curr_cpu_usage[i].sys, curr_cpu_usage[i].nice,
			curr_cpu_usage[i].idle, curr_cpu_usage[i].iowait, curr_cpu_usage[i].irq,
			curr_cpu_usage[i].softirq, curr_cpu_usage[i].steal, curr_cpu_usage[i].guest,
			curr_cpu_usage[i].gnice);
	}

	/* max/min/avg is initialized every period */
	g_cnf.cpu_load.record_num = 0;
	g_cnf.cpu_load.max = FLT_MIN;
	g_cnf.cpu_load.min = FLT_MAX;
	g_cnf.cpu_load.avg = 0;

	return ret;
}

/* Generate data to fill the prev cpu load */
static int cpu_load_init()
{
	g_cnf.cpu_load.record_num = 0;
	g_cnf.cpu_load.max = FLT_MIN;
	g_cnf.cpu_load.min = FLT_MAX;
	g_cnf.cpu_load.avg = 0;
	memset(prev_cpu_load, 0, sizeof(cpu_load_t) * (g_cnf.cpu_nums + 1));
	return get_cpu_load();
}

static int get_bpu_load()
{
	int i = 0;
	int len = 0;
	char tmpbuf[128];
	int buf_len = 128;

	if (unlikely(g_cnf.bpu_nums < 1))
		return -1;

	for (i = 0; i < g_cnf.bpu_nums; i++) {
		memset(tmpbuf, 0, sizeof(tmpbuf));
		len = read_file_by_fd(g_cnf.bpu_load[i].fd, tmpbuf, buf_len);
		if (unlikely(len < 0)) {
			pr_err("read bpu%d load failed\n", i);
			return -1;
		}
		tmpbuf[buf_len - 1] = 0;

		g_cnf.bpu_load[i].cur = (float)(atoi(tmpbuf) * 1.0);
		if (g_cnf.bpu_load[i].cur > g_cnf.bpu_load[i].max) {
			g_cnf.bpu_load[i].max = g_cnf.bpu_load[i].cur;
		}

		if (g_cnf.bpu_load[i].cur < g_cnf.bpu_load[i].min) {
			g_cnf.bpu_load[i].min = g_cnf.bpu_load[i].cur;
		}

		g_cnf.bpu_load[i].record_num++;
		g_cnf.bpu_load[i].avg =
			calc_average_float(g_cnf.bpu_load[i].avg, g_cnf.bpu_load[i].cur, g_cnf.bpu_load[i].record_num);
	}

	return 0;
}

static int gener_bpu_load_buf(char *buf, int offset)
{
	int ret = 0;
	int i = 0;

	for (i = 0; i < g_cnf.bpu_nums; i++) {
		ret += sprintf(buf + offset + ret, "BPU%d Load: cur:%-6.2f max:%-6.2f min:%-6.2f avg:%-6.2f\n", i,
			       g_cnf.bpu_load[i].cur, g_cnf.bpu_load[i].max, g_cnf.bpu_load[i].min,
			       g_cnf.bpu_load[i].avg);

		/* max/min/avg is initialized every period */
		g_cnf.bpu_load[i].record_num = 0;
		g_cnf.bpu_load[i].max = FLT_MIN;
		g_cnf.bpu_load[i].min = FLT_MAX;
		g_cnf.bpu_load[i].avg = 0;
	}

	return ret;
}

static int get_temp()
{
	int len = 0;
	char tmpbuf[128] = { 0 };
	int buf_len = 128;

	if (unlikely(g_cnf.temp.fd < 1))
		return -1;

	len = read_file_by_fd(g_cnf.temp.fd, tmpbuf, buf_len);
	if (unlikely(len < 0)) {
		pr_err("read temp failed\n");
		return -1;
	}
	tmpbuf[buf_len - 1] = 0;

	g_cnf.temp.cur = (float)(atoi(tmpbuf) / 1000.0);
	if (g_cnf.temp.cur > g_cnf.temp.max) {
		g_cnf.temp.max = g_cnf.temp.cur;
	}

	if (g_cnf.temp.cur < g_cnf.temp.min) {
		g_cnf.temp.min = g_cnf.temp.cur;
	}
	g_cnf.temp.record_num++;

	g_cnf.temp.avg = calc_average_float(g_cnf.temp.avg, g_cnf.temp.cur, g_cnf.temp.record_num);

	return 0;
}

static int gener_temp_buf(char *buf, int offset)
{
	int ret = 0;

	ret = sprintf(buf + offset, "CPU  Temp: cur:%-6.2f max:%-6.2f min:%-6.2f avg:%-6.2f\n", g_cnf.temp.cur,
		      g_cnf.temp.max, g_cnf.temp.min, g_cnf.temp.avg);

	/* max/min/avg is initialized every period */
	g_cnf.temp.record_num = 0;
	g_cnf.temp.max = FLT_MIN;
	g_cnf.temp.min = FLT_MAX;
	g_cnf.temp.avg = 0;

	return ret;
}

static inline void get_ddr_band_statistics(void)
{
	int i = 0;

	for (i = 0; i < g_cnf.ddr_band_nums; i++) {
		if (g_cnf.ddr_band[i].r_cur > g_cnf.ddr_band[i].r_max)
			g_cnf.ddr_band[i].r_max = g_cnf.ddr_band[i].r_cur;
		if (g_cnf.ddr_band[i].r_cur < g_cnf.ddr_band[i].r_min)
			g_cnf.ddr_band[i].r_min = g_cnf.ddr_band[i].r_cur;
		if (g_cnf.ddr_band[i].w_cur > g_cnf.ddr_band[i].w_max)
			g_cnf.ddr_band[i].w_max = g_cnf.ddr_band[i].w_cur;
		if (g_cnf.ddr_band[i].w_cur < g_cnf.ddr_band[i].w_min)
			g_cnf.ddr_band[i].w_min = g_cnf.ddr_band[i].w_cur;

		g_cnf.ddr_band[i].record_num++;
		g_cnf.ddr_band[i].r_avg = calc_average_uint32(g_cnf.ddr_band[i].r_avg, g_cnf.ddr_band[i].r_cur,
							      g_cnf.ddr_band[i].record_num);
		g_cnf.ddr_band[i].w_avg = calc_average_uint32(g_cnf.ddr_band[i].w_avg, g_cnf.ddr_band[i].w_cur,
							      g_cnf.ddr_band[i].record_num);
	}
}

/* obtain the number of ddr bands in the perf line */
static inline void get_ddr_band_nums(char *buf)
{
	int i = 0;
	char *token = NULL;
	if (unlikely(g_cnf.ddr_band_nums == 0)) {
		if (strstr(buf, DDR_BAND_NUMS_FLAG) != NULL) {
			token = strtok(buf, "|");
			token = strtok(NULL, "|");
			for (i = 0; i < DDR_PORT_NUM_MAX && token != NULL; i++) {
				if (sscanf(token, "%s", g_cnf.ddr_band[g_cnf.ddr_band_nums].name) == 1) {
					g_cnf.ddr_band_nums++;
				}
				token = strtok(NULL, "|");
			}
		}
	}
}

static int get_ddr_band()
{
	int i = 0;
	char tmpbuf[260] = { 0 };
	int tmpbuf_len = sizeof(tmpbuf);
	int tmpbuf_fgets_len = tmpbuf_len - 4;
	char *token = NULL;
	FILE *pipe = g_cnf.ddr_band_pipe;

	if (unlikely(!pipe))
		return -1;

	while (fgets(tmpbuf, tmpbuf_fgets_len, pipe) != NULL) {
		get_ddr_band_nums(tmpbuf);
		if (strstr(tmpbuf, DDR_BAND_READ_FLAG) != NULL) {
			token = strtok(tmpbuf, "|");
			token = strtok(NULL, "|");
			for (i = 0; i < g_cnf.ddr_band_nums && token != NULL; i++) {
				sscanf(token, "%u", &g_cnf.ddr_band[i].r_cur);
				token = strtok(NULL, "|");
			}
		} else if (strstr(tmpbuf, DDR_BAND_WRITE_FLAG) != NULL) {
			token = strtok(tmpbuf, "|");
			token = strtok(NULL, "|");
			for (i = 0; i < g_cnf.ddr_band_nums && token != NULL; i++) {
				sscanf(token, "%u", &g_cnf.ddr_band[i].w_cur);
				token = strtok(NULL, "|");
			}
		}
		memset(tmpbuf, 0, tmpbuf_len);
	}

	get_ddr_band_statistics();

	if (unlikely(g_cnf.ddr_band_nums < 1))
		return -1;

	return 0;
}

static int gener_ddr_band_buf(char *buf, int offset)
{
	int i = 0;
	int curret = 0;
	int maxret = 0;
	int minret = 0;
	int avgret = 0;
	char curbuf[512] = { 0 };
	char maxbuf[512] = { 0 };
	char minbuf[512] = { 0 };
	char avgbuf[512] = { 0 };

	curret += sprintf(curbuf, "DDR Band cur(MB/s):");
	maxret += sprintf(maxbuf, "DDR Band max(MB/s):");
	minret += sprintf(minbuf, "DDR Band min(MB/s):");
	avgret += sprintf(avgbuf, "DDR Band avg(MB/s):");
	for (i = 0; i < g_cnf.ddr_band_nums; i++) {
		curret += sprintf(curbuf + curret, " | %s R:%-3u W:%-3u", g_cnf.ddr_band[i].name,
				  g_cnf.ddr_band[i].r_cur, g_cnf.ddr_band[i].w_cur);
		maxret += sprintf(maxbuf + maxret, " | %s R:%-3u W:%-3u", g_cnf.ddr_band[i].name,
				  g_cnf.ddr_band[i].r_max, g_cnf.ddr_band[i].w_max);
		minret += sprintf(minbuf + minret, " | %s R:%-3u W:%-3u", g_cnf.ddr_band[i].name,
				  g_cnf.ddr_band[i].r_min, g_cnf.ddr_band[i].w_min);
		avgret += sprintf(avgbuf + avgret, " | %s R:%-3u W:%-3u", g_cnf.ddr_band[i].name,
				  g_cnf.ddr_band[i].r_avg, g_cnf.ddr_band[i].w_avg);
	}
	curret += sprintf(curbuf + curret, "\n");
	maxret += sprintf(maxbuf + maxret, "\n");
	minret += sprintf(minbuf + minret, "\n");
	avgret += sprintf(avgbuf + avgret, "\n");

	memcpy(buf + offset, curbuf, curret);
	memcpy(buf + offset + curret, maxbuf, maxret);
	memcpy(buf + offset + curret + maxret, minbuf, minret);
	memcpy(buf + offset + curret + maxret + minret, avgbuf, avgret);

	/* max/min/avg is initialized every period */
	for (i = 0; i < g_cnf.ddr_band_nums; i++) {
		g_cnf.ddr_band[i].r_max = 0;
		g_cnf.ddr_band[i].r_min = UINT_MAX;
		g_cnf.ddr_band[i].r_avg = 0;
		g_cnf.ddr_band[i].w_max = 0;
		g_cnf.ddr_band[i].w_min = UINT_MAX;
		g_cnf.ddr_band[i].w_avg = 0;
		g_cnf.ddr_band[i].record_num = 0;
	}

	return (curret + maxret + minret + avgret);
}

static void basic_data_init()
{
	int query_ddr_band_period_us = g_cnf.query_ddr_band_period_ms * 1000;
	char command[128] = { 0 };
	int fd = 0;
	int flags = 0;

	/* run the get ddr band command */
	if (!g_cnf.ddr_band_pipe) {
		snprintf(command, sizeof(command), "%s %d", DDR_BAND_GET_CMD, query_ddr_band_period_us);
		pr_info("get ddr band cmd: %s\n", command);
		g_cnf.ddr_band_pipe = popen(command, "r");
		if (g_cnf.ddr_band_pipe) {
			fd = fileno(g_cnf.ddr_band_pipe);
			flags = fcntl(fd, F_GETFL, 0);
			fcntl(fd, F_SETFL, flags | O_NONBLOCK);
		} else {
			pr_err("popen command error\n");
		}
	}

	if (cpu_load_init() < 0) {
		pr_err("cpu_load_init error\n");
	}

	/* sleep for generate ddr band and cpu data */
	usleep(query_ddr_band_period_us + 10000);
}

static void basic_data_exit()
{
	if (g_cnf.ddr_band_pipe) {
		pclose(g_cnf.ddr_band_pipe);
		g_cnf.ddr_band_pipe = NULL;
	}
}

static int log_file_init()
{
	struct stat statbuf;
	int ret = 0;

	/* if no path is configured, output to the console */
	if (g_cnf.file.path == NULL) {
		g_cnf.file.fd = STDOUT_FILENO;
	} else {
		g_cnf.file.fd = open(g_cnf.file.path, g_cnf.file.fd_oflag, S_IRUSR | S_IWUSR);
		if (g_cnf.file.fd < 0) {
			pr_err("couldn't open output file %s %d\n", g_cnf.file.path, g_cnf.file.fd);
			return g_cnf.file.fd;
		}
		fstat(g_cnf.file.fd, &statbuf);
		g_cnf.file.len = statbuf.st_size;
	}
	g_cnf.file.max_len = g_cnf.file.max_len << 10; /* KB to Byte */

	/* output id information */
	ret = write(g_cnf.file.fd, id_buf, strlen(id_buf));
	if (ret > 0)
		g_cnf.file.len += ret;

	return 0;
}

static void log_file_exit()
{
	if (g_cnf.file.path) {
		close(g_cnf.file.fd);
		g_cnf.file.fd = -1;
	}
}

static int save_log_file(char *buf, int len)
{
	int i = 0;
	int ret = 0;
	char file0[CHIP_LOG_PATH_LEN] = { 0 };
	char file1[CHIP_LOG_PATH_LEN] = { 0 };

	if (unlikely(!buf || len <= 0)) {
		pr_err("buf is null or len %d error\n", len);
		return -1;
	}

	/* if the file does not exist, create the file */
	if (unlikely(g_cnf.file.path && ((g_cnf.file.fd < 0) || (access(g_cnf.file.path, F_OK) == -1)))) {
		g_cnf.file.fd = open(g_cnf.file.path, g_cnf.file.fd_oflag, S_IRUSR | S_IWUSR);
		if (g_cnf.file.fd < 0) {
			pr_err("couldn't open output file %s %d\n", g_cnf.file.path, g_cnf.file.fd);
			return g_cnf.file.fd;
		}
		g_cnf.file.len = 0;

		ret = write(g_cnf.file.fd, id_buf, strlen(id_buf));
		if (ret > 0)
			g_cnf.file.len += ret;
	}

	ret = write(g_cnf.file.fd, buf, len);
	if (unlikely(ret != len)) {
		pr_err("output to %s %d\n", g_cnf.file.path, ret);
		close(g_cnf.file.fd);
		return ret;
	}
	g_cnf.file.len += ret;

	/* file size limit reached, dump file */
	if (unlikely(g_cnf.file.path && g_cnf.file.len >= g_cnf.file.max_len)) {
		close(g_cnf.file.fd);
		g_cnf.file.fd = -1;
		for (i = g_cnf.file.max_nums; i > 0; i--) {
			snprintf(file1, CHIP_LOG_PATH_LEN, "%s.%d", g_cnf.file.path, i);
			if (unlikely(i == 1)) {
				snprintf(file0, CHIP_LOG_PATH_LEN, "%s", g_cnf.file.path);
			} else {
				snprintf(file0, CHIP_LOG_PATH_LEN, "%s.%d", g_cnf.file.path, i - 1);
			}
			ret = rename(file0, file1);
			if (unlikely(ret < 0 && errno != ENOENT)) {
				pr_err("rotating log files error %d, %s to %s\n", ret, file0, file1);
			}
		}
	}
	return 0;
}

static void *save_file_thread(void *arg)
{
	while (!g_cnf.is_exit) {
		msg_dequeue(&g_cnf.queue);
		usleep(50000);
	}

	/* when exiting, if output to a file is synchronized */
	if (g_cnf.file.fd > 2) {
		fsync(g_cnf.file.fd);
	}

	pr_info("chip log save_file_thread exit\n");
	return NULL;
}

static void print_help(char *cmd)
{
	printf("Usage: %s [options]\n"
	       " --cpu  a b   cpu load    a:period query time(ms) b:period save time(ms)  default 1000 10000\n"
	       " --bpu  a b   bpu load    a:period query time(ms) b:period save time(ms)  default 1000 10000\n"
	       " --temp a b   junc temp   a:period query time(ms) b:period save time(ms)  default 1000 10000\n"
	       " --ddr  a b   ddr band    a:period query time(ms) b:period save time(ms)  default 1000 10000\n"
	       " --sleep a      a:sleep time(ms) default 1000\n"
	       " --temp_thres a a:temp save threshold default 115\n"
	       " --volume       volume production version default test version\n"
	       " --sched a b    a:priority b:0-SCHED_NORMAL 1-SCHED_FIFO 2-SCHED_RR default SCHED_NORMAL 0\n"
	       " --path  a      a:absolute path store directory default console\n"
	       " --file  a b    a:file max num b:file length(KB) default 100 2048KB\n",
	       cmd);
}

static inline void parse_value_check()
{
	if (g_cnf.sleep_period_ms <= 0)
		g_cnf.sleep_period_ms = DEFAULT_SLEEP_PERIOD_MS;

	if (g_cnf.query_cpu_load_period_ms < g_cnf.sleep_period_ms)
		g_cnf.query_cpu_load_period_ms = g_cnf.sleep_period_ms;
	if (g_cnf.save_cpu_load_period_ms < g_cnf.query_cpu_load_period_ms)
		g_cnf.save_cpu_load_period_ms = g_cnf.query_cpu_load_period_ms;

	if (g_cnf.query_bpu_load_period_ms < g_cnf.sleep_period_ms)
		g_cnf.query_bpu_load_period_ms = g_cnf.sleep_period_ms;
	if (g_cnf.save_bpu_load_period_ms < g_cnf.query_bpu_load_period_ms)
		g_cnf.save_bpu_load_period_ms = g_cnf.query_bpu_load_period_ms;

	if (g_cnf.query_temp_period_ms < g_cnf.sleep_period_ms)
		g_cnf.query_temp_period_ms = g_cnf.sleep_period_ms;
	if (g_cnf.save_temp_period_ms < g_cnf.query_temp_period_ms)
		g_cnf.save_temp_period_ms = g_cnf.query_temp_period_ms;

	if (g_cnf.query_ddr_band_period_ms < g_cnf.sleep_period_ms)
		g_cnf.query_ddr_band_period_ms = g_cnf.sleep_period_ms;
	if (g_cnf.save_ddr_band_period_ms < g_cnf.query_ddr_band_period_ms)
		g_cnf.save_ddr_band_period_ms = g_cnf.query_ddr_band_period_ms;

	if (g_cnf.file.max_nums <= 0)
		g_cnf.file.max_nums = DEFAULT_FILE_MAX_NUMS;
	if (g_cnf.file.max_len <= 0)
		g_cnf.file.max_len = DEFAULT_FILE_MAX_LEN;

	pr_info("working value: --cpu %d %d --bpu %d %d --temp %d %d --ddr %d %d --temp_thresh %.0f --path %s --file %d %d --sleep %d %s\n",
		g_cnf.query_cpu_load_period_ms, g_cnf.save_cpu_load_period_ms, g_cnf.query_bpu_load_period_ms,
		g_cnf.save_bpu_load_period_ms, g_cnf.query_temp_period_ms, g_cnf.save_temp_period_ms,
		g_cnf.query_ddr_band_period_ms, g_cnf.save_ddr_band_period_ms, g_cnf.temp_threshold, g_cnf.file.path,
		g_cnf.file.max_nums, g_cnf.file.max_len, g_cnf.sleep_period_ms, g_cnf.is_volume > 0 ? "--volume" : " ");
}

/* make sure to include only numbers */
static inline int isdigitstr1(char *str1, int num, int argc)
{
	return ((num + 1 < argc) && (strspn(str1, "-0123456789") == strlen(str1)));
}

static inline int isdigitstr2(char *str1, char *str2, int num, int argc)
{
	return ((num + 2 < argc) && (strspn(str1, "-0123456789") == strlen(str1)) &&
		(strspn(str2, "-0123456789") == strlen(str2)));
}

static int parse_cmd(int argc, char **argv)
{
	int i = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--cpu")) {
			if (isdigitstr2(argv[i + 1], argv[i + 2], i, argc)) {
				g_cnf.query_cpu_load_period_ms = atoi(argv[i + 1]);
				g_cnf.save_cpu_load_period_ms = atoi(argv[i + 2]);
				i += 2;
			} else {
				goto err;
			}
			continue;
		} else if (!strcmp(argv[i], "--bpu")) {
			if (isdigitstr2(argv[i + 1], argv[i + 2], i, argc)) {
				g_cnf.query_bpu_load_period_ms = atoi(argv[i + 1]);
				g_cnf.save_bpu_load_period_ms = atoi(argv[i + 2]);
				i += 2;
			} else {
				goto err;
			}
			continue;
		} else if (!strcmp(argv[i], "--temp")) {
			if (isdigitstr2(argv[i + 1], argv[i + 2], i, argc)) {
				g_cnf.query_temp_period_ms = atoi(argv[i + 1]);
				g_cnf.save_temp_period_ms = atoi(argv[i + 2]);
				i += 2;
			} else {
				goto err;
			}
			continue;
		} else if (!strcmp(argv[i], "--ddr")) {
			if (isdigitstr2(argv[i + 1], argv[i + 2], i, argc)) {
				g_cnf.query_ddr_band_period_ms = atoi(argv[i + 1]);
				g_cnf.save_ddr_band_period_ms = atoi(argv[i + 2]);
				i += 2;
			} else {
				goto err;
			}
			continue;
		} else if (!strcmp(argv[i], "--sched")) {
			if (isdigitstr2(argv[i + 1], argv[i + 2], i, argc)) {
				g_cnf.priority = atoi(argv[i + 1]);
				g_cnf.sched_police = atoi(argv[i + 2]);
				i += 2;
			} else {
				goto err;
			}
			continue;
		} else if (!strcmp(argv[i], "--file")) {
			if (isdigitstr2(argv[i + 1], argv[i + 2], i, argc)) {
				g_cnf.file.max_nums = atoi(argv[i + 1]);
				g_cnf.file.max_len = atoi(argv[i + 2]);
				i += 2;
			} else {
				goto err;
			}
			continue;
		} else if (!strcmp(argv[i], "--sleep")) {
			if (isdigitstr1(argv[i + 1], i, argc)) {
				g_cnf.sleep_period_ms = atoi(argv[i + 1]);
				i += 1;
			} else {
				goto err;
			}
			continue;
		} else if (!strcmp(argv[i], "--temp_thres")) {
			if (isdigitstr1(argv[i + 1], i, argc)) {
				g_cnf.temp_threshold = atoi(argv[i + 1]) * 1.0;
				i += 1;
			} else {
				goto err;
			}
			continue;
		} else if (!strcmp(argv[i], "--path")) {
			if (i + 1 < argc) {
				g_cnf.file.path = argv[i + 1];
				i += 1;
			} else {
				goto err;
			}
			continue;
		} else if (!strcmp(argv[i], "--volume")) {
			g_cnf.is_volume = 1;
			continue;
		} else {
			print_help(argv[0]);
			return -1;
		}
	}

	parse_value_check();

	return 0;

err:
	pr_err("%s not invalid param\n", argv[i]);
	print_help(argv[0]);
	return -1;
}

static void set_default_value()
{
	int i = 0;

	/* file config */
	g_cnf.file.path = NULL;
	g_cnf.file.max_nums = DEFAULT_FILE_MAX_NUMS;
	g_cnf.file.max_len = DEFAULT_FILE_MAX_LEN; /* KB */
	g_cnf.file.fd = -1;
	g_cnf.file.fd_oflag = O_WRONLY | O_APPEND | O_CREAT;

	g_cnf.temp_threshold = DEFAULT_TEMP_THRESHOLD;

	/* query data period ms */
	g_cnf.sleep_period_ms = DEFAULT_SLEEP_PERIOD_MS;
	g_cnf.query_cpu_load_period_ms = DEFAULT_QUERY_LOAD_PERIOD_MS;
	g_cnf.query_bpu_load_period_ms = DEFAULT_QUERY_LOAD_PERIOD_MS;
	g_cnf.query_temp_period_ms = DEFAULT_QUERY_LOAD_PERIOD_MS;
	g_cnf.query_ddr_band_period_ms = DEFAULT_QUERY_LOAD_PERIOD_MS;

	/* save to file period ms */
	g_cnf.save_cpu_load_period_ms = DEFAULT_SAVE_LOAD_PERIOD_MS;
	g_cnf.save_bpu_load_period_ms = DEFAULT_SAVE_LOAD_PERIOD_MS;
	g_cnf.save_temp_period_ms = DEFAULT_SAVE_LOAD_PERIOD_MS;
	g_cnf.save_ddr_band_period_ms = DEFAULT_SAVE_LOAD_PERIOD_MS;

	/* init max and min values */
	g_cnf.cpu_load.max = FLT_MIN;
	g_cnf.cpu_load.min = FLT_MAX;
	g_cnf.temp.max = FLT_MIN;
	g_cnf.temp.min = FLT_MAX;
	for (i = 0; i < BPU_NUM_MAX; i++) {
		g_cnf.bpu_load[i].max = FLT_MIN;
		g_cnf.bpu_load[i].min = FLT_MAX;
	}
	for (i = 0; i < DDR_PORT_NUM_MAX; i++) {
		g_cnf.ddr_band[i].w_max = 0;
		g_cnf.ddr_band[i].w_min = UINT_MAX;
		g_cnf.ddr_band[i].r_max = 0;
		g_cnf.ddr_band[i].r_min = UINT_MAX;
	}

	pr_info("default value: --cpu %d %d --bpu %d %d --temp %d %d --ddr %d %d --temp_thresh %.0f --path %s --file %d %d --sleep %d %s\n",
		g_cnf.query_cpu_load_period_ms, g_cnf.save_cpu_load_period_ms, g_cnf.query_bpu_load_period_ms,
		g_cnf.save_bpu_load_period_ms, g_cnf.query_temp_period_ms, g_cnf.save_temp_period_ms,
		g_cnf.query_ddr_band_period_ms, g_cnf.save_ddr_band_period_ms, g_cnf.temp_threshold, g_cnf.file.path,
		g_cnf.file.max_nums, g_cnf.file.max_len, g_cnf.sleep_period_ms,
		g_cnf.is_volume > 0 ? "--volume " : " ");
}

static void signal_handle(int sig)
{
	pr_info("signal %d is received\n", sig);
	g_cnf.is_exit = 1;
}

static void main_loop()
{
	int ret = 0;
	int offset = 0;
	uint64_t loop_cnt = 1;
	uint32_t temp_over_thres_cnt = 1; /* for to normal temperature branch */

	uint32_t query_cpu_load_cnt = 0;
	uint32_t save_cpu_load_cnt = 0;
	uint32_t query_cpu_load_cnt_backup = 0;
	uint32_t save_cpu_load_cnt_backup = 0;

	uint32_t query_bpu_load_cnt = 0;
	uint32_t save_bpu_load_cnt = 0;
	uint32_t query_bpu_load_cnt_backup = 0;
	uint32_t save_bpu_load_cnt_backup = 0;

	uint32_t query_ddr_band_cnt = 0;
	uint32_t query_ddr_band_cnt_backup = 0;
	uint32_t save_ddr_band_cnt = 0;
	uint32_t save_ddr_band_cnt_backup = 0;

	uint32_t query_temp_cnt = 0;
	uint32_t save_temp_cnt = 0;
	uint32_t save_temp_cnt_backup = 0;

	query_cpu_load_cnt_backup = query_cpu_load_cnt = g_cnf.query_cpu_load_period_ms / g_cnf.sleep_period_ms;
	query_bpu_load_cnt_backup = query_bpu_load_cnt = g_cnf.query_bpu_load_period_ms / g_cnf.sleep_period_ms;
	query_ddr_band_cnt_backup = query_ddr_band_cnt = g_cnf.query_ddr_band_period_ms / g_cnf.sleep_period_ms;
	query_temp_cnt = g_cnf.query_temp_period_ms / g_cnf.sleep_period_ms;

	save_cpu_load_cnt_backup = save_cpu_load_cnt = g_cnf.save_cpu_load_period_ms / g_cnf.sleep_period_ms;
	save_bpu_load_cnt_backup = save_bpu_load_cnt = g_cnf.save_bpu_load_period_ms / g_cnf.sleep_period_ms;
	save_ddr_band_cnt_backup = save_ddr_band_cnt = g_cnf.save_ddr_band_period_ms / g_cnf.sleep_period_ms;
	save_temp_cnt_backup = save_temp_cnt = g_cnf.save_temp_period_ms / g_cnf.sleep_period_ms;

	pr_info("cpu_cnt[%d,%d] bpu_cnt[%d,%d] ddr_cnt[%d,%d] temp_cnt[%d,%d]\n", query_cpu_load_cnt, save_cpu_load_cnt,
		query_bpu_load_cnt, save_bpu_load_cnt, query_ddr_band_cnt, save_ddr_band_cnt, query_temp_cnt,
		save_temp_cnt);

	while (!g_cnf.is_exit) {
		/* volume production mode, data is obtained only when the alarm is generated, reducing cpu consumption */
		if (g_cnf.is_volume) {
			if (g_cnf.temp.cur >= g_cnf.temp_threshold) { /* abnormal temperature */
				/* In the first stage, the save frequency is the query frequency */
				if (temp_over_thres_cnt == 0) {
					basic_data_init();
					query_cpu_load_cnt = query_cpu_load_cnt_backup;
					query_bpu_load_cnt = query_bpu_load_cnt_backup;
					query_ddr_band_cnt = query_ddr_band_cnt_backup;
					save_cpu_load_cnt = query_cpu_load_cnt;
					save_bpu_load_cnt = query_bpu_load_cnt;
					save_ddr_band_cnt = query_ddr_band_cnt;
					save_temp_cnt = save_cpu_load_cnt; /* use the cpu count as the temp count */
					loop_cnt = 1;
				}
				/* In the second stage, the save frequency is the save frequency */
				if (temp_over_thres_cnt == VOLUME_PRODUCTION_OVER_TEMP_SAVE_CNT) {
					save_cpu_load_cnt = save_cpu_load_cnt_backup >> 1; /* default 5s */
					save_bpu_load_cnt = save_bpu_load_cnt_backup >> 1;
					save_ddr_band_cnt = save_ddr_band_cnt_backup >> 1;
					save_temp_cnt = save_cpu_load_cnt; /* use the cpu count as the temp count */
				}
				temp_over_thres_cnt++;
			} else { /* normal temperature */
				if (unlikely(temp_over_thres_cnt != 0)) {
					temp_over_thres_cnt = 0;
					query_cpu_load_cnt = UINT_MAX;
					query_bpu_load_cnt = UINT_MAX;
					query_ddr_band_cnt = UINT_MAX;
					save_cpu_load_cnt = UINT_MAX;
					save_bpu_load_cnt = UINT_MAX;
					save_ddr_band_cnt = UINT_MAX;
					save_temp_cnt = save_temp_cnt_backup;
					loop_cnt = 1;
					basic_data_exit();
				}
			}
		}

		if (loop_cnt % query_temp_cnt == 0) {
			ret = get_temp();
			if (likely(ret == 0)) {
				if (loop_cnt % save_temp_cnt == 0)
					offset += gener_temp_buf(all_buf, offset);
			}
		}

		if (loop_cnt % query_bpu_load_cnt == 0) {
			ret = get_bpu_load();
			if (likely(ret == 0)) {
				if (loop_cnt % save_bpu_load_cnt == 0)
					offset += gener_bpu_load_buf(all_buf, offset);
			}
		}

		if (loop_cnt % query_cpu_load_cnt == 0) {
			ret = get_cpu_load();
			if (likely(ret == 0)) {
				if (loop_cnt % save_cpu_load_cnt == 0)
					offset += gener_cpu_load_buf(all_buf, offset);
			}
		}

		if (loop_cnt % query_ddr_band_cnt == 0) {
			ret = get_ddr_band();
			if (likely(ret == 0)) {
				if (loop_cnt % save_ddr_band_cnt == 0)
					offset += gener_ddr_band_buf(all_buf, offset);
			}
		}

		if (offset > 0) {
			ret = msg_enqueue(&g_cnf.queue, 0, offset, all_buf);
			if (unlikely(ret)) {
				pr_err("msg_enqueue error, length=%d, ret=%d\n", offset, ret);
			}
			offset = 0;
		}

		loop_cnt++;
		usleep(g_cnf.sleep_period_ms * 1000);
	}
}

int main(int argc, char **argv)
{
	struct sched_param param = { 0 };
	int ret = 0;

	set_default_value();
	if (parse_cmd(argc, argv) < 0)
		return -1;

	/* get the number of resources, open fd, request memory */
	if (basic_info_init() < 0)
		return -1;

	if (msg_queue_init(&g_cnf.queue, QUEUE_NUMS) < 0)
		goto err_basic_exit;

	get_id();

	if (log_file_init() < 0)
		goto err_queue_exit;

	if (pthread_create(&g_cnf.save_file_thread_id, NULL, save_file_thread, NULL) < 0) {
		pr_err("pthread_create error\n");
		goto err_file_exit;
	}

	param.sched_priority = g_cnf.priority;
	ret = sched_setscheduler(syscall(SYS_gettid), g_cnf.sched_police, &param);
	if (ret)
		pr_err("sched_setscheduler police=%d, priority=%d, ret=%d\n", g_cnf.sched_police, g_cnf.priority, ret);

	signal(SIGINT, signal_handle);
	signal(SIGTERM, signal_handle);

	/* road test mode, directly initialize the data */
	if (!g_cnf.is_volume)
		basic_data_init();

	main_loop();

	pthread_join(g_cnf.save_file_thread_id, NULL);

	basic_data_exit();
err_file_exit:
	log_file_exit();
err_queue_exit:
	msg_queue_exit(&g_cnf.queue);
err_basic_exit:
	basic_info_exit();

	pr_info("chip log main exit\n");
	return -1;
}
