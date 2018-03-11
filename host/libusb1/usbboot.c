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
#include <usb_boot.h>
#include "usbboot.h"

#define BLOCK_SIZE	0x00400000	// 4MiB

int getCPUInfo(libusb_device_handle *dev, char *p)
{
	if (libusb_control_transfer(dev, 0xc0, VR_GET_CPU_INFO,
				0, 0, (unsigned char *)p, 8, 0) != 8)
		return 1;
	p[8] = 0;
	printf("CPU info: %s\n", p);
	return 0;
}

int setAddress(libusb_device_handle *dev, uint32_t addr)
{
	return libusb_control_transfer(dev, 0x40, VR_SET_DATA_ADDRESS,
				addr >> 16, addr & 0xffff, 0, 0, 0) != 0;
}

int setLength(libusb_device_handle *dev, uint32_t len)
{
	return libusb_control_transfer(dev, 0x40, VR_SET_DATA_LENGTH,
				len >> 16, len & 0xffff, 0, 0, 0) != 0;
}

int flushCaches(libusb_device_handle *dev)
{
	return libusb_control_transfer(dev, 0x40, VR_FLUSH_CACHES,
				0, 0, 0, 0, 0) != 0;
}

int programStart1(libusb_device_handle *dev, uint32_t entry)
{
	printf("Executing program at 0x%08x...\n", entry);
	return libusb_control_transfer(dev, 0x40, VR_PROGRAM_START1,
				entry >> 16, entry & 0xffff, 0, 0, 0) != 0;
}

int programStart2(libusb_device_handle *dev, uint32_t entry)
{
	printf("Jump to program at 0x%08x...\n", entry);
	return libusb_control_transfer(dev, 0x40, VR_PROGRAM_START2,
				entry >> 16, entry & 0xffff, 0, 0, 0) != 0;
}

int transferData(libusb_device_handle *dev, uint32_t rw, uint32_t size, void *p)
{
	unsigned char ep = rw ? USBBOOT_EP_IN : USBBOOT_EP_OUT;
	int progress = size >= BLOCK_SIZE;
	while (size) {
		size_t s = size >= BLOCK_SIZE ? BLOCK_SIZE : size;
		// Bulk transfer
		int len = 0, err;
		if ((err = libusb_bulk_transfer(dev, ep, p, s, &len, 0)) || len != s) {
			fprintf(stderr, "Error transferring data %u: %s\n", len, libusb_strerror(err));
			return 1;
		}
		if (progress)
			putchar('.');
		fflush(stdout);
		size -= s;
		p += s;
	}
	if (progress)
		putchar('\n');
	return 0;
}

int readMem(libusb_device_handle *dev, uint32_t addr, uint32_t size, void *p)
{
	// Set start address
	if (setAddress(dev, addr)) {
		fprintf(stderr, "Error setting data address 0x%08x\n", addr);
		return 1;
	}

	// Set data length
	if (setLength(dev, size)) {
		fprintf(stderr, "Error setting data length %u\n", size);
		return 2;
	}

	return transferData(dev, 1, size, p);
}

int readFile(libusb_device_handle *dev, uint32_t addr, uint32_t size, const char *file)
{
	printf("Reading from 0x%08x of size %u to file %s...\n", addr, size, file);

	// Open file for write
	int fd = open(file, O_CREAT | O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Error opening file: %s\n", strerror(errno));
		return 1;
	}

	// Resize file
	if (ftruncate(fd, size)) {
		fprintf(stderr, "Error resizing file: %s\n", strerror(errno));
		close(fd);
		return 2;
	}

	// Memory map
	void *p = mmap(0, size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "Error mapping file: %s\n", strerror(errno));
		close(fd);
		return 3;
	}

	if (readMem(dev, addr, size, p)) {
		munmap(p, size);
		close(fd);
		return 4;
	}

	munmap(p, size);
	close(fd);
	return 0;
}

int writeMem(libusb_device_handle *dev, uint32_t addr, uint32_t size, const void *p)
{
	// Set start address
	if (setAddress(dev, addr)) {
		fprintf(stderr, "Error setting data address 0x%08x\n", addr);
		return 1;
	}

#if 0
	// Set data length
	if (setLength(dev, size)) {
		fprintf(stderr, "Error setting data length %lu\n", size);
		return 2;
	}
#endif

	return transferData(dev, 0, size, (void *)p);
}

int writeFile(libusb_device_handle *dev, uint32_t addr, const char *file)
{
	// Get file size
	struct stat st;
	if (stat(file, &st)) {
		fprintf(stderr, "Error getting file stat: %s\n", strerror(errno));
		return 1;
	}
	size_t size = st.st_size;

	printf("Writing file %s of size %lu to 0x%08x...\n", file, size, addr);

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

	if (writeMem(dev, addr, size, p)) {
		munmap(p, size);
		close(fd);
		return 4;
	}

	munmap(p, size);
	close(fd);
	return 0;
}
