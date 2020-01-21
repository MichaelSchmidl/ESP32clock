#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <driver/gpio.h>

#include "driver/timer.h"
#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_INTERVAL_SEC    (0.01)

#include "hardware.h"

#ifdef USE_M5_TFT
#include "diag_task.h"
#endif

#include "driver/ledc.h"
uint8_t currentBrightness = 128;
#define LEDC_CH_NUM            (1)
#define LEDC_HS_TIMER          LEDC_TIMER_1
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE
typedef struct {
    int channel;
    int io;
    int mode;
    int timer_idx;
} ledc_info_t;
#define LEDC_DOTS 0
ledc_info_t ledc_ch[LEDC_CH_NUM] = {
	{
		.channel   = LEDC_CHANNEL_0,
		.io        = RED_DOTS,
		.mode      = LEDC_HS_MODE,
		.timer_idx = LEDC_HS_TIMER
	}
};

static esp_timer_handle_t multiplex_timer;

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

static uint8_t digit_m10_segments = SEG_A_MASK | SEG_B_MASK | SEG_C_MASK | SEG_D_MASK | SEG_E_MASK | SEG_F_MASK | SEG_G_MASK;
static uint8_t digit_m1_segments =  SEG_A_MASK | SEG_B_MASK | SEG_C_MASK | SEG_D_MASK | SEG_E_MASK | SEG_F_MASK | SEG_G_MASK;
static uint8_t digit_h10_segments = SEG_A_MASK | SEG_B_MASK | SEG_C_MASK | SEG_D_MASK | SEG_E_MASK | SEG_F_MASK | SEG_G_MASK;
static uint8_t digit_h1_segments =  SEG_A_MASK | SEG_B_MASK | SEG_C_MASK | SEG_D_MASK | SEG_E_MASK | SEG_F_MASK | SEG_G_MASK;


static void _setSegments( char c, uint8_t *pDigitSegments )
{
	switch ( c )
	{
		case 'n':
			*pDigitSegments =     0      |     0      | SEG_C_MASK |     0      | SEG_E_MASK |     0      | SEG_G_MASK;
			break;
		case 'o':
			*pDigitSegments =     0      |     0      | SEG_C_MASK | SEG_D_MASK | SEG_E_MASK |     0      | SEG_G_MASK;
			break;
		case 'A':
			*pDigitSegments = SEG_A_MASK | SEG_B_MASK | SEG_C_MASK |     0      | SEG_E_MASK | SEG_F_MASK | SEG_G_MASK;
			break;
		case 'P':
			*pDigitSegments = SEG_A_MASK | SEG_B_MASK |     0      |     0      | SEG_E_MASK | SEG_F_MASK | SEG_G_MASK;
			break;
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
		case ' ':
			*pDigitSegments =     0      |     0      |     0      |     0      |     0      |     0      |     0     ;
			break;
		default:
			*pDigitSegments =     0      |     0      |     0      | SEG_D_MASK |     0      |     0      |     0     ;
			break;
	}
}


static void inline _stopDrivingLeds( void )
{
	gpio_set_level(     DIGIT_H10, DIGIT_INACTIVE );
    gpio_set_level(     DIGIT_H1,  DIGIT_INACTIVE );
    gpio_set_level(     DIGIT_M10, DIGIT_INACTIVE );
    gpio_set_level(     DIGIT_M1,  DIGIT_INACTIVE );
}

static void inline _setSegmentPins( uint8_t bitMask )
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

    switch ( multiplexCounter )
    {
        case 0:
        	_stopDrivingLeds();
            break;
        case 1:
        	_setSegmentPins( digit_h10_segments );
        	break;
        case 2:
            gpio_set_level( DIGIT_H10, DIGIT_ACTIVE );
        	break;
        case 3:
        	_stopDrivingLeds();
            break;
        case 4:
        	_setSegmentPins( digit_h1_segments );
        	break;
        case 5:
            gpio_set_level( DIGIT_H1, DIGIT_ACTIVE );
        	break;
        case 6:
        	_stopDrivingLeds();
            break;
        case 7:
        	_setSegmentPins( digit_m10_segments );
        	break;
        case 8:
            gpio_set_level( DIGIT_M10, DIGIT_ACTIVE );
        	break;
        case 9:
        	_stopDrivingLeds();
            break;
        case 10:
        	_setSegmentPins( digit_m1_segments );
        	break;
        case 11:
            gpio_set_level( DIGIT_M1, DIGIT_ACTIVE );
        	break;
        default:
        	multiplexCounter = 0;
        	break;
    }
	multiplexCounter++;
	if ( multiplexCounter >= 12 ) multiplexCounter = 0;

	gpio_set_level( DBG_PIN, 0 );
}


static void _initializeMultiplexPins( void )
{
    gpio_set_direction( DBG_PIN, GPIO_MODE_OUTPUT );
    gpio_set_level(     DBG_PIN, 0 );

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT, //set timer counter bit number
        .freq_hz = LEDC_PWM_FREQ,            //set frequency of pwm
        .speed_mode = LEDC_HS_MODE,          //timer mode,
        .timer_num = LEDC_HS_TIMER           //timer index
    };
    //configure timer0 for high speed channels
    if ( ESP_OK != ledc_timer_config(&ledc_timer))
    {
        ESP_LOGE(__func__, "ledc_timer_config failed");
    }

    for (int ch = 0; ch < LEDC_CH_NUM; ch++) {
        ledc_channel_config_t ledc_channel = {
            //set LEDC channel 0
            .channel = ledc_ch[ch].channel,
            //set the duty for initialization.(duty range is 0 ~ ((2**bit_num)-1)
            .duty = 0, // 0% by default
            //GPIO number
            .gpio_num = ledc_ch[ch].io,
            //GPIO INTR TYPE, as an example, we enable fade_end interrupt here.
            .intr_type = LEDC_INTR_DISABLE,
            //set LEDC mode, from ledc_mode_t
            .speed_mode = ledc_ch[ch].mode,
            //set LEDC timer source, if different channel use one timer,
            //the frequency and bit_num of these channels should be the same
            .timer_sel = ledc_ch[ch].timer_idx,
        };
        //set the configuration
        if (ESP_OK != ledc_channel_config(&ledc_channel))
        {
            ESP_LOGE(__func__, "ledc_channel_config failed");
        }
    }

    // set dots on
    ledc_set_duty(ledc_ch[LEDC_DOTS].mode, ledc_ch[LEDC_DOTS].channel, 64);
    ledc_update_duty(ledc_ch[LEDC_DOTS].mode, ledc_ch[LEDC_DOTS].channel);

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


void stop7SegMultiplex( void )
{
	ESP_ERROR_CHECK(esp_timer_stop( multiplex_timer ));
	_stopDrivingLeds();
}


void start7SegMultiplex( uint64_t period_us )
{
	_initializeMultiplexPins();

    ///////////////////////////////////////////////////////////////////////////
	const esp_timer_create_args_t multiplex_timer_args = {
            .callback = &multiplexTimer_callback,
            /* name is optional, but may help identify the timer when debugging */
            .name = "multiplex"
    };

    ESP_ERROR_CHECK(esp_timer_create(&multiplex_timer_args, &multiplex_timer));
    /* The timer has been created but is not running yet */

    /* Start the timer */
    ESP_ERROR_CHECK(esp_timer_start_periodic(multiplex_timer, period_us)); // [us]
}
