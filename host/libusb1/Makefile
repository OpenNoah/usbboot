SRC	:= main.c usbboot.c
PRG	:= usbboot

CFGFILE	?= ../../configs/NP1380.cfg

S1FILE	?= ../../device/stage1/fw/fw.bin
S1ADDR	?= 0x80002000

ARGS	?= cpu write $(S1ADDR) $(S1FILE) fwcfg $(CFGFILE) start1 $(S1ADDR) \
	   usleep 100000 cpu
RUN	?= sudo ../../../tools/reset.sh usb; sleep 1; sudo ./$(PRG) $(ARGS)

MEMTEST	?= 64M
MEMADDR	?= 0x20000000
MEMFILE	:= random$(MEMTEST).bin
MEMTMP	:= tmp.bin
MEMRUN	:= sudo ./$(PRG) write $(MEMADDR) $(MEMFILE) \
	   read $(MEMADDR) `stat -c %s $(MEMFILE)` $(MEMTMP)

CFLAGS	+= -O2 -Wall -Werror $(shell pkg-config --cflags libusb-1.0)
CFLAGS	+= -I../../device/stage1/fw
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
memtest: run $(MEMFILE)
	$(MEMRUN)
	sudo cmp $(MEMFILE) $(MEMTMP)

.PHONY: clean
clean:
	rm -f $(PRG) $(SRC:.c=.o) random*.bin tmp.bin
