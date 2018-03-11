#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libusb.h>
#include <hand.h>
#include "usbboot.h"

#define MAX_PAGES	32	// Limited by device bulk endpoint buffer sizes

void hexDump(void *addr, int len);
int setAddress(libusb_device_handle *dev, uint32_t addr);
int setLength(libusb_device_handle *dev, uint32_t len);
int transferData(libusb_device_handle *dev, uint32_t rw, uint32_t size, void *p);
int receiveHandshake(libusb_device_handle *dev, uint16_t *hs);
int checkConfig();

extern hand_t cfg_hand;

static int nandOp(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint8_t op)
{
	uint16_t wValue = ((uint16_t)opt << 12) | ((uint16_t)cs << 4) | ((uint16_t)op);
	int err = libusb_control_transfer(dev, 0x40, VR_NAND_OPS, wValue, 0, 0, 0, 0);
	if (err)
		fprintf(stderr, "Error initiating NAND operation: %s\n", libusb_strerror(err));
	return err;
}

int nandQuery(libusb_device_handle *dev, uint8_t cs)
{
	if (nandOp(dev, cs, 0, NAND_QUERY))
		return 1;
	uint8_t id[8];
	if (transferData(dev, 1, 8, id))
		return 2;
	printf("NAND VID:       0x%02x\n", id[0]);
	printf("NAND PID:       0x%02x\n", id[1]);
	printf("NAND CHIP ID:   0x%02x\n", id[2]);
	printf("NAND PAGE ID:   0x%02x\n", id[3]);
	printf("NAND PLANE ID:  0x%02x\n", id[4]);
	return receiveHandshake(dev, NULL);
}

int nandInit(libusb_device_handle *dev, uint8_t cs)
{
	return nandOp(dev, cs, 0, NAND_INIT);
}

static int nandRead(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num, void *p)
{
	if (!num)
		return 0;
	if (setAddress(dev, page)) {
		fprintf(stderr, "Error setting start page %u\n", page);
		return 2;
	}
	if (setLength(dev, num)) {
		fprintf(stderr, "Error setting number of pages %u\n", num);
		return 3;
	}
	if (nandOp(dev, cs, opt, NAND_READ))
		return 4;
	// Calculate data size
	uint32_t size = ((opt == NO_OOB ? 0 : cfg_hand.nand_os) + \
			cfg_hand.nand_ps) * num;
	if (transferData(dev, 1, size, p))
		return 5;
	return receiveHandshake(dev, NULL);
}

int nandReadMem(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num, void *p)
{
	if (checkConfig())
		return 1;
	while (num) {
		uint32_t n = num >= MAX_PAGES ? MAX_PAGES : num;
		int err = nandRead(dev, cs, opt, page, n, p);
		if (err)
			return err;
		page += n;
		num -= n;
		// Calculate data size
		uint32_t size = ((opt == NO_OOB ? 0 : cfg_hand.nand_os) + \
				cfg_hand.nand_ps) * n;
		p += size;
	}
	return 0;
}

int nandDump(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num)
{
	uint32_t size = ((opt == NO_OOB ? 0 : cfg_hand.nand_os) + \
			cfg_hand.nand_ps) * num;
	void *mem = malloc(size);
	int err = nandReadMem(dev, cs, opt, page, num, mem);
	if (!err) {
		void *p = mem;
		for (; num--; page++) {
			printf("NAND page %u data:\n", page);
			hexDump(p, cfg_hand.nand_ps);
			p += cfg_hand.nand_ps;

			if (opt == NO_OOB)
				continue;
			printf("NAND page %u OOB data:\n", page);
			hexDump(p, cfg_hand.nand_os);
			p += cfg_hand.nand_os;
		}
	}
	free(mem);
	return err;
}

int nandUploadFile(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num, const char *file)
{
	if (checkConfig())
		return 1;
	printf("Uploading %u pages from page %u to file %s...\n", num, page, file);

	// Open file for write
	int fd = open(file, O_CREAT | O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Error opening file: %s\n", strerror(errno));
		return 1;
	}

	// Calculate data size
	uint32_t size = ((opt == NO_OOB ? 0 : cfg_hand.nand_os) + \
			cfg_hand.nand_ps) * num;

	// Resize file
	if (ftruncate(fd, size)) {
		fprintf(stderr, "Error resizing file: %s\n", strerror(errno));
		close(fd);
		return 2;
	}

	// Memory map
	void *p = mmap(0, size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "Error mapping file to memory: %s\n", strerror(errno));
		close(fd);
		return 3;
	}

	if (nandReadMem(dev, cs, opt, page, num, p)) {
		munmap(p, size);
		close(fd);
		return 4;
	}

	munmap(p, size);
	close(fd);
	return 0;
}

