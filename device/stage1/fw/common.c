#include "jz4750.h"
#include "configs.h"

// https://www.linux-mips.org/archives/linux-mips/2009-05/msg00310.html
// The MIPS port no longer recognizes the h asm constraint from GCC 4.4
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 4)
#define GCC_NO_H_CONSTRAINT
#endif

#define GCC_REG_ACCUM "$0"

#define do_div64_32(res, high, low, base) ({    \
	unsigned long __quot32, __mod32; \
	unsigned long __cf, __tmp, __tmp2, __i; \
	\
	__asm__(".set	push\n\t" \
		".set	noat\n\t" \
		".set	noreorder\n\t" \
		"move	%2, $0\n\t" \
		"move	%3, $0\n\t" \
		"b	1f\n\t" \
		" li	%4, 0x21\n" \
		"0:\n\t" \
		"sll	$1, %0, 0x1\n\t" \
		"srl	%3, %0, 0x1f\n\t" \
		"or	%0, $1, %5\n\t" \
		"sll	%1, %1, 0x1\n\t" \
		"sll	%2, %2, 0x1\n" \
		"1:\n\t" \
		"bnez	%3, 2f\n\t" \
		" sltu	%5, %0, %z6\n\t" \
		"bnez	%5, 3f\n" \
		"2:\n\t" \
		" addiu	%4, %4, -1\n\t" \
		"subu	%0, %0, %z6\n\t" \
		"addiu	%2, %2, 1\n" \
		"3:\n\t" \
		"bnez	%4, 0b\n\t" \
		" srl	%5, %1, 0x1f\n\t" \
		".set	pop" \
		: "=&r" (__mod32), "=&r" (__tmp), \
		  "=&r" (__quot32), "=&r" (__cf), \
		  "=&r" (__i), "=&r" (__tmp2) \
		: "Jr" (base), "0" (high), "1" (low)); \
	\
	(res) = __quot32; \
	__mod32; })

/*
 * __do_divu -- unsigned interger dividing
 *
 * handle removal of 'h' constraint in GCC 4.4
 */
#ifndef GCC_NO_H_CONSTRAINT    /* gcc <= 4.3*/
#define __do_divu() ({ \
       __asm__("divu   $0, %z2, %z3" \
               : "=h" (__upper), "=l" (__high) \
               : "Jr" (__high), "Jr" (__base) \
               : GCC_REG_ACCUM); })

#else          /* gcc >= 4.4 */
#define __do_divu() ({ \
       __upper = (unsigned long long)__high % __base; \
       __high = (unsigned long long)__high / __base; })
#endif

#define do_div(n, base) ({ \
	unsigned long long __quot; \
	unsigned long __mod; \
	unsigned long long __div; \
	unsigned long __upper, __low, __high, __base; \
	\
	__div = (n); \
	__base = (base); \
	\
	__high = __div >> 32; \
	__low = __div; \
	__upper = __high; \
	\
	if (__high) \
		__do_divu(); \
	\
	__mod = do_div64_32(__low, __upper, __low, __base); \
	\
	__quot = __high; \
	__quot = __quot << 32 | __low; \
	(n) = __quot; \
	__mod; })

#define abs(x) ({				\
		int __x = (x);			\
		(__x < 0) ? -__x : __x;		\
	})

static unsigned short quot1[3] = {0}; /* quot[0]:baud_div, quot[1]:umr, quot[2]:uacr */
/* Calculate baud_div, umr and uacr for any frequency of uart */
static unsigned short * get_divisor(void)
{
	int err, sum, i, j;
	int a[12], b[12];
	unsigned short div, umr, uacr;
	unsigned short umr_best, div_best, uacr_best;
	long long t0, t1, t2, t3;
    unsigned int baud = CONFIG_BAUDRATE;
    unsigned int uartclk;
    
    if(CPU_ID == 0x4750)
        if (CFG_EXTAL > 16000000) {
            REG_CPM_CPCCR |= CPM_CPCCR_ECS;
            uartclk = CFG_EXTAL / 2;
        } else {
            REG_CPM_CPCCR &= ~CPM_CPCCR_ECS;
            uartclk = CFG_EXTAL;
        }
    else /* CONFIG_JZ4740 or CONFIG_JZ4730 */
        uartclk = CFG_EXTAL;
    
	sum = 0;
	umr_best = div_best = uacr_best = 0;
	div = 1;

	if ((uartclk % (16 * baud)) == 0) {
		quot1[0] = uartclk / (16 * baud);
		quot1[1] = 16;
		quot1[2] = 0;
		return quot1;
	}

	while (1) {
		umr = uartclk / (baud * div);
  		if (umr > 32) {
			div++;
			continue;
		}
		if (umr < 4) {
			break;
		}
		for (i = 0; i < 12; i++) {
			a[i] = umr;
			b[i] = 0;
			sum = 0;
			for (j = 0; j <= i; j++) {
				sum += a[j];
			}

            /* the precision could be 1/2^(36) due to the value of t0 */
			t0 = 0x1000000000LL;
			t1 = (i + 1) * t0;
			t2 = (sum * div) * t0;
			t3 = div * t0;
			do_div(t1, baud);
			do_div(t2, uartclk);
			do_div(t3, (2 * uartclk));
			err = t1 - t2 - t3;

			if (err > 0) {
				a[i] += 1;
				b[i] = 1;
			}
		}

		uacr = 0;
		for (i = 0; i < 12; i++) {
			if (b[i] == 1) {
				uacr |= 1 << i;
			}
		}

        /* the best value of umr should be near 16, and the value of uacr should better be smaller */
		if (abs(umr - 16) < abs(umr_best - 16) || (abs(umr - 16) == abs(umr_best - 16) && uacr_best > uacr)) {
			div_best = div;
			umr_best = umr;
			uacr_best = uacr;
		}
		div++;
	}

	quot1[0] = div_best;
	quot1[1] = umr_best;
	quot1[2] = uacr_best;

	return quot1;
}

void serial_putc (const char c)
{
	volatile u8 *uart_lsr = (volatile u8 *)(UART_BASE + OFF_LSR);
	volatile u8 *uart_tdr = (volatile u8 *)(UART_BASE + OFF_TDR);

	if (c == '\n') serial_putc ('\r');

	/* Wait for fifo to shift out some bytes */
	while ( !((*uart_lsr & (UART_LSR_TDRQ | UART_LSR_TEMT)) == 0x60) );

	*uart_tdr = (u8)c;
}

void serial_puts (const char *s)
{
	while (*s) {
		serial_putc (*s++);
	}
}

int serial_getc (void)
{
	volatile u8 *uart_rdr = (volatile u8 *)(UART_BASE + OFF_RDR);

	while (!serial_tstc());

	return *uart_rdr;
}

int serial_tstc (void)
{
	volatile u8 *uart_lsr = (volatile u8 *)(UART_BASE + OFF_LSR);

	if (*uart_lsr & UART_LSR_DR) {
		/* Data in rfifo */
		return (1);
	}
	return 0;
}


void serial_setbrg(void)
{
	volatile u8 *uart_lcr = (volatile u8 *)(UART_BASE + OFF_LCR);
	volatile u8 *uart_dlhr = (volatile u8 *)(UART_BASE + OFF_DLHR);
	volatile u8 *uart_dllr = (volatile u8 *)(UART_BASE + OFF_DLLR);
    volatile u8 *uart_umr = (volatile u8 *)(UART_BASE + OFF_UMR);
 	volatile u8 *uart_uacr = (volatile u8 *)(UART_BASE + OFF_UACR);
	u32 baud_div, tmp;

    get_divisor();
    
    *uart_umr = quot1[1];
    *uart_uacr = quot1[2];
    baud_div = quot1[0];

	tmp = *uart_lcr;
	tmp |= UART_LCR_DLAB;
	*uart_lcr = tmp;

	*uart_dlhr = (baud_div >> 8) & 0xff;
	*uart_dllr = baud_div & 0xff;

	tmp &= ~UART_LCR_DLAB;
	*uart_lcr = tmp;
}

void serial_init(void)
{
	volatile u8 *uart_fcr = (volatile u8 *)(UART_BASE + OFF_FCR);
	volatile u8 *uart_lcr = (volatile u8 *)(UART_BASE + OFF_LCR);
	volatile u8 *uart_ier = (volatile u8 *)(UART_BASE + OFF_IER);
	volatile u8 *uart_sircr = (volatile u8 *)(UART_BASE + OFF_SIRCR);

	/* Disable port interrupts while changing hardware */
	*uart_ier = 0;

	/* Disable UART unit function */
	*uart_fcr = ~UART_FCR_UUE;

	/* Set both receiver and transmitter in UART mode (not SIR) */
	*uart_sircr = ~(SIRCR_RSIRE | SIRCR_TSIRE);

	/* Set databits, stopbits and parity. (8-bit data, 1 stopbit, no parity) */
	*uart_lcr = UART_LCR_WLEN_8 | UART_LCR_STOP_1;

	/* Set baud rate */
    serial_setbrg();

	/* Enable UART unit, enable and clear FIFO */
	*uart_fcr = UART_FCR_UUE | UART_FCR_FE | UART_FCR_TFLS | UART_FCR_RFLS;
}

void serial_put_hex(unsigned int  d)
{
	unsigned char c[12];
	char i;
	for(i = 0; i < 8;i++)
	{
		c[i] = (d >> ((7 - i) * 4)) & 0xf;
		if(c[i] < 10)
			c[i] += 0x30;
		else
			c[i] += (0x41 - 10);
	}
	c[8] = '\n';
	c[9] = 0;
	serial_puts(c);

}
