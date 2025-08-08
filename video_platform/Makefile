KERN_DIR = /home/lubancat/lubancat_kernel/kernel

ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

obj-m += video_drv.o
obj-m += video_dev.o

all:
	make -C $(KERN_DIR) M=$(shell pwd) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	rm -f *.o *.o.d *.ko *.mod *.mod.c *.order *.symvers .*.cmd .tmp_versions/*


distclean:
	rm -f *.o *.ko *.mod *.mod.c *.order *.symvers .*.cmd .tmp_versions/* Module.markers modules.builtin

.PHONY: all clean distclean