/*
 * Copyright (C) 2009 Ingenic Semiconductor Inc.
 * Author: Taylor <cwjia@ingenic.cn>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "jz4750.h"

/*
 * External routines
 */
extern void flush_cache_all(void);
extern int serial_init(void);
extern void serial_puts(const char *s);
extern void sdram_init(void);

#define u32 unsigned int
#define u16 unsigned short
#define u8 unsigned char
static int rca;
static int sd2_0 = 0;

/*
 * GPIO definition
 */
#define MMC_IRQ_MASK()				\
do {						\
	REG_MSC_IMASK = 0xffff;			\
	REG_MSC_IREG = 0xffff;			\
} while (0)

static sd_mdelay(int sdelay)
{
	__tcu_set_count(0,0);
	__tcu_disable_pwm_output(0);
	__tcu_select_rtcclk(0);
	__tcu_select_clk_div1(0);

	__tcu_mask_half_match_irq(0); 
	__tcu_mask_full_match_irq(0);

	REG_TCU_TDFR(0) = 32; // 1 ms 
	__tcu_start_counter(0);

	while(1) {
		if(REG_TCU_TCNT(0) > sdelay)
			break;
	}

	__tcu_stop_counter(0);
	__tcu_stop_timer_clock(0);
}

/* Stop the MMC clock and wait while it happens */
static inline int jz_mmc_stop_clock(void)
{
	int timeout = 1000;
	int wait = 336; /* 1 us */ 

	REG_MSC_STRPCL = MSC_STRPCL_CLOCK_CONTROL_STOP;
	while (timeout && (REG_MSC_STAT & MSC_STAT_CLK_EN)) {
		timeout--;
		if (timeout == 0) {
			return -1;
		}
		wait = 336;
		while (wait--)
			;
	}
	return 0;
}

/* Start the MMC clock and operation */
static inline int jz_mmc_start_clock(void)
{
	REG_MSC_STRPCL = MSC_STRPCL_CLOCK_CONTROL_START | MSC_STRPCL_START_OP;
	return 0;
}

static u8 * mmc_cmd(u16 cmd, unsigned int arg, unsigned int cmdat, u16 rtype)
{
	static u8 resp[20];
	u32 timeout = 0x3fffff;
	int words, i;

	jz_mmc_stop_clock();
	REG_MSC_CMD   = cmd;
	REG_MSC_ARG   = arg;
	REG_MSC_CMDAT = cmdat;

	REG_MSC_IMASK = ~MSC_IMASK_END_CMD_RES;
	jz_mmc_start_clock();

	while (timeout-- && !(REG_MSC_STAT & MSC_STAT_END_CMD_RES))
		;

	REG_MSC_IREG = MSC_IREG_END_CMD_RES;

	switch (rtype) {
	case MSC_CMDAT_RESPONSE_R1:
		case MSC_CMDAT_RESPONSE_R3:
			words = 3;
			break;
		case MSC_CMDAT_RESPONSE_R2:
			words = 8;
			break;
		default:
			return 0;
	}

	for (i = words-1; i >= 0; i--) {
		u16 res_fifo = REG_MSC_RES;
		int offset = i << 1;
		
		resp[offset] = ((u8 *)&res_fifo)[0];
		resp[offset+1] = ((u8 *)&res_fifo)[1];
	}
	return resp;
}

int mmc_block_readm(u32 src, u32 num, u8 *dst)
{
	u8 *resp;
	u32 stat, timeout, data, cnt, wait, nob;

	resp = mmc_cmd(16, 0x200, 0x401, MSC_CMDAT_RESPONSE_R1);
	REG_MSC_BLKLEN = 0x200;
	REG_MSC_NOB = num / 512;

	if (sd2_0) 
		resp = mmc_cmd(18, src, 0x409, MSC_CMDAT_RESPONSE_R1); // for sdhc card
	else
		resp = mmc_cmd(18, src * 512, 0x409, MSC_CMDAT_RESPONSE_R1);
	nob  = num / 512;

	for (nob; nob >= 1; nob--) {
		timeout = 0x3ffffff;
		while (timeout) {
			timeout--;
			stat = REG_MSC_STAT;

			if (stat & MSC_STAT_TIME_OUT_READ) {
				serial_puts("\n MSC_STAT_TIME_OUT_READ\n\n");
				return -1;
			}
			else if (stat & MSC_STAT_CRC_READ_ERROR) {
				serial_puts("\n MSC_STAT_CRC_READ_ERROR\n\n");
				return -1;
			}
			else if (!(stat & MSC_STAT_DATA_FIFO_EMPTY)) {
				/* Ready to read data */
				break;
			}
			wait = 336;
			while (wait--)
				;
		}
		if (!timeout) {
			serial_puts("\n mmc/sd read timeout\n");
			return -1;
		}

		/* Read data from RXFIFO. It could be FULL or PARTIAL FULL */
		cnt = 128;
		while (cnt) {
			while (cnt && (REG_MSC_STAT & MSC_STAT_DATA_FIFO_EMPTY))
				;
			cnt --;
			data = REG_MSC_RXFIFO;
			{
				*dst++ = (u8)(data >> 0);
				*dst++ = (u8)(data >> 8);
				*dst++ = (u8)(data >> 16);
				*dst++ = (u8)(data >> 24);
			}
		}
	}

	resp = mmc_cmd(12, 0, 0x41, MSC_CMDAT_RESPONSE_R1);
	jz_mmc_stop_clock();

	while (!(REG_MSC_IREG & MSC_IREG_PRG_DONE))
		;
	REG_MSC_IREG = MSC_IREG_PRG_DONE;	/* clear status */

	return 0;
}

int mmc_block_writem(u32 src, u32 num, u8 *dst)
{
	u8 *resp;
	u32 stat, timeout, data, cnt, wait, nob, i, j;
	u32 *wbuf = (u32 *)dst;

	resp = mmc_cmd(16, 0x200, 0x401, MSC_CMDAT_RESPONSE_R1);
	REG_MSC_BLKLEN = 0x200;
	REG_MSC_NOB = num / 512;
	if (sd2_0)
		resp = mmc_cmd(25, src, 0x419, MSC_CMDAT_RESPONSE_R1); // for sdhc card
	else
		resp = mmc_cmd(25, src * 512, 0x419, MSC_CMDAT_RESPONSE_R1);
	nob  = num / 512;
	timeout = 0x3ffffff;


	for (i = 0; i < nob; i++) {
		timeout = 0x3FFFFFF;
		while (timeout) {
			timeout--;
			stat = REG_MSC_STAT;

			if (stat & (MSC_STAT_CRC_WRITE_ERROR | MSC_STAT_CRC_WRITE_ERROR_NOSTS))
				return -1;
			else if (!(stat & MSC_STAT_DATA_FIFO_FULL)) {
				/* Ready to write data */
				break;
			}
//			udelay(1)
			wait = 336;
			while (wait--)
				;
		}
		if (!timeout)
			return -1;

		/* Write data to TXFIFO */
		cnt = 128;
		while (cnt) {
			while(!(REG_MSC_IREG & MSC_IREG_TXFIFO_WR_REQ))
				;
			for (j=0; j<8; j++)
			{	
				REG_MSC_TXFIFO = *wbuf++;
				cnt--;
			}
		}
	}

	resp = mmc_cmd(12, 0, 0x441, MSC_CMDAT_RESPONSE_R1);
	while (!(REG_MSC_IREG & MSC_IREG_PRG_DONE))
		;
	REG_MSC_IREG = MSC_IREG_PRG_DONE;	/* clear status */
	jz_mmc_stop_clock();
	return 0;
}

static void sd_init(void)
{
	int retries, wait;
	u8 *resp;
	unsigned int cardaddr;
	serial_puts("SD card found!\n");

	resp = mmc_cmd(41, 0x40ff8000, 0x3, MSC_CMDAT_RESPONSE_R3);
	retries = 1000;
	while (retries-- && resp && !(resp[4] & 0x80)) {
		resp = mmc_cmd(55, 0, 0x1, MSC_CMDAT_RESPONSE_R1);
		resp = mmc_cmd(41, 0x40ff8000, 0x3, MSC_CMDAT_RESPONSE_R3);
		sd_mdelay(10);
	}

	if (resp[4] & 0x80) 
		serial_puts("SD init ok\n");
	else 
		serial_puts("SD init fail\n");

	/* try to get card id */
	resp = mmc_cmd(2, 0, 0x2, MSC_CMDAT_RESPONSE_R2);
	resp = mmc_cmd(3, 0, 0x6, MSC_CMDAT_RESPONSE_R1);
	cardaddr = (resp[4] << 8) | resp[3]; 
	rca = cardaddr << 16;

	resp = mmc_cmd(9, rca, 0x2, MSC_CMDAT_RESPONSE_R2);
	sd2_0 = (resp[14] & 0xc0) >> 6;
	REG_MSC_CLKRT = 0;
	resp = mmc_cmd(7, rca, 0x41, MSC_CMDAT_RESPONSE_R1);
	resp = mmc_cmd(55, rca, 0x1, MSC_CMDAT_RESPONSE_R1);
	resp = mmc_cmd(6, 0x2, 0x401, MSC_CMDAT_RESPONSE_R1);
}

#define PROID_4750 0x1ed0024f

#define read_32bit_cp0_processorid()                            \
({ int __res;                                                   \
        __asm__ __volatile__(                                   \
        "mfc0\t%0,$15\n\t"                                      \
        : "=r" (__res));                                        \
        __res;})

void gpio_as_msc0_4bit()
{
	REG_GPIO_PXFUNS(1) = 0x00008000;
	REG_GPIO_PXTRGS(1) = 0x00008000;
	REG_GPIO_PXSELC(1) = 0x00008000;
	REG_GPIO_PXPES(1)  = 0x00008000;
	REG_GPIO_PXFUNS(2) = 0x38030000;
	REG_GPIO_PXTRGS(2) = 0x00010000;
	REG_GPIO_PXTRGC(2) = 0x38020000;
	REG_GPIO_PXSELC(2) = 0x08010000;
	REG_GPIO_PXSELS(2) = 0x30020000;
	REG_GPIO_PXPES(2)  = 0x38030000;
}


void mmc_init_gpio(void)
{
	unsigned int processor_id = read_32bit_cp0_processorid();

	if (processor_id == PROID_4750)
		__gpio_as_msc0_8bit();
	else if (processor_id != 0x0ad0024f)
		gpio_as_msc0_4bit();

}

/* init mmc/sd card we assume that the card is in the slot */
int  mmc_init(void)
{
	int retries, wait;
	u8 *resp;


	REG_CPM_MSCCDR(0) = 13;
	REG_CPM_CPCCR |= CPM_CPCCR_CE;

	mmc_init_gpio();

	__msc_reset();
	MMC_IRQ_MASK();	
	REG_MSC_CLKRT = 7;    //187k

	serial_puts("\n\nMMC/SD INIT\n");

	/* just for reading and writing, suddenly it was reset, and the power of sd card was not broken off */
	resp = mmc_cmd(12, 0, 0x41, MSC_CMDAT_RESPONSE_R1);

	/* reset */
	resp = mmc_cmd(0, 0, 0x80, 0);
	resp = mmc_cmd(8, 0x1aa, 0x1, MSC_CMDAT_RESPONSE_R1);
	resp = mmc_cmd(55, 0, 0x1, MSC_CMDAT_RESPONSE_R1);
	if(!(resp[0] & 0x20) && (resp[5] != 0x37)) { 
		serial_puts("MMC card found!\n");
		resp = mmc_cmd(1, 0xff8000, 0x3, MSC_CMDAT_RESPONSE_R3);
		retries = 1000;
		while (retries-- && resp && !(resp[4] & 0x80)) {
			resp = mmc_cmd(1, 0x40300000, 0x3, MSC_CMDAT_RESPONSE_R3);
			sd_mdelay(10);
		}

		if (resp[4]== 0x80) 
			serial_puts("MMC init ok\n");
		else 
			serial_puts("MMC init fail\n");

		/* try to get card id */
		resp = mmc_cmd(2, 0, 0x2, MSC_CMDAT_RESPONSE_R2);
		resp = mmc_cmd(3, 0x10, 0x1, MSC_CMDAT_RESPONSE_R1);

		REG_MSC_CLKRT = 1;	/* 16/1 MHz */
		resp = mmc_cmd(7, 0x10, 0x1, MSC_CMDAT_RESPONSE_R1);
		resp = mmc_cmd(6, 0x3b70101, 0x401, MSC_CMDAT_RESPONSE_R1);
	}
	else
		sd_init();
	return 0;
}

static void gpio_init(void)
{
	/*
	 * Initialize SDRAM pins
	 */
	__gpio_as_sdram_32bit();

	/*
	 * Initialize UART1 pins
	 */
	__gpio_as_uart3();
}

/*
 * Load kernel image from MMC/SD into RAM
 */
static int mmc_load(int uboot_size, u8 *dst)
{
	mmc_init();
	mmc_block_readm(32, uboot_size, dst);

	return 0;
}

#if 0
void nand_boot(void)
{
	void (*uboot)(void);

	/*
	 * Init hardware
	 */
	gpio_init();
	serial_init();

	serial_puts("\n\nMSC Secondary Program Loader\n");

	sdram_init();

	/*
	 * Load U-Boot image from NAND into RAM
	 */
	mmc_load(CFG_MSC_U_BOOT_SIZE, (uchar *)CFG_MSC_U_BOOT_DST);

	uboot = (void (*)(void))CFG_NAND_U_BOOT_START;

	serial_puts("Starting U-Boot ...\n");

	/*
	 * Flush caches
	 */
	flush_cache_all();

	/*
	 * Jump to U-Boot image
	 */
	(*uboot)();
}
#endif
