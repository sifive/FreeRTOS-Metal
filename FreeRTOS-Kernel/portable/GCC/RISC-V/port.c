/*
 * FreeRTOS Kernel V10.2.1
 * Copyright (C) 2019 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

/*-----------------------------------------------------------
 * Implementation of functions defined in portable.h for the RISC-V RV32 port.
 *----------------------------------------------------------*/

/* Scheduler includes. */
#include "FreeRTOS.h"
#include <stdio.h>
#include "task.h"
#include "portmacro.h"
#include "string.h"


/*
 * xISRStackTop must be declared elsewhere.
 */
extern StackType_t xISRStackTop;

/*-----------------------------------------------------------*/

/* Used to program the machine timer compare register. */
uint64_t ullNextTime = 0ULL;
const uint64_t *pullNextTime = &ullNextTime;
const size_t uxTimerIncrementsForOneTick = ( size_t ) ( configCPU_CLOCK_HZ / configTICK_RATE_HZ ); /* Assumes increment won't go over 32-bits. */
volatile uint64_t * const pullMachineTimerCompareRegister = ( volatile uint64_t * const ) ( configCLINT_BASE_ADDRESS + 0x4000 );
volatile uint64_t * const pullMachineTimerRegister        = ( volatile uint64_t * const ) ( configCLINT_BASE_ADDRESS + 0xBFF8 );

/* Set configCHECK_FOR_STACK_OVERFLOW to 3 to add ISR stack checking to task
stack checking.  A problem in the ISR stack will trigger an assert, not call the
stack overflow hook function (because the stack overflow hook is specific to a
task stack, not the ISR stack). */
#if( configCHECK_FOR_STACK_OVERFLOW > 2 )
	#warning This path not tested, or even compiled yet.
	/* Don't use 0xa5 as the stack fill bytes as that is used by the kernerl for
	the task stacks, and so will legitimately appear in many positions within
	the ISR stack. */
	#define portISR_STACK_FILL_BYTE	0xee

	static const uint8_t ucExpectedStackBytes[] = {
									portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE,		\
									portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE,		\
									portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE,		\
									portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE,		\
									portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE, portISR_STACK_FILL_BYTE };	\

	#define portCHECK_ISR_STACK() configASSERT( ( memcmp( ( void * ) xISRStack, ( void * ) ucExpectedStackBytes, sizeof( ucExpectedStackBytes ) ) == 0 ) )
#else
	/* Define the function away. */
	#define portCHECK_ISR_STACK()
#endif /* configCHECK_FOR_STACK_OVERFLOW > 2 */
/*-----------------------------------------------------------*/

BaseType_t xPortFreeRTOSInit( StackType_t xTopOfStack )
{
	#ifndef configISR_STACK_SIZE
	# define configISR_STACK_SIZE            0
	#endif

	#if( configISR_STACK_SIZE == 0)
	UBaseType_t xISRStackLength = 0x100;
	#else
	UBaseType_t xISRStackLength = configISR_STACK_SIZE;
	#endif

	BaseType_t xValue;
	extern BaseType_t xPortMoveStack( StackType_t xStackTop, UBaseType_t xStackLength);

	/*
	 * stack mapping before :
	 * 		Top stack +----------------------+ xTopOfStack
	 * 		          | stack allocation     |
	 * 		          | before this function |
	 * 		          | Call Stack_SYS       |
	 * 		          +----------------------+
	 * 		          | ....                 |
	 * 		          |                      |
	 * 		Bottom    +----------------------+
	 *
	 * stack mapping after :
	 * 		Top stack +----------------------+ xTopOfStack
	 * 		          | Space to store       |
	 * 		          | context before       |
	 * 		          | FreeRtos scheduling  |
	 * 		          +----------------------+ xISRStackTop
	 * 		          | stack space for      |
	 * 		          | ISR execution        |
	 * 		          +----------------------+
	 * 		          | stack allocation     |
	 * 		          | before this function |
	 * 		          | Call Stack_SYS       |
	 * 		          +----------------------+
	 * 		          | ....                 |
	 * 		          |                      |
	 * 		Bottom    +----------------------+
	 */

	xValue = xPortMoveStack(xTopOfStack, xISRStackLength);

	if ( 0 == xValue ) {
		return -1;
	}
	xISRStackTop = xTopOfStack + xValue;

	#if( configASSERT_DEFINED == 1 )
	{
        /* Check alignment of the interrupt stack - which is the same as the
        stack that was being used by main() prior to the scheduler being
        started. */
        configASSERT( ( xISRStackTop & portBYTE_ALIGNMENT_MASK ) == 0 );
	}
	#endif /* configASSERT_DEFINED */

#if( configCLINT_BASE_ADDRESS != 0 )
	/* There is a clint then interrupts can branch directly to the FreeRTOS 
	 * trap handler.
	 */
 	 __asm volatile ("la t0, freertos_risc_v_trap_handler\n"
	                  "csrw mtvec, t0\n");
#else
# warning "*** The interrupt controller must to be configured before (ouside of this file). ***"
#endif
	return 0;
}
/*-----------------------------------------------------------*/

BaseType_t xPortStartScheduler( void )
{
	extern BaseType_t xPortStartFirstTask( void );
	BaseType_t xRetValue = pdFAIL;

	#if( configASSERT_DEFINED == 1 )
	{
		volatile uint32_t mtvec = 0;

		/* Check the least significant two bits of mtvec are 00 - indicating
		single vector mode. */
		__asm__ __volatile__ ( "csrr %0, mtvec" : "=r"( mtvec ) );
		configASSERT( ( mtvec & 0x03UL ) == 0 );

		/* Check alignment of the interrupt stack - which is the same as the
		stack that was being used by main() prior to the scheduler being
		started. */
		configASSERT( ( xISRStackTop & portBYTE_ALIGNMENT_MASK ) == 0 );
	}
	#endif /* configASSERT_DEFINED */

	xRetValue = xPortStartFirstTask();

	return xRetValue;
}
/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
	extern void xPortRestoreBeforeFirstTask(void);

	xPortRestoreBeforeFirstTask();

	/* 
	 * Should not get here as after calling xPortRestoreBeforeFirstTask() we should 
	 * return after de execution of xPortStartFirstTask in xPortStartScheduler function.
	 */
	for( ;; );
}
/*-----------------------------------------------------------*/