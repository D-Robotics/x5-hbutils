include Makefile.in
$(shell mkdir -p ${MODULE_OUT_DIR})
.PHONY: all deb clean utility timesync log hb_gpioinfo

all: utility timesync log deb hb_gpioinfo

utility:
	make all -C utility

log:
	make all -C log

timesync:
	make all -C remoteproc_timesync_utility

hb_gpioinfo:
	make all -C hb_gpioinfo
clean:
	rm -rf ${MODULE_OUT_DIR}/*
	rm -f ${PRODUCT_DEB_DIR}/*/${MODULE_NAME}*.deb
	rm -f ${PRODUCT_DEB_DIR}/*/${MODULE_NAME}*.tar.xz
