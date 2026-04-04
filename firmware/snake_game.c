#include <18f4550.h>
#Fuses HSPLL, NOWDT, NOPROTECT, MCLR, NOLVP, NODEBUG, USBDIV, PLL2, CPUDIV1, VREGEN
#device ADC=10
#use delay(clock=48000000)
#include "MLCD.c"

// Pines de bit-bang SPI
#define MAX_DIN   PIN_B0
#define MAX_CLK   PIN_B1
#define MAX_CS    PIN_B2

// Registros MAX7219
#define MAX7219_REG_NOOP        0x00
#define MAX7219_REG_DIGIT0      0x01
#define MAX7219_REG_DECODEMODE  0x09
#define MAX7219_REG_INTENSITY   0x0A
#define MAX7219_REG_SCANLIMIT   0x0B
#define MAX7219_REG_SHUTDOWN    0x0C
#define MAX7219_REG_DISPLAYTEST 0x0F

#define NUM_DEVICES 4   // 4 módulos ? 8×32
#define T_SPI_US    1   // retardo SPI bit-bang

// Deadzone joystick (500±100)
#define MID      500
#define MARGIN   100
#define TH_LOW   (MID - MARGIN)
#define TH_HIGH  (MID + MARGIN)

// Área interior (dentro del borde)
#define MIN_ROW   1
#define MAX_ROW   6
#define MIN_COL   1
#define MAX_COL   (NUM_DEVICES*8 - 2)  // = 30

// Dirección del movimiento del snake
#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

// Tamaño máximo del cuerpo del snake
#define MAX_LENGTH 32

// Posición inicial del snake
unsigned char snakeX[MAX_LENGTH], snakeY[MAX_LENGTH];
unsigned char snakeLength = 3; // longitud inicial
unsigned char direction = DIR_RIGHT; // dirección inicial
unsigned char foodX, foodY; // posición de la comida

// Buffer de display
unsigned char displayBuffer[NUM_DEVICES][8];

// Semilla de números aleatorios
unsigned int randomSeed = 0;

// Función de semilla (srand)
void srand(unsigned int seed) {
   randomSeed = seed;
}

// Función para obtener un número aleatorio
unsigned int rand() {
   randomSeed = (randomSeed * 1103515245 + 12345) & 0x7FFFFFFF; // Algoritmo de generación de números aleatorios
   return randomSeed;
}

// SPI bit-bang MSB-first
void spi_send_byte(unsigned char b) {
   for (int i = 0; i < 8; i++) {
      output_bit(MAX_DIN, (b & 0x80));
      delay_us(T_SPI_US);
      output_high(MAX_CLK);
      delay_us(T_SPI_US);
      output_low(MAX_CLK);
      b <<= 1;
   }
}

// Envío a MAX7219: solo el dispositivo `device` recibe addr/data, los demás NOOP
void max7219_write(unsigned char addr, unsigned char data, unsigned char device) {
   output_low(MAX_CS);
   for (int i = 0; i < NUM_DEVICES; i++) {
      if (i == (NUM_DEVICES - 1 - device)) {
         spi_send_byte(addr);
         spi_send_byte(data);
      } else {
         spi_send_byte(MAX7219_REG_NOOP);
         spi_send_byte(0x00);
      }
   }
   output_high(MAX_CS);
}

// Función para inicializar los módulos MAX7219
void initialize_max7219() {
   // Inicializa todos los módulos MAX7219
   for (unsigned char i = 0; i < NUM_DEVICES; i++) {
      max7219_write(MAX7219_REG_SHUTDOWN, 0x01, i);  // Modo de operación (despierta el dispositivo)
      max7219_write(MAX7219_REG_DISPLAYTEST, 0x00, i); // Desactiva la prueba de pantalla
      max7219_write(MAX7219_REG_SCANLIMIT, 0x07, i);  // Escanear los 8 dígitos (8 filas)
      max7219_write(MAX7219_REG_DECODEMODE, 0x00, i); // No decodificación BCD, solo control de LEDs
      max7219_write(MAX7219_REG_INTENSITY, 0x01, i);  // Establece la intensidad de la luz (8 es el valor medio)
   }
}

// Funciones de manejo de buffer
void clearBuffer() {
   for (unsigned char dev = 0; dev < NUM_DEVICES; dev++) {
      for (unsigned char row = 0; row < 8; row++) {
         displayBuffer[dev][row] = 0x00;
      }
   }
}

void bufferSetDot(unsigned char row, unsigned char col, int on) {
   unsigned char dev     = col / 8;
   unsigned char col_dev = col % 8;
   unsigned char bitMask = 1 << col_dev;

   if (on)
      displayBuffer[dev][7 - row] |= bitMask;    // fila 7 arriba, fila 0 abajo
   else
      displayBuffer[dev][7 - row] &= ~bitMask;
}

void refreshDisplay() {
   for (unsigned char dev = 0; dev < NUM_DEVICES; dev++) {
      for (unsigned char row = 0; row < 8; row++) {
         unsigned char addr = MAX7219_REG_DIGIT0 + row;
         max7219_write(addr, displayBuffer[dev][row], dev);
      }
   }
}

// Dibuja el borde
void drawBorder() {
   unsigned char i;
   // filas 0 y 7
   for (i = 0; i < NUM_DEVICES*8; i++) {
      bufferSetDot(0,  i, 1);
      bufferSetDot(7,  i, 1);
   }
   // columnas 0 y 31
   for (i = 0; i < 8; i++) {
      bufferSetDot(i, 0, 1);
      bufferSetDot(i, NUM_DEVICES*8 - 1, 1);
   }
}

// Inicializa el snake
void initSnake() {
   snakeLength = 3;
   direction = DIR_RIGHT;
   snakeX[0] = 16; snakeY[0] = 3; // cabeza
   snakeX[1] = 15; snakeY[1] = 3; // cuerpo
   snakeX[2] = 14; snakeY[2] = 3; // cuerpo
}

// Genera comida aleatoria
void generateFood() {
   foodX = MIN_COL + (rand() % (MAX_COL - MIN_COL)); // Usa rand() para generar la posición aleatoria
   foodY = MIN_ROW + (rand() % (MAX_ROW - MIN_ROW));
}

// Dibuja el snake
void drawSnake() {
   for (unsigned char i = 0; i < snakeLength; i++) {
      bufferSetDot(snakeY[i], snakeX[i], 1); // Dibuja cada parte del snake
   }
}

// Mueve el snake
void moveSnake() {
   unsigned char newHeadX = snakeX[0];
   unsigned char newHeadY = snakeY[0];

   // Mueve la cabeza según la dirección
   switch (direction) {
      case DIR_UP:    newHeadY--; break;
      case DIR_DOWN:  newHeadY++; break;
      case DIR_LEFT:  newHeadX--; break;
      case DIR_RIGHT: newHeadX++; break;
   }

   // Mueve el cuerpo
   for (unsigned char i = snakeLength; i > 0; i--) {
      snakeX[i] = snakeX[i-1];
      snakeY[i] = snakeY[i-1];
   }

   // Coloca la nueva cabeza
   snakeX[0] = newHeadX;
   snakeY[0] = newHeadY;
}

// Checa si el snake colisionó
int checkCollision() {
   // Colisión con el borde
   if (snakeX[0] == 0 || snakeX[0] == (NUM_DEVICES*8 - 1) || snakeY[0] == 0 || snakeY[0] == 7) {
      return 1; // Colisión
   }
   // Colisión con el cuerpo
   for (unsigned char i = 1; i < snakeLength; i++) {
      if (snakeX[i] == snakeX[0] && snakeY[i] == snakeY[0]) {
         return 1; // Colisión
      }
   }
   return 0; // Sin colisión
}

// Checa si el snake comió la comida
int checkFood() {
   if (snakeX[0] == foodX && snakeY[0] == foodY) {
      return 1; // Comió la comida
   }
   return 0; // No comió
}

void main() {
   int16 coordX, coordY;
   int1  isPressed;

   // Configura joystick y botón
   set_tris_a(0xFF);   // RA0/RA1 ADC
   set_tris_b(0xFF);   // RB3 botón
   setup_adc_ports(AN0_TO_AN1);
   setup_adc(ADC_CLOCK_INTERNAL);

   lcd_init();
   lcd_gotoxy(2,1);
   printf(lcd_putc, "Snake Game");

   // Configura SPI + display
   set_tris_b(0x00);
   output_low(MAX_CLK);
   output_low(MAX_DIN);
   output_high(MAX_CS);

   initialize_max7219();
   clearBuffer();
   drawBorder();
   srand(read_adc()); // Usamos el valor del ADC como semilla para rand
   initSnake();
   generateFood();
   refreshDisplay();

   while (TRUE) {
   // Lee joystick
   set_adc_channel(0); delay_us(20); coordX = read_adc();
   set_adc_channel(1); delay_us(20); coordY = read_adc();
   isPressed = !input(PIN_B3);

   // Calcula la nueva dirección
   if (coordY < TH_LOW  && direction != DIR_DOWN)  direction = DIR_UP;
   if (coordY > TH_HIGH && direction != DIR_UP)    direction = DIR_DOWN;
   if (coordX < TH_LOW  && direction != DIR_RIGHT) direction = DIR_RIGHT;
   if (coordX > TH_HIGH && direction != DIR_LEFT)  direction = DIR_LEFT;

   // Mueve el snake
   moveSnake();

   // Borra el buffer, dibuja el snake y la comida
   clearBuffer();
   drawBorder();
   drawSnake();
   bufferSetDot(foodY, foodX, 1); // Dibuja la comida
   refreshDisplay();

   // Checa si hay colisión
   if (checkCollision()) {
      delay_ms(500); // pequeño retraso para que se vea el choque
      break; // Termina el juego
   }

   // Checa si comió la comida
   if (checkFood()) {
      snakeLength++;
      generateFood(); // Genera nueva comida
   }

   delay_ms(300);
}
   lcd_gotoxy(1, 2);
   printf(lcd_putc, "Game Over!");
}
