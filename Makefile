ccflags-y := -std=gnu99 -Wno-declaration-after-statement
MODULES = charDeviceDriver.ko charDeviceDriverBlocking.ko
obj-m += charDeviceDriver.o charDeviceDriverBlocking.o

all: $(MODULES)

charDeviceDriver.ko: charDeviceDriver.c charDeviceDriver.h
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

charDeviceDriverBlocking.ko: charDeviceDriverBlocking.c charDeviceDriver.h
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

