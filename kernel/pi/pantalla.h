/* ZunzunOS - pantalla tactil de 3,5" para la Raspberry Pi ("3.5inch RPi LCD", HUGA y clones de la Waveshare (A),
   480x320). Si tu pantalla esta cableada de otra forma, cambia aqui los numeros (son GPIO de Broadcom, no patillas).

   Patillas del conector de 26 (las 26 primeras del de 40), segun la tabla de Waveshare:
     11 = GPIO17  TP_IRQ   el tactil baja esta linea mientras se toca
     18 = GPIO24  LCD_RS   orden (0) o dato (1) para el ILI9486
     19 = GPIO10  MOSI     SPI0, compartido
     21 = GPIO9   MISO     SPI0 (solo lo usa el tactil)
     22 = GPIO25  RST      reinicio del ILI9486
     23 = GPIO11  SCLK     SPI0, compartido
     24 = GPIO8   CE0      LCD_CS (pantalla)
     26 = GPIO7   CE1      TP_CS (tactil XPT2046/ADS7846) */
#pragma once
#define LCD_PIN_DC 24
#define LCD_PIN_RST 25
#define TP_PIN_IRQ 17
#define LCD_CS 0          /* SPI0 CE0 */
#define TP_CS 1           /* SPI0 CE1 */
/* El reloj del SPI0 sale del reloj del nucleo de la GPU (250 MHz en la Pi 2) dividido por un numero par.
   La pantalla (A) admite unos 16 MHz; el tactil, 2 MHz como mucho. */
#define LCD_SPI_DIV 16    /* 15,6 MHz */
#define TP_SPI_DIV 128    /* 1,95 MHz */
/* MADCTL del ILI9486: 0x28 = apaisada (480 de ancho) con orden BGR. Si la imagen sale girada 180 grados, 0xE8;
   si sale en espejo, prueba 0x68 o 0xA8. Lo mismo se puede poner sin recompilar en /sys/ZUNZUN/PANTALLA.TXT
   con una linea "madctl=0xE8". */
#define LCD_MADCTL 0x28
#define LCD_W 480
#define LCD_H 320
