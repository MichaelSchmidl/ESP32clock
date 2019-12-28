#ifndef __HARDWARE_H__
#define __HARDWARE_H__

#include <driver/gpio.h>

#define MULTIPLEX_RATE_US                (873)

#ifdef USE_M5_TFT
	#define M5_LCD_BL                    GPIO_NUM_32
	#define M5_LCD_RST                   GPIO_NUM_33
	#define M5_LCD_RS                    GPIO_NUM_27
	#define M5_LCD_MOSI                  GPIO_NUM_23
	#define M5_LCD_SCK                   GPIO_NUM_18
	#define M5_LCD_CS                    GPIO_NUM_14
	#define M5_AUDIO                     GPIO_NUM_25

	#define DBG_PIN                      GPIO_NUM_0

	#define PIN_BTN_A                    GPIO_NUM_39
	#define PIN_BTN_B                    GPIO_NUM_38
	#define PIN_BTN_C                    GPIO_NUM_37

	#define SEG_A                        GPIO_NUM_3
	#define SEG_B                        GPIO_NUM_1
	#define SEG_C                        GPIO_NUM_16
	#define SEG_D                        GPIO_NUM_17
	#define SEG_E                        GPIO_NUM_2
	#define SEG_F                        GPIO_NUM_5
	#define SEG_G                        GPIO_NUM_26

	#define DIGIT_M10                    GPIO_NUM_19
	#define DIGIT_M1                     GPIO_NUM_21
	#define DIGIT_H10                    GPIO_NUM_22
	#define DIGIT_H1                     GPIO_NUM_0
#else
	#define DBG_PIN                      GPIO_NUM_2

	#define SEG_A                        GPIO_NUM_4
	#define SEG_B                        GPIO_NUM_16
	#define SEG_C                        GPIO_NUM_25
	#define SEG_D                        GPIO_NUM_33
	#define SEG_E                        GPIO_NUM_32
	#define SEG_F                        GPIO_NUM_17
	#define SEG_G                        GPIO_NUM_26

	#define DIGIT_M10                    GPIO_NUM_22
	#define DIGIT_M1                     GPIO_NUM_23
	#define DIGIT_H10                    GPIO_NUM_19
	#define DIGIT_H1                     GPIO_NUM_21
#endif

#define DIGIT_INACTIVE               0
#define DIGIT_ACTIVE                 1

#define SEGMENT_ON                   1
#define SEGMENT_OFF                  0


#endif /*__HARDWARE_H__*/
