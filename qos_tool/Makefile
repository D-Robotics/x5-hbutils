CUR_DIR := $(shell pwd)

.PHONY: all clean install

all: install
	@echo "qos_tool: shell scripts, no build required"

clean:
	@echo "qos_tool: nothing to clean"

install:
	$(Q)install -d ${MODULE_OUT_DIR}/hbutils-runtime/usr/hobot/bin/
	$(Q)install -m 0755 ${CUR_DIR}/qos_tool.sh \
		${MODULE_OUT_DIR}/hbutils-runtime/usr/hobot/bin/qos_tool.sh
