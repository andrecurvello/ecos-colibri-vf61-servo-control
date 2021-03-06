/*
 * servo.c
 *
 * (c) 2015 Toradex AG
 */
#include <stdlib.h>
#include <cyg/hal/hal_intr.h>
#include <cyg/hal/hal_if.h>
#include <math.h>
#include "servo.h"

#define USING_MCC

#ifdef USING_MCC
#include <cyg/mcc/mcc_api.h>
#endif

#define CYGHWR_HAL_VYBRID_PORT_ALT_GPIO 0
#define CYGHWR_HAL_VYBRID_PORT_INPUT 0x21
#define CYGHWR_HAL_VYBRID_PORT_OUTPUT 0x22

// PTB19 => GPIO 41 => SO-DIMM 45 => Viola X9, Pin 16
#define PUL_PWM CYGHWR_HAL_VYBRID_PIN(B, 19, CYGHWR_HAL_VYBRID_PORT_ALT_GPIO, CYGHWR_HAL_VYBRID_PORT_OUTPUT)

#ifdef USING_MCC
typedef struct the_message
{
   cyg_uint32 DATA;
   cyg_uint32 STATUS;
} THE_MESSAGE;

unsigned int endpoint_a5[] = {0,MCC_NODE_A5,MCC_SENDER_PORT};
unsigned int endpoint_m4[] = {1,MCC_NODE_M4,MCC_RESPONDER_PORT};
#endif

//Threads
static unsigned char stack_thread_mcc[CYGNUM_HAL_STACK_SIZE_MINIMUM];

cyg_handle_t thread_mcc;

cyg_thread thread_data_mcc;

//Control variables
int deg = 0;
int servo_state = SERVO_STATE_END;

extern cyg_uint32 hal_cortexm_systick_clock;
//Main Thread

/*
 * Only support up to 1s!
 */
static void delay_us_exact(int us)
{
	cyg_int32 val_dst;
	cyg_uint32 times, period;
	cyg_uint32 t0, t1;

	times = 1000000000 / (us * 1000);

	val_dst = hal_cortexm_systick_clock / times;

	HAL_READ_UINT32(CYGARC_REG_SYSTICK_BASE+CYGARC_REG_SYSTICK_RELOAD, period );
	HAL_READ_UINT32(CYGARC_REG_SYSTICK_BASE+CYGARC_REG_SYSTICK_VALUE, t0);

	while ( val_dst > 0 )
	{
		HAL_READ_UINT32(CYGARC_REG_SYSTICK_BASE+CYGARC_REG_SYSTICK_VALUE, t1);
		if( t1 > t0 )
			val_dst -= (t0 + period - t1);
		else
			val_dst -= t0 - t1;
		t0 = t1;
	}
}

int main(int argc, char **argv)
{
	cyg_thread_create(10, (cyg_thread_entry_t*) thread_mcc_fn, 0, "MCC thread", &stack_thread_mcc[0], CYGNUM_HAL_STACK_SIZE_MINIMUM, &thread_mcc, &thread_data_mcc);

	diag_printf("----------------------\n");
	diag_printf(" eCos servo drive demo\n");
	diag_printf("----------------------\n");

	// Initialize GPIO
	hal_set_pin_function(PUL_PWM);

	// MCC communication thread
	cyg_thread_resume(thread_mcc);

	int count = 0;

	int usperdeg = 11;
	int period = 20000;
	
	int minduty = 1000;
	int maxduty = 2000;
	int duty = minduty;

	while (true)
	{
		count++;
		duty = minduty + deg * usperdeg;
		if (duty > maxduty)
			duty = maxduty;

		/* 
		 * High pulse, disable all interrupts for absolut highest
		 * precission
		 */
		cyg_interrupt_disable();
		hal_gpio_set_pin(PUL_PWM);
		delay_us_exact(duty);

		/* Low pulse */
		hal_gpio_clear_pin(PUL_PWM);
		delay_us_exact(period-duty);
		cyg_interrupt_enable();
		delay_us_exact(200);

		switch (servo_state) {
		case SERVO_STATE_CW:
			/* Next step every 200ms */
			if (count % 20)
				break;

			deg += 5;
			if (deg >= 90) {
				servo_state = SERVO_STATE_CCW;
				diag_printf("peak reached: %d\n", duty);
			}

			break;

		case SERVO_STATE_CCW:
			deg = 0;
			servo_state = SERVO_STATE_END;
			break;
		case SERVO_STATE_END:
			break;
		default:
			/* Auto restart... */
			servo_state = SERVO_STATE_CW;
			break;
		}
	}
}

/**
 * MCC Thread
 */
void thread_mcc_fn(void)
{

	THE_MESSAGE     msg, smsg;
	unsigned int    num_of_received_bytes;
	int             ret_value;

	msg.DATA = 1;
	ret_value = mcc_initialize(MCC_NODE_M4);

	if(ret_value) {
		diag_printf("Error! App stopped\n");
		return;
	}

	ret_value = mcc_create_endpoint(endpoint_m4, MCC_RESPONDER_PORT);

	while(1)
	{
		ret_value = mcc_recv_copy(endpoint_m4, &msg, sizeof(THE_MESSAGE), &num_of_received_bytes, 50000);
		if (ret_value)
			continue;

		if(msg.DATA <= SERVO_STATE_END)
			servo_state = msg.DATA;

		diag_printf("Responder OK... %d %d\n", msg.DATA, msg.STATUS);

		cyg_thread_delay(1);
		smsg.DATA = deg;
		smsg.STATUS = servo_state;
		ret_value = mcc_send(endpoint_a5, &smsg, sizeof(THE_MESSAGE), 5000000);

		//diag_printf("Plotter status: %d Degree: %d\n", servo_state, deg);
		cyg_thread_delay(50);
	}

}

