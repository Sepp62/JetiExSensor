/* 
  Jeti Sensor EX Telemetry C++ Library
  
  Jetibox Only - No EX sensors
  --------------------------------------------------------------------
  
  Copyright (C) 2015 Bernd Wokoeck

  *** Extended notice on additional work and copyrights, see header of JetiExProtocol.cpp ***

  Wiring:

    Arduino Mini  TXD-Pin 0 <-- Receiver "Ext." input (orange cable)

  Ressources:
    Uses built in UART of Arduini Mini Pro 328 or one of 3 Teensy UARTs
  
  Version history:
  0.90   02/26/2016  created

**************************************************************/

#include "JetiExProtocol.h"

JetiExProtocol jetiEx;

static char line1[ 16 ];
static char line2[ 16 ];

void setup()
{
#ifdef CORE_TEENSY 
  Serial.begin( 9600 );
#endif

  // jetiEx.Start();
  jetiEx.Start( NULL, NULL, JetiExProtocol::SERIAL2 );

  jetiEx.SetJetiboxText( JetiExProtocol::LINE1, "Start 1" );
  jetiEx.SetJetiboxText( JetiExProtocol::LINE2, "Start 2" );

  /* add your setup code here */
}

void loop()
{
  /* add your main program code here */

  int value1 = 99, value2 = 500;

  sprintf( line1, "value 1: %d", value1 );
  jetiEx.SetJetiboxText( JetiExProtocol::LINE1, line1 );

  sprintf( line2, "value 2: %d", value2 );
  jetiEx.SetJetiboxText( JetiExProtocol::LINE2, line2 );

  jetiEx.DoJetiSend(); 
}
