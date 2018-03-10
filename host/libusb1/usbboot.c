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
#include <configs.h>
#include "usbboot.h"

#define FW_ARGS_ADDR	0x80002008

#define VR_GET_CPU_INFO		0x00
#define VR_SET_DATA_ADDRESS	0x01
#define VR_SET_DATA_LENGTH	0x02
#define VR_FLUSH_CACHES		0x03
#define VR_PROGRAM_START1	0x04
#define VR_PROGRAM_START2	0x05

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

int readMem(libusb_device_handle *dev, unsigned long addr, size_t size, void *p)
{
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

	// Bulk transfer
	int len = 0, err;
	if ((err = libusb_bulk_transfer(dev, USBBOOT_EP_IN, p, size, &len, 5000)) || len != size) {
		fprintf(stderr, "Error transferring data %u: %s\n", len, libusb_strerror(err));
		return 3;
	}

	return 0;
}

int uploadFile(libusb_device_handle *dev, unsigned long addr, size_t size, const char *file)
{
	printf("Uploading from 0x%08lx of size %lu to file %s...\n", addr, size, file);

	// Open file for write
	int fd = open(file, O_CREAT | O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Error opening file: %s\n", strerror(errno));
		return 1;
	}

	// Resize file
	if (ftruncate(fd, size)) {
		fprintf(stderr, "Error truncating file: %s\n", strerror(errno));
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

	if (readMem(dev, addr, size, p)) {
		munmap(p, size);
		close(fd);
		return 4;
	}

	munmap(p, size);
	close(fd);
	return 0;
}

int writeMem(libusb_device_handle *dev, unsigned long addr, size_t size, const void *p)
{
	// Set start address
	if (setAddress(dev, addr)) {
		fprintf(stderr, "Error setting data address 0x%08lx\n", addr);
		return 1;
	}

#if 0
	// Set data length
	if (setLength(dev, size)) {
		fprintf(stderr, "Error setting data length %lu\n", size);
		return 2;
	}
#endif

	// Bulk transfer
	int len = 0, err;
	if ((err = libusb_bulk_transfer(dev, USBBOOT_EP_OUT, (void *)p, size, &len, 5000)) || len != size) {
		fprintf(stderr, "Error transferring data: %s\n", libusb_strerror(err));
		return 3;
	}

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

	// Open file for read
	int fd = open(file, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Error opening file: %s\n", strerror(errno));
		return 2;
	}

	// Memory map
	void *p = mmap(0, size, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "Error mapping file to memory: %s\n", strerror(errno));
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

int fwConfigFile(libusb_device_handle *dev, const char *file)
{
	// Initial fallback values for testing
	fw_args_t args = {
		// CPU ID
		.cpu_id = 0x4740,	// JZ4740
		// PLL args
		.ext_clk = 12,		// 12MHz
		.cpu_speed = 28,	// 336MHz
		.phm_div = 4,		// PLL DIV
		// UART args
		.use_uart = 0,
		.baudrate = 115200,
		// SDRAM args
		.bus_width = 1,
		.bank_num = 1,
		.row_addr = 13,
		.col_addr = 9,
		.is_mobile = 0,
		.is_busshare = 1,
		// Debug args
		.debug_ops = 0,
		.pin_num = 0,
		.start = 0,
		.size = 0,
	};

	printf("Firmware configurations:\n");
	printf("\tCPU ID:                      JZ%x\n", args.cpu_id);
	printf("\tExternal clock:              %u MHz\n", args.ext_clk);
	printf("\tCPU speed:                   %u MHz\n", args.ext_clk * args.cpu_speed);
	printf("\tPLL divider:                 %u\n", args.phm_div);
	printf("\tUsing UART:                  UART%u\n", args.use_uart);
	printf("\tUART baud rate:              %u\n", args.baudrate);
	printf("\tSDRAM data bus width:        %u bits\n", 32 - (args.bus_width << 4));
	printf("\tSDRAM banks:                 %u\n", (args.bank_num + 1) << 1);
	printf("\tSDRAM row address width:     %u\n", args.row_addr);
	printf("\tSDRAM column address width:  %u\n", args.col_addr);
	printf("\tMobile SDRAM mode:           %s\n", args.is_mobile ? "yes" : "no");
	printf("\tShared SDRAM bus:            %s\n", args.is_busshare ? "yes" : "no");
	printf("\tDebug mode:                  %s\n", args.debug_ops > 0 ? "yes" : "no");
	return writeMem(dev, FW_ARGS_ADDR, sizeof(args), &args);
}
