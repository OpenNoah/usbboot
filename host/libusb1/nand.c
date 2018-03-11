#include <stdint.h>
#include <stdio.h>
#include <libusb.h>
#include <usb_boot.h>
#include "usbboot.h"

int transferData(libusb_device_handle *dev, uint32_t rw, uint32_t size, void *p);
int receiveHandshake(libusb_device_handle *dev, uint16_t *hs);

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
	// Handshake packet
	return receiveHandshake(dev, NULL);
}
