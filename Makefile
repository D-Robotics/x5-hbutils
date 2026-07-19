include Makefile.in
$(shell mkdir -p ${MODULE_OUT_DIR})
.PHONY: all deb clean utility timesync log hb_gpioinfo boardinfo getdmkey qos_tool

# get commit id message
ifeq ($(wildcard $(PWD)/.git),)
    GIT_COMMIT_HASH := unknown
else
    GIT_COMMIT_HASH := $(shell cd ${PWD} && git rev-parse --verify --short HEAD 2>/dev/null)
    ifeq ($(GIT_COMMIT_HASH),)
        $(warning git rev-parse failed in $(PWD), GIT_COMMIT_HASH set to unknown)
        GIT_COMMIT_HASH := unknown
    else
        $(info Git commit hash: $(GIT_COMMIT_HASH))
        GIT_STATUS := $(shell cd ${PWD} && git diff-index --name-only HEAD 2>/dev/null)
        ifneq ($(strip $(GIT_STATUS)),)
            GIT_COMMIT_HASH := ${GIT_COMMIT_HASH}-dirty
        endif
    endif
endif

# 打印 GIT_COMMIT_HASH 的值
# $(info GIT_COMMIT_HASH is: $(GIT_COMMIT_HASH))

EXTRA_CFLAGS += -DGIT_COMMIT_HASH=\"$(GIT_COMMIT_HASH)\"
export EXTRA_CFLAGS

all: utility timesync log hb_gpioinfo boardinfo getdmkey qos_tool deb

utility:
	make all -C utility

log:
	make all -C log

timesync:
	make all -C remoteproc_timesync_utility

hb_gpioinfo:
	make all -C hb_gpioinfo

boardinfo:
	make all -C boardinfo

getdmkey:
	make all -C getdmkey

qos_tool:
	make all -C qos_tool

clean:
	rm -rf ${MODULE_OUT_DIR}/*
	rm -f ${PRODUCT_DEB_DIR}/*/${MODULE_NAME}*.deb
	rm -f ${PRODUCT_DEB_DIR}/*/${MODULE_NAME}*.tar.xz
