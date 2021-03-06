/******************************************************************************
*
* Copyright (C) 2002 - 2015 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/
/*****************************************************************************/
/**
* @file ultrasonic_sensor_driver.c
*
* This file contains a design for an Ultrasonic Sensor (HC-SR04) driver using the AXI GPIO driver (XGpio) and
* hardware device.  It only uses channel 1 of a GPIO device and assumes that
* the bit 0 of the GPIO is connected to the sensor Trigger pin and bit 1 to the Echo pin on the HW board.
*
******************************************************************************/

/***************************** Include Files *********************************/

#include <stdio.h>
#include "xparameters.h"
#include "xgpio.h"
#include "xil_printf.h"
#include "xtime_l.h"

#include "xtmrctr.h"
#include "xil_exception.h"

#ifdef XPAR_INTC_0_DEVICE_ID
#include "xintc.h"
#include <stdio.h>
#else
#include "xscugic.h"
#endif

/************************** Constant Definitions *****************************/

#define LIDAR_MOTOR 0x1   /* Assumes bit 0 of GPIO is connected to an TRIG pin  */

/*
 * The following constants map to the XPAR parameters created in the
 * xparameters.h file. They are defined here such that a user can easily
 * change all the needed parameters in one place.
 */
#define GPIO_EXAMPLE_DEVICE_ID  XPAR_GPIO_0_DEVICE_ID

/*
 * The following constant is used to specify the amount of
 * distance measurements performed per output reading
 */
#define TOTAL_CALC	10

/*
 * The following constant is used to determine which channel of the GPIO is
 * used for the sensor pins if there are 2 channels supported.
 */
#define CHANNEL 1


/*
 * The following constants are used for the pwm pins.
 */


#define BASE_SERVO_DEVICE_ID		XPAR_TMRCTR_0_DEVICE_ID
#define SHOULDER_SERVO_DEVICE_ID	XPAR_TMRCTR_1_DEVICE_ID
#define ELBOW_SERVO_DEVICE_ID		XPAR_TMRCTR_2_DEVICE_ID
#define CLAW_SERVO_DEVICE_ID		XPAR_TMRCTR_3_DEVICE_ID

#define BASE_SERVO_INTERRUPT_ID		XPAR_FABRIC_TMRCTR_0_VEC_ID
#define SHOULDER_SERVO_INTERRUPT_ID	XPAR_FABRIC_TMRCTR_1_VEC_ID
#define ELBOW_SERVO_INTERRUPT_ID	XPAR_FABRIC_TMRCTR_2_VEC_ID
#define CLAW_SERVO_INTERRUPT_ID		XPAR_FABRIC_TMRCTR_3_VEC_ID

#define INTC_DEVICE_ID          XPAR_SCUGIC_SINGLE_DEVICE_ID
#define INTC                    XScuGic
#define INTC_HANDLER            XScuGic_InterruptHandler

#define PWM_PERIOD              50000000    /* PWM period in (.500 ms) */
#define PWM_MIN_HIGH            277777      /* PWM minimujm high time (.00278 ms) */
#define TMRCTR_0                0            /* Timer 0 ID */
#define TMRCTR_1                1            /* Timer 1 ID */
#define CYCLE_PER_DUTYCYCLE     10           /* Clock cycles per duty cycle */
#define MAX_DUTYCYCLE           100          /* Max duty cycle */
#define DUTYCYCLE_DIVISOR       4            /* Duty cycle Divisor */
#define WAIT_COUNT              PWM_PERIOD   /* Interrupt wait counter */
#define ANGLE_SHIFT				5

/************************** Function Prototypes ******************************/
int TmrCtrPwmExample(INTC *IntcInstancePtr, XTmrCtr *InstancePtr, u16 DeviceId,
								u16 IntrId, u8 Div, int ID);
static void TimerCounterHandler(void *CallBackRef, u8 TmrCtrNumber);
static int TmrCtrSetupIntrSystem(INTC *IntcInstancePtr, XTmrCtr *InstancePtr,
						u16 DeviceId, u16 IntrId);
static void TmrCtrDisableIntr(INTC *IntcInstancePtr, u16 IntrId);


/************************** Variable Definitions *****************************/
/*
 * The following are declared globally so they are zeroed and so they are
 * easily accessible from a debugger
 */

XGpio Gpio; /* The Instance of the GPIO Driver */

INTC InterruptController;  /* The instance of the Interrupt Controller */
XTmrCtr TimerCounterInst;  /* The instance of the Timer Counter */

/*
 * The following variables are shared between non-interrupt processing and
 * interrupt processing such that they must be global.
 */
static int   PeriodTimerHit = FALSE;
static int   HighTimerHit = FALSE;

/************************** Function Definitions *****************************/
void start_lidar_motor();
void test_delay();
void _delay_();


/*****************************************************************************/
int main(void){

	int Status;
	int US_MASK = 1;

//	u8 Div0_list[5] = {0, 45, 90, 135, 180};
	int shift = 1;
	int angle = 0;

	/* Initialize the GPIO driver */
	Status = XGpio_Initialize(&Gpio, GPIO_EXAMPLE_DEVICE_ID);
	if (Status != XST_SUCCESS) {
		xil_printf("Gpio Initialization Failed\r\n");
		return XST_FAILURE;
	}

	start_lidar_motor();

	/* Loop forever measuring distance */
	while (1) {

		/* Run the Timer Counter for BASE_SERVO PWM */
		Status = TmrCtrPwmExample(&InterruptController, &TimerCounterInst,
					  BASE_SERVO_DEVICE_ID, BASE_SERVO_INTERRUPT_ID, angle, 0);
		if (Status != XST_SUCCESS) {
			xil_printf("BASE_SERVO PWM Failed\r\n");
			return XST_FAILURE;
		}

		/* Run the Timer Counter for SHOULDER_SERVO PWM */
		Status = TmrCtrPwmExample(&InterruptController, &TimerCounterInst,
				SHOULDER_SERVO_DEVICE_ID, SHOULDER_SERVO_INTERRUPT_ID, 72, 0);
		if (Status != XST_SUCCESS) {
			xil_printf("BASE_SERVO PWM Failed\r\n");
			return XST_FAILURE;
		}

		/* Run the Timer Counter for ELBOW_SERVO PWM */
		Status = TmrCtrPwmExample(&InterruptController, &TimerCounterInst,
				ELBOW_SERVO_DEVICE_ID, ELBOW_SERVO_INTERRUPT_ID, 130, 0);
		if (Status != XST_SUCCESS) {
			xil_printf("BASE_SERVO PWM Failed\r\n");
			return XST_FAILURE;
		}

		/* Run the Timer Counter for CLAW_SERVO PWM */
		Status = TmrCtrPwmExample(&InterruptController, &TimerCounterInst,
				CLAW_SERVO_DEVICE_ID, CLAW_SERVO_INTERRUPT_ID, 150, 0);
		if (Status != XST_SUCCESS) {
			xil_printf("BASE_SERVO PWM Failed\r\n");
			return XST_FAILURE;
		}

		if (angle == 180){
			shift = -ANGLE_SHIFT;
		}
		if (angle == 0){
			shift = ANGLE_SHIFT;
		}

		angle = angle + shift;
		//printf("ANGLE: %i", angle);
	}

	xil_printf("Successfully ran Gpio Example\r\n");
	return XST_SUCCESS;
}

/*****************************************************************************/
/**
* This function is the main function of the Tmrctr PWM example.
*
* @param	None.
*
* @return	XST_SUCCESS to indicate success, else XST_FAILURE to indicate a
*		Failure.
*
* @note		None.
*
******************************************************************************/


void start_lidar_motor(){

	/* Set the direction for all signals as inputs except the TRIG output */
	XGpio_SetDataDirection(&Gpio, CHANNEL, ~(LIDAR_MOTOR));

	/* Clear the TRIG bit */
	XGpio_DiscreteClear(&Gpio, CHANNEL, LIDAR_MOTOR);

	/* Set the TRIG to High */
	XGpio_DiscreteWrite(&Gpio, CHANNEL, LIDAR_MOTOR);
}


void test_delay(int delay_size){

	printf("Testing Delay %i us\n", delay_size);

	XTime tStart, tEnd;

	/* get time before delay */
	XTime_GetTime(&tStart);

	/* run the delay */
	_delay_(delay_size);

	/* get time after the delay */
	XTime_GetTime(&tEnd);

	printf("Start %f us\n", (1.0 * tStart));
	printf("End %f us\n", (1.0 * tEnd));

	/* calculate delay length in useconds */
	float delay = 1.0 * (tEnd - tStart) / (COUNTS_PER_SECOND/1000000);
	printf("Delay took %f us\n\n", delay);
}

void _delay_(int useconds){

	float delay;
	XTime tStart, tNext;

	/* get time at start of delay */
	XTime_GetTime(&tStart);

	while(1){
		/* check time for each loop */
		XTime_GetTime(&tNext);

		/* calculate the delay in micro-seconds */
		delay = 1.0 * (tNext - tStart) / (COUNTS_PER_SECOND/1000000);

		/* if the delay is greater than or equal to the specified time, end delay */
		if(delay >= useconds){
			return;
		}
	}
}

/*****************************************************************************/
/**
* This function demonstrates the use of tmrctr PWM APIs.
*
* @param	IntcInstancePtr is a pointer to the Interrupt Controller
*		driver Instance
* @param	TmrCtrInstancePtr is a pointer to the XTmrCtr driver Instance
* @param	DeviceId is the XPAR_<TmrCtr_instance>_DEVICE_ID value from
*		xparameters.h
* @param	IntrId is XPAR_<INTC_instance>_<TmrCtr_instance>_INTERRUPT_INTR
*		value from xparameters.h
*
* @return	XST_SUCCESS if the Test is successful, otherwise XST_FAILURE
*
* @note		none.
*
*****************************************************************************/
int TmrCtrPwmExample(INTC *IntcInstancePtr, XTmrCtr *TmrCtrInstancePtr,
						u16 DeviceId, u16 IntrId, u8 Div, int ID)
{
	u8  DutyCycle;
	u8  NoOfCycles;
	u32 Period;
	u32 HighTime;
	u64 WaitCount;
	int Status;

	/*
	 * Initialize the timer counter so that it's ready to use,
	 * specify the device ID that is generated in xparameters.h
	 */
	Status = XTmrCtr_Initialize(TmrCtrInstancePtr, DeviceId);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Perform a self-test to ensure that the hardware was built
	 * correctly. Timer0 is used for self test
	 */
	Status = XTmrCtr_SelfTest(TmrCtrInstancePtr, TMRCTR_0);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Connect the timer counter to the interrupt subsystem such that
	 * interrupts can occur
	 */
	Status = TmrCtrSetupIntrSystem(IntcInstancePtr, TmrCtrInstancePtr,
							DeviceId, IntrId);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Setup the handler for the timer counter that will be called from the
	 * interrupt context when the timer expires
	 */
	XTmrCtr_SetHandler(TmrCtrInstancePtr, TimerCounterHandler,
							TmrCtrInstancePtr);

	/* Enable the interrupt of the timer counter */
	XTmrCtr_SetOptions(TmrCtrInstancePtr, TMRCTR_0, XTC_INT_MODE_OPTION);
	XTmrCtr_SetOptions(TmrCtrInstancePtr, TMRCTR_1, XTC_INT_MODE_OPTION);

	/*
	 * We start with the fixed divisor and after every CYCLE_PER_DUTYCYCLE
	 * decrement the divisor by 1, as a result Duty cycle increases
	 * proportionally. This is done until duty cycle is reached upto
	 * MAX_DUTYCYCLE
	 */


	/* Disable PWM for reconfiguration */
	XTmrCtr_PwmDisable(TmrCtrInstancePtr);

	/* Configure PWM */
	Period = PWM_PERIOD;
	HighTime = PWM_MIN_HIGH + (Div * 12345);
	DutyCycle = XTmrCtr_PwmConfigure(TmrCtrInstancePtr, Period, HighTime);
	if (Status != XST_SUCCESS) {
		Status = XST_FAILURE;
		goto err;
	}

	xil_printf("\nPWM %i \r\n", ID);
	xil_printf("Angle = %d\r\n", Div);

	/* Enable PWM */
	XTmrCtr_PwmEnable(TmrCtrInstancePtr);

	WaitCount = PWM_PERIOD / 10;
	while (WaitCount > 0) {
		WaitCount--;
	}

	Status = XST_SUCCESS;
err:
	/* Disable PWM */
	XTmrCtr_PwmDisable(TmrCtrInstancePtr);

	/* Disable interrupts */
	TmrCtrDisableIntr(IntcInstancePtr, DeviceId);

	return Status;
}

/*****************************************************************************/
/**
* This function is the handler which performs processing for the timer counter.
* It is called from an interrupt context.
*
* @param	CallBackRef is a pointer to the callback function
* @param	TmrCtrNumber is the number of the timer to which this
*		handler is associated with.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
static void TimerCounterHandler(void *CallBackRef, u8 TmrCtrNumber)
{
	/* Mark if period timer expired */
	if (TmrCtrNumber == TMRCTR_0) {
		PeriodTimerHit = TRUE;
	}

	/* Mark if high time timer expired */
	if (TmrCtrNumber == TMRCTR_1) {
		HighTimerHit = TRUE;
	}
}

/*****************************************************************************/
/**
* This function setups the interrupt system such that interrupts can occur
* for the timer counter. This function is application specific since the actual
* system may or may not have an interrupt controller.  The timer counter could
* be directly connected to a processor without an interrupt controller.  The
* user should modify this function to fit the application.
*
* @param	IntcInstancePtr is a pointer to the Interrupt Controller
*		driver Instance.
* @param	TmrCtrInstancePtr is a pointer to the XTmrCtr driver Instance.
* @param	DeviceId is the XPAR_<TmrCtr_instance>_DEVICE_ID value from
*		xparameters.h.
* @param	IntrId is XPAR_<INTC_instance>_<TmrCtr_instance>_VEC_ID
*		value from xparameters.h.
*
* @return	XST_SUCCESS if the Test is successful, otherwise XST_FAILURE.
*
* @note		none.
*
******************************************************************************/
static int TmrCtrSetupIntrSystem(INTC *IntcInstancePtr,
			XTmrCtr *TmrCtrInstancePtr, u16 DeviceId, u16 IntrId)
{
	 int Status;

#ifdef XPAR_INTC_0_DEVICE_ID
	/*
	 * Initialize the interrupt controller driver so that
	 * it's ready to use, specify the device ID that is generated in
	 * xparameters.h
	 */
	Status = XIntc_Initialize(IntcInstancePtr, INTC_DEVICE_ID);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Connect a device driver handler that will be called when an interrupt
	 * for the device occurs, the device driver handler performs the
	 * specific interrupt processing for the device
	 */
	Status = XIntc_Connect(IntcInstancePtr, IntrId,
				(XInterruptHandler)XTmrCtr_InterruptHandler,
				(void *)TmrCtrInstancePtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Start the interrupt controller such that interrupts are enabled for
	 * all devices that cause interrupts, specific real mode so that
	 * the timer counter can cause interrupts through the interrupt
	 * controller
	 */
	Status = XIntc_Start(IntcInstancePtr, XIN_REAL_MODE);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Enable the interrupt for the timer counter */
	XIntc_Enable(IntcInstancePtr, IntrId);
#else
	XScuGic_Config *IntcConfig;

	/*
	 * Initialize the interrupt controller driver so that it is ready to
	 * use
	 */
	IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == IntcConfig) {
		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig,
					IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	XScuGic_SetPriorityTriggerType(IntcInstancePtr, IntrId,
					0xA0, 0x3);

	/*
	 * Connect the interrupt handler that will be called when an
	 * interrupt occurs for the device.
	 */
	Status = XScuGic_Connect(IntcInstancePtr, IntrId,
				 (Xil_ExceptionHandler)XTmrCtr_InterruptHandler,
				 TmrCtrInstancePtr);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	/* Enable the interrupt for the Timer device */
	XScuGic_Enable(IntcInstancePtr, IntrId);
#endif /* XPAR_INTC_0_DEVICE_ID */

	/* Initialize the exception table */
	Xil_ExceptionInit();

	/* Register the interrupt controller handler with the exception table */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
					(Xil_ExceptionHandler)
					INTC_HANDLER,
					IntcInstancePtr);

	/* Enable non-critical exceptions */
	Xil_ExceptionEnable();

	return XST_SUCCESS;
}

/******************************************************************************/
/**
*
* This function disconnects the interrupts for the Timer.
*
* @param	IntcInstancePtr is a reference to the Interrupt Controller
*		driver Instance.
* @param	IntrId is XPAR_<INTC_instance>_<Timer_instance>_VEC_ID
*		value from xparameters.h.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void TmrCtrDisableIntr(INTC *IntcInstancePtr, u16 IntrId)
{
	/* Disconnect the interrupt for the timer counter */
#ifdef XPAR_INTC_0_DEVICE_ID
	XIntc_Disconnect(IntcInstancePtr, IntrId);
#else
	XScuGic_Disconnect(IntcInstancePtr, IntrId);
#endif
}
