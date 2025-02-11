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

 #ifndef synth_wavetable_256_h_
 #define synth_wavetable_256_h_
 
 #include <Arduino.h>
 #include "AudioStream.h"
 #include "arm_math.h"
 
 // waveforms.c
 extern "C" {
 extern const int16_t AudioWaveformSine[257];
 }
 
 
 #define WAVEFORM_ARBITRARY         4
 
 
 typedef struct step_state256
 {
   int offset ;
   bool positive ;
 } step_state256 ;
 
 

 class BandLimitedWaveform256
 {
 public:
   BandLimitedWaveform256 (void) ;
 
 private:
   int32_t lookup (int offset) ;
   void insert_step (int offset, bool rising, int i) ;
   int32_t process_step (int i) ;
   int32_t process_active_steps (uint32_t new_phase) ;
   int32_t process_active_steps_saw (uint32_t new_phase) ;
   int32_t process_active_steps_pulse (uint32_t new_phase, uint32_t pulse_width) ;
 
   
   uint32_t phase_word ;
   int32_t dc_offset ;
   step_state256 states [32] ; // circular buffer of active steps
   int newptr ;         // buffer pointers into states, AND'd with PTRMASK to keep in buffer range.
   int delptr ;
   int32_t  cyclic[16] ;    // circular buffer of output samples
   bool pulse_state ;
   uint32_t sampled_width ; // pulse width is sampled once per waveform
 };
 
 
 
 class AudioWavetable256 : public AudioStream
 {
 public:
	 AudioWavetable256(void) : AudioStream(0,NULL),
		 phase_accumulator(0), phase_increment(0), phase_offset(0),
		 magnitude(0), pulse_width(0x40000000),
		 arbdata1(AudioWaveformSine), arbdata2(AudioWaveformSine),
		 sample(0),
		 tone_offset(0) {
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
	 void pulseWidth(float n) {	// 0.0 to 1.0
		 if (n < 0) {
			 n = 0;
		 } else if (n > 1.0f) {
			 n = 1.0f;
		 }
		 pulse_width = n * 4294967296.0f;
	 }
	 void begin(short t_type) {
		 phase_offset = 0;
		 tone_type = t_type;
	 }
	 void begin(float t_amp, float t_freq, short t_type) {
		 amplitude(t_amp);
		 frequency(t_freq);
		 phase_offset = 0;
		 begin (t_type);
	 }
	 void arbitraryWaveform(const int16_t *data, float maxFreq) {
		 arbdata1 = data;
	 }
	 void arbitraryWaveforms(const int16_t **data, const uint16_t num) {
		if(num <= 0) return;
		if(arbdata1==NULL) arbdata1 = data[0];
		if(arbdata2==NULL) arbdata2 = data[0];
		waveTables = data;
		waveTableLength = num;
		H = 65535/waveTableLength;
	 }
	 virtual void update(void);
 
 private:
	 uint32_t phase_accumulator;
	 uint32_t phase_increment;
	 uint32_t phase_offset;
	 int32_t  magnitude;
	 uint32_t pulse_width;
	 const int16_t *arbdata1;
	 const int16_t *arbdata2;
	 int16_t  sample; // for WAVEFORM_SAMPLE_HOLD
	 short    tone_type;
	 int16_t  tone_offset;
	BandLimitedWaveform256 band_limit_waveform ;

	uint16_t morph;
	uint32_t waveTableLength;
	const int16_t ** waveTables;
	uint16_t H;
 };
 
 
 class AudioWavetable256Modulated : public AudioStream
 {
 public:
	 AudioWavetable256Modulated(void) : AudioStream(2, inputQueueArray),
		 phase_accumulator(0), phase_increment(0), modulation_factor(32768),
		 magnitude(0), arbdata1(AudioWaveformSine), sample(0), tone_offset(0), 
		 modulation_type(0) {
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
	 void arbitraryWaveform(const int16_t *data, float maxFreq) {
		 arbdata1 = data;
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
	 virtual void update(void);
 
 private:
	 audio_block_t *inputQueueArray[2];
	 uint32_t phase_accumulator;
	 uint32_t phase_increment;
	 uint32_t modulation_factor;
	 int32_t  magnitude;
	 const int16_t *arbdata1;
	 uint32_t phasedata[AUDIO_BLOCK_SAMPLES];
	 int16_t  sample; // for WAVEFORM_SAMPLE_HOLD
	 int16_t  tone_offset;
	 uint8_t  tone_type;
	 uint8_t  modulation_type;
	BandLimitedWaveform256 band_limit_waveform ;

	uint16_t morph;
	uint32_t waveTableLength;
	const int16_t ** waveTables;
	uint16_t H;
 };
 
 
 #endif