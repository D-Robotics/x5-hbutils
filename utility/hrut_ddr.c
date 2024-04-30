/*
 *    COPYRIGHT NOTICE
 *    Copyright 2021 Horizon Robotics, Inc.
 *    All rights reserved.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
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
#include <sys/ioctl.h>
#include <asm/types.h>
#include <pthread.h>
#include <getopt.h>
#include <ctype.h>

#define MONITOR_DEV_PATH_X2	 "/dev/x2_ddrmonitor"
#define MONITOR_DEV_PATH_XJ3 "/dev/ddrmonitor"
#define MONITOR_DEV_PATH_X5 "/dev/axi_mon"
#define DDR_TYPE_PATH_XJ3    "/sys/class/socinfo/ddr_type"
#define MONITOR_MEMSIZE     (1024 * 400)
#define DDR_MONITOR_SAMPLE_CONFIG_SET   _IOW('m', 3, ddr_monitor_sample)
#define MEM_SIZE 100
#define BOARD_X2  1
#define BOARD_XJ3 2
#define BOARD_X5  3

#define CONFIG_HOBOT_X5
#ifdef CONFIG_HOBOT_XJ3
#define PORT_NUM 8
#elif defined(CONFIG_HOBOT_X5)
#define PORT_NUM 5
#else
#define PORT_NUM 6
#endif

#define MAX_REC_NUM 400

enum ddr_type {
	XJ3_DDR_TYPE_LPDDR4 = 1,
	XJ3_DDR_TYPE_DDR4 = 3,
	XJ3_DDR_TYPE_MAX,
};

char *xj3_ddr_type[XJ3_DDR_TYPE_MAX] = {
	"",
	"LPDDR4",
	"",
	"DDR4",
};

struct ddr_portdata {
	uint32_t wdata_num;
	uint32_t rdata_num;
#ifdef CONFIG_HOBOT_XJ3
	uint32_t waddr_num;
	uint32_t waddr_cyc;
	uint32_t waddr_latency;
	uint32_t raddr_num;
	uint32_t raddr_cyc;
	uint32_t raddr_latency;
	uint32_t max_rtrans_cnt;
	uint32_t max_rvalid_cnt;
	uint32_t acc_rtrans_cnt;
	uint32_t min_rtrans_cnt;

	uint32_t max_wtrans_cnt;
	uint32_t max_wready_cnt;
	uint32_t acc_wtrans_cnt;
	uint32_t min_wtrans_cnt;
#else
	uint32_t rd_max_latency;
	uint32_t rd_min_latency;
	uint32_t wr_max_latency;
	uint32_t wr_min_latency;
	uint32_t rd_time_total;
	uint32_t wr_time_total;
	uint32_t rd_max_ost;
	uint32_t wr_max_ost;
	uint32_t rd_stop_ost;
	uint32_t wr_stop_ost;
#endif
};

struct ddr_monitor_data {
    unsigned long long curtime;
    struct ddr_portdata portdata[PORT_NUM];
	uint32_t rd_cmd_num;
	uint32_t wr_cmd_num;
#ifdef CONFIG_HOBOT_XJ3
	uint32_t mwr_cmd_num;
	uint32_t rdwr_swi_num;
	uint32_t war_haz_num;
	uint32_t raw_haz_num;
	uint32_t waw_haz_num;
	uint32_t act_cmd_num;
	uint32_t act_cmd_rd_num;
	uint32_t per_cmd_num;
	uint32_t per_cmd_rdwr_num;
#endif
	uint32_t real_period;
};

struct ddr_monitor_data *ddr_info;

typedef struct ddr_monitor_sample_s {
	uint32_t sample_period;
	uint32_t sample_size;
} ddr_monitor_sample;

typedef struct cmd_info_s {
	uint32_t smaple_type;
	uint32_t sample_period;
} cmd_info;

static const char short_options[] = "t:p:hr";
static const struct option long_options[] = {
					{"type", required_argument, NULL, 't'},
					{"period", required_argument, NULL, 'p'},
					{"help", no_argument, NULL, 'h'},
					{"raw", no_argument, NULL, 'r'},
					{0, 0, 0, 0}};

static char *port_type[10] = {"CPU", "BIF", "BPU0", "BPU1", "VIO0",
				   "null", "null", "null", "null", "null"};

static ddr_monitor_sample sample;
pthread_t g_procthread;
char * g_pCurr = NULL;

int ddrmon_fd;
int g_sig = 0;
int board_type = 0;
int rd_cmd_bytes = 64;
int wr_cmd_bytes = 64;
int ddr_type;
int show_port_id;
int print_raw;
int print_raw_extra = 1;

static int get_ddr_type(void)
{
	int fd;
	char buf[8];
	int typeid;
	ssize_t ret;

	fd = open(DDR_TYPE_PATH_XJ3, O_RDONLY);
	if (fd < 0) {
		perror("failed open ddr type:");
		return -1;
	}

	ret = read(fd, buf, sizeof(buf));
	if (ret < 0) {
		close(fd);
		perror("failed to read ddr_type:");
		return -1;
	}

	buf[1] = 0;
	typeid = atoi(buf);

	if (typeid >= XJ3_DDR_TYPE_MAX) {
		close(fd);
		printf("ddr type id %d is not supported.\n", typeid);
		return -1;
	}

	printf("DDR type is %s\n", xj3_ddr_type[typeid]);
	close(fd);

	return typeid;
}

static int find_board_type(void)
{
	int ret = 1;
	struct stat buf;

	ret = stat(MONITOR_DEV_PATH_X2, &buf);
	if (ret == 0)
		return BOARD_X2;
	ret = stat(MONITOR_DEV_PATH_XJ3, &buf);
	if (ret == 0)
		return BOARD_XJ3;
	ret = stat(MONITOR_DEV_PATH_X5, &buf);
	if (ret == 0)
		return BOARD_X5;
	return 0;
}

static int parse_period_arg(uint32_t value)
{
	int ret = 0;
	int res = 1;

	if (value == 0) {
		value = 10;
	} else if (value > 1000) {
		printf("\nMax sample period is 1000ms.\n\n");
		return -1;
	}

	if (1000 % value)
		printf("Period will be rounded for precision\n");

	res = 1000 / value;
	value = 1000 / res;

	sample.sample_size = 1000 / value;
	sample.sample_period = value;

	printf("Set sample period:%dms, %d samples per seconds\n",
		sample.sample_period, sample.sample_size);

	return ret;
}

static int set_port_id(const char *src)
{
	int i = 0;
	char **type = port_type;

	if (board_type == BOARD_X2) {
		type[5] = "OTHER";
		type[6] = "SUM";
		type[7] = "ALL";
	} else if (board_type == BOARD_XJ3) {
		type[5] = "VPU";
		type[6] = "VIO1";
		type[7] = "PERI";
		type[8] = "SUM";
		type[9] = "ALL";
	}  else if (board_type == BOARD_X5) {
		type[0] = "CPU";
		type[1] = "BPU";
		type[2] = "VIN";
		type[3] = "CODEC";
		type[4] = "GPU";
		type[5] = "SUM";
		type[6] = "ALL";
	} else {
		printf("board type error :%d\n", board_type);
		return -1;
	}

	if (!src)
		return -1;

	for (i = 0; i < PORT_NUM + 2; i++) {
		if (!strcmp(type[i], src)) {
			show_port_id = i;
			return 0;
		}
	}

	printf("please input one of below port name:");
	printf("    cpu/bif/bpu0/bpu1/vio0/vpu/vio1/peri/sum/all\n");
	return -1;
}

static void signal_handle(int sig)
{
	g_sig = 1;
}

static int show_bw_info(long num)
{
	int i;
	int cur = 0;
	int length = 0;
	unsigned long read_bw = 0;
	unsigned long write_bw = 0;
	unsigned long bw_sum = 0;

	if (!ddr_info) {
		printf("%s ddr_info is NULL\n", __func__);
		printf("ddr_monitor not started \n");
		return 0;
	}

	for (cur = 0; cur < num; cur++) {
		printf("\nTime %.3fs       ", (double)ddr_info[cur].curtime / 1000.0);
		if (show_port_id >= PORT_NUM) {
#ifndef CONFIG_HOBOT_X5
			printf("\nMB/S   P0:CPU   P1:BIF  P2:BPU0  P3:BPU1  P4:VIO0   P5:VPU  P6:VIO1  P7:Peri      SUM    CMD_SUM\n");
#else
			printf("\nMB/S   P0:CPU   P1:BPU  P2:VIN  P3:CODEC  P4:GPU      SUM\n");
#endif
		} else  {
			printf("P%d:%s     SUM  MB/S", show_port_id, port_type[show_port_id]);
			printf("\n             ");
		}
		printf("Read :   ");
		for (i = 0; i < PORT_NUM; i++) {
#ifndef CONFIG_HOBOT_X5
			if (ddr_info[cur].portdata[i].raddr_num) {
				read_bw = ((unsigned long) ddr_info[cur].portdata[i].rdata_num) *
						  16 * (1000 / sample.sample_period) >> 20;
				if (show_port_id >= PORT_NUM || show_port_id == i)
					printf("%4lu     ", read_bw);
				bw_sum += read_bw;
			} else {
				if (show_port_id >= PORT_NUM || show_port_id == i)
					printf("%4u     ", 0);
			}
#else
			read_bw = ((unsigned long) ddr_info[cur].portdata[i].rdata_num) *
					(1000 / ddr_info[cur].real_period) >> 20;
			if (show_port_id >= PORT_NUM || show_port_id == i)
					printf("%4lu     ", read_bw);
			bw_sum += read_bw;
#endif
		}
		printf("%4lu     ", bw_sum);
		bw_sum = 0;
#ifndef CONFIG_HOBOT_X5
		read_bw = ((unsigned long) ddr_info[cur].rd_cmd_num) *
				  rd_cmd_bytes * (1000 / sample.sample_period) >> 20;
		if (show_port_id >= PORT_NUM)
			printf("%4lu", read_bw);
#endif
		printf("\n");
		if (show_port_id < PORT_NUM)
			printf("\n             ");
		printf("Write:   ");
		for (i = 0; i < PORT_NUM; i++) {
#ifndef CONFIG_HOBOT_X5
			if (ddr_info[cur].portdata[i].waddr_num) {
				write_bw = ((unsigned long) ddr_info[cur].portdata[i].wdata_num) *
							16 * (1000 / sample.sample_period) >> 20;
				if (show_port_id >= PORT_NUM || show_port_id == i)
					printf("%4lu     ", write_bw);
				bw_sum += write_bw;
			} else {
				if (show_port_id >= PORT_NUM || show_port_id == i)
					printf("%4u     ", 0);
			}
#else
			write_bw = ((unsigned long) ddr_info[cur].portdata[i].wdata_num) *
					(1000 / ddr_info[cur].real_period) >> 20;
			if (show_port_id >= PORT_NUM || show_port_id == i)
					printf("%4lu     ", write_bw);
			bw_sum += write_bw;
#endif
		}
		printf("%4lu     ", bw_sum);
		bw_sum = 0;
#ifndef CONFIG_HOBOT_X5
		write_bw = ((unsigned long) ddr_info[cur].wr_cmd_num) *
					wr_cmd_bytes * (1000 / sample.sample_period) >> 20;
		if (show_port_id >= PORT_NUM)
		printf("%4lu\n", write_bw);
#endif
		printf("\n");
	}
	return length;
}

static int show_raw_info(long num)
{
	int i;
	int cur = 0;
	int length = 0;
	unsigned long read_bw = 0;
	unsigned long write_bw = 0;
#ifndef CONFIG_HOBOT_X5
	unsigned long mask_bw = 0;
#endif

	if (!ddr_info) {
		printf("%s ddr_info is NULL\n", __func__);
		printf("ddr_monitor not started \n");
		return 0;
	}

	for (cur = 0; cur < num; cur++) {
		printf("Time %llu ", ddr_info[cur].curtime);
		printf("\nRead : ");
		for (i = 0; i < PORT_NUM; i++) {
#ifndef CONFIG_HOBOT_X5
			if (ddr_info[cur].portdata[i].raddr_num) {
				read_bw = ((unsigned long) ddr_info[cur].portdata[i].rdata_num) *
						  16 * (1000 / sample.sample_period) >> 20;
#else
				read_bw = ((unsigned long) ddr_info[cur].portdata[i].rdata_num) *
						  (1000 / ddr_info[cur].real_period) >> 20;
				printf("\n        p[%d](bw:%6lu cmd_num:%6u max_lantency:%6u min_lantency:%6u rd_cycle:%6u rd_stop_ost:%6u) ", i, \
						read_bw, \
						ddr_info[cur].rd_cmd_num, \
						ddr_info[cur].portdata[i].rd_max_latency, \
						ddr_info[cur].portdata[i].rd_min_latency, \
						ddr_info[cur].portdata[i].rd_time_total, \
						ddr_info[cur].portdata[i].rd_stop_ost);
#endif

#ifdef CONFIG_HOBOT_XJ3
				printf("\n        p[%d](bw:%6lu stall:%6u delay:%6u) ", i, \
						read_bw, \
						ddr_info[cur].portdata[i].raddr_cyc / ddr_info[cur].portdata[i].raddr_num, \
						ddr_info[cur].portdata[i].raddr_latency / ddr_info[cur].portdata[i].raddr_num);
				if (print_raw_extra) {
					printf("(maxRTrans:%u maxRvalid:%u accRtrans:%u minRtrans:%u) ", \
							ddr_info[cur].portdata[i].max_rtrans_cnt, \
							ddr_info[cur].portdata[i].max_rvalid_cnt, \
							ddr_info[cur].portdata[i].acc_rtrans_cnt / ddr_info[cur].portdata[i].waddr_num, \
							ddr_info[cur].portdata[i].min_rtrans_cnt);
				}
			}
			else {
				printf("\n        p[%d](bw:%6u stall:%6u delay:%6u) ", i, 0, 0, 0);
			}
#endif
		}
#ifndef CONFIG_HOBOT_X5
		read_bw = ((unsigned long) ddr_info[cur].rd_cmd_num) *
				  rd_cmd_bytes * (1000 / sample.sample_period) >> 20;
		printf("\n        ddrc:%6lu MB/s;\n", read_bw);
#endif
		printf("\nWrite: ");
		for (i = 0; i < PORT_NUM; i++) {
#ifndef CONFIG_HOBOT_X5
			if (ddr_info[cur].portdata[i].waddr_num) {
				write_bw = ((unsigned long) ddr_info[cur].portdata[i].wdata_num) *
							16 * (1000 / sample.sample_period) >> 20;
#else
				write_bw = ((unsigned long) ddr_info[cur].portdata[i].wdata_num) *
							(1000 / ddr_info[cur].real_period) >> 20;
				printf("\n        p[%d](bw:%6lu cmd_num:%6u max_lantency:%6u min_lantency:%6u rd_cycle:%6u rd_stop_ost:%6u) ", i, \
						write_bw, \
						ddr_info[cur].wr_cmd_num, \
						ddr_info[cur].portdata[i].wr_max_latency, \
						ddr_info[cur].portdata[i].wr_min_latency, \
						ddr_info[cur].portdata[i].wr_time_total, \
						ddr_info[cur].portdata[i].wr_stop_ost);
#endif

#ifdef CONFIG_HOBOT_XJ3
				printf("\n        p[%d](bw:%6lu stall:%6u delay:%6u) ", i, \
								write_bw, \
						ddr_info[cur].portdata[i].waddr_cyc / ddr_info[cur].portdata[i].waddr_num, \
						ddr_info[cur].portdata[i].waddr_latency / ddr_info[cur].portdata[i].waddr_num);
				if (print_raw_extra) {
					printf("(maxWTrans:%u maxWready:%u accWtrans:%u minWtrans:%u) ", \
							ddr_info[cur].portdata[i].max_wtrans_cnt, \
							ddr_info[cur].portdata[i].max_wready_cnt, \
							ddr_info[cur].portdata[i].acc_wtrans_cnt / ddr_info[cur].portdata[i].waddr_num, \
							ddr_info[cur].portdata[i].min_wtrans_cnt);
				}
			} else {
				printf("\n        p[%d](bw:%6u stall:%6u delay:%6u) ", i, 0, 0, 0);
			}
#endif
		}
#ifndef CONFIG_HOBOT_X5
		write_bw = ((unsigned long) ddr_info[cur].wr_cmd_num) *
					wr_cmd_bytes * (1000 / sample.sample_period) >> 20;
		mask_bw = ((unsigned int) ddr_info[cur].mwr_cmd_num) *
					wr_cmd_bytes * (1000 / sample.sample_period) >> 20;
		printf("\n        ddrc %6lu MB/s, mask %6lu MB/s\n", write_bw, mask_bw);
#endif
		printf("\n");
	}
	return length;
}


static void *proc_thread(void *arg)
{
	ssize_t ret = 0;
	int ddr_info_sz = MAX_REC_NUM * sizeof(*ddr_info);

	/* TODO add ioctl read to get max record num */
	ddr_info = malloc(ddr_info_sz);
	if (!ddr_info)
		return NULL;

	while (ddr_info && !g_sig) {
		ret = read(ddrmon_fd, ddr_info, ddr_info_sz);
		if (ret > 0 && ret <= ddr_info_sz) {
			if (!print_raw)
				show_bw_info(ret / sizeof(*ddr_info));
			else
				show_raw_info(ret / sizeof(*ddr_info));
		} else {
			printf("ret:%ld\n", ret);
			return NULL;
		}
	}

	free(ddr_info);

	return NULL;
}

int main(int argc, char *argv[])
{
	int cmd_parser_ret = 0;
	cmd_info cmd;
	cmd.smaple_type = 0;
	cmd.sample_period = 0;
	int ret = 0;
	char *port_type = NULL;
	int i;

	if (argc < 2) {
		printf(">>> -h for help\n");
		return -1;
	}

	board_type = find_board_type();
	while ((cmd_parser_ret = getopt_long(argc, argv, short_options,
			long_options, NULL)) != -1) {
		switch (cmd_parser_ret) {
			case 't':
				port_type = optarg;
				for (i = 0; i < 8 && port_type[i]; i++) {
					port_type[i] = (char)toupper(port_type[i]);
				}
				printf("port_type:%s\n", port_type);
				break;

			case 'p':
				cmd.sample_period = (atoi(optarg) < 0) ? 0 : atoi(optarg);
				break;
			case 'r':
				print_raw = 1;
				break;

			case 'h':
				printf("DDR MONITOR HELP INFORMATION\n");
				printf(">>> -t/--type");

				if (board_type == BOARD_X2) {
					printf("   sample type: cpu,bif,bpu0,bpu1,vio,other,sum,all\n");
				} else if (board_type == BOARD_XJ3) {
					printf("   sample type: cpu,bif,bpu0,");
					printf("bpu1,vio0,vpu,vio1,peri,sum,all\n");
				}  else if (board_type == BOARD_X5) {
					printf("   sample type: cpu,bpu,vin,");
					printf("codec,gpu,sum,all\n");
				} else {
					printf("can not find correct board_type\n");
				}

			    printf(">>> -p/--period sample period: [1-1000] ms\n");
			    printf(">>> -r/--raw    print raw data\n");
				return 0;
			}
	}

	/* chage cmd bytes to 32 for ddr4, use 64 for lpddr4 */
	ddr_type = get_ddr_type();
	if (ddr_type == XJ3_DDR_TYPE_DDR4) {
		rd_cmd_bytes = 32;
		wr_cmd_bytes = 32;
	}

	if (set_port_id(port_type) < 0)
		return -1;

	ret = parse_period_arg(cmd.sample_period);
	if (ret == -1)
		return -1;

	if (board_type == BOARD_X2) {
		ddrmon_fd = open(MONITOR_DEV_PATH_X2, O_RDWR | O_NONBLOCK, 0);
		if (!ddrmon_fd) {
			goto finish;
		}
	} else if (board_type == BOARD_XJ3) {
		ddrmon_fd = open(MONITOR_DEV_PATH_XJ3, O_RDWR | O_NONBLOCK, 0);
		if (!ddrmon_fd) {
			goto finish;
		}
	}  else if (board_type == BOARD_X5) {
		ddrmon_fd = open(MONITOR_DEV_PATH_X5, O_RDWR | O_NONBLOCK, 0);
		if (!ddrmon_fd) {
			goto finish;
		}
	} else {
		printf("can not find correct board\n");
		return -1;
	}
	ioctl(ddrmon_fd, DDR_MONITOR_SAMPLE_CONFIG_SET, &sample);
	signal(SIGINT, signal_handle);
	if ( 0 > pthread_create(&g_procthread, NULL, proc_thread, &cmd.smaple_type) )
		goto finish;

	pthread_join(g_procthread, NULL);
finish:
	if (ddrmon_fd)
		close(ddrmon_fd);
	return 0;
}
