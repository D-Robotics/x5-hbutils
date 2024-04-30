/*
 *    COPYRIGHT NOTICE
 *    Copyright 2020 Horizon Robotics, Inc.
 *    All rights reserved.
 */
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define MOVEUP(x) printf("\033[%dA", (x))
#define MOVEDOWN(x) printf("\033[%dB", (x))
static const char short_options[] = "b:p:c:r:f:te:h";
static const struct option long_options[] = {{"bpu", required_argument, NULL, 'b'},
                                             {"power", required_argument, NULL, 'p'},
                                             {"clock", required_argument, NULL, 'm'},
                                             {"frq", required_argument, NULL, 'f'},
                                             {"ratio", required_argument, NULL, 'r'},
                                             {"time", no_argument, NULL, 't'},
                                             {"enabletime", required_argument, NULL, 'e'},
                                             {"help", no_argument, NULL, 'h'},
                                             {0, 0, 0, 0}};
struct cmd_info {
	int  bpu_core;
	char *bpu_power;
	char  *bpu_clock;
	char *bpu_time_en;
	int  bpu_ratio;
	int  bpu_time;
	char *bpu_frq;
};

static int set_clock(int bpu_id,char *buf)
{
	int clock_fd;
	int ret;

	clock_fd = open("/sys/devices/system/bpu/bpu0/clock_enable", O_RDWR, 0664);
	if (clock_fd < 0)
		clock_fd = open("/sys/devices/system/bpu/bpu0/power_enable", O_RDWR, 0664);

	if (clock_fd == -1) {
		printf("clock_fd open error\n");
		return -1;
	}

	ret = write(clock_fd, buf, 1);
		if (ret < 1) {
		printf("set bpu[%d] clock failure\n",bpu_id);
	}

	char buf_clk = 0;
	ret = lseek(clock_fd, 0, SEEK_SET);
	if (ret < 0) {
		printf("lseek bpu[%d] clock status error\n", bpu_id);
		close(clock_fd);
		return -1;
	}

	ret = read(clock_fd, &buf_clk, 1);
	if (ret < 0) {
		printf("read bpu[%d] clock status error\n",bpu_id);
		close(clock_fd);
		return -1;
	}

	if (atoi(&buf_clk) == 0)
		printf("bpu[%d] clock is disable\n",bpu_id);
	else if (atoi(&buf_clk) == 1)
		printf("bpu[%d] clock is enable\n",bpu_id);
	close(clock_fd);
	return 0;
}

static int set_power(int bpu_id,char *buf)
{
	int power_fd;
	int ret;
	printf("set bpu[%d] power\n",bpu_id);
	if (bpu_id)
		power_fd = open("/sys/devices/system/bpu/bpu1/power_enable", O_RDWR, 0664);
	else
		power_fd = open("/sys/devices/system/bpu/bpu0/power_enable", O_RDWR, 0664);
	if(power_fd == -1) {
		printf("clock_fd open error\n");
		return -1;
	}
	ret = write(power_fd, buf, 1);
	if (ret < 1) {
		close(power_fd);
		printf("set bpu[%d] power failure\n",bpu_id);
		return -1;
	}
	close(power_fd);
	return 0;
}

static int set_fc_time (char *fc_enable)
{
	int setfc_fd;
	int ret;

	setfc_fd = open("/sys/devices/system/bpu/fc_time_enable", O_RDWR, 0664);
	if(setfc_fd  == -1) {
		return -1;
	}

	ret = write(setfc_fd, fc_enable, strlen(fc_enable));
	if (ret < strlen(fc_enable)) {
		printf("set fc time error\n");
		close(setfc_fd);
		return -1;
	}
	close(setfc_fd);
	return 0;
}

static int get_fc_time(int bpu_id)
{
	int fctime_fd;
	int ret;
	char buf[4096] = {0};
	if (bpu_id)
		fctime_fd = open("/sys/devices/system/bpu/bpu1/fc_time", O_RDONLY);
	else
		fctime_fd = open("/sys/devices/system/bpu/bpu0/fc_time", O_RDONLY);


	if(fctime_fd == -1) {
		printf("fctime_fd0 open error\n");
		return -1;
	}
	ret = read(fctime_fd, buf, 4096);
	if (ret < 1) {
		printf("read bpu[%d] fc time failed\n",bpu_id);
	}
	close(fctime_fd);
	printf("%s",buf);
	return 0;
}
static int get_ratio(int queue_fd, int ratio_fd, int core_index)
{
		char buf[4096] = {0};
		char tmp[100] = {0};
		int ret;
		sprintf(tmp,"%d", core_index);
		lseek(ratio_fd, 0, SEEK_SET);
		ret = read(ratio_fd, buf, 4096);
		int ratio = atoi(buf);
		if (ret < 1) {
			printf("read bpu ratio failed\n");
		}
		//close(ratio_fd0);
		sprintf(tmp + strlen(tmp), "%7d%%",ratio);
		//printf("ratio0:%s\n",buf);

		memset(buf, 0, 4096);
		lseek(queue_fd, 0, SEEK_SET);

		ret = read(queue_fd, buf, 4096);
		if (ret < 1) {
			printf("read bpu queue failed\n");
		}
		int queue = atoi(buf);
		//printf("queue0:%s\n",buf);
		sprintf(tmp + strlen(tmp), "%11d",queue);

		printf("%s\r", tmp);
		fflush(stdout);
		return 0;
}

static char *top_dir_file(char* path)
{
	DIR *d = NULL;
	struct dirent *dp = NULL;
	struct stat st;
	int is_find = 0;
	static char p[256] = {0};

	if(stat(path, &st) < 0 || !S_ISDIR(st.st_mode)) {
		printf("invalid path: %s\n", path);
		return NULL;
	}

	if(!(d = opendir(path))) {
		printf("opendir[%s] error: %m\n", path);
		return NULL;
	}

	while((dp = readdir(d)) != NULL) {
		if((!strncmp(dp->d_name, ".", 1)) || (!strncmp(dp->d_name, "..", 2))) {
			continue;
		}

		sprintf(p, "%s", dp->d_name);
		is_find = 1;
	}
	closedir(d);

	if (is_find) {
		return p;
	}

	return NULL;
}

static int set_frq(int bpu_id,char *freq)
{
	char node_name[4096] = {0};
	char *tmp_devfreq_name;
	int gov_fd,setfrq_fd,curfrq_fd;
	int ret;

	if (atoi(freq)  <= 0) {
		printf("bpu freq should be greater than 0\n");
		return -1;
	}
	if (bpu_id) {
		tmp_devfreq_name = top_dir_file("/sys/devices/system/bpu/bpu1/devfreq/");
		if (tmp_devfreq_name == NULL) {
			printf("Can't find bpu%d devfreq node\n", bpu_id);
			return -1;
		}

		sprintf(node_name, "/sys/devices/system/bpu/bpu1/devfreq/%s/governor", tmp_devfreq_name);
		gov_fd = open(node_name, O_RDWR, 0664);
		if(gov_fd == -1) {
			printf("gov_fd[%d] open error\n",bpu_id);
			return -1;
		}
		char *buf = "userspace";
		ret = write(gov_fd, buf,strlen(buf));
		if (ret < strlen(buf)) {
			close(gov_fd);
			printf("set bpu[%d] governor error\n",bpu_id);
			return -1;
		}
		sprintf(node_name, "/sys/devices/system/bpu/bpu1/devfreq/%s/userspace/set_freq", tmp_devfreq_name);
		setfrq_fd = open(node_name, O_RDWR, 0664);
		if(setfrq_fd == -1) {
			printf("setfrq_fd[%d] open error\n",bpu_id);
			close(gov_fd);
			return -1;
		}
		sprintf(node_name, "/sys/devices/system/bpu/bpu1/devfreq/%s/cur_freq", tmp_devfreq_name);
		curfrq_fd = open(node_name, O_RDONLY);
		if(curfrq_fd == -1) {
			close(gov_fd);
			close(setfrq_fd);
			printf("curfrq_fd[%d] open error\n",bpu_id);
			return -1;
		}
	}
	else {
		tmp_devfreq_name = top_dir_file("/sys/devices/system/bpu/bpu0/devfreq/");
		if (tmp_devfreq_name == NULL) {
			printf("Can't find bpu%d devfreq node\n", bpu_id);
			return -1;
		}
		sprintf(node_name, "/sys/devices/system/bpu/bpu0/devfreq/%s/governor", tmp_devfreq_name);
		gov_fd = open(node_name, O_RDWR, 0664);
		if(gov_fd == -1) {
			printf("gov_fd[%d] open error\n",bpu_id);
			return -1;
		}
		char *buf = "userspace";
		ret = write(gov_fd, buf,strlen(buf));
		if (ret < strlen(buf)) {
			printf("set bpu[%d] governor error\n",bpu_id);
			close(gov_fd);
			return -1;
		}
		sprintf(node_name, "/sys/devices/system/bpu/bpu0/devfreq/%s/userspace/set_freq", tmp_devfreq_name);
		setfrq_fd = open(node_name, O_RDWR, 0664);
		if(setfrq_fd == -1) {
			printf("setfrq_fd[%d] open error\n",bpu_id);
			close(gov_fd);
			return -1;
		}
		sprintf(node_name, "/sys/devices/system/bpu/bpu0/devfreq/%s/cur_freq", tmp_devfreq_name);
		curfrq_fd = open(node_name, O_RDONLY);
		if(curfrq_fd == -1) {
			printf("curfrq_fd[%d] open error\n",bpu_id);
			close(gov_fd);
			close(setfrq_fd);
			return -1;
		}
	}
	ret = write(setfrq_fd, freq, strlen(freq));
	if (ret < strlen(freq)) {
		printf("bpu[%d] set frequency error\n", bpu_id);
		close(curfrq_fd);
		close(gov_fd);
		close(setfrq_fd);
		return -1;
	}
	char tmp[20] = {0};
	ret = read(curfrq_fd, tmp, 20);
	if (ret < 0) {
		printf("read bpu devfreq failed\n");
		close(curfrq_fd);
		close(gov_fd);
		close(setfrq_fd);
		return -1;
	}
	printf("current bpu[%d] frequency:%s", bpu_id, tmp);
	close(curfrq_fd);
	close(gov_fd);
	close(setfrq_fd);
	return 0;
}

static void help_message()
{
	printf("BPU PROFILE HELP INFORMATION\n");
	printf(">>> -b/--bpu         [BPU CORE,0--bpu0,1--bpu1,2--ALL BPU] (required)\n");
	printf(">>> -p/--power       [POWER OFF/ON,0--OFF,1--ON]\n");
	printf(">>> -c/--clock       [CLOCK OFF/ON,0--OFF,1--ON]\n");
	printf(">>> -e/--enabletime  [GET FC TIME/ON,0--OFF,1--ON]\n");
	printf(">>> -t/--time        [GET FC TIME,NO ARGUMENT]\n");
	printf(">>> -f/--frq         [SET BPU FREQUENCY,ARGUMENT:N]\n");
	printf(">>> -r/--ratio       [BPU RATIO,N--N TIMES,0--FOREVER]\n");
}

int main(int argc, char **argv)
{
	int cmd_parser_ret = 0;
	struct cmd_info cmd = {-1,NULL,NULL,NULL,-1,-1,NULL};

	while ((cmd_parser_ret = getopt_long(argc, argv, short_options,
			long_options, NULL)) != -1) {
			switch (cmd_parser_ret) {
				case 'b':
					cmd.bpu_core = atoi(optarg);

				break;

				case 'p':
					cmd.bpu_power = optarg;
				break;

				case 'c':
					cmd.bpu_clock = optarg;
				break;

				case 'r':
					cmd.bpu_ratio = atoi(optarg);
				break;
				case 't':
					cmd.bpu_time = 1;
				break;
				case 'e':
					cmd.bpu_time_en = optarg;
				break;
				case 'f':
					cmd.bpu_frq = optarg;
				break;


				case 'h':
					help_message();
			        return 0;
				break;
			}
	}

	if (cmd.bpu_core < 0 || cmd.bpu_core > 2) {
		printf("bpu core error,you should select 0/1/2\n");
		help_message();
		return 0;
	}
	//printf("%s,%d,%s,%s,%d,%d\n",cmd.bpu_clock,cmd.bpu_core,cmd.bpu_frq,cmd.bpu_power,cmd.bpu_ratio,cmd.bpu_time);

	int ratio_fd0;
	int queue_fd0;
	int pro_frq_fd;
	int pro_en_fd;
	int ret;
	if (cmd.bpu_clock != NULL) {
		if (atoi(cmd.bpu_clock) == 0 || atoi(cmd.bpu_clock) == 1) {
			if (cmd.bpu_core == 2) {
				ret = set_clock(0, cmd.bpu_clock);
				ret |= set_clock(1, cmd.bpu_clock);
				if (ret)
					return -1;
			} else {
				if (set_clock(cmd.bpu_core, cmd.bpu_clock) != 0)
					return -1;
			}
		}
	}
	/*config bpu power*/
	if(cmd.bpu_power != NULL) {
		if (atoi(cmd.bpu_power) == 0 || atoi(cmd.bpu_power) == 1) {
			if (cmd.bpu_core == 2) {
				ret = set_power(0, cmd.bpu_power);
				ret |= set_power(1, cmd.bpu_power);
				if (ret)
					return -1;
			} else {
				if (set_power(cmd.bpu_core, cmd.bpu_power) != 0)
					return -1;
			}
		}
	}

	/*set bpu fc time enable*/
	if(cmd.bpu_time_en != NULL) {
		if (set_fc_time(cmd.bpu_time_en) != 0)
				return -1;
	}
	/*get bpu fc time*/
	if (cmd.bpu_time == 1) {
		if (cmd.bpu_core == 2) {
			ret = get_fc_time(0);
			ret |= get_fc_time(1);
			if (ret)
				return -1;
		} else {
			if (get_fc_time(cmd.bpu_core) != 0)
				return -1;
		}
	}
	/*set frequency*/
	if (cmd.bpu_frq != NULL) {
		if (cmd.bpu_core == 2) {
			ret = set_frq(0, cmd.bpu_frq);
			ret |= set_frq(1, cmd.bpu_frq);
			if (ret)
				return -1;
		} else {
			if (set_frq(cmd.bpu_core, cmd.bpu_frq) != 0)
				return -1;
		}
	}
	/*get bpu ratio*/
	int i = 0;
	if (cmd.bpu_ratio >= 0) {
		pro_frq_fd = open("/sys/devices/system/bpu/profiler_frequency", O_RDWR, 0664);
		if(pro_frq_fd > 0) {
			pro_en_fd = open("/sys/devices/system/bpu/profiler_enable", O_RDWR, 0664);
			if(pro_en_fd == -1) {
				printf("pro_en_fd open error\n");
				return -1;
			}
			ret = write(pro_frq_fd, "250", 3);
			if (ret < 3) {
				printf("set profile frequency error\n");
			}

			ret = write(pro_en_fd, "1", 1);
			if (ret < 1) {
				printf("set profile enable error\n");
			}
		}
		ratio_fd0 = open("/sys/devices/system/bpu/bpu0/ratio", O_RDONLY);
		if(ratio_fd0 == -1) {
			printf("ratio_fd0 open error\n");
			return -1;
		}
		queue_fd0 = open("/sys/devices/system/bpu/bpu0/queue", O_RDONLY);
		if(queue_fd0 == -1) {
			printf("queue_fd0 open error\n");
			return -1;
		}

		if (cmd.bpu_ratio == 0) {
			i = -1;
		} else {
			i = 0;
		}
		printf("BPU   RATIO    FREE QUEUE\n");
	}

	for (;i < cmd.bpu_ratio; i++) {

		if (cmd.bpu_ratio == 0) {
			i = -2;
		}

		if (cmd.bpu_core == 0) {
			if (get_ratio(queue_fd0, ratio_fd0, cmd.bpu_core) != 0)
				return -1;
		}

		sleep(1);
	}
	if (cmd.bpu_ratio >= 0) {
		if (cmd.bpu_core == 2)
			MOVEDOWN(2);
		else
			printf("\n");
		close(ratio_fd0);
		close(queue_fd0);
		if (pro_frq_fd > 0) {
			close(pro_en_fd);
			close(pro_frq_fd);
		}
	}

}
