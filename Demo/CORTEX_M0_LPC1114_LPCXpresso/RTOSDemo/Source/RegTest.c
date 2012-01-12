/*
    FreeRTOS V7.1.0 - Copyright (C) 2011 Real Time Engineers Ltd.


    ***************************************************************************
     *                                                                       *
     *    FreeRTOS tutorial books are available in pdf and paperback.        *
     *    Complete, revised, and edited pdf reference manuals are also       *
     *    available.                                                         *
     *                                                                       *
     *    Purchasing FreeRTOS documentation will not only help you, by       *
     *    ensuring you get running as quickly as possible and with an        *
     *    in-depth knowledge of how to use FreeRTOS, it will also help       *
     *    the FreeRTOS project to continue with its mission of providing     *
     *    professional grade, cross platform, de facto standard solutions    *
     *    for microcontrollers - completely free of charge!                  *
     *                                                                       *
     *    >>> See http://www.FreeRTOS.org/Documentation for details. <<<     *
     *                                                                       *
     *    Thank you for using FreeRTOS, and thank you for your support!      *
     *                                                                       *
    ***************************************************************************


    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation AND MODIFIED BY the FreeRTOS exception.
    >>>NOTE<<< The modification to the GPL is included to allow you to
    distribute a combined work that includes FreeRTOS without being obliged to
    provide the source code for proprietary components outside of the FreeRTOS
    kernel.  FreeRTOS is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
    more details. You should have received a copy of the GNU General Public
    License and the FreeRTOS license exception along with FreeRTOS; if not it
    can be viewed here: http://www.freertos.org/a00114.html and also obtained
    by writing to Richard Barry, contact details for whom are available on the
    FreeRTOS WEB site.

    1 tab == 4 spaces!

    http://www.FreeRTOS.org - Documentation, latest information, license and
    contact details.

    http://www.SafeRTOS.com - A version that is certified for use in safety
    critical systems.

    http://www.OpenRTOS.com - Commercial support, development, porting,
    licensing and training services.
*/

void vRegTest1Task( void ) __attribute__((naked));
void vRegTest2Task( void ) __attribute__((naked));

void vRegTest1Task( void )
{
	__asm volatile
	(
		".extern ulRegTest1LoopCounter		\n"
		"									\n"
		"	/* Fill the core registers with known values. */ \n"
		"	mov r1, #101					\n"
		"	mov r2, #102					\n"
		"	mov r3, #103					\n"
		"	mov	r4, #104					\n"
		"	mov	r5, #105					\n"
		"	mov	r6, #106					\n"
		"	mov r7, #107					\n"
		"	mov r0, #108					\n"
		"	mov	r8, r0						\n"
		"	mov	r0, #109					\n"
		"	mov r9, r0						\n"
		"	mov r0, #110					\n"
		"	mov	r10, r0						\n"
		"	mov r0, #111					\n"
		"	mov	r11, r0						\n"
		"	mov r0, #112					\n"
		"	mov r12, r0						\n"
		"	mov r0, #100					\n"
		"									\n"
		"reg1_loop:							\n"
		"									\n"
		"	cmp	r0, #100					\n"
		"	bne	reg1_error_loop				\n"
		"	cmp	r1, #101					\n"
		"	bne	reg1_error_loop				\n"
		"	cmp	r2, #102					\n"
		"	bne	reg1_error_loop				\n"
		"	cmp r3, #103					\n"
		"	bne	reg1_error_loop				\n"
		"	cmp	r4, #104					\n"
		"	bne	reg1_error_loop				\n"
		"	cmp	r5, #105					\n"
		"	bne	reg1_error_loop				\n"
		"	cmp	r6, #106					\n"
		"	bne	reg1_error_loop				\n"
		"	cmp	r7, #107					\n"
		"	bne	reg1_error_loop				\n"
		"	mov r0, #108					\n"
		"	cmp	r8, r0						\n"
		"	bne	reg1_error_loop				\n"
		"	mov r0, #109					\n"
		"	cmp	r9, r0						\n"
		"	bne	reg1_error_loop				\n"
		"	mov r0, #110					\n"
		"	cmp	r10, r0						\n"
		"	bne	reg1_error_loop				\n"
		"	mov r0, #111					\n"
		"	cmp	r11, r0						\n"
		"	bne	reg1_error_loop				\n"
		"	mov r0, #112					\n"
		"	cmp	r12, r0						\n"
		"	bne	reg1_error_loop				\n"
		"									\n"
		"	/* Everything passed, increment the loop counter. */ \n"
		"	push { r1 }						\n"
		"	ldr	r0, =ulRegTest1LoopCounter	\n"
		"	ldr r1, [r0]					\n"
		"	add r1, r1, #1					\n"
		"	str r1, [r0]					\n"
		"	pop { r1 }						\n"
		"									\n"
		"	/* Start again. */				\n"
		"	mov r0, #100					\n"
		"	b reg1_loop						\n"
		"									\n"
		"reg1_error_loop:					\n"
		"	/* If this line is hit then there was an error in a core register value. 	\n"
		"	The loop ensures the loop counter stops incrementing. */					\n"
		"	b reg1_error_loop				\n"
		"	nop								\n"
	);
}
/*-----------------------------------------------------------*/

void vRegTest2Task( void )
{
	__asm volatile
	(
		".extern ulRegTest2LoopCounter		\n"
		"									\n"
		"	/* Fill the core registers with known values. */ \n"
		"	mov r1, #1						\n"
		"	mov r2, #2						\n"
		"	mov r3, #3						\n"
		"	mov	r4, #4						\n"
		"	mov	r5, #5						\n"
		"	mov	r6, #6						\n"
		"	mov r7, #7						\n"
		"	mov r0, #8						\n"
		"	mov	r8, r0						\n"
		"	mov	r0, #9						\n"
		"	mov r9, r0						\n"
		"	mov r0, #10						\n"
		"	mov	r10, r0						\n"
		"	mov r0, #11						\n"
		"	mov	r11, r0						\n"
		"	mov r0, #12						\n"
		"	mov r12, r0						\n"
		"	mov r0, #10						\n"
		"									\n"
		"reg2_loop:							\n"
		"									\n"
		"	cmp	r0, #10						\n"
		"	bne	reg2_error_loop				\n"
		"	cmp	r1, #1						\n"
		"	bne	reg2_error_loop				\n"
		"	cmp	r2, #2						\n"
		"	bne	reg2_error_loop				\n"
		"	cmp r3, #3						\n"
		"	bne	reg2_error_loop				\n"
		"	cmp	r4, #4						\n"
		"	bne	reg2_error_loop				\n"
		"	cmp	r5, #5						\n"
		"	bne	reg2_error_loop				\n"
		"	cmp	r6, #6						\n"
		"	bne	reg2_error_loop				\n"
		"	cmp	r7, #7						\n"
		"	bne	reg2_error_loop				\n"
		"	mov r0, #8						\n"
		"	cmp	r8, r0						\n"
		"	bne	reg2_error_loop				\n"
		"	mov r0, #9						\n"
		"	cmp	r9, r0						\n"
		"	bne	reg2_error_loop				\n"
		"	mov r0, #10						\n"
		"	cmp	r10, r0						\n"
		"	bne	reg2_error_loop				\n"
		"	mov r0, #11						\n"
		"	cmp	r11, r0						\n"
		"	bne	reg2_error_loop				\n"
		"	mov r0, #12						\n"
		"	cmp	r12, r0						\n"
		"	bne	reg2_error_loop				\n"
		"									\n"
		"	/* Everything passed, increment the loop counter. */ \n"
		"	push { r1 }						\n"
		"	ldr	r0, =ulRegTest2LoopCounter	\n"
		"	ldr r1, [r0]					\n"
		"	add r1, r1, #1					\n"
		"	str r1, [r0]					\n"
		"	pop { r1 }						\n"
		"									\n"
		"	/* Start again. */				\n"
		"	mov r0, #10						\n"
		"	b reg2_loop						\n"
		"									\n"
		"reg2_error_loop:					\n"
		"	/* If this line is hit then there was an error in a core register value. 	\n"
		"	The loop ensures the loop counter stops incrementing. */					\n"
		"	b reg2_error_loop				\n"
		"	nop								\n"
	);
}
/*-----------------------------------------------------------*/




