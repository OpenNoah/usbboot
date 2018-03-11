#pragma once

#include <libusb.h>
#include <usb_boot.h>

#define USBBOOT_VID	0x601a
#define USBBOOT_PID	0x4740
#define USBBOOT_IFACE	0
#define USBBOOT_EP_IN	0x81
#define USBBOOT_EP_OUT	0x01

// Get CPU information (size: 8+1 bytes)
int getCPUInfo(libusb_device_handle *dev, char *p);

// Flush I-Cache and D-Cache
int flushCaches(libusb_device_handle *dev);

// Transfer data from D-Cache to I-Cache and branch to address in I-Cache
int programStart1(libusb_device_handle *dev, uint32_t entry);
// Branch to target address directly
int programStart2(libusb_device_handle *dev, uint32_t entry);

int readMem(libusb_device_handle *dev, uint32_t addr, uint32_t size, void *p);
int readFile(libusb_device_handle *dev, uint32_t addr, uint32_t size, const char *file);

int writeMem(libusb_device_handle *dev, uint32_t addr, uint32_t size, const void *p);
int writeFile(libusb_device_handle *dev, uint32_t addr, const char *file);

int loadConfigFile(const char *file);
int systemInit(libusb_device_handle *dev, const char *fw, const char *boot);

int nandQuery(libusb_device_handle *dev, uint8_t cs);
int nandInit(libusb_device_handle *dev, uint8_t cs);

// opt: OOB_ECC, OOB_NO_ECC or NO_OOB
int nandDump(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num);
int nandReadFile(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, uint32_t num, const char *file);
int nandProgramFile(libusb_device_handle *dev, uint8_t cs, uint8_t opt, uint32_t page, const char *file);
int nandErase(libusb_device_handle *dev, uint8_t cs, uint32_t blk, uint32_t num);
