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
#include <hand.h>
#include "usbboot.h"

#define FW_START_ADDR	0x80002000
#define BOOT_START_ADDR	0x80000000
#define BOOT_CODE_SIZE	0x00400000	// 4MiB
#define ARGS_OFFSET	8

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

	int progress = size >= BLOCK_SIZE;
	while (size) {
		size_t s = size >= BLOCK_SIZE ? BLOCK_SIZE : size;
		// Bulk transfer
		int len = 0, err;
		if ((err = libusb_bulk_transfer(dev, USBBOOT_EP_IN, p, s, &len, 0)) || len != s) {
			fprintf(stderr, "Error transferring data %u: %s\n", len, libusb_strerror(err));
			return 3;
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

int uploadFile(libusb_device_handle *dev, uint32_t addr, uint32_t size, const char *file)
{
	printf("Uploading from 0x%08x of size %u to file %s...\n", addr, size, file);

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

	// Block size of 4MiB
	int progress = size >= BLOCK_SIZE;
	while (size) {
		size_t s = size >= BLOCK_SIZE ? BLOCK_SIZE : size;
		// Bulk transfer
		int len = 0, err;
		if ((err = libusb_bulk_transfer(dev, USBBOOT_EP_OUT, (void *)p, s, &len, 0)) || len != s) {
			fprintf(stderr, "Error transferring data: %s\n", libusb_strerror(err));
			return 3;
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

int downloadFile(libusb_device_handle *dev, uint32_t addr, const char *file)
{
	// Get file size
	struct stat st;
	if (stat(file, &st)) {
		fprintf(stderr, "Error getting file stat: %s\n", strerror(errno));
		return 1;
	}
	size_t size = st.st_size;

	printf("Downloading file %s of size %lu to 0x%08x...\n", file, size, addr);

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

static int loadConfig(const char *file, hand_t *argv)
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
		.bus_width = 0,
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

	hand_t hand = {
		.pt = args.cpu_id,	// UNUSED!
		.nand_bw = 8,		// BUSWIDTH
		.nand_rc = 3,		// ROWCYCLES
		.nand_ps = 2048,	// PAGESIZE
		.nand_ppb = 128,	// PAGEPERBLOCK
		.nand_force_erase = 1,	// FORCEERASE
		.nand_pn = 1,		// UNUSED!
		.nand_os = 64,		// OOBSIZE
		.nand_eccpos = 28,	// ECCPOS
		.nand_bbpage = 127,	// BADBLOCKPAGE
		.nand_bbpos = 0,	// BADBLOCKPOS
		.nand_plane = 1,	// PLANENUM
		.nand_bchbit = 4,	// BCHBIT
		.nand_wppin = 0,	// WPPIN
		.nand_bpc = 0,		// BLOCKPERCHIP
		.fw_args = args,
	};

	// Read configuration file
	FILE *fp = fopen(file, "r");
	if (!fp) {
		fprintf(stderr, "Error opening file: %s\n", strerror(errno));
		return 1;
	}

	// Process lines
	enum {INVALID, PLL, SDRAM, NAND, DEBUG, END} section = INVALID;
	static const char *delim = " \t\r\n";
	int cnt = 0;
	char *line = NULL;
	size_t len = 0;
	ssize_t llen;
	while ((llen = getline(&line, &len, fp)) != -1) {
		cnt++;
		char *p = strtok(line, delim);
		if (!p || p[0] == ';')
			continue;
		if (section == END)
			break;

		// New section
		if (p[0] == '[') {
			if (strcmp(p, "[PLL]") == 0)
				section = PLL;
			else if (strcmp(p, "[SDRAM]") == 0)
				section = SDRAM;
			else if (strcmp(p, "[NAND]") == 0)
				section = NAND;
			else if (strcmp(p, "[DEBUG]") == 0)
				section = DEBUG;
			else if (strcmp(p, "[END]") == 0)
				section = END;
			else
				break;
			if (strtok(NULL, delim) != NULL)
				break;
			continue;
		}

		if (section == INVALID)
			break;

		// Expecting a value
		char *s = strtok(NULL, delim);
		if (!s)
			break;

		// Integer conversion
		errno = 0;
		unsigned long v = strtoul(s, NULL, 0);
		if (errno != 0)
			break;

		if (section == PLL) {
			if (strcmp(p, "EXTCLK") == 0)
				args.ext_clk = v;
			else if (strcmp(p, "CPUSPEED") == 0)
				args.cpu_speed = v / args.ext_clk;
			else if (strcmp(p, "PHMDIV") == 0)
				args.phm_div = v;
			else if (strcmp(p, "BOUDRATE") == 0 || \
					strcmp(p, "BAUDRATE") == 0)
				args.baudrate = v;
			else if (strcmp(p, "USEUART") == 0)
				args.use_uart = v;
			else
				break;
		} else if (section == SDRAM) {
			if (strcmp(p, "BUSWIDTH") == 0)
				args.bus_width = 2 - v / 16;
			else if (strcmp(p, "BANKS") == 0)
				args.bank_num = v / 2 - 1;
			else if (strcmp(p, "ROWADDR") == 0)
				args.row_addr = v;
			else if (strcmp(p, "COLADDR") == 0)
				args.col_addr = v;
			else if (strcmp(p, "ISMOBILE") == 0)
				args.is_mobile = v;
			else if (strcmp(p, "ISBUSSHARE") == 0)
				args.is_busshare = v;
			else
				break;
		} else if (section == NAND) {
			if (strcmp(p, "BUSWIDTH") == 0)
				hand.nand_bw = v;
			else if (strcmp(p, "ROWCYCLES") == 0)
				hand.nand_rc = v;
			else if (strcmp(p, "PAGESIZE") == 0)
				hand.nand_ps = v;
			else if (strcmp(p, "PAGEPERBLOCK") == 0)
				hand.nand_ppb = v;
			else if (strcmp(p, "FORCEERASE") == 0)
				hand.nand_force_erase = v;
			else if (strcmp(p, "OOBSIZE") == 0)
				hand.nand_os = v;
			else if (strcmp(p, "ECCPOS") == 0)
				hand.nand_eccpos = v;
			else if (strcmp(p, "BADBLACKPAGE") == 0 || \
					strcmp(p, "BADBLOCKPAGE") == 0)
				hand.nand_bbpage = v;
			else if (strcmp(p, "BADBLACKPOS") == 0 || \
					strcmp(p, "BADBLOCKPOS") == 0)
				hand.nand_bbpos = v;
			else if (strcmp(p, "PLANENUM") == 0)
				hand.nand_plane = v;
			else if (strcmp(p, "BCHBIT") == 0)
				hand.nand_bchbit = v;
			else if (strcmp(p, "WPPIN") == 0)
				hand.nand_wppin = v;
			else if (strcmp(p, "BLOCKPERCHIP") == 0)
				hand.nand_bpc = v;
			else
				break;
		} else {
			break;
		}
	}

	free(line);
	fclose(fp);

	if (llen != -1) {
		fprintf(stderr, "Invalid content at line %u\n", cnt);
		return 2;
	}

	printf("Platform configurations:\n");
	printf("\tCPU ID:                      JZ%x\n", args.cpu_id);
	printf("\tExternal clock:              %u MHz\n", args.ext_clk);
	printf("\tCPU speed:                   %u MHz\n", args.ext_clk * args.cpu_speed);
	printf("\tPLL divider:                 %u\n", args.phm_div);
	printf("\tUsing UART:                  UART%u\n", args.use_uart);
	printf("\tUART baud rate:              %u bps\n", args.baudrate);
	printf("\tSDRAM data bus width:        %u bits\n", 32 - (args.bus_width << 4));
	printf("\tSDRAM banks:                 %u banks\n", (args.bank_num + 1) << 1);
	printf("\tSDRAM row address width:     %u bits\n", args.row_addr);
	printf("\tSDRAM column address width:  %u bits\n", args.col_addr);
	printf("\tMobile SDRAM mode:           %s\n", args.is_mobile ? "yes" : "no");
	printf("\tShared SDRAM bus:            %s\n", args.is_busshare ? "yes" : "no");
	printf("\tDebug mode:                  %s\n", args.debug_ops > 0 ? "yes" : "no");
	printf("\tNAND row address cycles:     %u cycles\n", hand.nand_rc);
	printf("\tNAND data bus width:         %u bits\n", hand.nand_bw);
	printf("\tNAND page size:              %u bytes\n", hand.nand_ps);
	printf("\tNAND OOB size:               %u bytes\n", hand.nand_os);
	printf("\tNAND pages per block:        %u pages\n", hand.nand_ppb);
	printf("\tNAND blocks per chip:        %u block\n", hand.nand_bpc);
	printf("\tNAND planes:                 %u planes\n", hand.nand_plane);
	printf("\tNAND force erase:            %s\n", hand.nand_force_erase ? "yes" : "no");
	printf("\tNAND ECC offset:             %u bytes\n", hand.nand_eccpos);
	printf("\tNAND bad block flag offset:  %u bytes\n", hand.nand_bbpos);
	printf("\tNAND bad block page:         %u\n", hand.nand_bbpage);
	printf("\tNAND BCH algorithm:          %u bits\n", hand.nand_bchbit);
	if (hand.nand_wppin)
		printf("\tNAND write protect pin:      GPIO%u\n", hand.nand_wppin);

	hand.pt = args.cpu_id;
	hand.nand_pn = hand.nand_plane;
	hand.fw_args = args;
	memcpy(argv, &hand, sizeof(hand));
	return 0;
}

static int writeConfig(libusb_device_handle *dev, unsigned long addr, fw_args_t *argv)
{
	return writeMem(dev, addr, sizeof(*argv), argv);
}

static int uploadConfig(libusb_device_handle *dev, hand_t *argv)
{
	// Bulk transfer
	uint32_t size = sizeof(*argv);
	int len = 0, err;
	if ((err = libusb_bulk_transfer(dev, USBBOOT_EP_OUT, (void *)argv, size, &len, 0)) || len != size) {
		fprintf(stderr, "Error uploading configurations: %s\n", libusb_strerror(err));
		return 1;
	}

	if ((err = libusb_control_transfer(dev, 0x40, VR_CONFIGRATION, DS_hand, 0, 0, 0, 0))) {
		fprintf(stderr, "Error applying configurations: %s\n", libusb_strerror(err));
		return 2;
	}

	// Handshake packet
	uint16_t hs[4];
	size = sizeof(hs);
	if ((err = libusb_bulk_transfer(dev, USBBOOT_EP_IN, (void *)hs, size, &len, 0)) || len != size) {
		fprintf(stderr, "Error receiving handshake: %s\n", libusb_strerror(err));
		return 1;
	}

	return 0;
}

int systemInit(libusb_device_handle *dev, const char *fw, const char *boot, const char *cfg)
{
	hand_t hand;
	fw_args_t *args = &hand.fw_args;
	if (loadConfig(cfg, &hand))
		return 1;

	// Stage 1, initialise clocks, UART and SDRAM
	if (downloadFile(dev, FW_START_ADDR, fw))
		return 2;
	if (writeConfig(dev, FW_START_ADDR + ARGS_OFFSET, args))
		return 3;
	if (programStart1(dev, FW_START_ADDR))
		return 4;
	usleep(100000);

	// Optionally skip stage 2 (e.g. for memory test)
	if (boot[0] == 0)
		return 0;

	// Calculate SDRAM size and boot firmware start address
	uint32_t size = (1ul << (args->row_addr + args->col_addr)) *
		((args->bank_num + 1) << 1) * (4 - (args->bus_width << 1));
	uint32_t addr = BOOT_START_ADDR + size - BOOT_CODE_SIZE;

	// Stage 2, initialise NAND, handle advanced USB requests
	if (downloadFile(dev, addr, boot))
		return 5;
	if (writeConfig(dev, addr + ARGS_OFFSET, args))
		return 6;
	if (flushCaches(dev))
		return 7;
	if (programStart2(dev, addr))
		return 8;
	if (uploadConfig(dev, &hand))
		return 9;
	return 0;
}
