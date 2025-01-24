OUT_DIR := ${MODULE_OUT_DIR}/boardinfo
SUB_MODULE_OUT_DIR := $(OUT_DIR)

.PHONY: all build install clean

all: build install

LIB_VERSION_MAJOR := 1
LIB_VERSION_MINOR := 0
LIB_VERSION_PATCH := 0
ULTIS_VERSION = $(LIB_VERSION_MAJOR).$(LIB_VERSION_MINOR).$(LIB_VERSION_PATCH)
DEFINES += -DVERSION=\"${ULTIS_VERSION}\"
DEFINES += $(EXTRA_CFLAGS)
$(info ULTIS_DEFINES is: $(DEFINES))

LINKNAME := libboardinfo.so
SONAME := $(LINKNAME).$(LIB_VERSION_MAJOR)
RELNAME := $(SONAME).$(LIB_VERSION_MINOR).$(LIB_VERSION_PATCH)

SRCS     := $(wildcard ./src/*.c)
OBJS     := $(addprefix ${OUT_DIR}/, $(patsubst %.c, %.o, ${SRCS}))

LIB_SO := ${OUT_DIR}/$(RELNAME)
OBJECT = $(LIB_SO)

CFLAGS += $(DEFINES)
CFLAGS += -Wall
CFLAGS_DYNAMIC := -fPIC -shared
INCS := -I./include $(INCS)

build: ${OBJECT}

$(LIB_SO): $(OBJS)
	$(Q)mkdir -p $(abspath $(dir $@))
	$(Q)echo CC $@
	$(Q)$(CC) $(CFLAGS) $(CFLAGS_DYNAMIC) $(INCS) $^ $(LDFLAGS) $(LIBS) -Wl,-soname,$(SONAME) -o $@
	ln -rsf $@ ${OUT_DIR}/$(LINKNAME)

${OUT_DIR}/%.o: %.c
	$(Q)mkdir -p $(abspath $(dir $@))
	$(Q)echo CC $@
	$(Q)${CC} $(CFLAGS) $(CFLAGS_DYNAMIC) $(INCS) -c $< -o $@

clean:
	echo ${OUT_DIR}
	rm -rf ${OUT_DIR}/*.o $(OBJECT)

install: build
ifeq ($(HB_PKG_HBUTILS_RUNTIME_DEB), y)
	$(Q)install -d ${MODULE_RUNTIME_DIR}/usr/hobot/lib
	install $(LIB_SO) -m 644 ${MODULE_RUNTIME_DIR}/usr/hobot/lib
	ln -srf ${MODULE_RUNTIME_DIR}/usr/hobot/lib/$(RELNAME) \
		${MODULE_RUNTIME_DIR}/usr/hobot/lib/$(SONAME)

	$(Q)install -d ${MODULE_RUNTIME_DIR}/usr/hobot/include
	install ${SRC_PATH}/boardinfo/include/boardinfo.h ${MODULE_RUNTIME_DIR}/usr/hobot/include
endif

ifeq ($(HB_PKG_HBUTILS_DEV_DEB), y)
	$(Q)install -d ${MODULE_DEV_DIR}/usr/hobot/lib
	install $(LIB_SO) -m 644 ${MODULE_DEV_DIR}/usr/hobot/lib
	ln -srf ${MODULE_DEV_DIR}/usr/hobot/lib/$(RELNAME) \
		${MODULE_DEV_DIR}/usr/hobot/lib/$(SONAME)
	ln -srf ${MODULE_DEV_DIR}/usr/hobot/lib/$(RELNAME) \
		${MODULE_DEV_DIR}/usr/hobot/lib/$(LINKNAME)

	$(Q)install -d ${MODULE_DEV_DIR}/usr/hobot/include
	install ${SRC_PATH}/boardinfo/include/boardinfo.h ${MODULE_DEV_DIR}/usr/hobot/include

endif

	@if [ -n "${OUTPUT_HBROOTFS_DIR}" ]; then \
		mkdir -p ${OUTPUT_HBROOTFS_DIR}/usr/lib/; \
		ln -srf $(OBJECT) ${OUTPUT_HBROOTFS_DIR}/usr/lib/${LINKNAME}; \
		ln -srf ${SRC_PATH}/boardinfo/include/boardinfo.h ${OUTPUT_HBROOTFS_DIR}/include/boardinfo.h; \
	fi
