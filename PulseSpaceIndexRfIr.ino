// PulseSpaceIndex.ino
// Combine minimal AnaysRF sketch interrupt processing
// with NodoDueRkr (Paul Tonkes) and rfm69-ook-receive-dio2.ino (SevenW)
// Also inspired by pimatic, pilight and rflink...
//*
//* Please credit AnalysIR & provide a link or blog post, pointing to http://www.AnalysIR.com

/*
 * Copyright (c)2011-2018 Rinie Kervel
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
*/

#define SERIAL_BAUD 57600
#define RF_ReceiveDataPin           2  // Input of OOK 433Mhz-RF signal. LOW (Off): no signal
#define RF_TransmitDataPin          5  // Output of OOK 433Mhz-RF
#if 1
#define RF_TransmitPowerPin         4  // +5 volt / Vcc to transmitter
#define RF_ReceivePowerPin         12  // +5 volt / Vcc to receiver
#endif

#define IR_ReceiveDataPin           3  // Input of IR signal TSOP. HIGH: no signal
#define IR_TransmitDataPin         11  // Output IR-Led transmitter


#ifndef NINJA_BLOCK
#define MonitorLedPin              13  // status LED
#else
#define MonitorLedPin              BLUE_STAT_LED_PIN  // Status LED
#endif

#define rfPin RF_ReceiveDataPin
#define ledPin MonitorLedPin
#define PIN2HIGH (PIND & 0b00000100)
#define PIN3HIGH (PIND & 0b00001000)

#define EDGE_TIMEOUT 45000 // was 10000
#define MAX_PULSE 5000 // was 100
#define MIN_PULSE 75 // was 100
#define MIN_PSCOUNT 48
#define NODO_DUE
#include "pulsespaceindex.h"

typedef enum {psiNone, psiLoop, psiRf, psiIr} PsiCode;
volatile PsiCode psiCode = psiNone;

void setup()
{
	pinMode(IR_ReceiveDataPin,INPUT);
	pinMode(RF_ReceiveDataPin,INPUT);
	pinMode(RF_TransmitDataPin,OUTPUT);
	pinMode(RF_TransmitPowerPin,OUTPUT);
	pinMode(RF_ReceivePowerPin,OUTPUT);
	pinMode(IR_TransmitDataPin,OUTPUT);

#ifndef NINJA_BLOCK
	pinMode(MonitorLedPin,OUTPUT);
	//pinMode(BuzzerPin, OUTPUT);
#else
	pinMode(RED_LED_PIN, OUTPUT);
	pinMode(GREEN_LED_PIN, OUTPUT);
	pinMode(BLUE_LED_PIN, OUTPUT);
	digitalWrite(RED_LED_PIN, HIGH);            // set the RED LED  Off
	digitalWrite(GREEN_LED_PIN, HIGH);          // set the GREEN LED Off
	digitalWrite(BLUE_LED_PIN, HIGH);           // set the BLUE LED Off
#ifdef V11
	digitalWrite(BLUE_LED_PIN, LOW);           // Power on Status
#endif
#if defined(V12) || defined(VRPI10)
	pinMode(RED_STAT_LED_PIN, OUTPUT);
	pinMode(GREEN_STAT_LED_PIN, OUTPUT);
	pinMode(BLUE_STAT_LED_PIN, OUTPUT);
	digitalWrite(RED_STAT_LED_PIN, HIGH);		// set the RED STAT LED  Off
	digitalWrite(GREEN_STAT_LED_PIN, HIGH);	        // set the GREEN STAT LED Off
	digitalWrite(BLUE_STAT_LED_PIN, LOW);	        // Power on Status
#endif
#endif

	digitalWrite(IR_ReceiveDataPin,HIGH);  // enable pull-up to prevent garbage if pin not connected
	digitalWrite(RF_ReceiveDataPin,HIGH);  // enable pull-up to prevent garbage if pin not connected
	digitalWrite(RF_ReceivePowerPin,HIGH); // enable RF receiver power pin

//	LoadSettingsFromEeprom();	// store baudrate and mode in Eeprom

	psiInit();

	Serial.begin(SERIAL_BAUD);
#ifdef JS_OUTPUT
	/*
	 * Save output to js file and
	//test with node pulsespaceindex.js ./samples/capture.js
	module.exports = {
	samples:[
	...
	]
	};
	*/
	Serial.println(F("{ comment:`"));
#endif
	Serial.println(F("PulseSpaceIndexRfIr 57600!"));

	attachInterrupt(digitalPinToInterrupt(RF_ReceiveDataPin), rfReceiveInterrupt, CHANGE);
	attachInterrupt(digitalPinToInterrupt(IR_ReceiveDataPin), irReceiveInterrupt, CHANGE);
}

void loop()
{
	static uint16_t lastPsCount = 0;
	static uint32_t lastMicros = 0;
	static uint32_t lastSignal = 0;
//	static uint32_t lastChange = 0;

	if (psCount > 0) {
		uint32_t uSecs  = micros();
		if (psCount > lastPsCount) {
			lastPsCount = psCount;
//			lastChange = lastMicros;
			lastMicros = uSecs;
		}
		else if ((uSecs > lastMicros) && (uSecs - lastMicros) >= psiNoChangeTimeout()) {
			noInterrupts();
			psiCode = psiLoop;
			interrupts();

			digitalWrite(MonitorLedPin, HIGH);
#if 1
			if (psCount > 4) {
				Serial.print((fIsRf)? F("RF PSI "): F("IR PSI "));
				Serial.print(psiCount * 2);
				psiPrintComma(psCount,'#', 5);
				if (lastSignal > 0) {
					psiPrintChar('*');
					psiPrintComma(lastSignal - millis(),',', 5);
				}
				lastSignal = millis();
				psiPrintChar('!');
				psiPrintComma(uSecs - lastMicros,',', 5);
				Serial.println();
			}
#endif
			psiFinish();

			noInterrupts();
			psiCode = psiNone;
			interrupts();

			lastPsCount = 0;
			digitalWrite(MonitorLedPin, LOW);
		}
	}
}

void receiveInterrupt(PsiCode psiCodeP, byte signal)
{
	static uint32_t lastTime = 0;
	uint32_t now = micros();
	if (psiCode != psiLoop) {
		uint16_t pulse_dur = now-lastTime;

		if (psiCode != psiCodeP) {
			if ((psCount > 0) && ((psiCode == psiRf) || (psiCode == psiIr))) {
				return; // no RF and IR at the same time
			}
			psiCode = psiCodeP;
			fIsRf = (psiCode == psiRf) ? true : false;
			psCount = 0;
		}

//		if (fIsRf) {
			if (signal) {//signal is high, so record low time
				if (psCount > 0 && pulse_dur < EDGE_TIMEOUT) {
					psiAddPS(pulse_dur, 0, 1);
				}
			}
			else {  //get here if signal is low, so record high time
				if (pulse_dur > MIN_PULSE && pulse_dur < MAX_PULSE){
					psiAddPS(pulse_dur, 1, 1);
				}
				else if (psCount > 0 && psCount <= 16) {
					psCount = 0; // reset
					psiInit();
				}
			}
/*
		}
		else { // IR already noise suppression by 38khz modulation/TSOP
			if (signal) {//signal is high, so record low time
				if (psCount > 0 && pulse_dur < EDGE_TIMEOUT) {
					psiAddPS(pulse_dur, 0, 1);
				}
			}
			else {  //get here if signal is low, so record high time
				psiAddPS(pulse_dur, 1, 1);
			}
		}
*/
	}
	lastTime = now;
}

void rfReceiveInterrupt() {
	receiveInterrupt(psiRf, PIN2HIGH ? 1 : 0);
}

void irReceiveInterrupt() {
	receiveInterrupt(psiIr, PIN3HIGH ? 0 : 1); // IR is default high, signal low
}

