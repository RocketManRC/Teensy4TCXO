/*
  Teensy4TCXO
  
  This code for the Teensy 4.0 tests the idea of using a TCXO (Temperature Compensated Crystal Oscillator)
  as a stable clock reference. It is an interesting experiment if nothing else.

  The concept is to connect a 10 MHz TCXO to pin 9 and use the library FreqCount to count TCXO pulses for
  one second as determined by the internal CPU clock. At the same time the one second of the Pulse Per Second (PPS) from the
  GPS is measured using the CPU cycle counter. The ration of PPS counts versus TCXO counts is calculated which is
  independent of the CPU clock (i.e. it is a calibration of the TCXO). This is only calculated once.
  
  After 10 seconds (arbitrary) an interval timer is initialized with the calculated interval and then
  updated at the end of every interval using the TCXO as a reference. This creates a emulated PPS signal.

  After initialization the GPS PPS is only used to display the offset between the interval being generated and PPS,
  i.e. it tracks the drift of the system. If the TCXO didn't drift it would stay perfectly matched but of
  course the TCXO does drift but a lot less than the CPU clock.

  For this experiment I used an external system to manage the GPS and generate the PPS signal. It uses a u-blox M8
  series GNSS module and is optimized for timing. That code is for the TeensyLC or ESP32 but could easily be
  integrated into this program. The external system was created for another purpose and is documented at:

  https://github.com/RocketManRC/AboutTimeServer

  Possible applications for this are to build a more accurate time reference or real time clock, improve the
  accuracy of the FreqCount or FreqMeasure libraries and generate precision pulse sequences.

  Teensy4TCXO is Copyright (c) 2020 Rick MacDonald - MIT License:

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include <Arduino.h>
#include <FreqCount.h>
#include <IntervalTimer.h>

#define  TESTOUTPIN2 0  // set this to 1 to toggle pin 2 in the interval interrupt handler

IntervalTimer myTimer;

volatile uint32_t intervalCycles = 0; // the cycle counter recorded in the interval interrupt handler
uint32_t lastIntervalCycles = 0;      // to look for a change...

volatile uint32_t ppsCycles = 0;      // the cycle counter recorded in the PPS interrupt handler
uint32_t lastPpsCycles = 0;           // to look for a change...

volatile uint32_t ppsTcxoCount = 0;   // this is the TCXO counter at PPS
uint32_t lastPpsTcxoCount = 0;

extern IMXRT_TMR_t *TMRx;             // this is defined in the FreqCount library

void ppsInterrupt()
{
  ppsCycles = ARM_DWT_CYCCNT;   // record the cycle counter on PPS

  // Bare metal access to the FreqCount counter..
  ppsTcxoCount = TMRx->CH[2].CNTR | TMRx->CH[3].HOLD << 16;
}

void intervalInterrupt()
{
  intervalCycles = ARM_DWT_CYCCNT;  // record the cycle counter on interval

#if TESTOUTPIN2 // toggle pin 2 if we want to test with the oscilloscope
  static byte intervalCount = 0;

  digitalWriteFast( 2, intervalCount++ & 1 ); // toggle pin 2 for oscillosope display
#endif
}

void setup() 
{
  // NOTE: on Teensy 4.0 the cycle counter is already started.

  pinMode( 22, INPUT_PULLUP );    // PPS on pin 22 (can be any digital pin)

#if TESTOUTPIN2
  pinMode( 2, OUTPUT );           // toggle pin 2 in interval handler for oscilloscope display vs PPS
#endif
  attachInterrupt( digitalPinToInterrupt( 22 ), ppsInterrupt, RISING );

  Serial.begin( 115200 );
  
  delay( 2000 );
}

void loop() 
{
  static double tcxoMicros = 1000000.0;

  static int firstTimeCount = 0;
  static bool firstTimeFlag = true;

  static double interval = 1000000.0;
  static double countRatio = 1000000000000.0;

  noInterrupts();
  uint32_t ic = intervalCycles;
  uint32_t dt = ic - ppsCycles; // dt is the time difference in CPU cycles between the interval interrupt and the pps interrupt
  interrupts();

  if( ic != lastIntervalCycles )
  {
    // we get here the next pass through loop() after an interval timer interrupt
    static uint32_t intervalCount = 0;

    lastIntervalCycles = ic; // so we wait for the next interval interrupt

    intervalCount++;

    // update the interval timer value. Count ratio is the 
    interval = countRatio * tcxoMicros - 0.005; // (the 0.005 is a fudge factor determined by experiment)
    // A typical interval = 999995.0 which means the CPU clock is 5 PPM out which is within spec

    myTimer.update( interval );

    double dtus = dt / 600.0; // this is the time difference in microseconds (see the comment on dt above)

    // We don't keep track of seconds so if dtus is > 500000 it is considered negative
    if( dtus > 500000.0 && dtus < 1000000.0 )
    {
      Serial.printf( "%d,-%.1f,%.2f,%.2f\n", intervalCount, 1000000.0 - dtus, interval, tcxoMicros );
    }
    else
    {
      if( dtus > 1000000.0 )
        dtus = 0.0;

      Serial.printf( "%d,%.1f,%.2f,%.2f\n", intervalCount, dtus, interval, tcxoMicros );
    }
  }

  if( FreqCount.available() ) 
  {
    // We get here when we've counted TCXO pulses for 1 second according to the CPU clock
    unsigned long tcxoCount;

    tcxoCount = FreqCount.read();

    tcxoMicros = 10000000.0 / tcxoCount * 1000000.0;
  }

  noInterrupts();
  uint32_t pc = ppsCycles;
  uint32_t tc = ppsTcxoCount;
  interrupts();

  if( pc != lastPpsCycles ) 
  {
    // we get here the next pass through loop() after a PPS interrupt
    uint32_t dtt = pc - lastPpsCycles; // this is measuring the CPU clock count for one second from PPS. 

    double ppsMicros;

    ppsMicros =  dtt / 600.0; // this is the count in microseconds

    lastPpsCycles = pc; // so we wait for the next PPS

    if( firstTimeCount < 10 )
    {
      // Start counting the TCXO on PPS to get everything lined up to start
      if( firstTimeCount == 0 )
      {
        FreqCount.begin( 1000000 ); // count the TCXO (10 MHz) every 1 second
      }
      else
      {
        Serial.println( tc - lastPpsTcxoCount );
        lastPpsTcxoCount = tc;
      }
      
      firstTimeCount++;

      Serial.println( "waiting..." );
    }
    else if( firstTimeFlag )
    {
      // countRatio is calculated once after 10 seconds and it represents the calibration
      // of the TCXO versus PPS from the GPS
      // This is a "constant" but dependent on the stability of the TCXO
      countRatio = ppsMicros / tcxoMicros; 

      interval = countRatio * tcxoMicros;

      myTimer.begin( intervalInterrupt, interval );

      firstTimeFlag = false;

      Serial.println( "calculate countRatio and set timer..." );
    }
  }
}