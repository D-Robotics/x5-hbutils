#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "boardinfo.h"

#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))
#define BOARD_INFO_PATH		"/sys/class/socinfo"
#define BOARD_INFO_SZIE		256
#define BUF_LEN 			512

#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif
#ifndef VERSION
#define VERSION "unknown"
#endif

#define GIT_COMMIT_MAX_LEN 40
static char libboardinfo_git_commit[GIT_COMMIT_MAX_LEN] = GIT_COMMIT_HASH;
#define VERSION_MAX_LEN 40
static char libboardinfo_version[VERSION_MAX_LEN] = VERSION;

char* boardinfo_key[] = {
	"soc_gen",
	"soc_name",
	"hw_name",
	"board_version",
	"hw_info",
	"bootdevice_name",
	"soc_uid",
	"ddr_type",
	"ddr_size",
	"ddr_vendor",
	"ddr_freq",
	"board_id",
	"sec_chip",
	"sec_boot",
	"bak_slot"
};


/**
 * @description: Get both the git commit hash and version of libboardinfo.
 * @return: A pointer to a static string containing both git commit hash and lib version.
 */
const char* hb_libboardinfo_get_version_info(void) {
	static char version_info[GIT_COMMIT_MAX_LEN + VERSION_MAX_LEN];
	snprintf(version_info, sizeof(version_info), "%s-%s",
			 libboardinfo_version, libboardinfo_git_commit);
	return version_info;
}

static int32_t read_from_file(char *dst, char *file)
{
	int fd;
	int32_t rc;

	if (access(file, R_OK)) {
		return -1;
	}

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		return -1;
	}

	rc = read(fd, (void *)dst, BUF_LEN);

	close(fd);

	if (rc < 0) {
		return -1;
	}

	if (dst[rc - 1] == '\n') {
		dst[rc - 1] = '\0';
	} else {
		dst[rc] = '\0';
	}

	return 0;
}

static int32_t hobot_parse_info(char* type, char* dst)
{
	char place[100] = {'\0'};

	snprintf(place, sizeof(place), "%s/%s", BOARD_INFO_PATH, type);

	if (read_from_file(dst, place)) {
		return -HB_BINFO_PLAT_NOT_SUPPORT;
	}

	return 0;
}

static int32_t get_boardinfo_from_sysfs(char* key, char* dst)
{
	int32_t ret;
	uint32_t index;

	for (index = 0; index < ARRAY_LENGTH(boardinfo_key); index++) {
		if (strncmp(key, boardinfo_key[index], strlen(boardinfo_key[index])) == 0) {
			ret = hobot_parse_info(key, dst);
			goto exit;
		}
	}

	ret = -HB_BINFO_PLAT_NOT_SUPPORT;
exit:
	return  ret;
}

int32_t hb_get_boardinfo(char* key, void *dst, uint32_t len)
{
	int32_t ret = 0;
	char buf[BUF_LEN] = { 0 };

	if (key == NULL || dst == NULL) {
		ret = -HB_BINFO_INVALID_PARAM;
		goto exit;
	}
	if ((ret = get_boardinfo_from_sysfs(key, buf)) < 0) {
		goto exit;
	}

	if (len < (strlen(buf) + 1)) {
		ret = -HB_BINFO_SHORT_BUF;
		goto exit;
	}

	strncpy(dst, buf, len);
exit:
	return ret;
}
