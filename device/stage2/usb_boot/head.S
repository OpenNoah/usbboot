/*
 * head.S
 *
 * Entry point of the firmware.
 * The firmware code are executed in the ICache.
 * Do not edit!
 * Copyright (C) 2006 Ingenic Semiconductor Inc.
 *
 */

	.text
	.extern c_main
		
	.globl _start
	.set noreorder
_start:
	b	real_start
	nop
	.word	0x0                  //	its address == start address + 8
	.word	0x0
	.word	0x0
	.word	0x0
	.word	0x0
	.word	0x0
	.word	0x0
	.word	0x0

real_start:	
	//----------------------------------------------------
	// setup stack, jump to C code
	//----------------------------------------------------
	add	$sp, $20, 0x400000      // sp locate at start address + 4M
	add	$t9, $20, 0x40          // t9 = c_main()
	j	$t9
	nop

	.set reorder
