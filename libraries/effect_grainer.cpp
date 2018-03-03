/*
 * effect_pitchshifter.cpp
 *
 *  Created on: 10 feb. 2018
 *      Author: niksan
 */

#include "effect_grainer.h"

bool AudioEffectGrainer::fillAudioBuffer()
{
	if (mAudioBuffer.freeze)
		return true;
	uint16_t head = mAudioBuffer.head;
	uint16_t tail = mAudioBuffer.tail;
	uint16_t len = mAudioBuffer.len;
	audio_block_struct ** data = mAudioBuffer.data;

	if (++head >= len)
		head = 0;
	if (head == tail)
	{
		if (data[tail] != NULL)
		{
			release(data[tail]);
			data[tail] = NULL;
		}
		if (++tail >= len)
			tail = 0;
	}
	data[head] = receiveReadOnly();
	if (!data[head])
		return false;

	mAudioBuffer.head = head;
	mAudioBuffer.tail = tail;

	//queue needs to be full before dsp
	if (head == 0)
		mAudioBuffer.isFilled = true;

	return mAudioBuffer.isFilled;
}

void GrainParameter::pitch(float p)
{
	if (p > 1.0)
		p = 1.0;
	//TODO: IMPL!!!
}

void GrainParameter::durration(float ms)
{
	uint16_t blocks = ms2block(ms);
	if (blocks < 2)
		blocks = 2;
	else if (blocks > mAudioBuffer->len - 1)
		blocks = mAudioBuffer->len - 1;
	if (mSender.fade > blocks)
		mSender.fade = blocks;
	mSender.size = blocks;
}

void GrainParameter::pos(float ms)
{
	uint16_t blocks = ms2block(ms);
	if (blocks > mAudioBuffer->len - 1)
		blocks = mAudioBuffer->len - 1;
	mSender.start = blocks;
}

void GrainParameter::space(float ms)
{
	uint16_t blocks = ms2block(ms);
	mSender.space = blocks;
}

void GrainParameter::fade(float ms)
{
	uint16_t blocks = ms2block(ms);
	if (blocks > mSender.size)
		blocks = mSender.size;
	mSender.fade = blocks;
}

void GrainParameter::amplitude(float n)
{
	if (n < 0)
		n = 0;
	else if (n > 1.0)
		n = 1.0;
	mSender.magnitude = n * 65536.0;
}


void AudioEffectGrainer::interval(float ms)
{
	uint16_t blocks = ms2block(ms);
	if (blocks < 1)
		blocks = 1;
	mTriggGrain = blocks;
}

void AudioEffectGrainer::setBlock(audio_block_struct * out, GrainStruct* pGrain)
{

	uint16_t blockPos = pGrain->start;
	uint16_t state = pGrain->state;

	if (state == GRAIN_TRIG)
	{
		if (blockPos <= mAudioBuffer.head)
			blockPos = mAudioBuffer.head - blockPos;
		else
			blockPos = (mAudioBuffer.len + mAudioBuffer.head) - blockPos;
		state = GRAIN_PLAYING;
	}
	else
	{
		blockPos = pGrain->pos;
	}

	if (mWindow != NULL && pGrain->sizePos < (pGrain->size << 1))
	{

		uint16_t fadePhaseIncr = (0x8000 / pGrain->size); //TODO: move into GrainStruct
		uint32_t totalSample = pGrain->sizePos * AUDIO_BLOCK_SAMPLES;
		uint16_t windowSampleIndex = totalSample / ((int32_t) pGrain->size); //TODO: opt!!!
		uint16_t windowSampleOffset = totalSample % ((int32_t) pGrain->size); //TODO: opt!!!
		windowSampleIndex += (windowSampleOffset != 0);

		audio_block_t * in = mAudioBuffer.data[blockPos];
		int16_t * dst = out->data;
		const int16_t * inputSrc = in->data;
		const int16_t * windowSrc = mWindow + windowSampleIndex;
		int16_t * end = dst + AUDIO_BLOCK_SAMPLES;

		while (dst < end)
		{
			int16_t windowSample;

			if (windowSampleOffset == 0)
			{
				windowSample = *windowSrc++;
				++windowSampleIndex;
			}
			else
			{
				int32_t wPrev = *(windowSrc - 1);
				int32_t w = (windowSampleIndex >= WINDOW_SIZE) ? *mWindow : *windowSrc;
				int32_t F = windowSampleOffset * ((int32_t) fadePhaseIncr);
				int32_t val1 = wPrev * (0x8000 - F);
				int32_t val2 = w * F;
				windowSample = signed_saturate_rshift(val1 + val2, 16, 15);
			}

			if (++windowSampleOffset > pGrain->size - 1)
				windowSampleOffset = 0;

			int32_t inputSample = *inputSrc++;
			*dst++ += multiply_32x32_rshift32(
					multiply_16bx16b(inputSample, windowSample),
					pGrain->magnitude);
		}

	}
	else
	{
		pGrain->state = GRAIN_HAS_ENDED;
		return;
	}

	//update grain to next block set.
	blockPos = (1 + blockPos) % mAudioBuffer.len;
	pGrain->pos = blockPos;
	pGrain->sizePos += 1;
	pGrain->state = state;
}

GrainStruct * AudioEffectGrainer::getFreeGrain()
{
	GrainStruct * grain = mFreeGrain;
	if (grain == NULL)
		return NULL;
	mFreeGrain = mFreeGrain->next;
	grain->next = NULL;
	grain->state = GRAIN_AVAILABLE;
	return grain;
}

GrainStruct* AudioEffectGrainer::freeGrain(GrainStruct * grain, GrainStruct* prev)
	{
		GrainStruct* free = grain;
		if (mPlayGrain == grain)
			grain = mPlayGrain = grain->next;
		else if (prev != NULL)
			grain = prev->next = grain->next;
		else
			grain = grain->next;

		free->next = mFreeGrain;
		free->state = GRAIN_AVAILABLE;
		mFreeGrain = free;
		return grain;
	}

void AudioEffectGrainer::update()
{
	//Wait until audio buffer is full.
	if (!fillAudioBuffer())
		return;

	//Allocate output data.
	audio_block_t * out = AudioStream::allocate();
	if (!out) return;
	memset(out->data, 0, sizeof(out->data));

	if( ++mTriggCount >= mTriggGrain )
	{
		mTriggCount = 0;
		GrainStruct * triggedGrain = getFreeGrain();
		if(triggedGrain)
		{
			triggedGrain->next = mPlayGrain;
			mPlayGrain = triggedGrain;
		}
	}

	GrainStruct * grain = mPlayGrain;
	GrainStruct * prev = NULL;

	DEBUG_TRIG_ITER_ZERO_COUNT();

	//Start DSP.
	while (grain != NULL)
	{
		//Fetch grain parameter if grain is new.
		if (grain->state == GRAIN_AVAILABLE)
		{
			mGrainParam.resive(*grain);
			grain->state = GRAIN_TRIG;
		}

		DEBUG_PRINT_GRAIN(0, grain);

		setBlock(out, grain);

		if (grain->state == GRAIN_HAS_ENDED)
		{
			grain = freeGrain(grain, prev);
		} //if: grain has been played, free the grain.
		else
		{
			DEBUG_TRIG_ITER_ADD_GRAIN(grain);
			DEBUG_PRINT_GRAIN(1, grain);

			prev = grain;
			grain = grain->next;
		}
	}

	transmit(out);
	release(out);
}

void AudioEffectGrainer::queueLength(uint16_t l)
{
	if (l > GRAIN_BLOCK_QUEUE_SIZE)
		mAudioBuffer.len = GRAIN_BLOCK_QUEUE_SIZE;
	else
		mAudioBuffer.len = l;
}

void AudioEffectGrainer::freezer(bool f)
{
	mAudioBuffer.freeze = f;
}

GrainParameter * AudioEffectGrainer::next()
{
	mGrainParam.init(&mAudioBuffer);
	return &mGrainParam;
}

AudioEffectGrainer::AudioEffectGrainer() :
		AudioStream(1, mInputQueueArray), mWindow(AudioWindowHamming256)
{
	int i = GRAINS_MAX_NUM - 1;
	mGrains[i].next = NULL;
	mGrains[i].state = GRAIN_AVAILABLE;
	for (; i > 0; --i)
	{
		mGrains[i - 1].next = &(mGrains[i]);
		mGrains[i].state = GRAIN_AVAILABLE;
	}
	mFreeGrain = &(mGrains[0]);
	mPlayGrain = NULL;

	mTriggCount = 0;
}
