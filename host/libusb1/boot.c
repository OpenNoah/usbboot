#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <libusb.h>
#include <hand.h>
#include "usbboot.h"

#define FW_START_ADDR	0x80002000
#define BOOT_START_ADDR	0x80000000
#define BOOT_CODE_SIZE	0x00400000	// 4MiB
#define ARGS_OFFSET	8

int transferData(libusb_device_handle *dev, uint32_t rw, uint32_t size, void *p);

hand_t cfg_hand = {0};

int loadConfigFile(const char *file)
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
	printf("\tPLL divider:                 DIV %u\n", args.phm_div);
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
	if (hand.nand_bpc)
		printf("\tNAND blocks per chip:        %u blocks\n", hand.nand_bpc);
	printf("\tNAND planes:                 %u planes\n", hand.nand_plane);
	printf("\tNAND force erase:            %s\n", hand.nand_force_erase ? "yes" : "no");
	printf("\tNAND ECC offset:             %u bytes\n", hand.nand_eccpos);
	printf("\tNAND bad block flag offset:  %u bytes\n", hand.nand_bbpos);
	printf("\tNAND bad block page:         page %u\n", hand.nand_bbpage);
	if (args.cpu_id > 0x4740)
		printf("\tNAND BCH algorithm:          %u bits\n", hand.nand_bchbit);
	if (hand.nand_wppin)
		printf("\tNAND write protect pin:      GPIO%u\n", hand.nand_wppin);

	hand.pt = args.cpu_id;
	hand.nand_pn = hand.nand_plane;
	hand.fw_args = args;
	memcpy(&cfg_hand, &hand, sizeof(hand));
	return 0;
}

static int writeConfig(libusb_device_handle *dev, unsigned long addr)
{
	return writeMem(dev, addr, sizeof(fw_args_t), &cfg_hand.fw_args);
}

int receiveHandshake(libusb_device_handle *dev, uint16_t *hs)
{
	uint16_t tmp[4];
	if (!hs)
		hs = tmp;

	int err = transferData(dev, 1, 8, hs);
	if (err)
		fprintf(stderr, "Error receiving handshake\n");
	return err;
}

static int uploadConfig(libusb_device_handle *dev)
{
	// Bulk transfer
	if (transferData(dev, 0, sizeof(hand_t), &cfg_hand)) {
		fprintf(stderr, "Error uploading configurations\n");
		return 1;
	}

	int err = libusb_control_transfer(dev, 0x40, VR_CONFIGRATION, DS_hand, 0, 0, 0, 0);
	if (err) {
		fprintf(stderr, "Error applying configurations: %s\n", libusb_strerror(err));
		return 2;
	}

	// Handshake packet
	return receiveHandshake(dev, NULL);
}

int checkConfig()
{
	if (!cfg_hand.pt)
		fprintf(stderr, "Platform configurations not available\n");
	return !cfg_hand.pt;
}

int systemInit(libusb_device_handle *dev, const char *fw, const char *boot)
{
	if (checkConfig())
		return 1;

	if (fw[0] == 0)
		goto stage2;
	// Stage 1, initialise clocks, UART and SDRAM
	if (downloadFile(dev, FW_START_ADDR, fw))
		return 2;
	if (writeConfig(dev, FW_START_ADDR + ARGS_OFFSET))
		return 3;
	if (programStart1(dev, FW_START_ADDR))
		return 4;
	usleep(100000);

stage2:
	if (boot[0] == 0) {
		if (fw[0] == 0)
			goto config;
		else
			return 0;
	}
	// Calculate SDRAM size and boot firmware start address
	fw_args_t *args = &cfg_hand.fw_args;
	uint32_t size = (1ul << (args->row_addr + args->col_addr)) *
		((args->bank_num + 1) << 1) * (4 - (args->bus_width << 1));
	uint32_t addr = BOOT_START_ADDR + size - BOOT_CODE_SIZE;
	// Stage 2, initialise NAND, handle advanced USB requests
	if (downloadFile(dev, addr, boot))
		return 5;
	if (writeConfig(dev, addr + ARGS_OFFSET))
		return 6;
	if (flushCaches(dev))
		return 7;
	if (programStart2(dev, addr))
		return 8;
config:
	if (uploadConfig(dev))
		return 9;
	return 0;
}
