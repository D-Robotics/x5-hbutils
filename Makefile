include Makefile.in
$(shell mkdir -p ${MODULE_OUT_DIR})
.PHONY: all deb clean utility timesync log

all: utility timesync log deb

utility:
	make all -C utility

log:
	make all -C log

timesync:
	make all -C remoteproc_timesync_utility

clean:
	rm -rf ${MODULE_OUT_DIR}/*
	rm -f ${PRODUCT_DEB_DIR}/*/${MODULE_NAME}*.deb
	rm -f ${PRODUCT_DEB_DIR}/*/${MODULE_NAME}*.tar.xz
