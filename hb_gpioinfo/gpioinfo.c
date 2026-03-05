// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>
// SPDX-FileCopyrightText: 2022 Kent Gibson <warthog618@gmail.com>

#include <ctype.h>
#include <getopt.h>
#include <gpiod.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools-common.h"

struct config {
	bool by_name;
	bool strict;
	bool unquoted_strings;
	const char *chip_id;
};

#define PLATEFORM_HOBOTX5_GPIOPARSE
#ifdef PLATEFORM_HOBOTX5_GPIOPARSE
typedef struct {
	char pinname[50];       // eg: lsio_gpio_pin0
	char controlname[20];   // eg: 34130000.gpio
	int pinnum;             // eg: 347 //pinfistnum + linenumber
	int linenumber;         // eg: 0
	char currentfunc[32];   // eg: Defalut
	char pin_strength[16];  // eg: 9 mA (output drive strength from pinconf-pins)
} PinInfo;

typedef struct {
	PinInfo pininfo[64];
	char controlname[20];    // eg: 34130000.gpio
	char chipname[24];       // eg: gpiochip5
	int pinfistnum;          // eg: 347
	char pininterval[16];    // eg: 347-363
	int pincount;            // eg: total 17 pin
} Chipinfo_t;

#define MAX_CHIPS 10
#define MAX_PINS 64
Chipinfo_t chipinfo[MAX_CHIPS];

// 将 pinname 转换为大写
void convert_to_uppercase(char *str) {
	while (*str) {
		*str = toupper(*str);
		str++;
	}
}
int each_convert_to_uppercase(Chipinfo_t *chipinfo,int pinCount){
	int i = 0;
	for (i = 0; i < pinCount; i++) {
		convert_to_uppercase(chipinfo->pininfo[i].pinname);
	}
	return 0;
}

// Parse a single line and fill PinInfo
int parse_pinmux_line(const char *line, PinInfo *pinInfo) {
	char pinname[50] = {0}, controlname[50] = {0}, currentfunc[50] = {0};
	int pin_number = 0;

	// Parse pin and controlname
	int matched = sscanf(line, "pin %d (%[^)]): %49s", &pin_number, pinname, controlname);

	// If line format doesn't match, return failure
	if (matched < 3) return 0;

	// Attempt to extract group information
	char *group_ptr = strstr(line, "group ");
	if (group_ptr) {
		sscanf(group_ptr, "group %49s", currentfunc);
	} else {
		// If no group information, attempt to extract controlname followed by .gpio:422
		char *gpio_ptr = strstr(line, ".gpio:");
		if (gpio_ptr) {
			sscanf(gpio_ptr - 9, "%49s", currentfunc);  // Start extracting from controller name, includes "32150000.gpio:422"
		} else {
			// If no GPIO info is found, default to Default
			strcpy(currentfunc, "Default");
		}
	}

	// Fill the PinInfo structure
	if(strcmp(pinInfo->pinname,pinname)!=0){
		return 0;
	}
	strcpy(pinInfo->currentfunc, currentfunc);

	return 1;
}

// Parse the file and store the parsed pin information
int parse_pinmux_file(const char *filename, PinInfo *pinInfos, int max_pins, Chipinfo_t *chipinfo) {
	FILE *file = fopen(filename, "r");
	if (!file) {
		perror("Failed to open file");
		return -1;
	}

	char line[256];
	int pinCount = 0;

	// Skip the first line "Pinmux settings per pin"
	fgets(line, sizeof(line), file);

	// Parse the file line by line
	while (fgets(line, sizeof(line), file) && pinCount < max_pins) {
		if (strncmp(line, "pin", 3) == 0) {
			if (parse_pinmux_line(line, &pinInfos[pinCount])) {
				pinCount++;
			} else {
				continue;
			}
		}
	}

	fclose(file);
	return pinCount;
}

// Parse pinconf-pins line and update matching pin's drive strength.
// Line format: "pin 8 (hsio_enet_rx_clk): ... output drive strength (9 mA), ..."
static int parse_pinconf_line(const char *line, PinInfo *pinInfos, int pinCount) {
	char pinname[50] = {0};
	int pin_number = 0;
	int ma = 0;
	const char *drv_str;
	int i;

	if (sscanf(line, "pin %d (%49[^)])", &pin_number, pinname) < 2)
		return 0;
	drv_str = strstr(line, "output drive strength (");
	if (!drv_str || sscanf(drv_str, "output drive strength (%d mA)", &ma) != 1)
		return 0;
	for (i = 0; i < pinCount; i++) {
		if (strcmp(pinInfos[i].pinname, pinname) == 0) {
			snprintf(pinInfos[i].pin_strength, sizeof(pinInfos[i].pin_strength), "%d mA", ma);
			return 1;
		}
	}
	return 0;
}

// Parse pinconf-pins file and fill pin_strength for matching pins.
int parse_pinconf_file(const char *filename, PinInfo *pinInfos, int pinCount) {
	FILE *file = fopen(filename, "r");
	int i;

	for (i = 0; i < pinCount; i++)
		snprintf(pinInfos[i].pin_strength, sizeof(pinInfos[i].pin_strength), "-");
	if (!file)
		return 0;
	{
		char line[512];
		if (fgets(line, sizeof(line), file)) { /* skip "Pin config settings per pin" */ }
		if (fgets(line, sizeof(line), file)) { /* skip "Format: ..." */ }
		while (fgets(line, sizeof(line), file)) {
			if (strncmp(line, "pin", 3) == 0)
				parse_pinconf_line(line, pinInfos, pinCount);
		}
	}
	fclose(file);
	return 0;
}

// Remove the trailing colon from the string (if present)
void remove_trailing_colon(char *str) {
	int len = strlen(str);
	if (len > 0 && str[len - 1] == ':') {
		str[len - 1] = '\0';  // Replace the trailing colon with a null terminator
	}
}

// Wrapper function to parse the GPIO file and fill the chipinfo array
int parse_gpio_file(const char *filename, Chipinfo_t *chipinfo, int max_chips) {
	FILE *file = fopen(filename, "r");
	if (!file) {
		perror("Failed to open file");
		return -1;
	}

	char line[256];
	int chip_count = 0;

	// Read the file line by line
	while (fgets(line, sizeof(line), file) && chip_count < max_chips) {

		if (strncmp(line, "gpiochip", 8) == 0) {
			char chipname[24] = {0}, pininterval[16] = {0}, controlname[20] = {0};

			// Adjusted sscanf pattern to parse chipname, pininterval, and controlname
			int result = sscanf(line, "%23[^:]: GPIOs %15[^,], parent: platform/%*[^,], %19s",
					chipname, pininterval, controlname);

			// Check if all three fields were successfully matched
			if (result == 3) {
				// Remove the trailing colon from controlname
				remove_trailing_colon(controlname);

				// Parse the starting GPIO number
				int pinfistnum = 0;
				sscanf(pininterval, "%d-", &pinfistnum);

				// Store the parsed results in the structure
				strcpy(chipinfo[chip_count].chipname, chipname);
				strcpy(chipinfo[chip_count].pininterval, pininterval);
				strcpy(chipinfo[chip_count].controlname, controlname);
				chipinfo[chip_count].pinfistnum = pinfistnum;

				chip_count++;
			} else {
				// If parsing fails, print relevant information for debugging
				printf("Failed to parse line: %s (sscanf matched fields: %d)\n", line, result);
			}
		}
	}

	fclose(file);
	return chip_count; // Return the number of parsed chips
}

// Parse each line and extract the required information
int parse_line(const char* line, PinInfo* pinInfo, const Chipinfo_t *chipinfo) {
	sscanf(line, "pin %*d (%[^)]) %d:%s", pinInfo->pinname, &pinInfo->linenumber, pinInfo->controlname);
	pinInfo->pinnum = pinInfo->linenumber + chipinfo->pinfistnum;
	// If the first letter of controlname is "?" or if it doesn't match the chipinfo controlname, skip this line
	if (pinInfo->controlname[0] == '?' || strcmp(chipinfo->controlname, pinInfo->controlname) != 0) {
		return 0; // Do not parse
	}
	return 1; // Successfully parsed
}

// Wrapper function to parse the pin information from the specified file
int parse_pins(const char *filepath, PinInfo *pinInfos, int maxPins, Chipinfo_t *chipinfo) {
	FILE *file = fopen(filepath, "r"); // Open the file containing data
	if (!file) {
		perror("Failed to open file");
		return -1;
	}

	char line[256];
	int pinCount = 0;

	while (fgets(line, sizeof(line), file) && pinCount < maxPins) {
		if (strncmp(line, "pin", 3) == 0) {
			PinInfo pinInfo;
			if (parse_line(line, &pinInfo, chipinfo)) {
				// If successfully parsed, store it in the array
				pinInfos[pinCount++] = pinInfo;
			}
		}
	}
	chipinfo->pincount = pinCount;
	fclose(file);
	return pinCount; // Return the number of parsed pins
}

// Comparison function for qsort
int compare_pininfo(const void *a, const void *b) {
	PinInfo *pinA = (PinInfo *)a;
	PinInfo *pinB = (PinInfo *)b;
	return pinA->linenumber - pinB->linenumber; // Sort by linenumber in ascending order
}

// Wrapper function to sort the PinInfo array
void sort_pin_info(PinInfo *pinInfos, int pinCount) {
	qsort(pinInfos, pinCount, sizeof(PinInfo), compare_pininfo);
}

#endif //PLATEFORM_HOBOTX5_GPIOPARSE

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] [line]...\n", get_progname());
	printf("\n");
	printf("Print information about GPIO lines.\n");
	printf("\n");
	printf("Lines are specified by name, or optionally by offset if the chip option\n");
	printf("is provided.\n");
	printf("\n");
	printf("If no lines are specified then all lines are displayed.\n");
	printf("\n");
	printf("Options:\n");
	printf("      --by-name\t\ttreat lines as names even if they would parse as an offset\n");
	printf("  -c, --chip <chip>\trestrict scope to a particular chip\n");
	printf("  -h, --help\t\tdisplay this help and exit\n");
	printf("  -s, --strict\t\tcheck all lines - don't assume line names are unique\n");
	printf("      --unquoted\tdon't quote line or consumer names\n");
	printf("  -v, --version\t\toutput version information and exit\n");
	print_chip_help();
}

static int parse_config(int argc, char **argv, struct config *cfg)
{
	static const struct option longopts[] = {
		{ "by-name",	no_argument,	NULL,		'B' },
		{ "chip",	required_argument, NULL,	'c' },
		{ "help",	no_argument,	NULL,		'h' },
		{ "strict",	no_argument,	NULL,		's' },
		{ "unquoted",	no_argument,	NULL,		'Q' },
		{ "version",	no_argument,	NULL,		'v' },
		{ GETOPT_NULL_LONGOPT },
	};


	static const char *const shortopts = "+c:hsv";

	int opti, optc;

	memset(cfg, 0, sizeof(*cfg));

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
			case 'B':
				cfg->by_name = true;
				break;
			case 'c':
				cfg->chip_id = optarg;
				break;
			case 's':
				cfg->strict = true;
				break;
			case 'h':
				print_help();
				exit(EXIT_SUCCESS);
			case 'Q':
				cfg->unquoted_strings = true;
				break;
			case 'v':
				print_version();
				exit(EXIT_SUCCESS);
			case '?':
				die("try %s --help", get_progname());
			case 0:
				break;
			default:
				abort();
		}
	}

	return optind;
}

/*
 * Minimal version similar to tools-common that indicates if a line should be
 * printed rather than storing details into the resolver.
 * Does not die on non-unique lines.
 */
static bool resolve_line(struct line_resolver *resolver,
		struct gpiod_line_info *info, int chip_num)
{
	struct resolved_line *line;
	bool resolved = false;
	unsigned int offset;
	const char *name;
	int i;

	offset = gpiod_line_info_get_offset(info);

	for (i = 0; i < resolver->num_lines; i++) {
		line = &resolver->lines[i];

		/* already resolved by offset? */
		if (line->resolved && (line->offset == offset) &&
				(line->chip_num == chip_num)) {
			resolved = true;
		}

		if (line->resolved && !resolver->strict)
			continue;

		/* else resolve by name */
		name = gpiod_line_info_get_name(info);
		if (name && (strcmp(line->id, name) == 0)) {
			line->resolved = true;
			line->offset = offset;
			line->chip_num = chip_num;
			resolved = true;
		}
	}

	return resolved;
}

static void print_line_info(struct gpiod_line_info *info, bool unquoted_strings,Chipinfo_t *gpio_chip_data, int offset)
{
	char quoted_name[17];
	const char *name;
	int len;
	int j = 0;
	name = gpiod_line_info_get_name(info);
	if (!name) {
		name = "unnamed";
		unquoted_strings = true;
	}

	if (unquoted_strings) {
		printf("%-5s\t", name);
	} else {
		len = strlen(name);
		if (len <= 14) {
			quoted_name[0] = '"';
			memcpy(&quoted_name[1], name, len);
			quoted_name[len + 1] = '"';
			quoted_name[len + 2] = '\0';
			printf("%-16s\t", quoted_name);
		} else {
			printf("\"%s\"\t", name);
		}
	}
	print_line_attributes(info, unquoted_strings);
#ifdef PLATEFORM_HOBOTX5_GPIOPARSE
	//添加用于打印 Pin 信息的代码
	for (j = 0; j < gpio_chip_data->pincount; j++) {
		if (gpio_chip_data->pininfo[j].linenumber == offset) {
			printf(" %-20s\t%-8d %-18s %-10s",
					gpio_chip_data->pininfo[j].pinname,
					gpio_chip_data->pininfo[j].pinnum,
					gpio_chip_data->pininfo[j].currentfunc,
					gpio_chip_data->pininfo[j].pin_strength);
			break;
		}
	}
#endif //PLATEFORM_HOBOTX5_GPIOPARSE
}
/*
 * based on resolve_lines, but prints lines immediately rather than collecting
 * details in the resolver.
 */
static void list_lines(struct line_resolver *resolver, struct gpiod_chip *chip,
		int chip_num, struct config *cfg)
{
	struct gpiod_chip_info *chip_info;
	struct gpiod_line_info *info;
	int offset, num_lines;
	int i = 0;
	chip_info = gpiod_chip_get_info(chip);
	if (!chip_info)
		die_perror("unable to read info from chip %s",
				gpiod_chip_get_path(chip));

	num_lines = gpiod_chip_info_get_num_lines(chip_info);

	if ((chip_num == 0) && (cfg->chip_id && !cfg->by_name))
		resolve_lines_by_offset(resolver, num_lines);

	for (offset = 0; ((offset < num_lines) &&
				!(resolver->num_lines && resolve_done(resolver)));
			offset++) {
		info = gpiod_chip_get_line_info(chip, offset);
		if (!info)
			die_perror("unable to read info for line %d from %s",
					offset, gpiod_chip_info_get_name(chip_info));

		if (resolver->num_lines &&
				!resolve_line(resolver, info, chip_num))
			continue;

		if (resolver->num_lines) {
			printf("%s %u", gpiod_chip_info_get_name(chip_info),
					offset);
		} else {
#ifdef PLATEFORM_HOBOTX5_GPIOPARSE
			//gpiochip4 - 8 lines:     [PinName]          [PinNode]       [PinNum] @aon_gpio_porta: @31000000.gpio @498-505
			for (i=0; i < MAX_CHIPS; i++) {
				if(!strcmp(chipinfo[i].chipname,gpiod_chip_info_get_name(chip_info))){
					if (offset == 0) {
						printf("%s - %u lines: @%s: @%s\t\n",
								gpiod_chip_info_get_name(chip_info),
								num_lines,
								chipinfo[i].controlname,
								chipinfo[i].pininterval);
						//printf("[Number]                [Mode]  [Status]  [GpioName]       [PinName]              [PinNum]           [PinFunc] \t\n");
						printf("%*s", 8, "");
						printf("[Number]");
						printf("%*s", 16, "");
						printf("[Mode]");
						printf("%*s", 2, "");
						printf("[Status]");
						printf("%*s", 2, "");
						printf("[GpioName]");
						printf("%*s", 7, "");
						printf("[PinName]");
						printf("%*s", 10, "");
						printf("[PinNum]");
						printf("%*s", 3, "");
						printf("[PinFunc]");
						printf("%*s", 9, "");
						printf("[PinStrength]\n");
					}
					printf("\tline %2u:", offset);
					fputc('\t', stdout);
					print_line_info(info, cfg->unquoted_strings, &chipinfo[i], offset);
					fputc('\n', stdout);
					gpiod_line_info_free(info);
					resolver->num_found++;
				}

			}

		}
	}

	gpiod_chip_info_free(chip_info);
#endif //PLATEFORM_HOBOTX5_GPIOPARSE
}

int main(int argc, char **argv)
{
	struct line_resolver *resolver = NULL;
	int num_chips, i, ret = EXIT_SUCCESS;
	struct gpiod_chip *chip;
	struct config cfg;
	char **paths;
#ifdef PLATEFORM_HOBOTX5_GPIOPARSE
	int j = 0;

	// Parse the GPIO file and fill chipinfo array
	int chipCount = parse_gpio_file("/sys/kernel/debug/gpio", chipinfo, MAX_CHIPS);
	if (chipCount < 0) {
		return EXIT_FAILURE;
	}

	// File paths for various iomux controllers
	const char *hsio_iomuxcfilepath = "/sys/kernel/debug/pinctrl/35050000.hsio_iomuxc/pins";
	const char *lsio_iomuxcfilepath = "/sys/kernel/debug/pinctrl/34180000.lsio_iomuxc/pins";
	const char *aon_iomuxcfilepath = "/sys/kernel/debug/pinctrl/31040000.aon_iomuxc/pins";
	const char *dsp_iomuxcfilepath = "/sys/kernel/debug/pinctrl/31040014.dsp_iomuxc/pins";

	// File paths for the pinmux settings of various controllers
	const char *hsio_iomuxccpinmuxfilename = "/sys/kernel/debug/pinctrl/35050000.hsio_iomuxc/pinmux-pins";
	const char *lsio_iomuxcpinmuxfilename = "/sys/kernel/debug/pinctrl/34180000.lsio_iomuxc/pinmux-pins";
	const char *aon_iomuxcpinmuxfilename = "/sys/kernel/debug/pinctrl/31040000.aon_iomuxc/pinmux-pins";
	const char *dsp_iomuxcpinmuxfilename = "/sys/kernel/debug/pinctrl/31040014.dsp_iomuxc/pinmux-pins";

	// File paths for pinconf (output drive strength etc.)
	const char *hsio_pinconffilename = "/sys/kernel/debug/pinctrl/35050000.hsio_iomuxc/pinconf-pins";
	const char *lsio_pinconffilename = "/sys/kernel/debug/pinctrl/34180000.lsio_iomuxc/pinconf-pins";
	const char *aon_pinconffilename = "/sys/kernel/debug/pinctrl/31040000.aon_iomuxc/pinconf-pins";
	const char *dsp_pinconffilename = "/sys/kernel/debug/pinctrl/31040014.dsp_iomuxc/pinconf-pins";

	// Loop over the parsed chip information and check for control names
	for (j = 0; j < chipCount; j++) {

		if (strcmp(chipinfo[j].controlname, "35070000.gpio") == 0) {
			int pinCount = parse_pins(hsio_iomuxcfilepath, chipinfo[j].pininfo, 64, &chipinfo[j]); // hsio_gpio1
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			pinCount = parse_pinmux_file(hsio_iomuxccpinmuxfilename, chipinfo[j].pininfo, MAX_PINS, &chipinfo[j]);
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			parse_pinconf_file(hsio_pinconffilename, chipinfo[j].pininfo, pinCount);
			sort_pin_info(chipinfo[j].pininfo, pinCount);
			each_convert_to_uppercase(&chipinfo[j],pinCount);
		}

		if (strcmp(chipinfo[j].controlname, "35060000.gpio") == 0) {
			int pinCount = parse_pins(hsio_iomuxcfilepath, chipinfo[j].pininfo, 64, &chipinfo[j]); // hsio_gpio0
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			pinCount = parse_pinmux_file(hsio_iomuxccpinmuxfilename, chipinfo[j].pininfo, MAX_PINS, &chipinfo[j]);
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			parse_pinconf_file(hsio_pinconffilename, chipinfo[j].pininfo, pinCount);
			sort_pin_info(chipinfo[j].pininfo, pinCount);
			each_convert_to_uppercase(&chipinfo[j],pinCount);
		}

		if (strcmp(chipinfo[j].controlname, "34120000.gpio") == 0) {
			int pinCount = parse_pins(lsio_iomuxcfilepath, chipinfo[j].pininfo, 64, &chipinfo[j]); // lsio_gpio0
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			pinCount = parse_pinmux_file(lsio_iomuxcpinmuxfilename, chipinfo[j].pininfo, MAX_PINS, &chipinfo[j]);
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			parse_pinconf_file(lsio_pinconffilename, chipinfo[j].pininfo, pinCount);
			sort_pin_info(chipinfo[j].pininfo, pinCount);
			each_convert_to_uppercase(&chipinfo[j],pinCount);
		}

		if (strcmp(chipinfo[j].controlname, "34130000.gpio") == 0) {
			int pinCount = parse_pins(lsio_iomuxcfilepath, chipinfo[j].pininfo, 64, &chipinfo[j]); // lsio_gpio1
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			pinCount = parse_pinmux_file(lsio_iomuxcpinmuxfilename, chipinfo[j].pininfo, MAX_PINS, &chipinfo[j]);
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			parse_pinconf_file(lsio_pinconffilename, chipinfo[j].pininfo, pinCount);
			sort_pin_info(chipinfo[j].pininfo, pinCount);
			each_convert_to_uppercase(&chipinfo[j],pinCount);
		}

		if (strcmp(chipinfo[j].controlname, "31000000.gpio") == 0) {
			int pinCount = parse_pins(aon_iomuxcfilepath, chipinfo[j].pininfo, 64, &chipinfo[j]); // aon_gpio_0
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			pinCount = parse_pinmux_file(aon_iomuxcpinmuxfilename, chipinfo[j].pininfo, MAX_PINS, &chipinfo[j]);
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			parse_pinconf_file(aon_pinconffilename, chipinfo[j].pininfo, pinCount);
			sort_pin_info(chipinfo[j].pininfo, pinCount);
			each_convert_to_uppercase(&chipinfo[j],pinCount);
		}

		if (strcmp(chipinfo[j].controlname, "32150000.gpio") == 0) {
			int pinCount = parse_pins(dsp_iomuxcfilepath, chipinfo[j].pininfo, 64, &chipinfo[j]); // dsp_gpio0
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			pinCount = parse_pinmux_file(dsp_iomuxcpinmuxfilename, chipinfo[j].pininfo, MAX_PINS, &chipinfo[j]);
			if (pinCount < 0) {
				return EXIT_FAILURE;
			}
			parse_pinconf_file(dsp_pinconffilename, chipinfo[j].pininfo, pinCount);
			sort_pin_info(chipinfo[j].pininfo, pinCount);
			each_convert_to_uppercase(&chipinfo[j],pinCount);
		}

	}
#endif //PLATEFORM_HOBOTX5_GPIOPARSE

	i = parse_config(argc, argv, &cfg);
	argc -= i;
	argv += i;

	if (!cfg.chip_id)
		cfg.by_name = true;

	num_chips = chip_paths(cfg.chip_id, &paths);
	if (cfg.chip_id && (num_chips == 0))
		die("cannot find GPIO chip character device '%s'", cfg.chip_id);

	resolver = resolver_init(argc, argv, num_chips, cfg.strict,
			cfg.by_name);

	for (i = 0; i < num_chips; i++) {
		chip = gpiod_chip_open(paths[i]);
		if (chip) {
			list_lines(resolver, chip, i, &cfg);
			gpiod_chip_close(chip);
		} else {
			print_perror("unable to open chip '%s'", paths[i]);

			if (cfg.chip_id)
				return EXIT_FAILURE;

			ret = EXIT_FAILURE;
		}
		free(paths[i]);
	}
	free(paths);

	validate_resolution(resolver, cfg.chip_id);
	if (argc && resolver->num_found != argc)
		ret = EXIT_FAILURE;
	free(resolver);

	return ret;
}
