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

#define USE_LEDC_DIMMING

#include "driver/ledc.h"
uint8_t currentBrightness = 128;
#define LEDC_CH_NUM            (5)
#define LEDC_HS_TIMER          LEDC_TIMER_1
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE
typedef struct {
    int channel;
    int io;
    int mode;
    int timer_idx;
} ledc_info_t;
#define LEDC_H10  0
#define LEDC_H1   1
#define LEDC_M10  2
#define LEDC_M1   3
#define LEDC_DOTS 4
ledc_info_t ledc_ch[LEDC_CH_NUM] = {
	{
		.channel   = LEDC_CHANNEL_0,
		.io        = DIGIT_H10,
		.mode      = LEDC_HS_MODE,
		.timer_idx = LEDC_HS_TIMER
	},
	{
		.channel   = LEDC_CHANNEL_1,
		.io        = DIGIT_H1,
		.mode      = LEDC_HS_MODE,
		.timer_idx = LEDC_HS_TIMER
	},
	{
		.channel   = LEDC_CHANNEL_2,
		.io        = DIGIT_M10,
		.mode      = LEDC_HS_MODE,
		.timer_idx = LEDC_HS_TIMER
	},
	{
		.channel   = LEDC_CHANNEL_3,
		.io        = DIGIT_M1,
		.mode      = LEDC_HS_MODE,
		.timer_idx = LEDC_HS_TIMER
	},
	{
		.channel   = LEDC_CHANNEL_4,
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
		default:
			*pDigitSegments =     0      |     0      |     0      | SEG_D_MASK |     0      |     0      |     0     ;
			break;
	}
}


static void IRAM_ATTR inline _stopDrivingLeds( void )
{
#ifdef USE_LEDC_DIMMING
    for (int ch = 0; ch < LEDC_DOTS; ch++)
    {
        ledc_set_duty(ledc_ch[ch].mode, ledc_ch[ch].channel, 0);
        ledc_update_duty(ledc_ch[ch].mode, ledc_ch[ch].channel);
    }
#else
	gpio_set_level(     DIGIT_H10, DIGIT_INACTIVE );
    gpio_set_level(     DIGIT_H1,  DIGIT_INACTIVE );
    gpio_set_level(     DIGIT_M10, DIGIT_INACTIVE );
    gpio_set_level(     DIGIT_M1,  DIGIT_INACTIVE );
#endif
}

static void IRAM_ATTR inline _setSegmentPins( uint8_t bitMask )
{
    gpio_set_level( SEG_A, bitMask & SEG_A_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_B, bitMask & SEG_B_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_C, bitMask & SEG_C_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_D, bitMask & SEG_D_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_E, bitMask & SEG_E_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_F, bitMask & SEG_F_MASK ? SEGMENT_ON : SEGMENT_OFF );
    gpio_set_level( SEG_G, bitMask & SEG_G_MASK ? SEGMENT_ON : SEGMENT_OFF );
}


static void IRAM_ATTR multiplexTimer_callback(void* arg)
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
#ifdef USE_LEDC_DIMMING
            ledc_set_duty(ledc_ch[LEDC_H10].mode, ledc_ch[LEDC_H10].channel, currentBrightness);
            ledc_update_duty(ledc_ch[LEDC_H10].mode, ledc_ch[LEDC_H10].channel);
#else
            gpio_set_level( DIGIT_H10, DIGIT_ACTIVE );
#endif
        	break;
        case 3:
        	_stopDrivingLeds();
            break;
        case 4:
        	_setSegmentPins( digit_h1_segments );
        	break;
        case 5:
#ifdef USE_LEDC_DIMMING
            ledc_set_duty(ledc_ch[LEDC_H1].mode, ledc_ch[LEDC_H1].channel, currentBrightness);
            ledc_update_duty(ledc_ch[LEDC_H1].mode, ledc_ch[LEDC_H1].channel);
#else
            gpio_set_level( DIGIT_H1, DIGIT_ACTIVE );
#endif
        	break;
        case 6:
        	_stopDrivingLeds();
            break;
        case 7:
        	_setSegmentPins( digit_m10_segments );
        	break;
        case 8:
#ifdef USE_LEDC_DIMMING
            ledc_set_duty(ledc_ch[LEDC_M10].mode, ledc_ch[LEDC_M10].channel, currentBrightness);
            ledc_update_duty(ledc_ch[LEDC_M10].mode, ledc_ch[LEDC_M10].channel);
#else
            gpio_set_level( DIGIT_M10, DIGIT_ACTIVE );
#endif
        	break;
        case 9:
        	_stopDrivingLeds();
            break;
        case 10:
        	_setSegmentPins( digit_m1_segments );
        	break;
        case 11:
#ifdef USE_LEDC_DIMMING
            ledc_set_duty(ledc_ch[LEDC_M1].mode, ledc_ch[LEDC_M1].channel, currentBrightness);
            ledc_update_duty(ledc_ch[LEDC_M1].mode, ledc_ch[LEDC_M1].channel);
#else
            gpio_set_level( DIGIT_M1, DIGIT_ACTIVE );
#endif
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

#ifdef USE_LEDC_DIMMING
#warning LEDC_DIMMING is active
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
    ledc_set_duty(ledc_ch[LEDC_DOTS].mode, ledc_ch[LEDC_DOTS].channel, 255);
    ledc_update_duty(ledc_ch[LEDC_DOTS].mode, ledc_ch[LEDC_DOTS].channel);
#else
	gpio_set_direction( DIGIT_M10, GPIO_MODE_OUTPUT );
    gpio_set_level(     DIGIT_M10, DIGIT_INACTIVE );

    gpio_set_direction( DIGIT_M1, GPIO_MODE_OUTPUT );
    gpio_set_level(     DIGIT_M1, DIGIT_INACTIVE );

    gpio_set_direction( DIGIT_H10, GPIO_MODE_OUTPUT );
    gpio_set_level(     DIGIT_H10, DIGIT_INACTIVE );

    gpio_set_direction( DIGIT_H1, GPIO_MODE_OUTPUT );
    gpio_set_level(     DIGIT_H1, DIGIT_INACTIVE );
#endif
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


/*
 * Timer group0 ISR handler
 *
 * Note:
 * We don't call the timer API here because they are not declared with IRAM_ATTR.
 * If we're okay with the timer irq not being serviced while SPI flash cache is disabled,
 * we can allocate this interrupt without the ESP_INTR_FLAG_IRAM flag and use the normal API.
 */
void IRAM_ATTR timer_group0_isr(void *para)
{
    int timer_idx = (int) para;

    /* Retrieve the interrupt status and the counter value
       from the timer that reported the interrupt */
    timer_intr_t timer_intr = timer_group_intr_get_in_isr(TIMER_GROUP_0);
    uint64_t timer_counter_value = timer_group_get_counter_value_in_isr(TIMER_GROUP_0, timer_idx);


    /* Clear the interrupt
       and update the alarm time for the timer with without reload */
    if (timer_intr & TIMER_INTR_T0) {
        timer_group_intr_clr_in_isr(TIMER_GROUP_0, TIMER_0);
        timer_counter_value += (uint64_t) (TIMER_INTERVAL_SEC * TIMER_SCALE);
        timer_group_set_alarm_value_in_isr(TIMER_GROUP_0, timer_idx, timer_counter_value);
    }

	multiplexTimer_callback( NULL );

    /* After the alarm has been triggered
      we need enable it again, so it is triggered the next time */
    timer_group_enable_alarm_in_isr(TIMER_GROUP_0, timer_idx);
}

static void multiplexTask_workerFunction(void *p)
{
	while(1)
	{
		multiplexTimer_callback( NULL );
		vTaskDelay(pdMS_TO_TICKS(1));
	}
}


void start7SegMultiplex( uint64_t period_us )
{
	_initializeMultiplexPins();
#if 0
    xTaskCreate(multiplexTask_workerFunction, "multiplextask", 8192, NULL, tskIDLE_PRIORITY + 5, NULL);
#else


#if 0
	///////////////////////////////////////////////////////////////////////////
    /* Select and initialize basic parameters of the timer */
    timer_config_t config;
    config.divider = TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = 1;
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, TIMER_INTERVAL_SEC * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_group0_isr, (void *) TIMER_0, ESP_INTR_FLAG_IRAM, NULL);
    timer_start(TIMER_GROUP_0, TIMER_0);
#else

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
#endif
#endif
}
