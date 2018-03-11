#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "usbboot.h"

int nand_process(libusb_device_handle *dev, int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;

	char *cmd = *argv;
	if (++argv, !--argc)
		return 1;
	unsigned char cs = strtoul(*argv, NULL, 0);

	if (strcmp(cmd, "query") == 0) {
		if (nandQuery(dev, cs))
			fprintf(stderr, "Error querying NAND %u\n", cs);
	} else if (strcmp(cmd, "init") == 0) {
		if (nandInit(dev, cs))
			fprintf(stderr, "Error initialising NAND %u\n", cs);
	} else if (strcmp(cmd, "dump") == 0) {
		if (++argv, !--argc)
			return 1;
		unsigned char opt = NO_OOB;
		if (strcmp(*argv, "oob") == 0) {
			opt = OOB_ECC;
			if (++argv, !--argc)
				return 1;
		} else if (strcmp(*argv, "noecc") == 0) {
			opt = OOB_NO_ECC;
			if (++argv, !--argc)
				return 1;
		}
		unsigned long page = strtoul(*argv, NULL, 0);
		if (++argv, !--argc)
			return 1;
		unsigned long num = strtoul(*argv, NULL, 0);
		if (nandDump(dev, cs, opt, page, num))
			fprintf(stderr, "Error reading NAND %u\n", cs);
	}

	*argcp = argc;
	*argvp = argv;
	return 0;
}

void process(libusb_device_handle *dev, int argc, char **argv)
{
	while (++argv, --argc) {
		if (strcmp(*argv, "cpu") == 0) {
			char data[9];
			if (getCPUInfo(dev, data))
				fprintf(stderr, "Error getting CPU info\n");
		} else if (strcmp(*argv, "flush") == 0) {
			if (flushCaches(dev))
				fprintf(stderr, "Error flushing caches\n");
		} else if (strcmp(*argv, "mdw") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long addr = strtoul(*argv, NULL, 0);
			unsigned long value = 0;
			if (readMem(dev, addr, 4, &value))
				fprintf(stderr, "Error dumping memory %s\n", *argv);
			else
				printf("0x%08lx => 0x%08lx\n", addr, value);
		} else if (strcmp(*argv, "mww") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long addr = strtoul(*argv, NULL, 0);
			if (++argv, !--argc)
				return;
			unsigned long value = strtoul(*argv, NULL, 0);
			if (writeMem(dev, addr, 4, &value))
				fprintf(stderr, "Error writing memory %s\n", *argv);
			else
				printf("0x%08lx <= 0x%08lx\n", addr, value);
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
			if (programStart2(dev, entry))
				fprintf(stderr, "Error starting at %s\n", *argv);
		} else if (strcmp(*argv, "cfg") == 0) {
			if (++argv, !--argc)
				return;
			if (loadConfigFile(*argv))
				fprintf(stderr, "Error loading configurations\n");
		} else if (strcmp(*argv, "init") == 0) {
			if (++argv, !--argc)
				return;
			char *fw = *argv;
			if (++argv, !--argc)
				return;
			char *boot = *argv;
			if (systemInit(dev, fw, boot))
				fprintf(stderr, "Error initialising system\n");
		} else if (strcmp(*argv, "usleep") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long v = strtoul(*argv, NULL, 0);
			usleep(v);
		} else if (strcmp(*argv, "sleep") == 0) {
			if (++argv, !--argc)
				return;
			unsigned long v = strtoul(*argv, NULL, 0);
			sleep(v);
		} else if (strcmp(*argv, "nand") == 0) {
			if (++argv, !--argc)
				return;
			if (nand_process(dev, &argc, &argv))
				return;
		}
	}
}

int main(int argc, char *argv[])
{
	libusb_context *ctx;
	libusb_init(&ctx);

	libusb_device_handle *dev = libusb_open_device_with_vid_pid(ctx, USBBOOT_VID, USBBOOT_PID);
	if (dev) {
		fprintf(stderr, "Device %04x:%04x opened\n", USBBOOT_VID, USBBOOT_PID);
		if (!libusb_claim_interface(dev, USBBOOT_IFACE)) {
			process(dev, argc, argv);
			libusb_release_interface(dev, USBBOOT_IFACE);
		}
		libusb_close(dev);
	}

	libusb_exit(ctx);
	return 0;
}
