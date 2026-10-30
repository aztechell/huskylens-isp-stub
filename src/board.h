#ifndef ISP_BOARD_H
#define ISP_BOARD_H

/* The uploader selects a binary; the stub never guesses the attached board. */
#ifndef ISP_BOARD_CUBE
#define ISP_BOARD_CUBE 0
#endif

#define LCD_HEIGHT 240U
#define GPIO_LCD_RESET 14U
#define GPIO_LCD_DC 15U
#define GPIO_LCD_BACKLIGHT 13U

#if ISP_BOARD_CUBE
#define LCD_WIDTH 240U
#define LCD_CLOCK_HZ 15000000U
#define LCD_PIN_DC 38U
#define LCD_PIN_CS 36U
#define LCD_PIN_CLOCK 39U
#define LCD_PIN_RESET 37U
#define LCD_PIN_BACKLIGHT 17U
#define LCD_BACKLIGHT_ON 0U
#define ISP_UPDATE_TITLE "MAIX CUBE UPDATE"
#else
#define LCD_WIDTH 320U
#define LCD_CLOCK_HZ 40000000U
#define LCD_PIN_DC 18U
#define LCD_PIN_CS 19U
#define LCD_PIN_CLOCK 20U
#define LCD_PIN_MOSI 21U
#define LCD_PIN_RESET 22U
#define LCD_PIN_BACKLIGHT 24U
#define LCD_BACKLIGHT_ON 1U
#define ISP_UPDATE_TITLE "HUSKYLENS UPDATE"
#endif

#endif
