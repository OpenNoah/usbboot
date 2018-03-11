#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <libusb.h>
#include <hand.h>
#include "usbboot.h"

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

int nandReadMem(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num, void *p)
{
	if (checkConfig())
		return 1;
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

int nandDump(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num)
{
	uint32_t size = ((opt == NO_OOB ? 0 : cfg_hand.nand_os) + \
			cfg_hand.nand_ps) * num;
	void *mem = malloc(size);
	int err = nandReadMem(dev, cs, opt, page, num, mem);
	if (!err) {
		void *p = mem;
		while (num--) {
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
