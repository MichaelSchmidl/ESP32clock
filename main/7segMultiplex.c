#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <driver/gpio.h>

#include "hardware.h"

#ifdef USE_M5_TFT
#include "diag_task.h"
#endif

/*
             A
           F   B
             G
           E   C
             D
 */
#define SEG_A_MASK 0x01
#define SEG_B_MASK 0x02
#define SEG_C_MASK 0x04
#define SEG_D_MASK 0x08
#define SEG_E_MASK 0x10
#define SEG_F_MASK 0x20
#define SEG_G_MASK 0x40

static uint8_t digit_m10_segments = SEG_G_MASK;
static uint8_t digit_m1_segments = SEG_G_MASK;
static uint8_t digit_h10_segments = SEG_G_MASK;
static uint8_t digit_h1_segments = SEG_G_MASK;


static void _setSegments( char c, uint8_t *pDigitSegments )
{
	switch ( c )
	{
		case '0':
			*pDigitSegments = SEG_A_MASK | SEG_B_MASK | SEG_C_MASK | SEG_D_MASK | SEG_E_MASK | SEG_F_MASK |     0     ;
			break;
		case '1':
			*pDigitSegments =     0      | SEG_B_MASK | SEG_C_MASK |     0      |     0      |     0      |     0     ;
			break;
		case '2':
			*pDigitSegments = SEG_A_MASK | SEG_B_MASK |     0      | SEG_D_MASK | SEG_E_MASK |     0      | SEG_G_MASK;
			break;
		case '3':
			*pDigitSegments = SEG_A_MASK | SEG_B_MASK | SEG_C_MASK | SEG_D_MASK |     0      |     0      | SEG_G_MASK;
			break;
		case '4':
			*pDigitSegments =     0      | SEG_B_MASK | SEG_C_MASK |     0      |     0      | SEG_F_MASK | SEG_G_MASK;
			break;
		case '5':
			*pDigitSegments = SEG_A_MASK |     0      | SEG_C_MASK | SEG_D_MASK |     0      | SEG_F_MASK | SEG_G_MASK;
			break;
		case '6':
			*pDigitSegments = SEG_A_MASK |     0      | SEG_C_MASK | SEG_D_MASK | SEG_E_MASK | SEG_F_MASK | SEG_G_MASK;
			break;
		case '7':
			*pDigitSegments = SEG_A_MASK | SEG_B_MASK | SEG_C_MASK |     0      |     0      |     0      |     0     ;
			break;
		case '8':
			*pDigitSegments = SEG_A_MASK | SEG_B_MASK | SEG_C_MASK | SEG_D_MASK | SEG_E_MASK | SEG_F_MASK | SEG_G_MASK;
			break;
		case '9':
			*pDigitSegments = SEG_A_MASK | SEG_B_MASK | SEG_C_MASK | SEG_D_MASK |     0      | SEG_F_MASK | SEG_G_MASK;
			break;
		default:
			break;
	}
}


static void _setSegmentPins( uint8_t bitMask )
{
    gpio_set_level( SEG_A, bitMask & SEG_A_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_B, bitMask & SEG_B_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_C, bitMask & SEG_C_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_D, bitMask & SEG_D_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_E, bitMask & SEG_E_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_F, bitMask & SEG_F_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_G, bitMask & SEG_G_MASK ? SEGMENT_ON : SEGMENT_OFF );
}


static void multiplexTimer_callback(void* arg)
{
	static uint8_t multiplexCounter = 0;
    gpio_set_level( DBG_PIN, 1 );
//    ESP_LOGI(__func__, "multiplexing #%d %02x %02x %02x %02x", multiplexCounter, digit_h10_segments, digit_h1_segments, digit_m10_segments, digit_m1_segments);


#if 0
    digit_h10_segments = SEG_D_MASK;
    digit_h1_segments = SEG_B_MASK;
    digit_m10_segments = 0;
    digit_m1_segments = 0;
#endif

    _setSegmentPins( 0 ); // all off

    gpio_set_level(     DIGIT_M10, DIGIT_INACTIVE );
    gpio_set_level(     DIGIT_M1,  DIGIT_INACTIVE );
    gpio_set_level(     DIGIT_H10, DIGIT_INACTIVE );
    gpio_set_level(     DIGIT_H1,  DIGIT_INACTIVE );

    _setSegmentPins( 0 ); // all off

    switch ( multiplexCounter )
    {
        case 0:
            gpio_set_level( DIGIT_H10, DIGIT_ACTIVE );
        	_setSegmentPins( digit_h10_segments );
        	multiplexCounter++;
        	break;
        case 1:
            gpio_set_level( DIGIT_H1, DIGIT_ACTIVE );
        	_setSegmentPins( digit_h1_segments );
        	multiplexCounter++;
        	break;
        case 2:
            gpio_set_level( DIGIT_M10, DIGIT_ACTIVE );
        	_setSegmentPins( digit_m10_segments );
        	multiplexCounter++;
        	break;
        case 3:
            gpio_set_level( DIGIT_M1, DIGIT_ACTIVE );
        	_setSegmentPins( digit_m1_segments );
        	multiplexCounter = 0;
        	break;
        default:
        	multiplexCounter = 0;
        	break;
    }
    gpio_set_level( DBG_PIN, 0 );
}


static void _initializeMultiplexPins( void )
{
    gpio_set_direction( DBG_PIN, GPIO_MODE_OUTPUT );
    gpio_set_level(     DBG_PIN, 0 );


	gpio_set_direction( DIGIT_M10, GPIO_MODE_OUTPUT );
    gpio_set_level(     DIGIT_M10, DIGIT_INACTIVE );

    gpio_set_direction( DIGIT_M1, GPIO_MODE_OUTPUT );
    gpio_set_level(     DIGIT_M1, DIGIT_INACTIVE );

    gpio_set_direction( DIGIT_H10, GPIO_MODE_OUTPUT );
    gpio_set_level(     DIGIT_H10, DIGIT_INACTIVE );

    gpio_set_direction( DIGIT_H1, GPIO_MODE_OUTPUT );
    gpio_set_level(     DIGIT_H1, DIGIT_INACTIVE );

    gpio_set_direction( SEG_A, GPIO_MODE_OUTPUT );
    gpio_set_level(     SEG_A, SEGMENT_OFF );

    gpio_set_direction( SEG_B, GPIO_MODE_OUTPUT );
    gpio_set_level(     SEG_B, SEGMENT_OFF );

    gpio_set_direction( SEG_C, GPIO_MODE_OUTPUT );
    gpio_set_level(     SEG_C, SEGMENT_OFF );

    gpio_set_direction( SEG_D, GPIO_MODE_OUTPUT );
    gpio_set_level(     SEG_D, SEGMENT_OFF );

    gpio_set_direction( SEG_E, GPIO_MODE_OUTPUT );
    gpio_set_level(     SEG_E, SEGMENT_OFF );

    gpio_set_direction( SEG_F, GPIO_MODE_OUTPUT );
    gpio_set_level(     SEG_F, SEGMENT_OFF );

    gpio_set_direction( SEG_G, GPIO_MODE_OUTPUT );
    gpio_set_level(     SEG_G, SEGMENT_OFF );
}


void multiplex_setTime( char *szTime )
{
    ESP_LOGI(__func__, "[%s]", szTime);
#ifdef USE_M5_TFT
    tftShowTime( szTime );
#endif
    _setSegments( szTime[0], &digit_h10_segments );
	_setSegments( szTime[1], &digit_h1_segments );
	_setSegments( szTime[3], &digit_m10_segments );
	_setSegments( szTime[4], &digit_m1_segments );
}


void start7SegMultiplex( uint64_t period_us )
{
	_initializeMultiplexPins();

	const esp_timer_create_args_t multiplex_timer_args = {
            .callback = &multiplexTimer_callback,
            /* name is optional, but may help identify the timer when debugging */
            .name = "multiplex"
    };

    esp_timer_handle_t multiplex_timer;
    ESP_ERROR_CHECK(esp_timer_create(&multiplex_timer_args, &multiplex_timer));
    /* The timer has been created but is not running yet */

    /* Start the timer */
    ESP_ERROR_CHECK(esp_timer_start_periodic(multiplex_timer, period_us)); // [us]
}
