SRC	:= main.c
PRG	:= usbboot

S1FILE	?= main.c
S1ADDR	?= 0x80002000
S1SIZE	?= $(shell wc -c $(S1FILE) | awk '{print $$1}')

ARGS	?= cpu write $(S1ADDR) $(S1FILE) read $(S1ADDR) $(S1SIZE) tmp.out
RUN	?= sudo ./$(PRG) $(ARGS)

CFLAGS	+= -Wall -Werror $(shell pkg-config --cflags libusb-1.0)
LIBS	+= $(shell pkg-config --libs libusb-1.0)

.PHONY: all
all: $(PRG)

$(PRG): $(SRC:.c=.o)
	$(CC) -o $@ $^ $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: run
run: $(PRG)
	$(RUN)

.PHONY: clean
clean:
	rm -f $(PRG) $(SRC:.c=.o)
