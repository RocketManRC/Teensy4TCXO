# Teensy4TCXO
This code for the Teensy 4.0 tests the idea of using a TCXO (Temperature Compensated Crystal Oscillator) as a stable clock reference. It is an interesting experiment if nothing else.

The concept is to connect a 10 MHz TCXO to pin 9 and use the library FreqCount to count TCXO pulses for one second as determined by the internal CPU clock. At the same time the one second of the Pulse Per Second (PPS) from the GPS is measured using the CPU cycle counter. The ratio of PPS counts versus TCXO counts is calculated which is independent of the CPU clock (i.e. it is a calibration of the TCXO). This is only calculated once.

After 10 seconds (arbitrary) an interval timer is initialized with the calculated interval and then updated at the end of every interval using the TCXO as a reference. This creates a emulated PPS signal.

After initialization the GPS PPS is only used to display the offset between the interval being generated and PPS, i.e. it tracks the drift of the system. If the TCXO didn't drift it would stay perfectly matched but of course the TCXO does drift but a lot less than the CPU clock.

For this experiment I used an external system to manage the GPS and generate the PPS signal. It uses a u-blox M8 series GNSS module and is optimized for timing. That code is for the TeensyLC or ESP32 but could easily be integrated into this program. The external system was created for another purpose and is documented at:

https://github.com/RocketManRC/AboutTimeServer

Possible applications for this are to build a more accurate time reference or real time clock, improve the accuracy of the FreqCount or FreqMeasure libraries and generate precision pulse sequences.

# Results
I didn't spend much time testing this but it appeared to achieve at least as well as the rated performance of the TCXO which is 200 PPB (or 15 ms/day).
