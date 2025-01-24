include Makefile.in
$(shell mkdir -p ${MODULE_OUT_DIR})
.PHONY: all deb clean utility timesync log hb_gpioinfo boardinfo

# get commit id message
ifeq ($(wildcard $(PWD)/.git),)
    GIT_COMMIT_HASH := unknown
else
    GIT_COMMIT_HASH := $(shell cd ${PWD} && git rev-parse --verify --short HEAD 2>&1)
    $(info Git commit hash: $(GIT_COMMIT_HASH))
    # 如果 git 命令失败，输出错误信息
    ifeq ($(findstring fatal,$(GIT_COMMIT_HASH)),fatal)
        $(error Git command failed: $(GIT_COMMIT_HASH))
    endif
    GIT_STATUS := $(shell cd ${PWD} && git diff-index --name-only HEAD)
    ifneq ($(strip $(GIT_STATUS)),)
        GIT_COMMIT_HASH := ${GIT_COMMIT_HASH}-dirty
    endif
endif

# 打印 GIT_COMMIT_HASH 的值
# $(info GIT_COMMIT_HASH is: $(GIT_COMMIT_HASH))

EXTRA_CFLAGS += -DGIT_COMMIT_HASH=\"$(GIT_COMMIT_HASH)\"
export EXTRA_CFLAGS

all: utility timesync log deb hb_gpioinfo boardinfo

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

clean:
	rm -rf ${MODULE_OUT_DIR}/*
	rm -f ${PRODUCT_DEB_DIR}/*/${MODULE_NAME}*.deb
	rm -f ${PRODUCT_DEB_DIR}/*/${MODULE_NAME}*.tar.xz
