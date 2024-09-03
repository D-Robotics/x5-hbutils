MODULE_OUT_DIR_HB_GPIOINFO = ${MODULE_OUT_DIR}/hbutils/hb_gpioinfo
$(shell mkdir -p ${MODULE_OUT_DIR_HB_GPIOINFO})

SRCS_C = $(wildcard *.c)
OBJS = $(addprefix ${MODULE_OUT_DIR_HB_GPIOINFO}/, $(patsubst %.c, %.o, ${SRCS_C}))
OBJECT = ${MODULE_OUT_DIR_HB_GPIOINFO}/hb_gpioinfo

INCS := -I./ $(INCS)

LDFLAGS = -L${OUTPUT_BUILD_DIR}/../deploy/system/usr/lib/ -lgpiod
CFLAGS += -fno-exceptions

.PHONY: all clean install

all: $(OBJECT) install

${MODULE_OUT_DIR_HB_GPIOINFO}/%.o: ./%.c
	@$(CC) $(CFLAGS) $(INCS) -c $< -o $@

$(OBJECT): $(OBJS)
	@echo "Linking $@"
	@$(CC) $(OBJS) $(LDFLAGS) -o $@

install: $(OBJECT)
	$(Q)install -d ${MODULE_RUNTIME_DIR}/usr/hobot/bin/
	install $(OBJECT) ${MODULE_RUNTIME_DIR}/usr/hobot/bin/

clean:
	rm -rf ${MODULE_OUT_DIR_HB_GPIOINFO}/*
