/*
 *    COPYRIGHT NOTICE
 *    Copyright 2023 Horizon Robotics, Inc.
 *    All rights reserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMDLINE_FILE "/proc/cmdline"
#define TARGET_STRING "hobotboot.reason="

int main()
{
        char cmdline[1024] = {0};
        char *reason_start = NULL;
        char *reason_end = NULL;
        char extracted_content[256] = {0};
        FILE *cmdline_file = fopen(CMDLINE_FILE, "r");

        if (cmdline_file == NULL) {
                perror("Error opening /proc/cmdline");
                return EXIT_FAILURE;
        }

        if (fgets(cmdline, sizeof(cmdline), cmdline_file) == NULL) {
                perror("Error reading /proc/cmdline");
                fclose(cmdline_file);
                return EXIT_FAILURE;
        }

        fclose(cmdline_file);

        reason_start = strstr(cmdline, TARGET_STRING);
        if (reason_start == NULL) {
                printf("Target string not found in /proc/cmdline.\n");
                return EXIT_SUCCESS;
        }

        reason_start += strlen(TARGET_STRING);

        reason_end = strpbrk(reason_start, " ");
        if (reason_end == NULL) {
                reason_end = reason_start + strlen(reason_start);
        }

        strncpy(extracted_content, reason_start, reason_end - reason_start);
        extracted_content[reason_end - reason_start] = '\0';

        printf("reset reason is: %s\n", extracted_content);

        return EXIT_SUCCESS;
}
