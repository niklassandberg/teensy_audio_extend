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
	if (p > 1.0) p = 1.0;
	else if(p<0.0) p = 0.0;
	mSender.grain_phase_increment = float(1<<14)*p;
}

void GrainParameter::durration(float ms)
{
	uint32_t samples = MS_TO_SAMPLE_SCALE * ms;
	if (samples < (AUDIO_BLOCK_SAMPLES<<1) )
		samples = AUDIO_BLOCK_SAMPLES<<1;

	mSender.size = samples;
	//1<<24= 0x1000000 = 16777216
	mSender.window_phase_increment =
			33554432.0 * (float(AUDIO_BLOCK_SAMPLES)/samples) + .5;
}

void AudioEffectGrainer::interval(float ms)
{
	uint32_t samples = MS_TO_SAMPLE_SCALE * ms + .5;
	if (samples < AUDIO_BLOCK_SAMPLES)
		samples = AUDIO_BLOCK_SAMPLES;
	uint16_t blocks = samples >> ShiftOp<AUDIO_BLOCK_SAMPLES>::result;
	mTriggGrain = blocks;
}

void GrainParameter::pos(float ms)
{
	uint32_t samples = MS_TO_SAMPLE_SCALE * ms + .5;
	if (blockPos(samples) >= mAudioBuffer->len)
		samples = samplePos(mAudioBuffer->len) - 1;
	else
		samples <<= 14;
	mSender.start = samples;
}

void GrainParameter::amplitude(uint8_t ch, float n)
{
	if (n < 0)
		n = 0;
	else if (n > 1.0)
		n = 1.0;
	if(ch>3) ch = 3;
	mSender.magnitude[ch] = n * 65536.0;
}

bool AudioEffectGrainer::setGrainBlock(GrainStruct* pGrain)
{
	uint32_t grainPos = pGrain->sizePos;
	uint32_t grainLen = pGrain->size;
	uint32_t prevBlock, block;

	if (grainPos >= grainLen)
	{
		pGrain->sizePos = 0;
		pGrain->window_phase_accumulator = 0;
		return false;
	}

	uint32_t sample;

	//Window variables.
	int32_t wPh = pGrain->window_phase_accumulator;
	int32_t wInc = pGrain->window_phase_increment;
	int32_t grInc = pGrain->grain_phase_increment;
	uint8_t wIndex = 0; //need to be uint8_t !!!
	int32_t val1, val2, wScale;
	//Out- and Inputs of audio and window.
	int32_t * dst = mGrainBlock;
	int32_t * end = dst + AUDIO_BLOCK_SAMPLES;
	int32_t windowSample, inputSample;


	if ( grainPos == 0 )
	{
		//set position relative to newly read block.
		sample = pGrain->start;
		block = blockPos( sample );
		uint32_t i = sampleIndex(sample);
		if (block <= mAudioBuffer.head)
			block = mAudioBuffer.head - block;
		else
			block = (mAudioBuffer.len + mAudioBuffer.head) - block;
		sample = (i<<14) + samplePos(block); //phase==0
		//sample &= (~0x3FFF); //phase == 0 for sertain.
		prevBlock = block;
	}
	else
	{
		sample = pGrain->buffSamplePos;
		block = blockPos(sample);
		prevBlock = block - 1; //any value prevBlock != block
	}

	const int16_t * inputSrc = mAudioBuffer.data[block]->data;

	while (dst < end)
	{
		if( grainPos >= grainLen )
		{
			*dst++ = 0;
			continue;
		}

		//Window interpolate.
		wIndex = wPh >> 24;
		val1 = mWindow[wIndex];
		val2 = mWindow[wIndex+1];
		wScale = (wPh >> 8) & 0xFFFF;
		val1 *= 0x10000 - wScale;
		val2 *= wScale;
		windowSample = val1 + val2;
		wPh += wInc;

		//Audio
		//TODO: when we do similar as window with scale on audio we get
		//		int32 without sat shift. We then do ul32 >> 32 on res. saves clock cycles!!!

		if(block != prevBlock )
		{
			if( block >= mAudioBuffer.len)
			{
				sample -= samplePos(mAudioBuffer.len);
				block = blockPos(sample);
			}
			prevBlock = block;
			inputSrc = mAudioBuffer.data[block]->data;
		}

		wIndex = sampleIndex(sample);
		val1 = inputSrc[wIndex];
		if( ++wIndex >= AUDIO_BLOCK_SAMPLES )
		{
			wIndex = 0;
			++block;
			if( block >= mAudioBuffer.len)
			{
				block = 0;
			}
			inputSrc = mAudioBuffer.data[block]->data;
		}
		val2 = inputSrc[wIndex];

		wScale = (sample & 0x3FFF) << 2;
		val1 *= 0x10000 - wScale;
		val2 *= wScale;
		inputSample = val1 + val2;
		sample += grInc;

		//old
		//val2 = inputSrc[sampleIndex(sample+1)];

		//Audio + window
		*dst++ = multiply_32x32_rshift32(inputSample, windowSample);
		//*dst++ = multiply_16bx16b(inputSample, windowSample);

		//next iteration.
		++grainPos;
		block = blockPos(sample);
	}

	//update grain to next block set.
	pGrain->buffSamplePos = sample;
	pGrain->window_phase_accumulator = wPh;

	pGrain->sizePos = grainPos;

	return true;
}

GrainStruct * AudioEffectGrainer::getFreeGrain()
{
	GrainStruct * grain = mFreeGrain;
	if (grain == NULL)
		return NULL;
	mFreeGrain = mFreeGrain->next;
	grain->next = NULL;
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
		mFreeGrain = free;
		return grain;
	}

bool AudioEffectGrainer::allocateOutputs(audio_block_t* outs[4])
{
	//Allocate output data.
	for (uint8_t ch = 0; ch < 4; ++ch)
	{
		//TODO: disable functionality
		audio_block_t* out = AudioStream::allocate();
		if ( !out )
		{
			for (uint8_t i = 0; i < ch; ++i)
			{
				out = outs[i];
				if(out) release(out);
				outs[i] = NULL;
			}
			return false;
		}
		memset(out->data, 0, sizeof(out->data));
		outs[ch] = out;
	}
	return true;
}

void AudioEffectGrainer::setOutputs(audio_block_t* outs[4], GrainStruct* grain)
{
	for (uint8_t ch = 0; ch < 4; ++ch)
	{
		if (outs[ch] == NULL || grain->magnitude[ch] == 0)
			continue;

		int32_t* src = mGrainBlock;
		int16_t* dst = outs[ch]->data;
		int16_t* end = dst + AUDIO_BLOCK_SAMPLES;
		while (dst < end)
		{
			*dst++ += multiply_32x32_rshift32(*src++, grain->magnitude[ch]);
		}
	}
}

void AudioEffectGrainer::transmitOutputs(audio_block_t* outs[4])
{
	for (uint8_t ch = 0; ch < 4; ++ch)
	{
		if (outs[ch] == NULL)
			continue;

		transmit(outs[ch],ch);
		release(outs[ch]);
	}
}

void AudioEffectGrainer::update()
{
	//Wait until audio buffer is full.
	if (!fillAudioBuffer()) return;

	if( ++mTriggCount >= mTriggGrain )
	{
		mTriggCount = 0;
		GrainStruct * triggedGrain = getFreeGrain();
		if(triggedGrain)
		{
			//Fetch grain parameter if grain is new.
			mGrainParam.resive(*triggedGrain);
			triggedGrain->next = mPlayGrain;
			mPlayGrain = triggedGrain;
		}
	}

	GrainStruct * grain = mPlayGrain;
	GrainStruct * prev = NULL;

	DEBUG_TRIG_ITER_ZERO_COUNT();

	audio_block_t * outs[4];
	for(uint8_t ch = 0 ; ch < 4 ; ++ch)
		outs[ch] = NULL;

	if( grain != NULL )
	{
		if( ! allocateOutputs(outs) ) return;
	}

	//Start DSP.
	while (grain != NULL)
	{
		DEBUG_PRINT_GRAIN(0, grain);

		if ( ! setGrainBlock(grain) )
		{
			grain = freeGrain(grain, prev);
		} //if: grain has been played, free the grain.
		else
		{
			DEBUG_TRIG_ITER_ADD_GRAIN(grain);
			DEBUG_PRINT_GRAIN(1, grain);

			setOutputs(outs, grain);
			prev = grain;
			grain = grain->next;
		}
	}

	transmitOutputs(outs);

}

void AudioEffectGrainer::queueLength(uint16_t l)
{
	if(l<1024) l = 1024;

	if (l > GRAIN_BLOCK_QUEUE_SIZE)
	{
		mAudioBuffer.len = GRAIN_BLOCK_QUEUE_SIZE;
	}
	else
	{
		mAudioBuffer.len = l;
	}
}

void AudioEffectGrainer::freezer(bool f)
{
	mAudioBuffer.freeze = f;
}

float AudioEffectGrainer::bufferMS()
{
	return block2ms(mAudioBuffer.len);
}

GrainParameter * AudioEffectGrainer::next()
{
	mGrainParam.init(&mAudioBuffer);
	return &mGrainParam;
}

AudioEffectGrainer::AudioEffectGrainer() :
		AudioStream(1, mInputQueueArray), mWindow(AudioWindowNuttall256)
{
	int i = GRAINS_MAX_NUM - 1;
	mGrains[i].next = NULL;
	for (; i > 0; --i)
	{
		mGrains[i - 1].next = &(mGrains[i]);
	}
	mFreeGrain = &(mGrains[0]);
	mPlayGrain = NULL;

	mTriggCount = 0;
}
