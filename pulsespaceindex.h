/*
 * General OOK decoding without knowing the protocol before hand.
 *
 * A lot of opensource software/hardware OOK decoding solutions
 * disregard the fact that commercial solutions do work with cheap receivers
 * and without lowpass filters or such.
 *
 * IMHO this stems from the fact that the protocols are designed to overcome
 * the limitations of these receivers:
 * - Add a preamble or Sync to give the AGC time to adjust.
 * - Repeat the signal at least 3 times, so that the 2nd and 3rd are received
 *   correctly with a tuned AGC and compared for identical packages.
 * - Use a weak checksum/CRC as the computing power is limited (optimized for low power,
 *   sending from a battery operated sensor).
 * - Use a simple encoding so typically 1 Pulse(On)/Space(Off) time combination for '1'
 *   and 1 Pulse/Space time combination for '0' is used.
 *	 Preamble, Gap or Sync use clearly distinctive timings or standard timings repeated often.
 *
 * Using these properties instead of a CRC checksum on the individual packages,
 * I try to guess from the relative timings of the signal what encoding is used,
 * and where the gap between the packages occur:
 * - GAP should be clear enough to capture package 2 and 3 reliable,
 *   but should be small enough to keep AGC correct.
 * - Few time variations can be stored as index instead of exact timepulse.
 *
 * Change sort to sort merge?
 *
 * Test with
 *	ORSV2
 *		200..1200 split on 700
 *		Manchester encoding, 160 bits, Short pulse within first 32 bits, start bit
 *
 *		http://wmrx00.sourceforge.net/Arduino/OregonScientific-RF-Protocols.pdf
 *
 *		For version 2.1 sensors only, each data bit is actually sent four times. This is
 *		accomplished by first sending each data bit twice (inverted the first time),
 *		doubling the size of the original message. A one bit is sent as a “01” sequence
 *		and a zero bit is sent as “10”. Secondly, the entire message is repeated once.
 *		 Oregon Scientific RF Protocols
 *		 Page 2 of 23
 *		Some sensors will insert a gap (about 10.9 msec for the THGR122NX) between
 *		the two repetitions, while others (e.g. UVR128) have no gap at all.
 *
 *		Both 2.1 and 3.0 protocols have a similar message structure containing four
 *		parts.
 *		1. The preamble consists of “1” bits, 24 bits (6 nibbles) for v3.0 sensors
 *		and 16 bits (4 nibbles) for v2.1 sensors (since a v2.1 sensor bit stream
 *		contains an inverted and interleaved copy of the data bits, there is in
 *		fact a 32 bit sequence of alternating “0” and “1” bits in the preamble).
 *		2. A sync nibble (4-bits) which is “0101” in the order of transmission. With
 *		v2.1 sensors this actually appears as “10011001”. Since nibbles are sent
 *		LSB first, the preamble nibble is actually “1010” or a hexadecimal “A”.
 *		3. The sensor data payload, which is described in the “Message Formats”
 *		section below.
 *		4. A post-amble (usually) containing two nibbles, the purpose or format of
 *		which is not understood at this time. At least one sensor (THR238NF)
 *		sends a 5-nibble post-amble where the last four nibbles are all zero.
 *		The number of bits in each message is sensor-dependent. The duration of most
 *		v3.0 messages is about 100msec. Since each bit is doubled in v2.1 messages,
 *		and each v2.1 message is repeated once in its entirety, these messages last
 *		about four times as long, or 400msec.
 *
 *  	KAKU
 *  		NodeDue: Encoding volgens Princeton PT2262 / MOSDESIGN M3EB / Domia Lite spec.
 *  		Pulse (T) is 350us PWDM
 *  		0 = T,3T,T,3T, 1 = T,3T,3T,T, short 0 = T,3T,T,T
 *
 *			P2S2 encoding 0 = 0101, 1 = 0110, short = 0100
 *
 * 		Timings theoretical: 350, 1050
 *
 *		Jeelabs: Split 700, Gap 2500
 *
 *	KAKUA/KAKUNEW:
 *		NodeDue: Encoding volgens Arduino Home Easy pagina
 *		Pulse (T) is 275us PDM
 *		0 = T,T,T,4T, 1 = T,4T,T,T, dim = T,T,T,T op bit 27
 *		8T // us, Tijd van de space na de startbit
 *		Timings theoretical: 275, 1100, 2200
 *
 *		Jeelabs: Split 700, 1900, Max 3000
 *		P1S2 encoding 0 = 01, 1 = 10 and dim = 00
 *
 *	XRF / X10:
 *		http://davehouston.net/rf.htm
 *
 * 	WS249 plant humidity sensor.
 *		SevenWatt: Normal signals between 170 and 2600. Sync (space after high( between 5400 and 6100))
 *		Split 1600, 64..66 pulse/spaces
 *
 * RcSwitch timing definitions:
 *  Format for protocol definitions:
 *  {pulselength, Sync bit, "0" bit, "1" bit}
 *
 *  pulselength: pulse length in microseconds, e.g. 350
 *  Sync bit: {1, 31} means 1 high pulse and 31 low pulses
 *      (perceived as a 31*pulselength long pulse, total length of sync bit is
 *      32*pulselength microseconds), i.e:
 *       _
 *      | |_______________________________ (don't count the vertical bars)
 *  "0" bit: waveform for a data bit of value "0", {1, 3} means 1 high pulse
 *      and 3 low pulses, total length (1+3)*pulselength, i.e:
 *       _
 *      | |___
 *  "1" bit: waveform for a data bit of value "1", e.g. {3,1}:
 *       ___
 *      |   |_
 *
 *  These are combined to form Tri-State bits when sending or receiving codes.
 *
 *   { 350, {  1, 31 }, {  1,  3 }, {  3,  1 }, false },    // protocol 1
 *   { 650, {  1, 10 }, {  1,  2 }, {  2,  1 }, false },    // protocol 2
 *   { 100, { 30, 71 }, {  4, 11 }, {  9,  6 }, false },    // protocol 3
 *   { 380, {  1,  6 }, {  1,  3 }, {  3,  1 }, false },    // protocol 4
 *   { 500, {  6, 14 }, {  1,  2 }, {  2,  1 }, false },    // protocol 5
 *   { 450, { 23,  1 }, {  1,  2 }, {  2,  1 }, true }      // protocol 6 (HT6P20B)
 *   RKR: OR 450, {1, 23}, {2, 1}, {1, 2} not inverted?
 *   Gap after message > space time  of sync
 *
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
#ifndef __PULSESPACEINDEX_H__
#define __PULSESPACEINDEX_H__
// RKR modifications
typedef unsigned long ulong; //RKR U N S I G N E D is so verbose
typedef unsigned int uint;

volatile bool fIsRf = true;
uint psiCount;
byte psMinMaxCount = 0;
#define JS_OUTPUT	// prepare easy js import
#define PSI_OVERFLOW 0x0F
#define PS_MICRO_ELEMENTS 15
uint psMicroMin[PS_MICRO_ELEMENTS]; // nibble index, 0x0F is overflow so max 15
uint psMicroMax[PS_MICRO_ELEMENTS]; // nibble index, 0x0F is overflow so max 15
#include <limits.h>
ulong psMicroSum[PS_MICRO_ELEMENTS]; // AVG Sum/SumCount
uint psMicroSumCount[PS_MICRO_ELEMENTS];
#define PS_MERGE
#undef PS_MERGE_DEBUG
#define PS_MINDIFF	50 // value for merge
typedef enum {psixPulse, psixSpace, psixPulseSpace, PSIXNRELEMENTS} psiIx; //
uint psixCount[PS_MICRO_ELEMENTS][PSIXNRELEMENTS]; // index frequency, makes sense to split Pulse/Space to detect signal type...

byte psiNibbles[512]; // psiCount pulseIndex << 4 | spaceIndex
#define NRELEMENTS(a) (sizeof(a) / sizeof(*(a)))

#define psiNibblePulse(psiNibbles, i) (((psiNibbles)[(i)] >> 4) & 0x0F)
#define psiNibbleSpace(psiNibbles, i) (((psiNibbles)[(i)]) & 0x0F)
#define psPulseSpaceNibble(pulse, space) ((((pulse) & 0x0F) << 4) | ((space) & 0x0F))
#define psiNibblePS(psiNibbles, j) (((j) & 1) ? psiNibbleSpace(psiNibbles, (uint)((j) / 2)) : psiNibblePulse(psiNibbles, (uint)((j) / 2)))

uint jDataStart[8];
uint jDataEnd[8];

static void psiPrintChar(byte S) {
	Serial.write(S);
}

static void psiPrintDash(void) {
	psiPrintChar('-');
}

static void psiPrintComma(void) {
	Serial.print(F(", "));
}

static void psiPrintComma(uint x, char c, int digits, int maxVal=0) {
	// Rinie add space for small digits
	if(c) {
		psiPrintChar(c);
	}

	// aling with max value
	for (uint i=1, val=10; i < digits + 2; i++, val *= 10) {
		if (maxVal < val) {
			if (i > digits) {
				digits = i;
			}
			break;
		}
	}
	for (uint i=1, val=10; i < digits; i++, val *= 10) {
		if (x < val) {
			psiPrintChar(' ');
		}
	}

	Serial.print(x,DEC);
}

static void psiPrintNumHex(uint x, char c, uint digits) {
	// Rinie add space for small digits
	if(c) {
		psiPrintChar(c);
	}
	for (uint i=1, val=16; i < digits; i++, val *= 16) {
		if (x < val) {
			psiPrintChar('0');
		}
	}
	Serial.print(x,HEX);
}

static void psiSortMicroMinMax() {
	byte psNewIndex[PS_MICRO_ELEMENTS];
	uint prevMinVal = 0;
	uint prevMaxVal = 0;
	byte jPrev = 0;
	byte mergeCount = 0;

	// psMicroMin/psMicroMax have actual timings
	// store sort in psNewIndex
	for (byte i = 0; i < psMinMaxCount - mergeCount; i++) {
  	    byte j2=0;
  	    uint minVal = EDGE_TIMEOUT; // max
		for (byte j = 0; j < psMinMaxCount; j++) {
			uint curMinVal = psMicroMin[j];
			if ((curMinVal < minVal) && (curMinVal > prevMinVal)) {
				minVal = curMinVal;
				j2 = j;
			}
		}
		psNewIndex[j2] = i;
		prevMaxVal = psMicroMax[j2];
		prevMinVal = psMicroMin[j2];
	}

	// swap sort psMicroMin, psMicroMax and psiCount gaat fout op merge!
	bool fDoneSome;
	do {
		fDoneSome = false;
		for (byte j = 0; j < psMinMaxCount-1; j++) {
			if (psMicroMin[j] > psMicroMin[j+1]) {
				uint psMicroMinTemp = psMicroMin[j];
				psMicroMin[j] = psMicroMin[j+1];
				psMicroMin[j+1] = psMicroMinTemp;

				uint psMicroMaxTemp = psMicroMax[j];
				psMicroMax[j] = psMicroMax[j+1];
				psMicroMax[j+1] = psMicroMaxTemp;

				ulong psMicroSumTemp = psMicroSum[j];
				psMicroSum[j] = psMicroSum[j+1];
				psMicroSum[j+1] = psMicroSumTemp;

				uint psMicroSumCountTemp = psMicroSumCount[j];
				psMicroSumCount[j] = psMicroSumCount[j+1];
				psMicroSumCount[j+1] = psMicroSumCountTemp;

				// enum {psiPulse, psiSpace, psiPulseSpace, PSINRELEMENTS} psiIx; //
				for (uint ix = 0; ix < PSIXNRELEMENTS; ix++) {
					uint psixCountTemp = psixCount[j][ix];
					psixCount[j][ix] = psixCount[j+1][ix];
					psixCount[j+1][ix] = psixCountTemp;
				}
				fDoneSome = true;
			}
		}
	}
	while (fDoneSome);

	// replace index values
	for (uint i=0; i < psiCount; i++) {
		byte pulse = psiNibblePulse(psiNibbles, i);
		byte space = psiNibbleSpace(psiNibbles, i);
		pulse = (pulse < psMinMaxCount) ? psNewIndex[pulse] : pulse;
		space = (space < psMinMaxCount) ? psNewIndex[space] : space;
		psiNibbles[i] = psPulseSpaceNibble(pulse, space);
	}
}

#ifdef PS_MERGE

static void psiMergeMicroMinMax() {
	byte psNewIndex[PS_MICRO_ELEMENTS];
	uint prevMinVal = 0;
	uint prevMaxVal = 0;
	byte jPrev = 0;
	byte mergeCount = 0;

	// psMicroMin/psMicroMax have actual timings
	// store sort in psNewIndex
	psNewIndex[0] = 0;
	for (byte i = 1; i < psMinMaxCount; i++) {
		byte j = i - mergeCount;
		psNewIndex[i] = j;
		if (psMicroMin[i] < (psMicroMax[j-1] + PS_MINDIFF)) {
#ifdef PS_MERGE_DEBUG
			Serial.print(F("Merge["));
			psiPrintComma(psMicroMax[j-1], ' ', 3);
			psiPrintComma(psMicroMin[i], ']', 3);
			psiPrintComma(i, '-', 1);
			psiPrintComma(j-1, ' ', 1);
			Serial.println();
#endif
			psMicroMax[j-1] = psMicroMax[i];

			// Sum may overflow so check
			if (ULONG_MAX - psMicroSum[i] > psMicroSum[j-1]) {
				psMicroSum[j-1] += psMicroSum[i];
				psMicroSumCount[j-1] += psMicroSumCount[i];
			}
			else { // improve this!
				psMicroSum[j-1] = psMicroSum[j-1]/psMicroSumCount[j-1] + psMicroSum[i] / psMicroSumCount[i];
				psMicroSumCount[j-1] = 1;
			}
			// enum {psiPulse, psiSpace, psiPulseSpace, PSINRELEMENTS} psiIx; //
			for (uint ix = 0; ix < PSIXNRELEMENTS; ix++) {
				psixCount[j-1][ix] += psixCount[i][ix];
			}
			psNewIndex[i] = j-1;
			mergeCount++;
		}
		else if (j < i) {
			psMicroMin[j] = psMicroMin[i];
			psMicroMax[j] = psMicroMax[i];

			psMicroSum[j] = psMicroSum[i];
			psMicroSumCount[j] = psMicroSumCount[i];
			// enum {psiPulse, psiSpace, psiPulseSpace, PSINRELEMENTS} psiIx; //
			for (uint ix = 0; ix < PSIXNRELEMENTS; ix++) {
				psixCount[j][ix] = psixCount[i][ix];
			}
		}
	}

	if (mergeCount > 0) {
		// replace index values
		for (uint i=0; i < psiCount; i++) {
			byte pulse = psiNibblePulse(psiNibbles, i);
			byte space = psiNibbleSpace(psiNibbles, i);
			pulse = (pulse < psMinMaxCount) ? psNewIndex[pulse] : pulse;
			space = (space < psMinMaxCount) ? psNewIndex[space] : space;
			psiNibbles[i] = psPulseSpaceNibble(pulse, space);
		}
		psMinMaxCount -= mergeCount;
	}
}
#endif

void psiPrint() {
	// 2 determine per pulse/space/pulse+space what Short/Long timing is. Gap > psiDataLong
	// Short/Long should occur more frequently than GAPS so top 2 of frequency
	uint psiDataShort[PSIXNRELEMENTS];
	uint psiCountDataShort[PSIXNRELEMENTS];

	uint psiDataLong[PSIXNRELEMENTS];
	uint psiCountDataLong[PSIXNRELEMENTS];

	uint psiCountData[PSIXNRELEMENTS];
	uint psiCountDataMin = (fIsRf) ? 16 : 4; // min count for data, max count for Gap
	uint psiCountGapMax;

	for (uint ix = 0; ix < PSIXNRELEMENTS; ix++) {
		// init DataShort/DataLong with 0 and 1 but for psixPulseSpace with max individual values
		psiDataShort[ix] = (ix < psixPulseSpace) ? 0 : max(psiDataShort[psixPulse], psiDataShort[psixSpace]);
		psiCountDataShort[ix] = psixCount[psiDataShort[ix]][ix];
		psiDataLong[ix] = (ix < psixPulseSpace) ? 1 : max(psiDataLong[psixPulse], psiDataLong[psixSpace]);
		psiCountDataLong[ix] = psixCount[psiDataLong[ix]][ix];
		psiCountGapMax = 0;
		for (uint i = psiDataLong[ix] + 1; i < psMinMaxCount; i++) {
			uint psiCount = psixCount[i][ix];
			if (psiCount > psiCountDataLong[ix]) { // new 1st max frequency, new long
				if (psiCountDataLong[ix] > psiCountDataShort[ix]) { // Old Long -> new Short only if occurs more
					psiDataShort[ix] = psiDataLong[ix];
					psiCountDataShort[ix] = psiCountDataLong[ix];
				}
				psiDataLong[ix] = i;
				psiCountDataLong[ix] = psiCount;
				psiCountGapMax = 0;
			}
			else if (psiCount > psiCountDataShort[ix]) { // new 2nd max frequency, shift long->short, long new value
				psiDataShort[ix] = psiDataLong[ix];
				psiCountDataShort[ix] = psiCountDataLong[ix];
				psiDataLong[ix] = i;
				psiCountDataLong[ix] = psiCount;
				psiCountGapMax = 0;
			}
			else if (psiCount > psiCountGapMax) {	// i is GAP index, record max
				psiCountGapMax = psiCount;
			}
		}

		if (ix < psixPulseSpace) {
			psiCountData[ix] = 0;
			for (uint i=0; i < psMinMaxCount; i++) {
				uint psiCount = psixCount[i][ix];
				if (psiCount > psiCountDataMin) {
					psiCountData[ix] += 1;
				}
			}
		}
		else { // psixPulseSpace max of individual values
			psiCountData[ix] = max(psiCountData[psixPulse], psiCountData[psixSpace]);
		}
	}

	// real data should occur more than psiCountGapMax otherwise single data timing
	for (uint ix = 0; ix < PSIXNRELEMENTS-1; ix++) {
		if (psiCountDataLong[ix] < psiCountGapMax) {
				psiDataShort[ix] = psiDataLong[ix];
				psiCountDataLong[ix] += psiCountDataShort[ix];
				psiCountDataShort[ix] = 0;
				psiCountData[ix] = 1;
		}
	}
	uint j = 0;

#if 1
	// 4 preamble/sync/gap lengths
	// Todo: start gap max 16, keep last long
	// End gap max 1, keep last long
	// ? Rely on pulse/space?: not yet
	//Serial.print(F(" Gap Intervals "));

	uint jMaxCount = 0; // likely number of packages
//	uint jPreStart, jStart, jEnd;
	uint jMatchCount = 0; // likely number of packages
	uint jDataMax = 0;
	uint jDataCount = 0;
	uint jMax = (fIsRf) ? 16 : 4; // min package length
	for (uint i=0; i < psiCount; i++, j++) {
		byte pulse = psiNibblePulse(psiNibbles, i);
		byte space = psiNibbleSpace(psiNibbles, i);

		for (uint ix = 0; ix < PSIXNRELEMENTS-1; ix++) {
			byte ps = (ix == psixPulse) ? pulse : space;

			if (ps > psiDataLong[ix]) { // GAP (no data)
				uint jj = j * 2 + ix;
				if (jj > jMax) {
					if (jj > jMax + 4) { // start/end of package may be garbled so allow 4 tolerance
						jMaxCount = 0; // discard old
					}
					jMax = jj;
					jDataMax = jMax;
				}
				if (jj >= jMax - 4) { // start/end of package may be garbled so allow 4 tolerance
					uint ii = i * 2 + ix;
					if (jMaxCount < NRELEMENTS(jDataEnd)) {
						jDataEnd[jMaxCount] = ii;
						jDataStart[jMaxCount] = ii - jj;
						jDataCount = jMaxCount + 1;
					}
					jMaxCount++;
				}
				//j = (jj < 16) ? 1 : 0; // include start GAP ?
				j = 0;
			}
		}
	}

	psiPrintChar('{');
	Serial.println();
	Serial.print((fIsRf) ? F("RF "): F("IR "));
	psiPrintComma(psiCountData[psixPulse], 'P', 1);
	psiPrintComma(psiCountData[psixSpace], 'S', 1);
	psiPrintComma(jMaxCount, '#', 1);
	psiPrintComma((jMax & 1)?jMax - 1:jMax, '*', 2);
	psiPrintChar(':');
	if (jMaxCount > 1) { //assume repeated packages
		uint jDataRepeat = 0;
		uint j = 0;
		// try all packages, print last match incl surrounding gaps, continue at first non match
		// disable for JS analysis
	}
	j = 0;

	Serial.println();
	j = 0;
#endif
#ifdef JS_OUTPUT
	Serial.println(F("`,"));
#endif
	// prepare for js analysis
//	Serial.println();
	Serial.print(F("minMicro: ["));
	psiPrintComma(psMicroMin[0], 0, 3, psMicroMax[0]);
	for (uint i=1; i < psMinMaxCount; i++) {
		psiPrintComma(psMicroMin[i], ',', 3, psMicroMax[i]);
	}
	Serial.println(F("],"));

	Serial.print(F("maxMicro: ["));
	psiPrintComma(psMicroMax[0], 0, 3, psMicroMax[0]);
	for (uint i=1; i < psMinMaxCount; i++) {
		psiPrintComma(psMicroMax[i], ',', 3, psMicroMax[i]);
	}
	Serial.println(F("],"));

	Serial.print(F("avgMicro: ["));
	psiPrintComma(psMicroSum[0] / psMicroSumCount[0] , 0, 3, psMicroMax[0]);
	for (uint i=1; i < psMinMaxCount; i++) {
		psiPrintComma(psMicroSum[i] / psMicroSumCount[i] , ',', 3, psMicroMax[i]);
	}
	Serial.println(F("],"));
	Serial.print(F("Index:    ["));
	psiPrintComma(0, 0, 3, psMicroMax[0]);
	for (uint i=1; i < psMinMaxCount; i++) {
		psiPrintComma(i , ',', 3, psMicroMax[i]);
	}
	Serial.println(F("],"));

#if 1	// pulseCount and spaceCount
	Serial.print(F("pulseCnt: ["));
	psiPrintComma(psixCount[0][psixPulse], 0, 3, psMicroMax[0]);
	for (uint i=1; i < psMinMaxCount; i++) {
			psiPrintComma(psixCount[i][psixPulse], ',', 3, psMicroMax[i]);
	}
	Serial.println(F("],"));

	Serial.print(F("spaceCnt: ["));
	psiPrintComma(psixCount[0][psixSpace], 0, 3, psMicroMax[0]);
	for (uint i=1; i < psMinMaxCount; i++) {
			psiPrintComma(psixCount[i][psixSpace], ',', 3, psMicroMax[i]);
	}
	Serial.println(F("],"));
#endif
	Serial.print(F("ps: "));
	Serial.println();
#ifdef JS_OUTPUT
	Serial.print(F(" '"));
#endif
	for (uint i=0; i < psiCount; i++, j++) {
		byte pulse = psiNibblePulse(psiNibbles, i);
		byte space = psiNibbleSpace(psiNibbles, i);

#if 0	// js disable data rounding
		pulse = ((psiCountData[psixPulse] == 2) && (pulse <= psiDataShort[psixPulse]))
			? 0 : ((pulse <= psiDataLong[psixPulse]) ? 1 : pulse);
		space = ((psiCountData[psixSpace] == 2) && (space <= psiDataShort[psixSpace]))
			? 0 : ((space <= psiDataLong[psixSpace]) ? 1 : space);
#endif
		if ((pulse > psiDataLong[psixPulse]) && ((j > 16))) { // sync pulse
#ifndef JS_OUTPUT
			Serial.println();
#else
			Serial.println(F("\'"));
			Serial.print(F("+'"));
#endif
			j = 0;
		}
		Serial.print(pulse,HEX);
		Serial.print(space,HEX);
		if ((space > psiDataLong[psixSpace]) && ((j > 16))) { // long gap
#ifndef JS_OUTPUT
			Serial.println();
#else
			Serial.println(F("\'"));
			Serial.print(F("+'"));
#endif
			j = 0;
		}
	}
#ifndef JS_OUTPUT
			Serial.println();
#else
			Serial.println(F("\',"));
#endif
	Serial.println(F("},"));
#ifdef JS_OUTPUT
	Serial.println(F("{ comment:`"));
#endif
}

void psiInit(void) {
	psMinMaxCount = 0;
	psiCount = 0;
}

/*
 *	psNibbleIndex
 *
 * Lookup/Store timing of pulse and space in psMicroMin/psMicroMax/psiCount array
 * Could use seperate arrays for pulses and spaces but 15 (0x0F for overflow) seems enough
 */
static byte psNibbleIndex(uint pulse, uint space) {
	byte psNibble = 0;
	uint value = pulse; // pulse, then space...
	for (int j = 0; j < 2; j++) {
		int i = 0;
		if (value > 0) {
			// existing match first
			for (i = 0; i < psMinMaxCount; i++) {
				if ((psMicroMin[i] <= value) && (value <= psMicroMax[i])) {
					psixCount[i][psixPulseSpace]++;
					if (j == 0) {
						psixCount[i][psixPulse]++;
					}
					else {
						psixCount[i][psixSpace]++;
					}
					if (ULONG_MAX - value > psMicroSum[i]) {
						psMicroSum[i] += value;
						psMicroSumCount[i] += 1;
					}
					break;
				}
			}
			if (i >= psMinMaxCount) { // no existing match check within tolerance
				// Either a new length or just outside the current boundaries of a current value
				uint k;
				uint offBy = value;
				// this still sux, occasional spikes give new index. 90% Compensated by data/gap split and value merging
				// uint tolerance = (value < 500) ? 400 : (value < 1000) ? 200 : ((value < 2000) ? 400 : ((value < 5000) ? 600 : 1000));
				// 20180916 was:
				//uint tolerance = (value < 400) ? 300 : (value < 800) ? 400 : (value < 1200) ? 200 : ((value < 2000) ? 400 : ((value < 5000) ? 600 : 2000));
				uint tolerance = (value < 1000) ? 150 : (value < 2000) ? 200 : (value < 3000) ? 300 : ((value < 4000) ? 400 : ((value < 5000) ? 600 : 2000));
				for (k = 0; k < psMinMaxCount; k++) { // determine closest interval
					uint offByi = value;
					if ((value > psMicroMax[k]) && (value <= psMicroMin[k] + tolerance)) { // new max
						offByi = value - psMicroMax[k];
						if (offByi < offBy) {
							i = k;
							offBy = offByi;
						}
					}
					else if ((value < psMicroMin[k]) && (value + tolerance >= psMicroMax[k])) { // new min?
						offByi = psMicroMin[k] - value;
						if (offByi < offBy) {
							i = k;
							offBy = offByi;
						}
					}
				}
				if (i < psMinMaxCount) { // existing match
					if (value < psMicroMin[i]) { // new min
						psMicroMin[i] = value;
					}
					else if (value > psMicroMax[i]) { // new max
						psMicroMax[i] = value;
					}
					if ((ULONG_MAX - value) > psMicroSum[i]) {
						psMicroSum[i] += value;
						psMicroSumCount[i] += 1;
					}
					if (j == 0) {
						psixCount[i][psixPulse]++;
					}
					else {
						psixCount[i][psixSpace]++;
					}
					psixCount[i][psixPulseSpace]++;
				}
				else {
					i = psMinMaxCount; // no match
				}
			}
			if (i >= psMinMaxCount && i < PSI_OVERFLOW) { // new value
				if (i < PS_MICRO_ELEMENTS) {
					psMinMaxCount++;
					psMicroMin[i] = value;
					psMicroMax[i] = value;
					psMicroSum[i] = value;
					psMicroSumCount[i] = 1;
					if (j == 0) {
						psixCount[i][psixPulse] = 1;
						psixCount[i][psixSpace] = 0;
					}
					else {
						psixCount[i][psixPulse] = 0;
						psixCount[i][psixSpace] = 1;
					}
					psixCount[i][psixPulseSpace] = 1;
				}
				else {
					i = PSI_OVERFLOW; // overflow
				}
			}
		}
		else {
			i = PSI_OVERFLOW; //invalid data
		}
		//psNibble = psPulseSpaceNibble(psNibble, i);
		psNibble = ((psNibble & 0x0F) << 4) | (i & 0x0F);
		value = space;
	}
	return psNibble;
}

#if 1 // Rinie get to know code
	static volatile uint16_t psCount = 0;
	static uint32_t startSignal;
	static uint32_t startSignalm;
	static uint32_t lastSignal = 0;
	static bool fCheckClear = false;
#endif

#define SIGNAL_TIMEOUT_RF      EDGE_TIMEOUT
#define SIGNAL_TIMEOUT_IR      10000 // Nodo Due Timing

/*
 * return micros psCount should not increase
 * to assume end of signal
 *
 */
uint psiNoChangeTimeout(void){
	uint max;
	if (psCount < 16) {
		return (fIsRf) ? SIGNAL_TIMEOUT_RF : SIGNAL_TIMEOUT_IR;
	}
#if 0
	max = psMicroMax[0];
	for (byte i = 1; i < psMinMaxCount; i++) {
		if (psMicroMax[i] > max) {
			max = psMicroMax[i];
		}
	}
	return max * 32;
#else
	return EDGE_TIMEOUT;
#endif
}

//#include "analysepacket.h"
static void psiFinish() {
	if ((psCount > 48 && fIsRf) || (psCount > 16 && !fIsRf)) {
		uint32_t now = millis();
		uint32_t nowm = micros();
#ifndef NODO_DUE
		printRSSI();
#endif
		/* if (printRSSI()) */
		{
			//Serial.println();

			psiSortMicroMinMax();
#ifdef PS_MERGE
#ifdef PS_MERGE_DEBUG
			if (fIsRf) {
				psiPrint();
			}
#endif
			if (fIsRf) {
				psiMergeMicroMinMax();
			}
#endif
			psiPrint();
			lastSignal = millis();
		}
	}
	psCount = 0;
	psiInit();
}

/*
 * psiAddPS
 *
 * Interface to external code for measuring pulse/space lengths
 * calls psNibbleIndex(pulseTime, spaceTime) to compute pulse/space nibble index
 */
bool psiAddPS(uint16_t pulse_dur, uint8_t signal, uint8_t rssi) {
	static uint lastPulseDur = 0;
	 //add (possibly garbled) first pulse/space pair last...
	static uint firstPulseDur = 0;
	static uint firstSpaceDur = 0;
	if (pulse_dur > 1) {
		if ((pulse_dur > 75) && (pulse_dur < EDGE_TIMEOUT)){
			if (psCount == 0) {
				startSignal = millis();
				startSignalm = micros();
				if (!lastSignal) {
					lastSignal = startSignal;
				}
				psiInit();
			}
			if (psCount & 1) {	// Odd means pulse and space, so pulse_dur is space
				if (psiCount < NRELEMENTS(psiNibbles)) {
					if (psCount <= 1) { // first timing can be partial noise
							firstPulseDur = lastPulseDur;
							firstSpaceDur = pulse_dur;
							psiCount = 1;
					}
					else {
						psiNibbles[psiCount++] = psNibbleIndex(lastPulseDur, pulse_dur);
					}
					if (psiCount >=  NRELEMENTS(psiNibbles)) {
						psiNibbles[0] = psNibbleIndex(firstPulseDur, firstSpaceDur);
						psiFinish();
						return false;
					}
				}
				else {
					psiNibbles[0] = psNibbleIndex(firstPulseDur, firstSpaceDur);
					psiFinish();
					return false;
				}
			}
			else {
				lastPulseDur = pulse_dur;
			}
			psCount++;
		}
	}
	if ((!rssi) && (pulse_dur == 1)) { // footer, fake pulse, print and reset
		psiNibbles[0] = psNibbleIndex(firstPulseDur, firstSpaceDur);
		psiFinish();
	}
	return false; // return true to skip decoders...
}
#endif // __PULSESPACEINDEX_H__

