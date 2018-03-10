#include <stdio.h>
#include <libusb.h>

#define VID	0x601a
#define PID	0x4740

#define VR_GET_CPU_INFO		0x00
#define VR_SET_DATA_ADDRESS	0x01
#define VR_SET_DATA_LENGTH	0x02
#define VR_FLUSH_CACHES		0x03
#define VR_PROGRAM_START1	0x04
#define VR_PROGRAM_START2	0x05

// Get CPU information
int getCPUInfo(libusb_device_handle *dev)
{
	char data[9];
	if (libusb_control_transfer(dev, 0xc0, VR_GET_CPU_INFO,
				0, 0, (unsigned char *)data, 8, 0) != 8)
		return 0;
	data[8] = 0;
	printf("CPU info: %s\n", data);
	return 1;
}

// Set address for next bulk-in/bulk-out transfer
int setAddress(libusb_device_handle *dev, uint32_t addr)
{
	return libusb_control_transfer(dev, 0x40, VR_SET_DATA_ADDRESS,
				addr >> 16, addr & 0xffff, 0, 0, 0) == 0;
}

// Set length in byte for next bulk-in/bulk-out transfer
int setLength(libusb_device_handle *dev, uint32_t len)
{
	return libusb_control_transfer(dev, 0x40, VR_SET_DATA_LENGTH,
				len >> 16, len & 0xffff, 0, 0, 0) == 0;
}

// Flush I-Cache and D-Cache
int flushCaches(libusb_device_handle *dev)
{
	return libusb_control_transfer(dev, 0x40, VR_FLUSH_CACHES,
				0, 0, 0, 0, 0) == 0;
}

// Transfer data from D-Cache to I-Cache and branch to address in I-Cache
int programStart1(libusb_device_handle *dev, uint32_t entry)
{
	return libusb_control_transfer(dev, 0x40, VR_PROGRAM_START1,
				entry >> 16, entry & 0xffff, 0, 0, 0) == 0;
}

// Branch to target address directly
int programStart2(libusb_device_handle *dev, uint32_t entry)
{
	return libusb_control_transfer(dev, 0x40, VR_PROGRAM_START2,
				entry >> 16, entry & 0xffff, 0, 0, 0) == 0;
}

int main()
{
	libusb_context *ctx;
	libusb_init(&ctx);

	libusb_device_handle *dev =
		libusb_open_device_with_vid_pid(ctx, VID, PID);
	if (dev) {
		printf("Device %04x:%04x opened.\n", VID, PID);
		getCPUInfo(dev);
		if (!flushCaches(dev))
			printf("Error flush caches\n");
		libusb_close(dev);
	}

	libusb_exit(ctx);
	return 0;
}
