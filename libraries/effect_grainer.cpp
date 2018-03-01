/*
 * effect_pitchshifter.cpp
 *
 *  Created on: 10 feb. 2018
 *      Author: niksan
 */

#include "effect_grainer.h"

bool AudioEffectGrainer::fillAudioBuffer() {
	if (audioBuffer.freeze)
		return true;
	uint16_t head = audioBuffer.head;
	uint16_t tail = audioBuffer.tail;
	uint16_t len = audioBuffer.len;
	audio_block_struct ** data = audioBuffer.data;

	if (++head >= len)
		head = 0;
	if (head == tail) {
		if (data[tail] != NULL) {
			release(data[tail]);
			data[tail] = NULL;
		}
		if (++tail >= len)
			tail = 0;
	}
	data[head] = receiveReadOnly();
	if (!data[head])
		return false;

	audioBuffer.head = head;
	audioBuffer.tail = tail;

	//queue needs to be full before dsp
	if (head == 0)
		audioBuffer.isFilled = true;

	return audioBuffer.isFilled;
}

void GrainParameter::pitch(float p) {
	if (p > 1.0)
		p = 1.0;
	//TODO: IMPL!!!
}

void GrainParameter::durration(float ms) {
	uint16_t blocks = ms2block(ms);
	if (blocks < 2)
		blocks = 2;
	else if (blocks > queue->len - 1)
		blocks = queue->len - 1;
	if (sender.fade > blocks)
		sender.fade = blocks;
	sender.size = blocks;
}

void GrainParameter::pos(float ms) {
	uint16_t blocks = ms2block(ms);
	if (blocks > queue->len - 1)
		blocks = queue->len - 1;
	sender.start = blocks;
}

void GrainParameter::space(float ms) {
	uint16_t blocks = ms2block(ms);
	sender.space = blocks;
}

void GrainParameter::fade(float ms) {
	uint16_t blocks = ms2block(ms);
	if (blocks > sender.size)
		blocks = sender.size;
	sender.fade = blocks;
}

void GrainParameter::amplitude(float n) {
	if (n < 0) n = 0;
	else if (n > 1.0) n = 1.0;
	sender.magnitude = n * 65536.0;
}

void AudioEffectGrainer::setBlock(audio_block_struct * out, GrainStruct* pGrain) {

	uint16_t blockPos = pGrain->start;
	uint16_t state = pGrain->state;

	if (state == GRAIN_TRIG) {
		if (blockPos <= audioBuffer.head)
			blockPos = audioBuffer.head - blockPos;
		else
			blockPos = (audioBuffer.len + audioBuffer.head) - blockPos;
		state = GRAIN_PLAYING;
	} else {
		blockPos = pGrain->pos;
	}

	if (window != NULL && pGrain->sizePos < (pGrain->size << 1)) {

		uint16_t fadePhaseIncr = (0x8000 / pGrain->size); //TODO: move into GrainStruct
		uint32_t totalSample = pGrain->sizePos * AUDIO_BLOCK_SAMPLES;
		uint16_t windowSampleIndex = totalSample / ((int32_t) pGrain->size); //TODO: opt!!!
		uint16_t windowSampleOffset = totalSample % ((int32_t) pGrain->size); //TODO: opt!!!
		windowSampleIndex += (windowSampleOffset != 0);

		audio_block_t * in = audioBuffer.data[blockPos];
		int16_t * dst = out->data;
		const int16_t * inputSrc = in->data;
		const int16_t * windowSrc = window + windowSampleIndex;
		int16_t * end = dst + AUDIO_BLOCK_SAMPLES;

		while (dst < end) {
			int16_t windowSample;

			if (windowSampleOffset == 0)
			{
				windowSample = *windowSrc++;
				++windowSampleIndex;
			}
			else
			{
				int32_t wPrev = *(windowSrc - 1);
				int32_t w = (windowSampleIndex >= WINDOW_SIZE) ? *window : *windowSrc;
				int32_t F = windowSampleOffset * ((int32_t) fadePhaseIncr);
				int32_t val1 = wPrev * (0x8000 - F);
				int32_t val2 = w * F;
				windowSample = signed_saturate_rshift( val1 + val2 , 16, 15);
			}

			if (++windowSampleOffset > pGrain->size - 1)
				windowSampleOffset = 0;

			int32_t inputSample = *inputSrc++;
			*dst++ += multiply_32x32_rshift32(
							multiply_16bx16b(inputSample, windowSample),
					pGrain->magnitude );
		}

	} else {
		pGrain->state = GRAIN_ENDED;
		return;
	}

	//Over window size. We want overlap!!!
	if (pGrain->sizePos >= pGrain->size) {
		state |= GRAIN_OVERLAP;
	}

	//update grain to next block set.
	blockPos = (1 + blockPos) % audioBuffer.len;
	pGrain->pos = blockPos;
	pGrain->sizePos += 1;
	pGrain->state = state;
}

void AudioEffectGrainer::update() {
	//Wait until audio buffer is full.
	if (!fillAudioBuffer()) return;
	//Allocate output data.
	audio_block_t * out = AudioStream::allocate();
	if (!out) return;
	memset(out->data, 0, sizeof(out->data));

	//Get next free grain if no grains is playing.
	if ( (! playGrain ) && freeGrain) {
		playGrain = freeGrain;
		freeGrain = freeGrain->next;
		playGrain->next = NULL;
		//TODO: WHY DO WE NEED TO DO THIS!!!!
		playGrain->state = GRAIN_AVAILABLE;
	}

	GrainStruct * grain = playGrain;
	GrainStruct * prev = NULL;

	DEBUG_GRAIN(0,grain);

	//Start grain DSP.
	while (grain != NULL)
	{
		//Fetch grain parameter if grain is new.
		if (grain->state == GRAIN_AVAILABLE)
		{
			DEBUG_GRAIN(10,grain);
			grainParam.resive(*grain);
			grain->state = GRAIN_TRIG;
		}

		setBlock(out, grain);

		if (grain->state == GRAIN_ENDED) {

			DEBUG_GRAIN(1,grain);

			//No overlaps => reuse in next iteration.
			if (playGrain == grain && grain->next == NULL) {
				grain->state = GRAIN_AVAILABLE;
				continue;
			}

			GrainStruct * free = grain;

			if (playGrain == grain) grain = playGrain = grain->next;
			else if (prev != NULL) grain = prev->next = grain->next;
			else grain = grain->next;

			free->next = freeGrain;
			free->state = GRAIN_AVAILABLE;
			freeGrain = free;

		}
		else {

			DEBUG_GRAIN(2,grain);

			if( ( grain->state & GRAIN_OVERLAP ) && (! grain->next) && freeGrain )
			{
				DEBUG_GRAIN(3,grain);
				GrainStruct * overlapGrain = freeGrain;
				freeGrain = freeGrain->next;
				overlapGrain->next = NULL;
				overlapGrain->state = GRAIN_AVAILABLE;
				grain->next = overlapGrain;
			}
			prev = grain;
			grain = grain->next;
		}
	}

	transmit(out);
	release(out);
}

void AudioEffectGrainer::queueLength(uint16_t l) {
	if (l > GRAIN_BLOCK_QUEUE_SIZE)
		audioBuffer.len = GRAIN_BLOCK_QUEUE_SIZE;
	else
		audioBuffer.len = l;
}

void AudioEffectGrainer::freezer(bool f) {
	audioBuffer.freeze = f;
}

GrainParameter * AudioEffectGrainer::next() {
	grainParam.init(&audioBuffer);
	return &grainParam;
}

AudioEffectGrainer::AudioEffectGrainer() :
		AudioStream(1, inputQueueArray), window(AudioWindowHamming256) {
	int i = GRAINS_MAX_NUM - 1;
	grains[i].next = NULL;
	grains[i].state = GRAIN_AVAILABLE;
	for (; i > 0; --i) {
		grains[i - 1].next = &(grains[i]);
		grains[i].state = GRAIN_AVAILABLE;
	}
	freeGrain = &(grains[0]);
	playGrain = NULL;
}
