/*
* Copyright (c) 2023 Horizon Robotics
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *file_boardid = "/sys/class/socinfo/board_id";

void help_msg(void)
{
        printf("Usage:\n");
        printf("\thrut_boardid: prints current boardid\n");
        printf("\t-h: Print this message\n");

        return;
}

int main(int argc, char **argv)
{
        size_t ret = 0;
        FILE *stream;
        char boardid[40] = {0};

        if (argc == 2 && !strncmp("-h", argv[1], strlen(argv[1]))) {
                help_msg();
                return 0;
        }

        stream = fopen(file_boardid, "r");
        if (!stream) {
                printf("open board_id fail\n");
                return -1;
        }
        ret = fread(boardid, sizeof(char), sizeof(boardid), stream);
        if (ret == 0 && ferror(stream)) {
                printf("read board_id fail\n");
                fclose(stream);
                return -1;
        }
        fclose(stream);
        printf("board_id: %s\n", boardid);
        return 0;
}
