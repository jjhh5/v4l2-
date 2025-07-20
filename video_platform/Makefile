# 内核源码树的路径
KERN_DIR = /home/lubancat/lubancat_kernel/kernel

# 默认的架构和交叉编译器
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

# 要编译的模块
obj-m += video_drv.o
obj-m += video_dev.o

# "all" 目标：编译模块 (这一部分是正确的)
all:
	make -C $(KERN_DIR) M=$(shell pwd) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

# "clean" 目标：清理模块 (【已修正】)
clean:
	rm -f *.o *.o.d *.ko *.mod *.mod.c *.order *.symvers .*.cmd .tmp_versions/*

# "distclean" 目标：更彻底的清理
distclean:
	rm -f *.o *.ko *.mod *.mod.c *.order *.symvers .*.cmd .tmp_versions/* Module.markers modules.builtin

.PHONY: all clean distclean