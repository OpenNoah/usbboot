SRC	:= main.c usbboot.c boot.c nand.c common.c
PRG	:= usbboot

S1FILE	?= ../../device/stage1/fw/fw.bin
S2FILE	?= ../../device/stage2/usb_boot.bin
CFGFILE	?= ../../configs/NP1380.cfg

MEMTEST	?= 64M
MEMADDR	?= 0x80000000

MEMFILE	:= random$(MEMTEST).bin
MEMTMP	:= tmp.bin

UBTFILE	?= u-boot-nand.bin
UBTCFG	?= ../../configs/NP1380_uboot.cfg

RESET	:= sudo ../../../tools/reset.sh usb; sleep 1
RUN	:= $(RESET); sudo ./$(PRG) cpu cfg $(CFGFILE) \
	   init $(S1FILE) $(S2FILE) cpu
MEMRUN	:= $(RESET); sudo ./$(PRG) cpu cfg $(CFGFILE) init $(S1FILE) "" \
	   write $(MEMADDR) $(MEMFILE) \
	   read $(MEMADDR) `stat -c %s $(MEMFILE)` $(MEMTMP)
UBTRUN	:= $(RESET); sudo ./$(PRG) cpu cfg $(UBTCFG) init $(S1FILE) $(S2FILE) \
	   cpu nand 0 erase 0 8 nand 0 prog 0 $(UBTFILE); \
	   sudo ../../../tools/reset.sh

CFLAGS	+= -O2 -Wall -Werror $(shell pkg-config --cflags libusb-1.0)
#CFLAGS	+= -I../../device/stage1/fw
CFLAGS	+= -I../../device/stage2/include
LIBS	+= $(shell pkg-config --libs libusb-1.0)

.PHONY: all
all: $(PRG)

$(PRG): $(SRC:.c=.o)
	$(CC) -o $@ $^ $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

random%M.bin:
	dd if=/dev/urandom of=$@ bs=1M count=$*

.PHONY: run
run: $(PRG)
	$(RUN)

.PHONY: memtest
memtest: $(PRG) $(MEMFILE)
	$(MEMRUN)
	sudo cmp $(MEMFILE) $(MEMTMP)

.PHONY: uboot
uboot: $(PRG) $(UBTFILE)
	$(UBTRUN)

.PHONY: clean
clean:
	rm -f $(PRG) $(SRC:.c=.o) random*.bin tmp.bin
