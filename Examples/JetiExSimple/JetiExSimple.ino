/* 
  Jeti Sensor EX Telemetry C++ Library
  
  Simple Main program
  --------------------------------------------------------------------
  
  Copyright (C) 2015 Bernd Wokoeck

  *** Extended notice on additional work and copyrights, see header of JetiExProtocol.cpp ***

  Wiring:

    Arduino Mini  TXD-Pin 0 <-- Receiver "Ext." input (orange cable)

  Ressources:
    Uses built in UART of Arduini Mini Pro 328 or one of 3 Teensy UARTs
  
  Version history:
  0.90   11/22/2015  created
  0.95   12/23/2015  new sample sensors for GPS and date/time
  0.96   02/21/2016  comPort number as optional parameter for Teensy in Start(...)
                     sensor device id as optional parameter (SetDeviceId(...))
  1.02   03/28/2017  New sensor memory management. Sensor data can be located in PROGMEM

**************************************************************/

#include "JetiExProtocol.h"

JetiExProtocol jetiEx;

enum
{
  ID_VOLTAGE = 1,
  ID_ALTITUDE,
  ID_TEMP,
  ID_CLIMB,
  ID_FUEL,
  ID_RPM,
  ID_GPSLON,
  ID_GPSLAT,
  ID_DATE,
  ID_TIME,
};

// id from 1..15
JETISENSOR_CONST sensors[] PROGMEM =
{
  // id            name          unit         data type             precision 0->0, 1->0.0, 2->0.00
  { ID_VOLTAGE,    "Voltage",    "V",         JetiSensor::TYPE_14b, 1 },
  { ID_ALTITUDE,   "Altitude",   "m",         JetiSensor::TYPE_14b, 0 },
  { ID_TEMP,       "Temp",       "\xB0\x43",  JetiSensor::TYPE_14b, 0 }, // �C
  { ID_CLIMB,      "Climb",      "m/s",       JetiSensor::TYPE_14b, 2 },
  { ID_FUEL,       "Fuel",       "%",         JetiSensor::TYPE_14b, 0 },
  { ID_RPM,        "RPM x 1000", "/min",      JetiSensor::TYPE_14b, 1 },

  { ID_GPSLON,     "Longitude",  " ",         JetiSensor::TYPE_GPS, 0 },
  { ID_GPSLAT,     "Latitude",   " ",         JetiSensor::TYPE_GPS, 0 },
  { ID_DATE,       "Date",       " ",         JetiSensor::TYPE_DT,  0 },
  { ID_TIME,       "Time",       " ",         JetiSensor::TYPE_DT,  0 },

  0 // end of array
};

void setup()
{
#ifdef CORE_TEENSY 
  Serial.begin( 9600 );
#endif

  jetiEx.Start( "ECU", sensors );

  jetiEx.SetJetiboxText( JetiExProtocol::LINE1, "Start 1" );
  jetiEx.SetJetiboxText( JetiExProtocol::LINE2, "Start 2" );

  /* add your setup code here */
}

void loop()
{
  /* add your main program code here */

  jetiEx.SetSensorValue( ID_VOLTAGE,  74 );  // 7.4 V
  jetiEx.SetSensorValue( ID_ALTITUDE, 120 ); // 120 m
  jetiEx.SetSensorValue( ID_TEMP,     31 );  // 31 degrees C
  jetiEx.SetSensorValue( ID_CLIMB,    123 ); // 1.23 m/s
  jetiEx.SetSensorValue( ID_FUEL,     75 );  // 75%
  jetiEx.SetSensorValue( ID_RPM,      901 ); // 90.1 * 1000/min

  jetiEx.SetSensorValueGPS( ID_GPSLON, true,  11.55616f ); // E 11° 33' 22.176"
  jetiEx.SetSensorValueGPS( ID_GPSLAT, false, 48.24570f ); // N 48° 14' 44.520"
  jetiEx.SetSensorValueDate( ID_DATE,  29, 12, 2015 );
  jetiEx.SetSensorValueTime( ID_TIME,  19, 16, 37 );

  jetiEx.DoJetiSend(); 
}
