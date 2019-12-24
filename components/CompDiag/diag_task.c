/*##*************************************************************************************************************************************************************
 *      Includes
 **************************************************************************************************************************************************************/
#include <driver/gpio.h>

#include "diag_task.h"

#include "hardware.h"

#include "tft.h"
#include "tftspi.h"

/*##*************************************************************************************************************************************************************
 *      Intern type declarations
 **************************************************************************************************************************************************************/
static const char *TAG = "diagTask";


/*##*************************************************************************************************************************************************************
 *      Intern function declarations
 **************************************************************************************************************************************************************/

/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
static void _doSelfTest( void );

/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
static void _init( void );

/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
static void _createTask( void );

/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
static void _createQueue( void );

/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
static void _diag_task();

/*##*************************************************************************************************************************************************************
 *      Function implementation
 **************************************************************************************************************************************************************/

/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
extern void DiagTask_init( void )
{
	_doSelfTest();

	_init();

    _createQueue();     // consider order! first create queue
    _createTask();

	ESP_LOGI(TAG, "DiagTask_init done");
}


/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
extern void DiagTask_connect( void )
{
    ESP_LOGI(TAG, "DiagTask_connect done");
}


/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
static void _doSelfTest( void )
{
}


/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
static void _init( void )
{
	// ==========================================================
	// Define which spi bus to use TFT_VSPI_HOST or TFT_HSPI_HOST
	#define SPI_BUS TFT_HSPI_HOST
	// ==========================================================

	// ========  PREPARE DISPLAY INITIALIZATION  =========
    esp_err_t ret;

    // ====================================================================
    // === Pins MUST be initialized before SPI interface initialization ===
    // ====================================================================
    TFT_PinsInit();

    // ====  CONFIGURE SPI DEVICES(s)  ====================================================================================

    spi_lobo_device_handle_t spi;

    spi_lobo_bus_config_t buscfg={
        .miso_io_num=PIN_NUM_MISO,				// set SPI MISO pin
        .mosi_io_num=PIN_NUM_MOSI,				// set SPI MOSI pin
        .sclk_io_num=PIN_NUM_CLK,				// set SPI CLK pin
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
		.max_transfer_sz = 6*1024,
    };
    spi_lobo_device_interface_config_t devcfg={
        .clock_speed_hz=8000000,                // Initial clock out at 8 MHz
        .mode=0,                                // SPI mode 0
        .spics_io_num=-1,                       // we will use external CS pin
		.spics_ext_io_num=PIN_NUM_CS,           // external CS pin
		.flags=LB_SPI_DEVICE_HALFDUPLEX,        // ALWAYS SET  to HALF DUPLEX MODE!! for display spi
    };

    vTaskDelay(500 / portTICK_RATE_MS);
	printf("\r\n==============================\r\n");
    printf("TFT display, LoBo 11/2017\r\n");
	printf("==============================\r\n");
    printf("Pins used: miso=%d, mosi=%d, sck=%d, cs=%d\r\n", PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
	printf("==============================\r\n\r\n");

	// ==================================================================
	// ==== Initialize the SPI bus and attach the LCD to the SPI bus ====

	ret=spi_lobo_bus_add_device(SPI_BUS, &buscfg, &devcfg, &spi);
    assert(ret==ESP_OK);
	printf("SPI: display device added to spi bus (%d)\r\n", SPI_BUS);
	disp_spi = spi;

	// ==== Test select/deselect ====
	ret = spi_lobo_device_select(spi, 1);
    assert(ret==ESP_OK);
	ret = spi_lobo_device_deselect(spi);
    assert(ret==ESP_OK);

	printf("SPI: attached display device, speed=%u\r\n", spi_lobo_get_speed(spi));
	printf("SPI: bus uses native pins: %s\r\n", spi_lobo_uses_native_pins(spi) ? "true" : "false");

	// ================================
	// ==== Initialize the Display ====

	printf("SPI: display init...\r\n");
	TFT_display_init();
    printf("OK\r\n");

	// ---- Detect maximum read speed ----
	max_rdclock = find_rd_speed();
	printf("SPI: Max rd speed = %u\r\n", max_rdclock);

    // ==== Set SPI clock used for display operations ====
	spi_lobo_set_speed(spi, DEFAULT_SPI_CLOCK);
	printf("SPI: Changed speed to %u\r\n", spi_lobo_get_speed(spi));

	printf("DisplayController: ");
    switch (tft_disp_type) {
        case DISP_TYPE_ILI9341:
            printf("ILI9341");
            break;
        case DISP_TYPE_ILI9488:
            printf("ILI9488");
            break;
        case DISP_TYPE_ST7789V:
            printf("ST7789V");
            break;
        case DISP_TYPE_ST7735:
            printf("ST7735");
            break;
        case DISP_TYPE_ST7735R:
            printf("ST7735R");
            break;
        case DISP_TYPE_ST7735B:
            printf("ST7735B");
            break;
        default:
            printf("Unknown");
    }
	printf("\r\n");

	font_rotate = 0;
	text_wrap = 0;
	font_transparent = 0;
	font_forceFixed = 0;
	gray_scale = 0;
    TFT_setGammaCurve(DEFAULT_GAMMA_CURVE);
	TFT_setRotation(LANDSCAPE);
	TFT_setFont(DEJAVU18_FONT, NULL);
	TFT_resetclipwin();
	TFT_invertDisplay( 1 );
	TFT_fillScreen(TFT_BLACK);

#ifdef PIN_BTN_A
    gpio_pad_select_gpio(PIN_BTN_A);
    gpio_set_direction(PIN_BTN_A, GPIO_MODE_INPUT);
#endif
#ifdef PIN_BTN_B
	gpio_pad_select_gpio(PIN_BTN_B);
	gpio_set_direction(PIN_BTN_B, GPIO_MODE_INPUT);
#endif
#ifdef PIN_BTN_C
	gpio_pad_select_gpio(PIN_BTN_C);
	gpio_set_direction(PIN_BTN_C, GPIO_MODE_INPUT);
#endif
}


/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
static void _createTask( void )
{
    xTaskCreate( _diag_task,
    		     "diag_task",
				 4096, // stack size
				 NULL,
				 (configMAX_PRIORITIES / 2) - 1, // below normal
				 NULL);
}


/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
static void _createQueue( void )
{
}


/*!************************************************************************************************************************************************************
 *
 *************************************************************************************************************************************************************/
#define XTITLE 10
#define X1 5
#define X2 120
#define X3 240
#define TITLE1 "MemFree"

void tftShowTime( char *szTime)
{
    TFT_Y = DEFAULT_TFT_DISPLAY_HEIGHT / 2;
	TFT_setFont(DEJAVU24_FONT, NULL);
    _fg = TFT_YELLOW;
    TFT_fillRect( 120,
    		      TFT_Y,
				  TFT_getStringWidth("00:00:00"),
				  TFT_getfontheight(),
				  _bg );
    TFT_print(szTime, 100, TFT_Y);
}


static void _diag_task()
{
    char szTmp[50];
    uint32_t n1, n2;


    uint32_t FreeSpaceBaseline = 0UL;

    for(;;) {
    	TFT_setFont( SMALL_FONT, NULL );
    	_fg = TFT_YELLOW;
    	TFT_print("DeltaClr", 38, BOTTOM );
    	if ( gpio_get_level( PIN_BTN_A ) == 0 )
    	{
            FreeSpaceBaseline= heap_caps_get_free_size(MALLOC_CAP_8BIT);
    	}

    	_fg = TFT_WHITE;
    	TFT_print("ESP32clock Info", 0, 0 );


        TickType_t now = (xTaskGetTickCount() / pdMS_TO_TICKS( 1000 ) );
        snprintf(szTmp, sizeof(szTmp), "%ld:%02ld:%02ld\n", now / 3600UL, (now % 3600UL) / 60UL, now % 60UL);
        _fg = TFT_WHITE;
        TFT_print(szTmp, RIGHT, 0);

        //////////////////////////////////////////////////////////////////////
        TFT_Y += 5;
    	TFT_setFont( UBUNTU16_FONT, NULL );

        TFT_drawRect( 0,
        		      TFT_Y + TFT_getfontheight() / 2,
					  DEFAULT_TFT_DISPLAY_WIDTH-1,
					  3 * TFT_getfontheight(),
					  TFT_WHITE);
        _fg = TFT_WHITE;
        TFT_fillRect( XTITLE,
        		      TFT_Y,
					  TFT_getStringWidth(TITLE1),
					  TFT_getfontheight(),
					  _bg );
        TFT_print(TITLE1, XTITLE, TFT_Y); TFT_print("\n", TFT_X, TFT_Y);
        _fg = TFT_WHITE;
        TFT_print("free", X1, TFT_Y);
        TFT_print("largest", X2, TFT_Y);
        TFT_print("delta\n", X3, TFT_Y);

        n1 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        n2 = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

        snprintf(szTmp, sizeof(szTmp), "%d", n1);
        _fg = TFT_GREEN;
        TFT_fillRect( X1,
        		      TFT_Y,
					  TFT_getStringWidth("0000000"),
					  TFT_getfontheight(),
					  _bg );
        TFT_print(szTmp, X1, TFT_Y);

        snprintf(szTmp, sizeof(szTmp), "%d", n2);
        _fg = TFT_GREEN;
        TFT_fillRect( X2,
        		      TFT_Y,
					  TFT_getStringWidth("0000000"),
					  TFT_getfontheight(),
					  _bg );
        TFT_print(szTmp, X2, TFT_Y);

        snprintf(szTmp, sizeof(szTmp), "%d", FreeSpaceBaseline - n1);
        _fg = TFT_YELLOW;
        TFT_fillRect( X3,
        		      TFT_Y,
					  DEFAULT_TFT_DISPLAY_WIDTH - X3 - 2,
					  TFT_getfontheight(),
					  _bg );
        TFT_print(szTmp, X3, TFT_Y);

        //////////////////////////////////////////////////////////////////////
        TFT_Y += 25;

        tcpip_adapter_ip_info_t ipInfo;

        // IP address.
        tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
        snprintf(szTmp, sizeof(szTmp), "%d.%d.%d.%d",
        		 (ipInfo.ip.addr >>  0) & 0x0FF,
        		 (ipInfo.ip.addr >>  8) & 0x0FF,
        		 (ipInfo.ip.addr >> 16) & 0x0FF,
        		 (ipInfo.ip.addr >> 24) & 0x0FF );
        _fg = TFT_YELLOW;
        TFT_fillRect( X3,
        		      TFT_Y,
					  DEFAULT_TFT_DISPLAY_WIDTH - X2 - 2,
					  TFT_getfontheight(),
					  _bg );
        TFT_print(szTmp, X2, TFT_Y);

        //////////////////////////////////////////////////////////////////////

        vTaskDelay( pdMS_TO_TICKS( 1000 ) ); // wait a second
    }
}

