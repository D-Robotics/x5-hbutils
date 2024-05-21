MODULE_OUT_DIR_LOG = ${MODULE_OUT_DIR}/log
$(shell mkdir -p ${MODULE_OUT_DIR_LOG})

SRCS_C = $(filter-out chip_log.c, $(wildcard *.c))
OBJS = $(addprefix ${MODULE_OUT_DIR_LOG}/, $(patsubst %.c, %.o, ${SRCS_C}))
OBJECT = $(addprefix ${MODULE_OUT_DIR_LOG}/, $(patsubst %.c, %, ${SRCS_C}))

INCS := -I./ $(INCS)
LIBS += -lalog -lpthread

.PHONY: all clean install
all: $(OBJECT) install

${MODULE_OUT_DIR_LOG}/%:${MODULE_OUT_DIR_LOG}/%.o
	@echo CC $@
	$(Q)$(CC) $(CFLAGS) $< $(INCS) $(LDFLAGS) $(LIBS) -o $@

${MODULE_OUT_DIR_LOG}/%.o:%.c
	$(Q)$(CC) $(CFLAGS) -c $< $(INCS) $(LDFLAGS) $(LIBS) -o $@

install: $(OBJECT)
ifeq ($(HB_PKG_HBUTILS_RUNTIME_DEB), y)
	$(Q)install -d ${MODULE_RUNTIME_DIR}/usr/hobot/bin/
	install $(OBJECT) ${MODULE_RUNTIME_DIR}/usr/hobot/bin/
endif

clean:
	rm -rf ${MODULE_OUT_DIR_LOG}/*
