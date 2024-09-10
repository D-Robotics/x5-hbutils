/*
* Horizon switch reason
*
* Maintainer: zhiwei01.li
*
* Copyright (C) 2024 D-Robotics
*
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SWITCH_REASON_RECORD 10
#define SWITCH_REASON_OFFSET 3 * 1024
#define MAX_REASON_COUNT 7
#define MISC_PATH "/dev/block/platform/by-name/misc"

static char *switch_reason_s [MAX_REASON_COUNT] = {
	"unused",
	"misc broken",
	"boot corruted",
	"dm-verity corruped",
	"uboot corruped",
	"miniboot corruped",
	"misc info err",
};

struct switch_reason
{
	uint32_t current;
	uint32_t reason[MAX_SWITCH_REASON_RECORD];
	uint32_t crc32_le;
};

uint32_t CRC32(const uint8_t *buf, size_t size)
{
	uint32_t    crc_table[256] = { 0 };
	uint32_t    ret = 0xFFFFFFFF;
	size_t      i = 0;

	// Compute the CRC-32 table only once.
	if (!crc_table[1]) {
		for (i = 0; i < 256; ++i) {
			uint32_t crc = i;
			for (uint32_t j = 0; j < 8; ++j) {
				uint32_t mask = -(crc & 1);
				crc = (crc >> 1) ^ (0xEDB88320 & mask);
			}
			crc_table[i] = crc;
		}
	}
		for (i = 0; i < size; ++i) {
			ret = (ret >> 8) ^ crc_table[(ret ^ buf[i]) & 0xFF];
		}

	return ~ret;
}

void usage(int argc, char *argv[])
{
	fprintf(stderr, "Usage: %s [misc_path] <--current|--all>\n",
		argv[0]);
	fprintf(stderr, "  misc_path: misc partition or file path (default: %s)\n",
		MISC_PATH);
	fprintf(stderr, "  --current: The reason for the last abnormal switch\n");
	fprintf(stderr, "  --all: The reason for the all abnormal switch\n");
	fprintf(stderr, "  example: %s --all\n", argv[0]);
}

int main(int argc, char *argv[])
{
	char *filename = NULL;
	char *option = NULL;
	FILE *file = NULL;
	struct switch_reason sr = {0};
	size_t read_size = 0;
	uint32_t calculated_crc32 = 0;

	if (argc < 2 || argc > 3) {
		usage(argc, argv);
		return EXIT_FAILURE;
	}
	if (argc == 2) {
		if (strcmp(argv[1], "--help") == 0 ||
		    strcmp(argv[1], "-h") == 0) {
			usage(argc, argv);
			return EXIT_SUCCESS;
		}
		filename = MISC_PATH; // Get the filename from the command line arguments
		option = argv[1]; // Get the option from the command line arguments
	} else {
		filename = argv[1];  // Get the filename from the command line arguments
		option = argv[2];    // Get the option from the command line arguments
	}

	file = fopen(filename, "rb");

	if (file == NULL) {
		perror("Failed to open file");
		return EXIT_FAILURE;
	}

	// Move to the 3072nd byte in the file
	if (fseek(file, SWITCH_REASON_OFFSET, SEEK_SET) != 0) {
		perror("Failed to seek to the specified location in the file");
		fclose(file);
		return EXIT_FAILURE;
	}

	// Read the structure data
	read_size = fread(&sr, sizeof(struct switch_reason), 1, file);
	if (read_size != 1) {
		perror("Failed to read data");
		fclose(file);
		return EXIT_FAILURE;
	}

	fclose(file);

	// Perform CRC32 check
	calculated_crc32 = CRC32((uint8_t*)&sr, sizeof(sr) - sizeof(sr.crc32_le));
	if (calculated_crc32 != sr.crc32_le) {
		fprintf(stderr, "CRC32 check failed. Expected: 0x%08x, Calculated: 0x%08x\n",
			sr.crc32_le, calculated_crc32);
		return EXIT_FAILURE;
	}

	// Check the option and print the appropriate data
	if (strcmp(option, "--current") == 0) {
		// Print the current reason

		if (sr.current >= MAX_SWITCH_REASON_RECORD) {
			fprintf(stderr, "Error: Current index is out of range.\n");
			return EXIT_FAILURE;
		}
		if (sr.reason[sr.current] > MAX_REASON_COUNT - 1) {
			fprintf(stderr, "Error: Invalid reason %d.\n",
				sr.reason[sr.current]);
			return EXIT_FAILURE;
		}
		printf("%s\n", switch_reason_s[sr.reason[sr.current]]);

	} else if (strcmp(option, "--all") == 0) {
		// Print all reasons
		printf("All Reasons (From the latest to the oldest):\n");
		for (int i = 0; i < MAX_SWITCH_REASON_RECORD; i++) {
			if (sr.reason[sr.current] > MAX_REASON_COUNT - 1) {
				fprintf(stderr, "Error: Invalid reason %d, number %d.\n",
					sr.reason[sr.current], sr.current);
				return EXIT_FAILURE;
			}
			int idx = (MAX_SWITCH_REASON_RECORD + sr.current - i) % \
						MAX_SWITCH_REASON_RECORD;
			printf("  %d: %s\n", i + 1, switch_reason_s[sr.reason[idx]]);
		}

	} else {
		fprintf(stderr, "Invalid option: %s. Please use --current or --all.\n", option);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
