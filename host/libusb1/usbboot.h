#pragma once

#include <libusb.h>

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
int uploadFile(libusb_device_handle *dev, uint32_t addr, uint32_t size, const char *file);

int writeMem(libusb_device_handle *dev, uint32_t addr, uint32_t size, const void *p);
int downloadFile(libusb_device_handle *dev, uint32_t addr, const char *file);

int systemInit(libusb_device_handle *dev, const char *fw, const char *boot, const char *cfg);

int nandQuery(libusb_device_handle *dev, uint8_t cs);
