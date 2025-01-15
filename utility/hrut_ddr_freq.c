/*
* Copyright (c) 2023 Horizon Robotics
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#define FREQNAME   "soc:ddrc-freq"
int process_folder(const char *folder_path) {
        char name_file_path[256] = {0};
        char cur_freq_file_path[256] = {0};
        char name[256] = {0};
        char cur_freq[256] = {0};

        snprintf(name_file_path, sizeof(name_file_path), "%s/name", folder_path);

        FILE *name_file = fopen(name_file_path, "r");
        if (name_file == NULL) {
                perror("Error opening name file");
                return -1;
        }

        if (fgets(name, sizeof(name), name_file) == NULL) {
                perror("Error reading name file");
                fclose(name_file);
                return -1;
        }

        name[strcspn(name, "\n")] = '\0';

        fclose(name_file);

        if (strcmp(name, FREQNAME) == 0) {
                snprintf(cur_freq_file_path, sizeof(cur_freq_file_path), "%s/cur_freq", folder_path);

                int cur_freq_fd = open(cur_freq_file_path, O_RDONLY);
                if (cur_freq_fd == -1) {
                        perror("Error opening cur_freq file");
                        return -1;
                }

                ssize_t bytes_read = read(cur_freq_fd, cur_freq, sizeof(cur_freq));
                if (bytes_read == -1) {
                        perror("Error reading cur_freq file");
                        close(cur_freq_fd);
                        return -1;
                }

                cur_freq[bytes_read] = '\0';

                printf("ddr cur_freq: %s", cur_freq);
                close(cur_freq_fd);
                return 0;
        }
        return -1;
}

int main(int argc, char *argv[]) {
        struct dirent *entry = NULL;
        DIR *dir = NULL;
        int ret = 0;
        char folder_path[512] = {0};

        if (argc > 1) {
                fprintf(stderr, "*** this tool is used to get current ddr frequency ***\n");
                fprintf(stderr, " usage: hrut_ddr_freq\n");
                exit(EXIT_FAILURE);
        }
        dir = opendir("/sys/class/devfreq");
        if (dir == NULL) {
                perror("Error opening directory");
                return EXIT_FAILURE;
        }

        while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                        snprintf(folder_path, sizeof(folder_path), "/sys/class/devfreq/%s", entry->d_name);
                        ret = process_folder(folder_path);
                        if (ret == 0)
                                goto exit;
                }
        }
        if (ret) {
                printf("can not find devfreq: %s\n", FREQNAME);
                closedir(dir);
                return EXIT_FAILURE;
        }
exit:
        closedir(dir);

        return EXIT_SUCCESS;
}
