Changes in Teensy libraries
===========================

Activate 9 Bit UART support
---------------------------
  ...\Arduino\hardware\teensy\avr\cores\teensy3\HardwareSerial.h

// uncomment to enable 9 bit formats

#define SERIAL_9BIT_SUPPORT


Increase TX buffer to avoid "busy waiting" while sending JETI data blocks
--------------------------------------------------------------------------
  ...\Arduino\hardware\teensy\avr\cores\teensy3\serial2.c
#define SERIAL2_TX_BUFFER_SIZE     128 // number of outgoing bytes to buffer


Changes in Teensy HardwareSerial library in order to receive Jeti keys
You dont need it, as long as you do not interconnect RX2 and TX2
----------------------------------------------------------------------
// comment 
in: ...\Arduino\hardware\teensy\avr\cores\teensy3\serial2.c
/* 

#ifdef HAS_KINETISK_UART1_FIFO

  #define C2_ENABLE      UART_C2_TE | UART_C2_RE | UART_C2_RIE | UART_C2_ILIE
#else

  #define C2_ENABLE      UART_C2_TE | UART_C2_RE | UART_C2_RIE

#endif

#define C2_TX_ACTIVE     C2_ENABLE | UART_C2_TIE

#define C2_TX_COMPLETING C2_ENABLE | UART_C2_TCIE

#define C2_TX_INACTIVE   C2_ENABLE

*/

// new code:


#ifdef HAS_KINETISK_UART1_FIFO

  #define C2_RXENABLE    UART_C2_RE | UART_C2_RIE | UART_C2_ILIE

#else

  #define C2_RXENABLE    UART_C2_RE | UART_C2_RIE

#endif

#define C2_TX_ACTIVE     UART_C2_TE | UART_C2_TIE

#define C2_TX_COMPLETING UART_C2_TE | UART_C2_TCIE

#define C2_TX_INACTIVE   C2_RXENABLE

