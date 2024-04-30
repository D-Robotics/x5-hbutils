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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define DSP_LOG_PATH "/sys/class/remoteproc/remoteproc0/log"
#define BL31_LOG_PATH "/dev/optee_log_dev"

#define DEFAULT_ROTATED_LOGS 4
#define DEFAULT_FILE_SIZE_KBYTES 16
#define MAX_FILE_SIZE_KBYTES 4194304
#define OUTPUT_FILENAME_LEN 256
#define ROTATED_FILENAME_LEN (OUTPUT_FILENAME_LEN + 64)
#define READ_LOG_LEN 65536
#define unlikely(x) __builtin_expect(!!(x), 0)

static int g_out_fd = -1;

static void show_help(const char *cmd)
{
	printf("Usage: %s [options]\n", cmd);
	printf("options include:\n"
	       "  -f <filename>   Log to file, length is limited to %d. Default to stdout\n"
	       "  -r [<kbytes>]   Rotate log every kbytes, length is limited to %dKB. default 16. Requires -f\n"
	       "  -n <count>      Sets max number of rotated logs to <count>, default 4\n"
	       "  -b <buffer>     Request alternate device log buffer\n"
	       "  -s              Synchronous write file\n",
	       OUTPUT_FILENAME_LEN, MAX_FILE_SIZE_KBYTES);
}

static void signal_handler(int signum)
{
	if (signum == SIGTERM || signum == SIGINT) {
		if (g_out_fd > 2) {
			fsync(g_out_fd);
			close(g_out_fd);
		}
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = 0;
	int opt = 0;
	int out_fd_oflag = O_WRONLY | O_APPEND | O_CREAT;
	//static char *nodename = NULL;
	static char nodename[256];
	const char *output_filename = NULL;
	int max_rotated_logs = 0;
	int file_size_kbytes = 0;
	unsigned int file_size_bytes = 0;
	struct stat statbuf;
	char read_log[READ_LOG_LEN] = { 0 };
	char write_log[READ_LOG_LEN] = { 0 };
	int read_count = 0;
	int write_count = 0;
	unsigned int out_byte_count = 0;
	int i = 0;
	char file0[ROTATED_FILENAME_LEN];
	char file1[ROTATED_FILENAME_LEN];

	if (argc == 1 || (argc == 2 && 0 == strcmp(argv[1], "--help"))) {
		show_help(argv[0]);
		return 0;
	}
	for (;;) {
		opt = getopt(argc, argv, "f:r:n:b:s");
		if (opt < 0) {
			break;
		}

		switch (opt) {
		case 'b':
			//nodename = optarg;
			if (strcmp(optarg, "dsp") == 0) {
				strcpy(nodename, DSP_LOG_PATH);
			} else if (strcmp(optarg, "bl31") == 0) {
				strcpy(nodename, BL31_LOG_PATH);
			} else {
				printf("device name error\n");
				show_help(argv[0]);
			}
			break;
		case 'f':
			/* redirect output to a file */
			output_filename = optarg;
			if (strlen(output_filename) > OUTPUT_FILENAME_LEN) {
				printf("ERR: filename is too long\n");
				show_help(argv[0]);
				return -1;
			}
			break;
		case 'r':
			file_size_kbytes = atoi(optarg);
			if (file_size_kbytes > MAX_FILE_SIZE_KBYTES) {
				printf("ERR: file size is too big\n");
				show_help(argv[0]);
				return -1;
			}
			break;
		case 'n':
			max_rotated_logs = atoi(optarg);
			break;
		case 's':
			out_fd_oflag = O_WRONLY | O_SYNC | O_APPEND | O_CREAT;
			break;
		default:
			show_help(argv[0]);
			return -1;
		}
	}

	if (nodename == NULL) {
		printf("ERR: device log buffer node is NULL\n");
		show_help(argv[0]);
		return -1;
	}

	if (file_size_kbytes == 0) {
		file_size_kbytes = DEFAULT_FILE_SIZE_KBYTES;
	}
	file_size_bytes = file_size_kbytes << 10;

	if (max_rotated_logs == 0) {
		max_rotated_logs = DEFAULT_ROTATED_LOGS;
	}

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	fd = open(nodename, O_RDONLY);
	if (fd < 0) {
		printf("ERR: open %s error %d\n", nodename, fd);
		return fd;
	}

	if (output_filename == NULL) {
		g_out_fd = STDOUT_FILENO;
	} else {
		g_out_fd = open(output_filename, out_fd_oflag, S_IRUSR | S_IWUSR);
		if (g_out_fd < 0) {
			printf("ERR: couldn't open output file %s %d\n", output_filename, g_out_fd);
			close(fd);
			return g_out_fd;
		}
		fstat(g_out_fd, &statbuf);
		out_byte_count = statbuf.st_size;
	}

	while (1) {
		usleep(40000);
		write_count = 0;
		lseek(fd, 0, SEEK_SET);
		read_count = read(fd, read_log, READ_LOG_LEN);
		if (read_count < 0) {
			printf("ERR: read log %d\n", read_count);
			close(g_out_fd);
			close(fd);
			return read_count;
		}
		for (i = 0; i < read_count; i++) {
			if (read_log[i] != '\0')
				write_log[write_count++] = read_log[i];
		}
		if (write_count == 0) {
			continue;
		}

		/* if the file does not exist, create the file */
		if (output_filename && ((g_out_fd < 0) || (access(output_filename, F_OK) == -1))) {
			g_out_fd = open(output_filename, out_fd_oflag, S_IRUSR | S_IWUSR);
			if (g_out_fd < 0) {
				printf("ERR: couldn't open output file %s %d\n", output_filename, g_out_fd);
				close(fd);
				return g_out_fd;
			}
			out_byte_count = 0;
		}

		ret = write(g_out_fd, write_log, write_count);
		if (ret < 0) {
			printf("ERR: output to %s %d\n", output_filename, ret);
			close(g_out_fd);
			close(fd);
			return ret;
		}
		out_byte_count += ret;
		//printf("%s out_byte_count : %d\n", output_filename, out_byte_count);

		/* dump file */
		if (output_filename && out_byte_count >= file_size_bytes) {
			close(g_out_fd);
			g_out_fd = -1;
			for (i = max_rotated_logs; i > 0; i--) {
				snprintf(file1, ROTATED_FILENAME_LEN, "%s.%d", output_filename, i);
				if (unlikely(i == 1)) {
					snprintf(file0, ROTATED_FILENAME_LEN, "%s", output_filename);
				} else {
					snprintf(file0, ROTATED_FILENAME_LEN, "%s.%d", output_filename,
						 i - 1);
				}
				//printf("rename %s to %s \n", file0, file1);
				ret = rename(file0, file1);
				if (ret < 0 && errno != ENOENT) {
					printf("ERR: rotating log files error %d, %s to %s\n", ret, file0,
					       file1);
				}
			}
		}
	}
}
