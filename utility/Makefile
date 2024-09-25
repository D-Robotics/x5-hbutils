MODULE_OUT_DIR_UTILITY = ${MODULE_OUT_DIR}/hbutils/utility
$(shell mkdir -p ${MODULE_OUT_DIR_UTILITY})

SRCS_C = $(wildcard *.c)
OBJS = $(addprefix ${MODULE_OUT_DIR_UTILITY}/, $(patsubst %.c, %.o, ${SRCS_C}))
OBJECT = $(addprefix ${MODULE_OUT_DIR_UTILITY}/, $(patsubst %.c, %, ${SRCS_C}))

INCS := -I./ $(INCS)
LIBS += -lalog -lpthread

.PHONY: all clean install
all: $(OBJECT) install

${MODULE_OUT_DIR_UTILITY}/%.o:./%.c
	@$(CC) $(CFLAGS) $(INCS) $(LDFLAGS) -o $@ -c $<

${MODULE_OUT_DIR_UTILITY}/%:${MODULE_OUT_DIR_UTILITY}/%.o
	@echo CC $@
	@$(CC) $(CFLAGS) $(INCS) $(LDFLAGS) -o $@ $^


${MODULE_OUT_DIR_UTILITY}/%:${MODULE_OUT_DIR_UTILITY}/%.o
	@echo CC $@
	$(Q)$(CC) $(CFLAGS) $< $(INCS) $(LDFLAGS) $(LIBS) -o $@


install: $(OBJECT)
	$(Q)install -d ${MODULE_RUNTIME_DIR}/usr/hobot/bin/
	install $(OBJECT) ${MODULE_RUNTIME_DIR}/usr/hobot/bin/
	install ./prebuilds/* ${MODULE_RUNTIME_DIR}/usr/hobot/bin/

clean:
	rm -rf ${MODULE_OUT_DIR_UTILITY}/*
