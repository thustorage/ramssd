
OBJS = ssd_address.o ssd_block.o ssd_bus.o ssd_channel.o ssd_config.o ssd_die.o ssd_event.o ssd_package.o ssd_page.o ssd_plane.o ssd_quicksort.o ssd_ssd.o

obj-m += ramssd.o

ramssd-objs:= brd.o ${OBJS}


# gcc -O2 (the kernel default)  is overaggressive on ppc32 when many inline
# functions are used.  This causes the compiler to advance the stack
# pointer out of the available stack space, corrupting kernel space,
# and causing a panic. Since this behavior only affects ppc32, this ifeq
# will work around it. If any other architecture displays this behavior,
# add it here.
ifeq ($(CONFIG_PPC32),y)
EXTRA_CFLAGS := $(call cc-ifversion, -lt, 0400, -O1)
endif
#EXTRA_CFLAGS+=-D_DEBUG
EXTRA_CFLAGS+=-DFOR_JASMINE
#EXTRA_CFLAGS+=-DFOR_JASMINE -DNO_PERSIST
EXTRA_CFLAGS+=-DENABLE_UPS

K_DIR = $(shell uname -r)
KERNELDIR:=/lib/modules/$(K_DIR)/build

PWD:=$(shell pwd)

default:
	make -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -rf *.o *.mod.c *.ko *.symvers  *.order
