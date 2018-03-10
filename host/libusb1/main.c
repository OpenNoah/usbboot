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

#define VID	0x601a
#define PID	0x4740
#define IFACE	0
#define EP_IN	0x81
#define EP_OUT	0x01

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
		return 1;
	data[8] = 0;
	printf("CPU info: %s\n", data);
	return 0;
}

// Set address for next bulk-in/bulk-out transfer
int setAddress(libusb_device_handle *dev, uint32_t addr)
{
	return libusb_control_transfer(dev, 0x40, VR_SET_DATA_ADDRESS,
				addr >> 16, addr & 0xffff, 0, 0, 0) != 0;
}

// Set length in byte for next bulk-in/bulk-out transfer
int setLength(libusb_device_handle *dev, uint32_t len)
{
	return libusb_control_transfer(dev, 0x40, VR_SET_DATA_LENGTH,
				len >> 16, len & 0xffff, 0, 0, 0) != 0;
}

// Flush I-Cache and D-Cache
int flushCaches(libusb_device_handle *dev)
{
	return libusb_control_transfer(dev, 0x40, VR_FLUSH_CACHES,
				0, 0, 0, 0, 0) != 0;
}

// Transfer data from D-Cache to I-Cache and branch to address in I-Cache
int programStart1(libusb_device_handle *dev, uint32_t entry)
{
	printf("Executing program at 0x%08x...\n", entry);
	return libusb_control_transfer(dev, 0x40, VR_PROGRAM_START1,
				entry >> 16, entry & 0xffff, 0, 0, 0) != 0;
}

// Branch to target address directly
int programStart2(libusb_device_handle *dev, uint32_t entry)
{
	printf("Jump to program at 0x%08x...\n", entry);
	return libusb_control_transfer(dev, 0x40, VR_PROGRAM_START2,
				entry >> 16, entry & 0xffff, 0, 0, 0) != 0;
}

int uploadFile(libusb_device_handle *dev, unsigned long addr, size_t size, const char *file)
{
	printf("Uploading from 0x%08lx of size %lu to file %s...\n", addr, size, file);

	// Set start address
	if (setAddress(dev, addr)) {
		fprintf(stderr, "Error setting data address 0x%08lx\n", addr);
		return 1;
	}

	// Set data length
	if (setLength(dev, size)) {
		fprintf(stderr, "Error setting data length %lu\n", size);
		return 2;
	}

	// Open file for write
	int fd = open(file, O_CREAT | O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Error opening file: %s\n", strerror(errno));
		return 3;
	}

	// Resize file
	if (ftruncate(fd, size)) {
		fprintf(stderr, "Error truncating file: %s\n", strerror(errno));
		close(fd);
		return 4;
	}

	// Memory map
	void *p = mmap(0, size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "Error mapping file to memory: %s\n", strerror(errno));
		close(fd);
		return 5;
	}

	// Bulk transfer
	int len = 0, err;
	if ((err = libusb_bulk_transfer(dev, EP_IN, p, size, &len, 1000)) || len != size) {
		fprintf(stderr, "Error transferring data %u: %s\n", len, libusb_strerror(err));
		munmap(p, size);
		close(fd);
		return 6;
	}

	munmap(p, size);
	close(fd);
	return 0;
}

int downloadFile(libusb_device_handle *dev, unsigned long addr, const char *file)
{
	// Get file size
	struct stat st;
	if (stat(file, &st)) {
		fprintf(stderr, "Error getting file stat: %s\n", strerror(errno));
		return 1;
	}
	size_t size = st.st_size;

	printf("Downloading file %s of size %lu to 0x%08lx...\n", file, size, addr);

	// Set start address
	if (setAddress(dev, addr)) {
		fprintf(stderr, "Error setting data address 0x%08lx\n", addr);
		return 2;
	}

#if 0
	// Set data length
	if (setLength(dev, size)) {
		fprintf(stderr, "Error setting data length %lu\n", size);
		return 3;
	}
#endif

	// Open file for read
	int fd = open(file, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Error opening file: %s\n", strerror(errno));
		return 4;
	}

	// Memory map
	void *p = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "Error mapping file to memory: %s\n", strerror(errno));
		close(fd);
		return 5;
	}

	// Bulk transfer
	int len = 0, err;
	if ((err = libusb_bulk_transfer(dev, EP_OUT, p, size, &len, 1000)) || len != size) {
		fprintf(stderr, "Error transferring data: %s\n", libusb_strerror(err));
		munmap(p, size);
		close(fd);
		return 6;
	}

	munmap(p, size);
	close(fd);
	return 0;
}

void process(libusb_device_handle *dev, int argc, char **argv)
{
	while (++argv, --argc) {
		if (strcmp(*argv, "cpu") == 0) {
			if (getCPUInfo(dev))
				fprintf(stderr, "Error getting CPU info\n");
		} else if (strcmp(*argv, "flush") == 0) {
			if (flushCaches(dev))
				fprintf(stderr, "Error flushing caches\n");
#if 0
		} else if (strcmp(*argv, "addr") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long addr = strtoul(*argv, NULL, 0);
			if (setAddress(dev, addr))
				fprintf(stderr, "Error setting address %s\n", *argv);
		} else if (strcmp(*argv, "len") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long len = strtoul(*argv, NULL, 0);
			if (setLength(dev, len))
				fprintf(stderr, "Error setting data length %s\n", *argv);
#endif
		} else if (strcmp(*argv, "write") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long addr = strtoul(*argv, NULL, 0);
			if (++argv, !--argc)
				return;
			if (downloadFile(dev, addr, *argv))
				fprintf(stderr, "Error writing data from %s\n", *argv);
		} else if (strcmp(*argv, "read") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long addr = strtoul(*argv, NULL, 0);
			if (++argv, !--argc)
				return;
			unsigned long len = strtoul(*argv, NULL, 0);
			if (++argv, !--argc)
				return;
			if (uploadFile(dev, addr, len, *argv))
				fprintf(stderr, "Error reading data to %s\n", *argv);
		} else if (strcmp(*argv, "start1") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long entry = strtoul(*argv, NULL, 0);
			if (programStart1(dev, entry))
				fprintf(stderr, "Error starting at %s\n", *argv);
		} else if (strcmp(*argv, "start2") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long entry = strtoul(*argv, NULL, 0);
			if (programStart1(dev, entry))
				fprintf(stderr, "Error starting at %s\n", *argv);
		}
	}
}

int main(int argc, char *argv[])
{
	libusb_context *ctx;
	libusb_init(&ctx);

	libusb_device_handle *dev = libusb_open_device_with_vid_pid(ctx, VID, PID);
	if (dev) {
		fprintf(stderr, "Device %04x:%04x opened\n", VID, PID);
		if (!libusb_claim_interface(dev, IFACE)) {
			process(dev, argc, argv);
			libusb_release_interface(dev, IFACE);
		}
		libusb_close(dev);
	}

	libusb_exit(ctx);
	return 0;
}
