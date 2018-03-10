SRC	:= main.c
PRG	:= usbboot
RUN	= sudo ./$(PRG)

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
