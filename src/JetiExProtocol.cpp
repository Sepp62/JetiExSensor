/* 
  Jeti Sensor EX Telemetry C++ Library
  
  JetiExProtocol - EX protocol implementation
  --------------------------------------------------------------------
  
  Copyright (C) 2015, 2016 Bernd Wokoeck

  - Thanks to "Jeti telemetry" pioneers who published their code:
        Uwe Gartmann, Alexander Buschek, Denis Artru, Rainer Schlosshan, Henning Stoecklein
  
  Version history:
  0.90   11/22/2015  created
  0.92   12/14/2015  15 sensor values now, data type 22b added
  0.93   12/14/2015  bug with buffer length calc for type 22b removed
  0.95   12/23/2015  Refactoring, data types GPS, date/time, 6b, 30b added
  0.96   02/21/2016  comPort number as parameter for Teensy
                     sensor device id as optional parameter (SetDeviceId())
  0.97   02/26/2016  runs w/o EX sensors (Jetibox only)
  0.98   04/09/2016  slightly improved sensor packet size calculation (avoids "flickering" values)
  0.99   06/03/2016  support for more than 15 sensors, set MAX_SENSORS in JetiExPotocol.h
                     bug in encoding of type_6b removed (thanks to AlexM_1977)
  1.00   01/29/2017  Some refactoring:
                     - Bugixes for Jetibox keys and morse alarms (Thanks to Ingmar !)
                     - Optimized half duplex control for AVR CPUs in JetiExHardwareSerialInt class (for improved Jetibox key handling)
                     - Reduced size of serial transmit buffer (128-->64 words) 
                     - Changed bitrates for serial communication for AVR CPUs (9600-->9800 bps)
                     - EX encryption removed, as a consequence: new manufacturer ID: 0xA409
                       *** Telemetry setup in transmitter must be reconfigured (telemetry reset) ***
                     - Delay at startup increased (to give receiver more init time)
                     - New HandleMenu() function in JetiexSensor.ini (including more alarm test)
                     - JETI_DEBUG and BLOCKING_MODE removed (cleanup)
  1.02   03/28/2017  New sensor memory management. Sensor data can be located in PROGMEM
  1.03   07/14/2017  Send dictionary already in serial initialization for the 1st time
                     in order to improve behaviour on telemetry reset
  1.04   07/18/2017  dynamic sensor de-/activation
  1.05   11/12/2017  send 3 textframes before start of EX transmission to get transmitter ready
  1.06   02/14/2021  Rainer Stransky: added implementation for priorized sensor send capabilities (SetSensorValue(id, value, prio))
                     to avoid deffered transmission of high prio data (vario). 
  1.07   02/18/2021  Rainer Stransky: fixed priorized sensor implementation and added interface to speed up frame send cycle 
                     SetJetiSendCycle(aTime); 
  1.08   02/28/2021  - bug fix: ex buffer overrun for sensor ids >15 fixed (forced strange behaviour in prio handling)
                     - some more index size checks
                     - simplified code for prio data handling
                     - handling of -1 as invalid data removed, due to dynamic prio (SetSensorValue(..., prio) and SetSensorActive() 
                       interface

  Todo:
  - better check for ex buffer overruns

  Hints:
  - http://j-log.eu/forum/viewtopic.php?p=8501#p8501
    
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

#include "JetiExProtocol.h"

// JetiSensor work data
///////////////////////
JetiSensor::JetiSensor( int arrIdx, JetiExProtocol * pProtocol )
  : m_id( 0 ), m_value( -1 ), m_bActive( true ), m_textLen( 0 ), m_unitLen( 0 ), m_dataType( 0 ), m_precision( 0 ), m_bufLen( 0 )
{
  // sensor state
  m_bActive = (pProtocol->m_activeSensors[arrIdx >> 3] & (1 << (arrIdx & 7))) ? true : false;
  if( !m_bActive )
    return;

  // copy constant data 
  JetiSensorConst constData;
  memcpy_P( &constData, &pProtocol->m_pSensorsConst[ arrIdx ], sizeof(JetiSensorConst) );

  m_dataType = constData.dataType; 
  m_id       = constData.id;

  // value
  m_value = pProtocol->m_pValues[ arrIdx ].m_value;
  m_prio =  pProtocol->m_pValues[ arrIdx ].m_prio;

  // copy to combined sensor/value buffer
  copyLabel( (const uint8_t*)constData.text, (const uint8_t*)constData.unit, m_label, sizeof( m_label ), &m_textLen, &m_unitLen );

  // 0...2 decimal places
  switch( constData.precision )
  {
  case 1: m_precision = 0x20; break; 
  case 2: m_precision = 0x40; break; 
  }

  // set needed space in EX frame buffer
  switch( m_dataType )
  {
  case TYPE_6b:  m_bufLen = 2; break;  //  1 byte id and data type + 1 byte value (incl. sign and prec)
  case TYPE_14b: m_bufLen = 3; break;  //  1 byte id and data type + 2 byte value (incl. sign and prec)
  case TYPE_22b: m_bufLen = 4; break;  //  1 byte id and data type + 3 byte value (incl. sign and prec)
  case TYPE_DT:  m_bufLen = 4; break;  //  1 byte id and data type + 3 byte value
  case TYPE_30b: m_bufLen = 5; break;  //  1 byte id and data type + 4 byte value
  case TYPE_GPS: m_bufLen = 5; break;  //  1 byte id and data type + 4 byte value
  }
}

// JetiExProtocol
/////////////////
JetiExProtocol::JetiExProtocol() :
  m_tiLastSend( 0 ), m_frameCnt( 0 ), m_nameLen( 0 ), m_pSensorsConst( 0 ), m_pValues( 0 ), m_nSensors( 0 ),
  m_sensorIdx( 0 ), m_dictIdx( 0 ), m_pSerial( 0 ), m_alarmChar( 0 ), m_bExitNav( 0 ), 
  m_devIdLow( DEVICE_ID_LOW ), m_devIdHi( DEVICE_ID_HI ), m_ExFrameSendCycle ( 150 ) , m_ValueCycleCnt ( 1 )
{
  m_name[0] = '\0';
  memset( m_activeSensors, 255, sizeof(m_activeSensors) ); // default: all sensors active
  memset( m_sensorMapper, 0, sizeof( m_sensorMapper ) );
}

void JetiExProtocol::Start( const char * name, JETISENSOR_CONST * pSensorArray, enComPort comPort )
{
  // call it once only !
  if( m_nameLen != 0 )
    return;

  // init buffer memory
  memset( m_exBuffer, 0, sizeof( m_exBuffer ) );
  memset( m_textBuffer, ' ', sizeof( m_textBuffer ) );

  // sensor name
  strncpy( m_name, name, sizeof( m_name ) - 1 );
  m_nameLen = strlen( name );

  // map sensor values
  if( m_nSensors == 0 ) // dont do it more than once
    InitSensorMapper( pSensorArray );

  // init sensor value array
  m_pValues = new JetiValue[ m_nSensors ];

  // init serial port 
  m_pSerial = JetiExSerial::CreatePort( comPort );
  m_pSerial->Init(); 

  // reset state machine
  m_sensorIdx = m_dictIdx = m_frameCnt = 0;

  // send sensor dictionary for the 1st time
  unsigned long tiLoop = millis() + 2000;
  while( m_pSensorsConst && tiLoop > millis() )
  { 
    int i;
    for( i = 0; i <= 3; i++ )
    { // send 3 text frames to get transmitter ready for EX
      SendJetiboxTextFrame();
      delay( 150 ); 
    }
    for( i = 0; i <= m_nSensors; i++ )
    {
      SendExFrame( i<<1 );
      SendJetiboxTextFrame();
      delay( 150 ); 
    }
  }
  while( GetJetiboxKey() ) // flush RX-Queue
    ;         
}

uint8_t JetiExProtocol::GetJetiboxKey()
{
  // key handling
  return m_pSerial->Getchar(); 
}

/// set the send cycle to the given value in ms (min = 75)
void JetiExProtocol::SetJetiSendCycle(uint8_t aTime) { 
  m_ExFrameSendCycle = max (75, aTime);     
}

uint8_t JetiExProtocol::DoJetiSend()
{
  // send every 150 ms only
  if( ( m_tiLastSend + m_ExFrameSendCycle ) <= millis() )
  {
    m_tiLastSend = millis(); 

    // navigator exit
    if( m_bExitNav )
    {
      SendJetiboxExit();
      m_bExitNav = false;
    }
    // morse alarm
    else if( m_alarmChar )
    {
      SendJetiAlarm( m_alarmChar );
      m_alarmChar = 0;
    }
    // EX frame...
    else if( m_pSensorsConst )
    {
      SendExFrame( m_frameCnt++ );
    }

    // followed by "simple text" frame
    SendJetiboxTextFrame();
  }

  return 0;
}

/**
 * @id: id of the sensor
 * @value: value of the sensor to be transferred
 * @prio: transmission prioity: 1=send with every frame, 2=send with every second frame, ....,  
 * #defines avaiable
 *    JEP_PRIO_ULTRA_HIGH  1
 *    JEP_PRIO_HIGH        3
 *    JEP_PRIO_STANDARD    5
 *    JEP_PRIO_LOW        10
 *    JEP_PRIO_ULTRA_LOW  15
 */
void JetiExProtocol::SetSensorValue( uint8_t id, int32_t value , uint8_t prio)
{
  if( m_pValues && id < sizeof( m_sensorMapper ) ) { 
    if (m_sensorMapper[ id ] < m_nSensors) {
      m_pValues[ m_sensorMapper[ id ] ].m_value = value;
      m_pValues[ m_sensorMapper[ id ] ].m_prio = prio;
    }
  } 
}

void JetiExProtocol::SetSensorValueGPS( uint8_t id, bool bLongitude, float value, uint8_t prio )
{
  // Jeti doc: If the lowest bit of a decimal point (Bit 5) equals log. 1, the data represents longitude. According to the highest bit (30) of a decimal point it is either West (1) or East (0).
  // Jeti doc: If the lowest bit of a decimal point (Bit 5) equals log. 0, the data represents latitude. According to the highest bit (30) of a decimal point it is either South (1) or North (0).
  // Byte 0: lo of minute, Byte 1: hi of minute, Byte 2: lo von degree, Byte 3: hi of degree 
  union
  {
    int32_t vInt;
    char    vBytes[4];
  } gps;

  // i.e.:
  // E 11� 33' 22.176" --> 11.55616 --> 11� 33.369' see http://www.gpscoordinates.eu/convert-gps-coordinates.php
  // N 48� 14' 44.520" --> 48.24570 --> 48� 14.742'
  float deg, frac = modff( value, &deg );
  uint16_t deg16 = (uint16_t)fabs( deg );
  uint16_t min16 = (uint16_t)fabs( frac * 0.6f * 100000 );
  gps.vInt = 0;
  gps.vBytes[0]  = min16 & 0xFF;
  gps.vBytes[1]  = ( min16 >> 8 ) & 0xFF;
  gps.vBytes[2]  = deg16 & 0xFF;                      // degrees 0..255
  gps.vBytes[3]  = ( deg16 >> 8 ) & 0x01;             // degrees 256..359
  gps.vBytes[3] |= bLongitude  ? 0x20 : 0;
  gps.vBytes[3] |= (value < 0) ? 0x40 : 0;
  
  SetSensorValue( id, gps.vInt, prio );
}

void JetiExProtocol::SetSensorValueDate( uint8_t id, uint8_t day, uint8_t month, uint16_t year, uint8_t prio )
{
  // Jeti doc: If the lowest bit of a decimal point equals log. 1, the data represents date
  // Jeti doc: (decimal representation: b0-7 day, b8-15 month, b16-20 year - 2 decimals, number 2000 to be added).
  // doc seems to be wrong, this is working: b0-b7 year, b16-b20: day
  union
  {
    int32_t vInt;
    char    vBytes[4];
  } date;

  if( year >= 2000 )
    year -= 2000;

  date.vInt = 0;
  date.vBytes[0]  = year;
  date.vBytes[1]  = month;
  date.vBytes[2]  = day & 0x1F;
  date.vBytes[2] |= 0x20;
  
  SetSensorValue( id, date.vInt, prio );
}

void JetiExProtocol::SetSensorValueTime( uint8_t id, uint8_t hour, uint8_t minute, uint8_t second, uint8_t prio )
{
  // If the lowest bit of a decimal point equals log. 0, the data represents time
  // (decimal representation: b0-7 seconds, b8-15 minutes, b16-20 hours).
  union
  {
    int32_t vInt;
    char    vBytes[4];
  } date;

  date.vInt = 0;
  date.vBytes[0]  = second;
  date.vBytes[1]  = minute;
  date.vBytes[2]  = hour & 0x1F;
  
  SetSensorValue( id, date.vInt, prio );
}

void JetiExProtocol::SetSensorActive( uint8_t id, bool bEnable, JETISENSOR_CONST * pSensorArray )
{
  if( m_nSensors == 0 && pSensorArray ) // dont do it more than once
    InitSensorMapper( pSensorArray );

  if( id < sizeof( m_sensorMapper ) ) {
    int idx = m_sensorMapper[ id ];
    if (idx < m_nSensors) {
      if( bEnable )
        m_activeSensors[ idx >>3 ] |=   1 << (idx & 7);
      else
        m_activeSensors[ idx >>3 ] &= ~(1 << (idx & 7));
    }
  }

  // restart sending dictionary
  m_sensorIdx = m_dictIdx = m_frameCnt = 0;
}

void JetiExProtocol::InitSensorMapper( JETISENSOR_CONST * pSensorArray )
{ 
  // map sensor id to index to give quick access by sensor ID
  int i;
  m_nSensors = 0;
  m_pSensorsConst = pSensorArray;
  memset( m_sensorMapper, 0, sizeof( m_sensorMapper ) );
  for( i = 0; i < MAX_SENSORS; i++ )
  {
    // get sensor id and check for end of array
    JetiSensorConst sensorConst;
    memcpy_P( &sensorConst, &m_pSensorsConst[i], sizeof(sensorConst) );
    if( sensorConst.id == 0 )
      break;

    if( sensorConst.id < sizeof( m_sensorMapper ) )
      m_sensorMapper[ sensorConst.id ] = i;
    m_nSensors++;
  }
}

void JetiExProtocol::SetJetiboxText( enLineNo lineNo, const char* text )
{
  if( text == 0 )
    return;

  char * pStart = 0;
  switch( lineNo )
  {
  default:
  case 0: pStart = m_textBuffer; break;
  case 1: pStart = m_textBuffer + 16; break;
  }
  
  bool bPadding = false;
  for( int i = 0; i < 16; i++ )
  {
    if( text[ i ] == '\0' )
      bPadding = true;

    if( !bPadding )
      pStart[ i ] = text[ i ];
    else
      pStart[ i ] = ' ';
  }
}

void JetiExProtocol::SendJetiboxTextFrame()
{
  int i;

  // send 34 byte  text message
  m_pSerial->Send( 0xFE, false );
  if( m_textBuffer[ 0 ] != '\0' )
  {
    for( i = 0; i < 32; i++)
      m_pSerial->Send( m_textBuffer[ i ], true );
  }
  // send empty message
  else
  {
    for( i = 0; i < 32; i++)
      m_pSerial->Send( 0, true );
  }
  m_pSerial->Send( 0xFF, false );    
}

void JetiExProtocol::SendJetiboxExit()
{
  m_pSerial->Send( 0x7E, false );
  m_pSerial->Send( 0x91, true );
  m_pSerial->Send( 0x31, true );
}

void JetiExProtocol::SendJetiAlarm( char code ) // upper case character produces sound, lower case is silent
{
  bool bSound = true;
  if( islower( code ) )
  {
    code = toupper( code );
    bSound = false;
  }

  m_pSerial->Send( 0x7E, false );                      
  m_pSerial->Send( 0x02, true );                         // length
  m_pSerial->Send( 0x22 | (bSound ? 0x01 : 0x00), true); // alarm type "vario" w/o sound or "normal"
  m_pSerial->Send( code, true );                         // send "morse code" character
}


void JetiExProtocol::SendExFrame( uint8_t frameCnt )
{
  uint8_t n = 0;
  uint8_t i = 0;

  // sensor name in frame 0
  if( frameCnt == 0 )
  {                                                                // sensor name
    m_exBuffer[2] = 0x00;                                           // 2Bit packet type(0-3) 0x40=Data, 0x00=Text 
    m_exBuffer[8] = 0x00;                                          // 8Bit id 
    m_exBuffer[9] = m_nameLen<<3;                                  // 5Bit description, 3Bit unit length (use one space character)
    memcpy( m_exBuffer + 10, m_name, m_nameLen );                  // copy label plus unit to ex buffer starting from pos 10
    n += m_nameLen + 10;                                          
  }
  // sensor dictionary: use the first few frames with even numbers to transfer 
  else if( ( (frameCnt/2) <= m_nSensors && (frameCnt % 2) == 0 ) )
  {
    for( int nDict = 0; nDict < m_nSensors; nDict++ )
    {
      JetiSensor sensor( m_dictIdx, this );
      if( ++m_dictIdx >= m_nSensors )
        m_dictIdx = 0;

      if( sensor.m_bActive )
      {
        m_exBuffer[2] = 0x00;                                          // 2Bit packet type(0-3) 0x40=Data, 0x00=Text
        m_exBuffer[8] = sensor.m_id;                                   // 8Bit id
        m_exBuffer[9] = (sensor.m_textLen<<3) | sensor.m_unitLen;       // 5Bit description, 3Bit unit length 
        n = sensor.jetiCopyLabel( m_exBuffer, 10 ) + 10;               // copy label plus unit to ex buffer starting from pos 10
        break;
      }
    }
  }
  // send EX values in all other frames
  else
  {
    int bufLen;
    int nVal = 0;                                                  // count values 
    m_exBuffer[ 2 ] = 0x40;                                         // 2Bit Type(0-3) 0x40=Data, 0x00=Text
    n=8;                                                           // start at nineth byte in buffer

    do
    {
      JetiSensor sensor( m_sensorIdx, this );

      if( sensor.m_bActive && ((m_ValueCycleCnt%sensor.m_prio) == 0)) {
        //  id+type+value+precision + CRC
        bufLen = sensor.m_bufLen + 1 ;
        if( sensor.m_id > 15 ) {
          //  next byte for id > 15
          bufLen++;
	  if (n + bufLen > JEP_MAX_BYTE_PER_BUF) {
	    // not enough bytes free
            break;
	  }
          m_exBuffer[n++] = 0x0 | (sensor.m_dataType & 0x0F);               // sensor id > 15 --> put id to next byte
          m_exBuffer[n++] = sensor.m_id;    
        } else {
	  if (n + bufLen > JEP_MAX_BYTE_PER_BUF) {
	    // not enough bytes free
            break;
	  }
          m_exBuffer[n++] = (sensor.m_id<<4) | (sensor.m_dataType & 0x0F);  // 4Bit id, 4 bit data type (i.e. int14_t)
	}
      
        n += sensor.jetiEncodeValue( m_exBuffer, n );
      }

      if( ++m_sensorIdx >= m_nSensors ) {                                    // wrap index when array is at the end
        m_sensorIdx = 0;
	m_ValueCycleCnt++;
      }
      if( ++nVal >= m_nSensors ) {                                           // dont send twice in a frame
        break;
      }
    }
    while( true );                                           // jeti spec says max 29 Bytes per buffer
  }

  // complete some more EX frame data
  //  8 Byte Header
  m_exBuffer[0] = 0x7E;                m_exBuffer[1] = 0x2F;                // EX-Frame Separator
  m_exBuffer[2] |= n-2;                                                      // frame length to Byte 2
  m_exBuffer[3] = MANUFACTURER_ID_LOW; m_exBuffer[4] = MANUFACTURER_ID_HI;  // sensor ID
  m_exBuffer[5] = m_devIdLow;          m_exBuffer[6] = m_devIdHi;
  m_exBuffer[7] = 0x00; // reserved (key for encryption)

  // calculate crc
  // 1 Byte CRC
  m_exBuffer[n] = jeti_crc8( m_exBuffer, n );

  // serial transmission
  m_pSerial->Send( 0x7E, false );                                 // send EX frame header tag
  for( i = 1; i <= n; i++ )                                       // followed by EX data frame (start from byte 1, since 0x7e has already been sent)
    m_pSerial->Send( m_exBuffer[i], true );
}


// **************************************
// Helpers
// **************************************
// merge name and unit and terminate with '\0'
void JetiSensor::copyLabel( const uint8_t * name, const uint8_t * unit,  uint8_t * dest, int dest_size, uint8_t * nameLen, uint8_t * unitLen )
{
  int maxchar = dest_size -1 ;

  uint8_t i = 0, j = 0;
  while( name[i] != '\0' && j < maxchar )
    dest[ j++ ] = name[ i++ ];
  *nameLen = i;

  i = 0;
  while( unit[i] != '\0' && j < maxchar )
    dest[ j++ ] = unit[ i++ ];
  *unitLen = i;

  dest[ j ] = '\0';
}

// encode sensor value to jeti ex format and copy to buffer
uint8_t JetiSensor::jetiEncodeValue( uint8_t * exbuf, uint8_t n )
{
  switch( m_dataType )
  {
  case TYPE_6b:
    exbuf[n]  = ( m_value & 0x1F) | ((m_value < 0) ? 0x80 :0x00 );                   // 5 bit value and sign 
    exbuf[n] |= m_precision;                                                         // precision in bit 5/6 (0, 20, 40)
    return 1;
  
  case TYPE_14b:
    exbuf[n]      = m_value & 0xFF;                                                  // lo byte
    exbuf[n + 1]  = ( (m_value >> 8) & 0x1F) | ((m_value < 0) ? 0x80 :0x00 );        // 5 bit hi byte and sign 
    exbuf[n + 1] |= m_precision;                                                     // precision in bit 5/6 (0, 20, 40)
    return 2;

  case TYPE_22b:
    exbuf[n]      = m_value & 0xFF;                                                  // lo byte
    exbuf[n + 1]  = (m_value >> 8 ) & 0xFF;                                          // mid byte
    exbuf[n + 2]  = ( (m_value >> 16) & 0x1F) | ((m_value < 0) ? 0x80 :0x00 );       // 5 bit hi byte and sign 
    exbuf[n + 2] |= m_precision;                                                     // precision in bit 5/6 (0, 20, 40)
    return 3;

  case TYPE_DT:
    exbuf[n]      = m_value & 0xFF;                                                  // value has been prepared by SetSensorValueDate/Time 
    exbuf[n + 1]  = (m_value >> 8 ) & 0xFF;
    exbuf[n + 2]  = ( (m_value >> 16) & 0xFF) | ((m_value < 0) ? 0x80 :0x00 );
    return 3;

  case TYPE_30b:
    exbuf[n]      = m_value & 0xFF;                                                  // lo byte
    exbuf[n + 1]  = (m_value >> 8 ) & 0xFF;
    exbuf[n + 2]  = (m_value >> 16 ) & 0xFF;
    exbuf[n + 3]  = ( (m_value >> 24) & 0x1F) | ((m_value < 0) ? 0x80 :0x00 );       // 5 bit hi byte and sign 
    exbuf[n + 3] |= m_precision;                                                     // precision in bit 5/6 (0, 20, 40)
    return 4;

  case TYPE_GPS:
    exbuf[n]      = m_value & 0xFF;                                                  // value has been prepared by SetSensorValueGPS 
    exbuf[n + 1]  = (m_value >> 8 ) & 0xFF;                                          
    exbuf[n + 2]  = (m_value >> 16) & 0xFF;
    exbuf[n + 3]  = (m_value >> 24) & 0xFF;
    return 4;
  }
  return 0;
}

// copy sensor label to ex buffer
uint8_t JetiSensor::jetiCopyLabel( uint8_t * exbuf, uint8_t n )
{
  uint8_t i = 0;

  while( m_label[i] != '\0' )
    exbuf[ n++ ] = m_label[ i++ ];

  return( i ) ; // number of bytes copied
}


// **************************************
// Jeti helpers
// **************************************

// Published in "JETI Telemetry Protocol EN V1.06"
//* Jeti EX Protocol: Calculate 8-bit CRC polynomial X^8 + X^2 + X + 1
uint8_t JetiExProtocol::update_crc (uint8_t crc, uint8_t crc_seed)
{
  unsigned char crc_u;
  unsigned char i;
  crc_u = crc;
  crc_u ^= crc_seed;
  for (i=0; i<8; i++)
    crc_u = ( crc_u & 0x80 ) ? POLY ^ ( crc_u << 1 ) : ( crc_u << 1 );
  return (crc_u);
}

//* Calculate CRC8 Checksum over EX-Frame, Original code by Jeti
uint8_t JetiExProtocol::jeti_crc8 (uint8_t *exbuf, unsigned char framelen)
{
  uint8_t crc = 0;
  uint8_t c;

  for(c=2; c<framelen; c++)
    crc = update_crc (exbuf[c], crc);
  return (crc);
}

