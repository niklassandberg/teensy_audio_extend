/*
#ifndef synth_wavetable_256_h_
#define synth_wavetable_256_h_

#include <Arduino.h>
#include "AudioStream.h"
#include "arm_math.h"

class AudioWavetable256 : public AudioStream
{
public:
	AudioWavetable256(void) : AudioStream(1,inputQueueArray),
		phase_accumulator(0), phase_increment(0), phase_offset(0),
		magnitude(0), pulse_width(0x40000000),
		arbdata1(NULL), arbdata2(NULL), sample(0),
		tone_offset(0), morph(0), waveTables(NULL), waveTableLength(0), H(0) {
			debugFlag = 0;
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
   void morphing(float m) {
	   morph = uint16_t(m*65535.F);
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
	
	void begin(float t_amp, float t_freq, float morph) {
		amplitude(t_amp);
		frequency(t_freq);
		morphing(morph);
		phase_offset = 0;
	}
	void arbitraryWaveforms(const int16_t **data, const uint16_t num) {
	   if(num <= 0) return;
	   if(arbdata1==NULL) arbdata1 = data[0];
	   if(arbdata2==NULL) arbdata2 = data[0];
	   waveTables = data;
	   waveTableLength = num;
	   H = 65535/waveTableLength;
	}

	void wave(float wave) {
	   morph = 0.5f + wave * 65535.f;
	}

	int debugFlag = 0;

private:

   virtual void update(void);

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

	uint16_t morph;
	uint32_t waveTableLength;
	const int16_t ** waveTables;
	uint16_t H;

	
   audio_block_t *inputQueueArray[1];
};

#endif // AUDIO_WAVETABLE256_H
*/