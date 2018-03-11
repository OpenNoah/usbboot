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
int receiveHandshake(libusb_device_handle *dev, void *hs);
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

static int nandTransfer(libusb_device_handle *dev, uint32_t rw, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num, void *p)
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

	// Calculate data size
	uint32_t size = ((opt == NO_OOB ? 0 : cfg_hand.nand_os) + \
			cfg_hand.nand_ps) * num;
	// Read data
	if (rw) {
		if (nandOp(dev, cs, opt, NAND_READ))
			return 4;
		if (transferData(dev, rw, size, p))
			return 5;
		return receiveHandshake(dev, NULL);
	}
	// Program data
	if (transferData(dev, rw, size, p))
		return 6;
	if (nandOp(dev, cs, opt, NAND_PROGRAM))
		return 7;
	uint32_t data[2];
	if (receiveHandshake(dev, data))
		return 8;
	// Return value should be the next page
	if (data[0] != page + num) {
		fprintf(stderr, "Incorrect page number returned: %u\n", data[0]);
		return 9;
	}
	return 0;
}

static int nandTransferMem(libusb_device_handle *dev, uint32_t rw, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num, void *p)
{
	if (checkConfig())
		return 1;
	while (num) {
		uint32_t n = num >= MAX_PAGES ? MAX_PAGES : num;
		int err = nandTransfer(dev, rw, cs, opt, page, n, p);
		if (err)
			return err;
		page += n;
		num -= n;
		// Calculate data size
		uint32_t size = ((opt == NO_OOB ? 0 : cfg_hand.nand_os) + cfg_hand.nand_ps) * n;
		p += size;
	}
	return 0;
}

int nandDump(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num)
{
	uint32_t size = ((opt == NO_OOB ? 0 : cfg_hand.nand_os) + \
			cfg_hand.nand_ps) * num;
	void *mem = malloc(size);
	int err = nandTransferMem(dev, 1, cs, opt, page, num, mem);
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

int nandReadFile(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num, const char *file)
{
	if (checkConfig())
		return 1;
	printf("Reading %u pages from page %u to file %s...\n", num, page, file);

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

	if (nandTransferMem(dev, 1, cs, opt, page, num, p)) {
		munmap(p, size);
		close(fd);
		return 4;
	}

	munmap(p, size);
	close(fd);
	return 0;
}

int nandProgramFile(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, const char *file)
{
	// Get file size
	struct stat st;
	if (stat(file, &st)) {
		fprintf(stderr, "Error getting file stat: %s\n", strerror(errno));
		return 1;
	}
	size_t size = st.st_size;

	if (checkConfig())
		return 1;
	// Calculate page size
	uint32_t ps = (opt == NO_OOB ? 0 : cfg_hand.nand_os) + cfg_hand.nand_ps;
	uint32_t num = (size + ps - 1) / ps;
	printf("Programming %u pages from page %u from file %s of size %lu...\n", num, page, file, size);

	// Open file for read
	int fd = open(file, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Error opening file: %s\n", strerror(errno));
		return 2;
	}

	// Memory map
	void *p = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "Error mapping file: %s\n", strerror(errno));
		close(fd);
		return 3;
	}

	uint32_t n = size / ps;
	if (nandTransferMem(dev, 0, cs, opt, page, n, p)) {
		munmap(p, size);
		close(fd);
		return 4;
	}

	// Extra data not able to fill a page
	if (n != num) {
		void *mem = malloc(ps);
		uint32_t offset = n * ps;
		uint32_t extra = size - offset;
		memcpy(mem, p + offset, extra);
		memset(mem + extra, 0xff, ps - extra);
		if (nandTransfer(dev, 0, cs, opt, page + n, 1, mem)) {
			free(mem);
			munmap(p, size);
			close(fd);
			return 5;
		}
		free(mem);
	}

	munmap(p, size);
	close(fd);
	return 0;
}

int nandErase(libusb_device_handle *dev, uint8_t cs, uint32_t blk, uint32_t num)
{
	if (checkConfig())
		return 1;
	printf("Erasing %u blocks starting from block %u...\n", num, blk);
	if (setAddress(dev, blk)) {
		fprintf(stderr, "Error setting start block %u\n", blk);
		return 2;
	}
	if (setLength(dev, num)) {
		fprintf(stderr, "Error setting number of blocks %u\n", num);
		return 3;
	}
	if (nandOp(dev, cs, 0, NAND_ERASE))
		return 4;
	uint32_t data[2];
	if (receiveHandshake(dev, data))
		return 5;
	// Return value should be the next page
	if (data[0] != (blk + num) * cfg_hand.nand_ppb) {
		fprintf(stderr, "Incorrect page number returned: %u\n", data[0]);
		return 6;
	}
	return 0;
}
