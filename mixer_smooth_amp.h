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

#ifndef mixer_smooth_amp_h_
#define mixer_smooth_amp_h_

#include "Arduino.h"
#include "AudioStream.h"
#include "ns_utility.h"

#define SmoothInt32 KnobsFilter<int32_t,67108856>

class AudioMixer4Smooth : public AudioStream
{
#if defined(__ARM_ARCH_7EM__)
public:
	AudioMixer4Smooth(void) : AudioStream(4, inputQueueArray) {
		for (int i=0; i<4; i++) multiplier[i] = 65536;
		for (int i=0; i<4; i++) dis[i] = true;
	}
	virtual void update(void);
	void gain(unsigned int channel, float gain) {
		if (channel >= 4) return;
		if (gain > 32767.0f) gain = 32767.0f;
		else if (gain < -32767.0f) gain = -32767.0f;
		multiplier[channel] = gain * 65536.0f; // TODO: proper roundoff?
	}
	void enable(unsigned int channel, boolean d) {
		if (channel >= 4) return;
		dis[channel] = !d;
	}
private:
	SmoothInt32 multiplier[4];
	boolean dis[4];
	audio_block_t *inputQueueArray[4];

#elif defined(KINETISL)
public:
	AudioMixer4Smooth(void) : AudioStream(4, inputQueueArray) {
		for (int i=0; i<4; i++) multiplier[i] = 256;
	}
	virtual void update(void);
	void gain(unsigned int channel, float gain) {
		if (channel >= 4) return;
		if (gain > 127.0f) gain = 127.0f;
		else if (gain < -127.0f) gain = -127.0f;
		multiplier[channel] = gain * 256.0f; // TODO: proper roundoff?
	}
private:
	int16_t multiplier[4];
	audio_block_t *inputQueueArray[4];
#endif
};

class AudioAmplifierSmooth : public AudioStream
{
public:
	AudioAmplifierSmooth(void) : AudioStream(1, inputQueueArray), multiplier(65536) {
	}
	virtual void update(void);
	void gain(float n) {
		if (n > 32767.0f) n = 32767.0f;
		else if (n < -32767.0f) n = -32767.0f;
		multiplier = n * 65536.0f;
	}
private:
	SmoothInt32 multiplier;
	audio_block_t *inputQueueArray[1];
};

#endif
