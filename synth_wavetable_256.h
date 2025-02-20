/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

 #ifndef synth_waveform256_h_
 #define synth_waveform256_h_
 
 #include <Arduino.h>
 #include "AudioStream.h"
 #include "arm_math.h"
 
 // waveforms.c
 extern "C" {
 extern const int16_t AudioWaveformSine[257];
 }
 
 class AudioWavetable256 : public AudioStream
 {
 public:
	 AudioWavetable256(void) : AudioStream(1,inputQueueArray),
		 phase_accumulator(0), phase_increment(0), phase_offset(0),
		 magnitude(0), modMagnitude(0), wave_tables(NULL), tone_offset(0), scanw(0) {
	 }
 
	 void frequency(float freq) {
		 if (freq < 0.0f) {
			 freq = 0.0;
		 } else if (freq > AUDIO_SAMPLE_RATE_EXACT / 2.0f) {
			 freq = AUDIO_SAMPLE_RATE_EXACT / 2.0f;
		 }
		 phase_increment = freq * (4294967296.0f / AUDIO_SAMPLE_RATE_EXACT);
		 if (phase_increment > 0x7FFE0000u) phase_increment = 0x7FFE0000;
	 }
	 void phase(float angle) {
		 if (angle < 0.0f) {
			 angle = 0.0;
		 } else if (angle > 360.0f) {
			 angle = angle - 360.0f;
			 if (angle >= 360.0f) return;
		 }
		 phase_offset = angle * (float)(4294967296.0 / 360.0);
	 }
	 void amplitude(float n) {	// 0 to 1.0
		 if (n < 0) {
			 n = 0;
		 } else if (n > 1.0f) {
			 n = 1.0;
		 }
		 magnitude = n * 65536.0f;
	 }
	 void offset(float n) {
		 if (n < -1.0f) {
			 n = -1.0f;
		 } else if (n > 1.0f) {
			 n = 1.0f;
		 }
		 tone_offset = n * 32767.0f;
	 }
	 void begin(short t_type) {
		 phase_offset = 0;
	 }
	 void begin(float t_amp, float t_freq, short t_type) {
		 amplitude(t_amp);
		 frequency(t_freq);
		 phase_offset = 0;
		 begin (t_type);
	 }

	 void modAmplitude(float n) {
		 if (n < 0) {
			 n = 0;
		 } else if (n > 1.0f) {
			 n = 1.0;
		 }
		 modMagnitude = n * 2147483647.0f;
	 }
	 void arbitraryWaveforms(int16_t *data[256], const int32_t num) {
		if(num != 256) return;
		wave_tables = data;
	 }
	 
	 void waveSelect(float s) {
		 if(s > 1.f) {
			 s = 1.f;
		 }
		 scanw = s * 4294967295.f;
	 }
	 
	 virtual void update(void);

 private:
 	audio_block_t *inputQueueArray[1];
	 uint32_t phase_accumulator;
	 uint32_t phase_increment;
	 uint32_t phase_offset;
	 int32_t  magnitude;
	 int32_t  modMagnitude;
	 const int16_t **wave_tables;
	 uint32_t scanw;
	 int16_t  tone_offset;
 };
 
 class AudioWavetable256Modulated : public AudioStream
 {
 public:
	 AudioWavetable256Modulated(void) : AudioStream(2, inputQueueArray),
		 phase_accumulator(0), phase_increment(0), 
		 //phase_offset(0), //TODO: added, needed I think but not in org impl. 
		 modulation_factor(32768),
		 magnitude(0), modMagnitude(0), wave_tables(NULL), tone_offset(0),
		 scanw(0), modulation_type(0) {
	 }
 
	 void frequency(float freq) {
		 if (freq < 0.0f) {
			 freq = 0.0;
		 } else if (freq > AUDIO_SAMPLE_RATE_EXACT / 2.0f) {
			 freq = AUDIO_SAMPLE_RATE_EXACT / 2.0f;
		 }
		 phase_increment = freq * (4294967296.0f / AUDIO_SAMPLE_RATE_EXACT);
		 if (phase_increment > 0x7FFE0000u) phase_increment = 0x7FFE0000;
	 }
	 void amplitude(float n) {	// 0 to 1.0
		 if (n < 0) {
			 n = 0;
		 } else if (n > 1.0f) {
			 n = 1.0f;
		 }
		 magnitude = n * 65536.0f;
	 }
	 void offset(float n) {
		 if (n < -1.0f) {
			 n = -1.0f;
		 } else if (n > 1.0f) {
			 n = 1.0f;
		 }
		 tone_offset = n * 32767.0f;
	 }
	 void begin(short t_type) {
		 tone_type = t_type;
	 }
	 void begin(float t_amp, float t_freq, short t_type) {
		 amplitude(t_amp);
		 frequency(t_freq);
		 begin (t_type) ;
	 }
	 void frequencyModulation(float octaves) {
		 if (octaves > 12.0f) {
			 octaves = 12.0f;
		 } else if (octaves < 0.1f) {
			 octaves = 0.1f;
		 }
		 modulation_factor = octaves * 4096.0f;
		 modulation_type = 0;
	 }
	 void phaseModulation(float degrees) {
		 if (degrees > 9000.0f) {
			 degrees = 9000.0f;
		 } else if (degrees < 30.0f) {
			 degrees = 30.0f;
		 }
		 modulation_factor = degrees * (float)(65536.0 / 180.0);
		 modulation_type = 1;
	 }

	 void modAmplitude(float n) {
		 if (n < 0) {
			 n = 0;
		 } else if (n > 1.0f) {
			 n = 1.0;
		 }
		 modMagnitude = n * 2147483647.0f;
	 }
	 void arbitraryWaveforms(int16_t *data[256], const int32_t num) {
		if(num != 256) return;
		wave_tables = data;
	 }
	 
	 void waveSelect(float s) {
		 if(s > 1.f) {
			 s = 1.f;
		 }
		 scanw = s * 4294967295.f;
	 }
	 
	 virtual void update(void);

	 int32_t debugFlag;
 
 private:
	 audio_block_t *inputQueueArray[2];
	 uint32_t phase_accumulator;
	 uint32_t phase_increment;
	 uint32_t phase_offset;
	 uint32_t modulation_factor;
	 int32_t  magnitude;
	 int32_t  modMagnitude;
	 const int16_t **wave_tables;
	 uint32_t phasedata[AUDIO_BLOCK_SAMPLES];
	 uint32_t scanw;
	 int16_t  tone_offset;
	 uint8_t  tone_type;
	 uint8_t  modulation_type;
 };

 #endif
 