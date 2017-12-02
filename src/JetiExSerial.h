/* 
  Jeti Sensor EX Telemetry C++ Library
  
  JetiExSerial - EX serial output implementation
  --------------------------------------------------------------------
  
  Copyright (C) 2017 Bernd Wokoeck
  
  Version history:
  0.90   11/22/2015  created
  0.94   12/22/2015  Teensy 3.x support on Serial2  
  0.95   12/23/2015  Refactoring
  0.96   02/21/2016  comPort number as parameter for Teensy
  1.00   01/29/2017  Some refactoring:
                     - Optimized half duplex control for AVR CPUs in JetiExHardwareSerialInt class (for improved Jetibox key handling)
                     - Reduced size of serial transmit buffer (128-->64 words) 
                     - Changed bitrates for serial communication for AVR CPUs (9600-->9800 bps)
                     - JETI_DEBUG and BLOCKING_MODE removed (cleanup)
  1.0.1  02/15/2017  Support for ATMega32u4 CPU in Leonardo/Pro Micro

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.

**************************************************************/

#ifndef JETIEXSERIAL_H
#define JETIEXSERIAL_H

#if ARDUINO >= 100
 #include <Arduino.h>
#else
 #include <WProgram.h>
#endif

class JetiExSerial
{
public:
  static JetiExSerial * CreatePort( int comPort ); // comPort: 0=default, Teensy: 1..3

  virtual void    Init() = 0;
  virtual void    Send( uint8_t data, boolean bit8 ) = 0;
  virtual uint8_t Getchar(void) = 0;

  virtual void TxOn() = 0;
  virtual void TxOff() = 0;
};

// Teensy
/////////
#ifdef CORE_TEENSY 

  class JetiExTeensySerial : public JetiExSerial
  {
  public:
    JetiExTeensySerial( int comPort );
    virtual void Init();
    virtual void Send( uint8_t data, boolean bit8 );
    virtual uint8_t Getchar(void);
    virtual void TxOn() {}
    virtual void TxOff() {}
  protected:
    bool m_bNextIsKey;
    HardwareSerial * m_pSerial;
  };

#else

  #if defined (__AVR_ATmega32U4__)
    #define _BV(bit) (1 << (bit))
    #define UCSRA UCSR1A
    #define UCSRB UCSR1B
    #define UCSRC UCSR1C

    #define UCSZ2 UCSZ12
    #define RXEN RXEN1
    #define TXEN TXEN1
    #define UCSZ0 UCSZ10
    #define UCSZ1 UCSZ11
    #define UCSZ2 UCSZ12

    #define UPM0 UPM10
    #define UPM1 UPM11

    #define UBRRH UBRR1H
    #define UBRRL UBRR1L

    #define RXCIE RXCIE1
    #define UDRIE UDRIE1
    #define TXCIE TXCIE1

    #define TXB8 TXB81

    #define UDR UDR1
    #define UDRIE UDRIE1

    #define USART_RX_vect USART1_RX_vect
    #define USART_TX_vect USART1_TX_vect
    #define USART_UDRE_vect USART1_UDRE_vect
  #else
    #define UCSRA UCSR0A
    #define UCSRB UCSR0B
    #define UCSRC UCSR0C

    #define RXEN RXEN0
    #define TXEN TXEN0
    #define UCSZ0 UCSZ00
    #define UCSZ1 UCSZ01
    #define UCSZ2 UCSZ02

    #define UPM0 UPM00
    #define UPM1 UPM01

    #define UBRRH UBRR0H
    #define UBRRL UBRR0L

    #define RXCIE RXCIE0
    #define UDRIE UDRIE0
    #define TXCIE TXCIE0

    #define TXB8 TXB80
    #define UDR UDR0
    #define UDRIE UDRIE0
  #endif 

  // ATMega
  //////////
  class JetiExAtMegaSerial : public JetiExSerial
  {
  public:
    virtual void Init();
  };

  // interrupt driven transmission
  // ->  low CPU usage (~1ms per frame), slightly higher latency
  ////////////////////////////////

  extern "C" void USART_UDRE_vect(void) __attribute__ ((signal)); // make C++ class accessible for ISR
  extern "C" void USART_RX_vect(void) __attribute__ ((signal)); // make C++ class accessible for ISR
  extern "C" void USART_TX_vect(void) __attribute__ ((signal)); // make C++ class accessible for ISR

  class JetiExHardwareSerialInt : public JetiExAtMegaSerial
  {
    friend void USART_UDRE_vect(void);
    friend void USART_RX_vect(void);
    friend void USART_TX_vect(void);

  public:
    virtual void Init();
    virtual void Send( uint8_t data, boolean bit8 );
    virtual uint8_t Getchar(void);

    virtual void TxOn() {}
    virtual void TxOff() {}

  protected:
    enum
    {
      TX_RINGBUF_SIZE = 64, // 34 bytes text buffer plus 30 bytes ex buffer
      RX_RINGBUF_SIZE = 4,
    };

    // tx buffer
    volatile uint16_t   m_txBuf[ TX_RINGBUF_SIZE ]; // use uint16_t to store bit 9 in a convenient way
    volatile uint16_t * m_txHeadPtr;
    volatile uint16_t * m_txTailPtr;
    volatile uint8_t    m_txNumChar;
    volatile uint16_t * IncBufPtr( volatile uint16_t * ptr, volatile uint16_t * ringBuf, size_t bufSize );

    // rx buffer
    volatile uint8_t   m_rxBuf[ RX_RINGBUF_SIZE ]; 
    volatile uint8_t * m_rxHeadPtr;
    volatile uint8_t * m_rxTailPtr;
    volatile uint8_t   m_rxNumChar;
    volatile uint8_t * IncBufPtr8( volatile uint8_t * ptr, volatile uint8_t * ringBuf, size_t bufSize );

    // receiver state
    volatile bool       m_bSending;
  };
  
#endif // CORE_TEENSY

#endif // JETIEXSERIAL_H