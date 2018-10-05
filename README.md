# PulseSpaceIndexRfIr
 General OOK decoding without knowing the protocol before hand.
 Receive OOK data using Arduino and Nodo layout:
  SERIAL_BAUD 57600
  RF_ReceiveDataPin           2  // Input of OOK 433Mhz-RF signal. LOW (Off): no signal
  IR_ReceiveDataPin           3  // Input of IR signal TSOP. HIGH: no signal

 Not yet:
 RF_TransmitDataPin          5  // Output of OOK 433Mhz-R
 IR_TransmitDataPin         11  // Output IR-Led transmitter

 See also PulseSpaceIndex node.js ES6 for analyzing OOK 433 and RF signals

 General OOK decoding without knowing the protocol before hand.

 A lot of opensource software/hardware OOK decoding solutions
 disregard the fact that commercial solutions do work with cheap receivers
 and without lowpass filters or such.

 IMHO this stems from the fact that the protocols are designed to overcome
 the limitations of these receivers:
 - Add a preamble or Sync to give the AGC time to adjust.
 - Repeat the signal at least 3 times, so that the 2nd and 3rd are received
   correctly with a tuned AGC and compared for identical packages.
 - Use a weak checksum/CRC as the computing power is limited (optimized for low power,
   sending from a battery operated sensor).
 - Use a simple encoding so typically 1 Pulse(On)/Space(Off) time combination for '1'
   and 1 Pulse/Space time combination for '0' is used.
	 Preamble, Gap or Sync use clearly distinctive timings or standard timings repeated often.

 Using these properties instead of a CRC checksum on the individual packages,
 I try to guess from the relative timings of the signal what encoding is used,
 and where the gap between the packages occur:
 - GAP should be clear enough to capture package 2 and 3 reliable,
   but should be small enough to keep AGC correct.
 - Few time variations can be stored as index instead of exact timepulse.

 Change sort to sort merge?

 Test with
	ORSV2
		200..1200 split on 700
		Manchester encoding, 160 bits, Short pulse within first 32 bits, start bit

		http://wmrx00.sourceforge.net/Arduino/OregonScientific-RF-Protocols.pdf

		For version 2.1 sensors only, each data bit is actually sent four times. This is
		accomplished by first sending each data bit twice (inverted the first time),
		doubling the size of the original message. A one bit is sent as a “01” sequence
		and a zero bit is sent as “10”. Secondly, the entire message is repeated once.
		 Oregon Scientific RF Protocols
		 Page 2 of 23
		Some sensors will insert a gap (about 10.9 msec for the THGR122NX) between
		the two repetitions, while others (e.g. UVR128) have no gap at all.

		Both 2.1 and 3.0 protocols have a similar message structure containing four
		parts.
		1. The preamble consists of “1” bits, 24 bits (6 nibbles) for v3.0 sensors
		and 16 bits (4 nibbles) for v2.1 sensors (since a v2.1 sensor bit stream
		contains an inverted and interleaved copy of the data bits, there is in
		fact a 32 bit sequence of alternating “0” and “1” bits in the preamble).
		2. A sync nibble (4-bits) which is “0101” in the order of transmission. With
		v2.1 sensors this actually appears as “10011001”. Since nibbles are sent
		LSB first, the preamble nibble is actually “1010” or a hexadecimal “A”.
		3. The sensor data payload, which is described in the “Message Formats”
		section below.
		4. A post-amble (usually) containing two nibbles, the purpose or format of
		which is not understood at this time. At least one sensor (THR238NF)
		sends a 5-nibble post-amble where the last four nibbles are all zero.
		The number of bits in each message is sensor-dependent. The duration of most
		v3.0 messages is about 100msec. Since each bit is doubled in v2.1 messages,
		and each v2.1 message is repeated once in its entirety, these messages last
		about four times as long, or 400msec.

  	KAKU
  		NodeDue: Encoding volgens Princeton PT2262 / MOSDESIGN M3EB / Domia Lite spec.
  		Pulse (T) is 350us PWDM
  		0 = T,3T,T,3T, 1 = T,3T,3T,T, short 0 = T,3T,T,T

			P2S2 encoding 0 = 0101, 1 = 0110, short = 0100

 		Timings theoretical: 350, 1050

		Jeelabs: Split 700, Gap 2500

	KAKUA/KAKUNEW:
		NodeDue: Encoding volgens Arduino Home Easy pagina
		Pulse (T) is 275us PDM
		0 = T,T,T,4T, 1 = T,4T,T,T, dim = T,T,T,T op bit 27
		8T // us, Tijd van de space na de startbit
		Timings theoretical: 275, 1100, 2200

		Jeelabs: Split 700, 1900, Max 3000
		P1S2 encoding 0 = 01, 1 = 10 and dim = 00

	XRF / X10:
		http://davehouston.net/rf.htm

 	WS249 plant humidity sensor.
		SevenWatt: Normal signals between 170 and 2600. Sync (space after high( between 5400 and 6100))
		Split 1600, 64..66 pulse/spaces

 RcSwitch timing definitions:
  Format for protocol definitions:
  {pulselength, Sync bit, "0" bit, "1" bit}

  pulselength: pulse length in microseconds, e.g. 350
  Sync bit: {1, 31} means 1 high pulse and 31 low pulses
      (perceived as a 31*pulselength long pulse, total length of sync bit is
      32*pulselength microseconds), i.e:
       _
      | |_______________________________ (don't count the vertical bars)
  "0" bit: waveform for a data bit of value "0", {1, 3} means 1 high pulse
      and 3 low pulses, total length (1+3)*pulselength, i.e:
       _
      | |___
  "1" bit: waveform for a data bit of value "1", e.g. {3,1}:
       ___
      |   |_

  These are combined to form Tri-State bits when sending or receiving codes.

   { 350, {  1, 31 }, {  1,  3 }, {  3,  1 }, false },    // protocol 1
   { 650, {  1, 10 }, {  1,  2 }, {  2,  1 }, false },    // protocol 2
   { 100, { 30, 71 }, {  4, 11 }, {  9,  6 }, false },    // protocol 3
   { 380, {  1,  6 }, {  1,  3 }, {  3,  1 }, false },    // protocol 4
   { 500, {  6, 14 }, {  1,  2 }, {  2,  1 }, false },    // protocol 5
   { 450, { 23,  1 }, {  1,  2 }, {  2,  1 }, true }      // protocol 6 (HT6P20B)
   RKR: OR 450, {1, 23}, {2, 1}, {1, 2} not inverted?
   Gap after message > space time  of sync
