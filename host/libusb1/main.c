#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "usbboot.h"

void help(char *prg)
{
	printf("Usage: %s command1 command2...\n"
		"\nBasic commands:\n"
		"cpu                           | Query CPU ID\n"
		"flush                         | Flush I-Cache and D-Cache\n"
		"mdw addr                      | Dump a 4-byte word from addr\n"
		"mww addr value                | Write value as a 4-byte word to addr\n"
		"write addr file               | Download data from file to addr\n"
		"read addr size file           | Upload data of size bytes from addr to file\n"
		"start1 addr                   | Branch to addr in I-Cache\n"
		"start2 addr                   | Branch to addr directly\n"
		"usleep us                     | Delay us microseconds\n"
		"\nPlatform commands:\n"
		"cfg usb-boot.cfg              | Load platform configurations from usb-boot.cfg\n"
		"init fw.bin usb-boot.bin      | Initialise the platform using 2-stage firmwares\n"
		"nand chip# subcommand [args]  | Execute NAND subcommand on chip#\n"
		"\nNAND subcommands:\n"
		"init                          | Initialise NAND chip (not used)\n"
		"query                         | Query NAND chip IDs\n"
		"dump [oob|noecc] page num     | Dump num pages starting from page\n"
		"                              | oob:   Dump OOB data too\n"
		"                              | noecc: Dump OOB data with ECC bytes masked\n"
		"", prg);
}

int nand_process(libusb_device_handle *dev, unsigned char cs, int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;
	char *cmd = *argv;
	if (strcmp(cmd, "query") == 0) {
		if (nandQuery(dev, cs))
			fprintf(stderr, "Error querying NAND %u\n", cs);
	} else if (strcmp(cmd, "init") == 0) {
		if (nandInit(dev, cs))
			fprintf(stderr, "Error initialising NAND %u\n", cs);
	} else if (strcmp(cmd, "dump") == 0) {
		if (++argv, !--argc)
			goto errargs;
		unsigned char opt = NO_OOB;
		if (strcmp(*argv, "oob") == 0) {
			opt = OOB_ECC;
			if (++argv, !--argc)
				goto errargs;
		} else if (strcmp(*argv, "noecc") == 0) {
			opt = OOB_NO_ECC;
			if (++argv, !--argc)
				goto errargs;
		}
		unsigned long page = strtoul(*argv, NULL, 0);
		if (++argv, !--argc)
			goto errargs;
		unsigned long num = strtoul(*argv, NULL, 0);
		if (nandDump(dev, cs, opt, page, num))
			fprintf(stderr, "Error reading NAND %u\n", cs);
	} else {
		fprintf(stderr, "Invalid NAND subcommand: %s\n", cmd);
		return -1;
	}

	*argcp = argc;
	*argvp = argv;
	return 0;
errargs:
	fprintf(stderr, "NAND subcommand missing arguments: %s\n", cmd);
	return -1;
}

int process(libusb_device_handle *dev, int argc, char **argv)
{
	char *cmd = 0;
	while (++argv, --argc) {
		cmd = *argv;
		if (strcmp(*argv, "cpu") == 0) {
			char data[9];
			if (getCPUInfo(dev, data))
				fprintf(stderr, "Error getting CPU info\n");
		} else if (strcmp(*argv, "flush") == 0) {
			if (flushCaches(dev))
				fprintf(stderr, "Error flushing caches\n");
		} else if (strcmp(*argv, "mdw") == 0) {
			if (++argv, !--argc)
				goto errargs;
			unsigned long addr = strtoul(*argv, NULL, 0);
			unsigned long value = 0;
			if (readMem(dev, addr, 4, &value))
				fprintf(stderr, "Error dumping memory %s\n", *argv);
			else
				printf("0x%08lx => 0x%08lx\n", addr, value);
		} else if (strcmp(*argv, "mww") == 0) {
			if (++argv, !--argc)
				goto errargs;
			unsigned long addr = strtoul(*argv, NULL, 0);
			if (++argv, !--argc)
				goto errargs;
			unsigned long value = strtoul(*argv, NULL, 0);
			if (writeMem(dev, addr, 4, &value))
				fprintf(stderr, "Error writing memory %s\n", *argv);
			else
				printf("0x%08lx <= 0x%08lx\n", addr, value);
		} else if (strcmp(*argv, "write") == 0) {
			if (++argv, !--argc)
				goto errargs;
			unsigned long addr = strtoul(*argv, NULL, 0);
			if (++argv, !--argc)
				goto errargs;
			if (downloadFile(dev, addr, *argv))
				fprintf(stderr, "Error writing data from %s\n", *argv);
		} else if (strcmp(*argv, "read") == 0) {
			if (++argv, !--argc)
				goto errargs;
			unsigned long addr = strtoul(*argv, NULL, 0);
			if (++argv, !--argc)
				goto errargs;
			unsigned long len = strtoul(*argv, NULL, 0);
			if (++argv, !--argc)
				goto errargs;
			if (uploadFile(dev, addr, len, *argv))
				fprintf(stderr, "Error reading data to %s\n", *argv);
		} else if (strcmp(*argv, "start1") == 0) {
			if (++argv, !--argc)
				goto errargs;
			unsigned long entry = strtoul(*argv, NULL, 0);
			if (programStart1(dev, entry))
				fprintf(stderr, "Error starting at %s\n", *argv);
		} else if (strcmp(*argv, "start2") == 0) {
			if (++argv, !--argc)
				goto errargs;
			unsigned long entry = strtoul(*argv, NULL, 0);
			if (programStart2(dev, entry))
				fprintf(stderr, "Error starting at %s\n", *argv);
		} else if (strcmp(*argv, "cfg") == 0) {
			if (++argv, !--argc)
				goto errargs;
			if (loadConfigFile(*argv))
				fprintf(stderr, "Error loading configurations\n");
		} else if (strcmp(*argv, "init") == 0) {
			if (++argv, !--argc)
				goto errargs;
			char *fw = *argv;
			if (++argv, !--argc)
				goto errargs;
			char *boot = *argv;
			if (systemInit(dev, fw, boot))
				fprintf(stderr, "Error initialising system\n");
		} else if (strcmp(*argv, "usleep") == 0) {
			if (++argv, !--argc)
				goto errargs;
			unsigned long v = strtoul(*argv, NULL, 0);
			usleep(v);
		} else if (strcmp(*argv, "sleep") == 0) {
			if (++argv, !--argc)
				goto errargs;
			unsigned long v = strtoul(*argv, NULL, 0);
			sleep(v);
		} else if (strcmp(*argv, "nand") == 0) {
			if (++argv, !--argc)
				goto errargs;
			unsigned char cs = strtoul(*argv, NULL, 0);
			if (++argv, !--argc)
				goto errargs;
			int err = nand_process(dev, cs, &argc, &argv);
			if (err)
				return err;
		} else {
			fprintf(stderr, "Invalid command: %s\n", *argv);
			return -1;
		}
	}
	return 0;
errargs:
	fprintf(stderr, "Command missing arguments: %s\n", cmd);
	return -1;
}

int main(int argc, char *argv[])
{
	if (argc == 1) {
		help(argv[0]);
		return 0;
	}

	libusb_context *ctx;
	libusb_init(&ctx);

	int err = 0;
	libusb_device_handle *dev = libusb_open_device_with_vid_pid(ctx, USBBOOT_VID, USBBOOT_PID);
	if (dev) {
		fprintf(stderr, "USB device %04x:%04x opened\n", USBBOOT_VID, USBBOOT_PID);
		if (!libusb_claim_interface(dev, USBBOOT_IFACE)) {
			err = process(dev, argc, argv);
			libusb_release_interface(dev, USBBOOT_IFACE);
		} else {
			fprintf(stderr, "Error: Could not claim USB interface\n");
			err = EACCES;
		}
		libusb_close(dev);
	} else {
		fprintf(stderr, "Error: USB device not found or inaccessible\n");
		err = ENODEV;
	}

	libusb_exit(ctx);
	return err;
}
