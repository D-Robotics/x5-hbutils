MODULE_OUT_DIR_LOG = ${MODULE_OUT_DIR}/getdmkey
$(shell mkdir -p ${MODULE_OUT_DIR_LOG})

SRCS_C = $(wildcard *.c)
OBJS = $(addprefix ${MODULE_OUT_DIR_LOG}/, $(patsubst %.c, %.o, ${SRCS_C}))
OBJECT = $(addprefix ${MODULE_OUT_DIR_LOG}/, $(patsubst %.c, %, ${SRCS_C}))

LIBS += -lteec

.PHONY: all clean install
all: $(OBJECT) install

${MODULE_OUT_DIR_LOG}/%:${MODULE_OUT_DIR_LOG}/%.o
	@echo CC $@
	$(Q)$(CC) $(CFLAGS) $< $(INCS) $(LDFLAGS) $(LIBS) -o $@

${MODULE_OUT_DIR_LOG}/%.o:%.c
	$(Q)$(CC) $(CFLAGS) -c $< $(INCS) $(LDFLAGS) $(LIBS) -o $@

install: $(OBJECT)
	$(Q)install -d ${MODULE_RUNTIME_DIR}/usr/hobot/bin/
	install $(OBJECT) ${MODULE_RUNTIME_DIR}/usr/hobot/bin/

clean:
	rm -rf ${MODULE_OUT_DIR_LOG}/*
