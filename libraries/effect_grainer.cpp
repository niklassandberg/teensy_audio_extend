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

void AudioEffectGrainer::pitch(float p)
{
	if (p > 2.0) p = 2.0;
	else if(p<0.0) p = 0.0;
	//1<<24 = 0x1000000 = 16777216
	mResiver.grainPhaseIncrement = 16777216.0 * p;
}

void AudioEffectGrainer::durration(float ms)
{
	uint32_t samples = MS_TO_SAMPLE_SCALE * ms;
	if (samples < (AUDIO_BLOCK_SAMPLES<<1) )
		samples = AUDIO_BLOCK_SAMPLES<<1;

	mResiver.size = samples;
	//1<<24 = 0x1000000 = 16777216
	mResiver.windowPhaseIncrement =
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

void AudioEffectGrainer::pos(float ms)
{
	uint32_t samples = MS_TO_SAMPLE_SCALE * ms + .5;
	if ( samples  >= mAudioBuffer.sampleSize )
		samples = mAudioBuffer.sampleSize - 1;
	mResiver.sampleStart = samples;
}

void AudioEffectGrainer::amplitude(uint8_t ch, float n)
{
	if (n < 0)
		n = 0;
	else if (n > 1.0)
		n = 1.0;
	if(ch>3) ch = 3;
	mResiver.magnitude[ch] = n * 65536.0;
}

bool AudioEffectGrainer::writeGrainBlock(GrainStruct* pGrain)
{
	//Grain variables

	uint32_t grainPosition = pGrain->position;
	uint32_t grainSize = pGrain->size;
	int32_t grainIncrement = pGrain->grainPhaseIncrement;
	uint32_t grainBlockPosition, grainPhase;

	uint32_t grainBuffertPosition;

	//Window variables.

	int32_t windowPhase;
	int32_t windowIncrement = pGrain->windowPhaseIncrement;

	//Shared variables.

	uint8_t interpolateIndex; //need to be uint8_t !!!
	int32_t val1;
	int32_t val2;
	int32_t scale;

	//Out- and Inputs of audio and window.

	int32_t * dst = mGrainBlock;
	int32_t windowSample;
	int32_t inputSample;

	uint32_t bNext;

	grainBuffertPosition = pGrain->buffertPosition;
	grainBlockPosition = getBlockPosition(grainBuffertPosition);

	windowPhase = pGrain->windowPhaseAccumulator;
	grainPhase = pGrain->grainPhaseAccumulator;

	const int16_t * inputSrc;

	int32_t samplesLeft = grainSize - grainPosition;
	bool lastBlock = samplesLeft <= AUDIO_BLOCK_SAMPLES;

	int32_t end = AUDIO_BLOCK_SAMPLES;
	if(lastBlock) end = samplesLeft;

	while (end--)
	{
//		if( grainPosition >= grainSize )
//		{
//			*dst++ = 0;
//			continue;
//		}

		// ---------------------------------------------
		// ------------- Window Interpolate ------------
		// ---------------------------------------------

		//Window interpolate.

		interpolateIndex = windowPhase >> 24;
		val1 = mWindow[interpolateIndex];
		val2 = mWindow[interpolateIndex+1];
		scale = (windowPhase >> 8) & 0xFFFF;
		val1 *= 0x10000 - scale;
		val2 *= scale;
		windowSample = val1 + val2;
		windowPhase += windowIncrement;

		// ---------------------------------------------
		// ------------------- Audio -------------------
		// ---------------------------------------------

		// Find first interpolate value.

		inputSrc = mAudioBuffer.data[grainBlockPosition]->data;
		interpolateIndex = getSampleIndex(grainBuffertPosition);
		val1 = inputSrc[interpolateIndex];

		// Find next interpolate value.

		++grainBuffertPosition;
		if( grainBuffertPosition >= mAudioBuffer.sampleSize )
			bNext = 0;
		else
			bNext = getBlockPosition(grainBuffertPosition);
		interpolateIndex = getSampleIndex(grainBuffertPosition);
		inputSrc = mAudioBuffer.data[bNext]->data;
		--grainBuffertPosition;

		val2 = inputSrc[interpolateIndex];

		//Audio interpolate.

		scale = (grainPhase >> 8) & 0xFFFF;
		val1 *= 0x10000 - scale;
		val2 *= scale;
		inputSample = val1 + val2;
		grainPhase += grainIncrement;

		//sync phase increment with sample and block position.

		uint32_t buffertIndexIncrement = grainPhase >> 24;
		grainPhase &= 0xFFFFFF;
		grainBuffertPosition += buffertIndexIncrement;
		if( grainBuffertPosition >= mAudioBuffer.sampleSize )
			grainBuffertPosition -= mAudioBuffer.sampleSize;
		grainBlockPosition = getBlockPosition(grainBuffertPosition);

		// ---------------------------------------------
		// ------------- Audio * Window ----------------
		// ---------------------------------------------

		*dst++ = multiply_32x32_rshift32(inputSample, windowSample);

		//next iteration.
		++grainPosition;
	}

	if( lastBlock && samplesLeft < AUDIO_BLOCK_SAMPLES )
		memset((void*)dst,0,(AUDIO_BLOCK_SAMPLES-samplesLeft)*sizeof(int32_t) );

	//update grain to next block set.

	pGrain->buffertPosition = grainBuffertPosition;
	pGrain->windowPhaseAccumulator = windowPhase;
	pGrain->grainPhaseAccumulator = grainPhase;
	pGrain->position = grainPosition;

	if ( lastBlock )
	{
		pGrain->position = 0;
		return true;
	}

	return false;
}

GrainStruct * AudioEffectGrainer::getFreeGrain()
{
	GrainStruct * grain = mFreeGrain;
	if (grain == NULL)
		return NULL;
	mFreeGrain = mFreeGrain->next;
	grain->next = NULL;
	++mConcurrentGrains;
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
		--mConcurrentGrains;
		return grain;
	}

bool AudioEffectGrainer::allocateOutputs(audio_block_t* outs[4])
{
	//Allocate output data.
	for (uint8_t ch = 0; ch < 4; ++ch)
	{

		if(mDisableChannel[ch])
		{
			outs[ch] = NULL;
			continue;
		}

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
			resive(*triggedGrain);
			triggedGrain->next = mPlayGrain;
			mPlayGrain = triggedGrain;
		}
	}

	GrainStruct * grain = mPlayGrain;
	GrainStruct * prev = NULL;

	audio_block_t * outs[4];
	for(uint8_t ch = 0 ; ch < 4 ; ++ch)
		outs[ch] = NULL;

	if( grain )
	{
		if( ! allocateOutputs(outs) ) return;
	}

	//Start DSP.
	while ( grain )
	{
		DEBUG_PRINT_GRAIN(0, grain);

		if ( writeGrainBlock(grain) )
		{
			setOutputs(outs, grain);
			grain = freeGrain(grain, prev);
		} //if: grain has been played, free the grain.
		else
		{
			setOutputs(outs, grain);
			prev = grain;
			grain = grain->next;
		}
	}

	transmitOutputs(outs);

}

void AudioEffectGrainer::resive(GrainStruct & g)
{
	g.grainPhaseIncrement = mResiver.grainPhaseIncrement;
	g.windowPhaseIncrement = mResiver.windowPhaseIncrement;
	g.size = mResiver.size;
	g.position = 0;
	g.windowPhaseAccumulator = 0;
	g.grainPhaseAccumulator = 0;

	g.magnitude[0] = mResiver.magnitude[0];
	g.magnitude[1] = mResiver.magnitude[1];
	g.magnitude[2] = mResiver.magnitude[2];
	g.magnitude[3] = mResiver.magnitude[3];

	//set position relative to head block.
	uint32_t samples = mResiver.sampleStart;
	uint32_t headBuffertPosition = samplePos( mAudioBuffer.head );
	if (samples <= headBuffertPosition)
		samples = headBuffertPosition - samples;
	else
		samples = ( mAudioBuffer.sampleSize
				- samples ) + headBuffertPosition;
	g.buffertPosition = samples;
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
	mAudioBuffer.sampleSize = samplePos(l);
}

void AudioEffectGrainer::freezer(bool f)
{
	mAudioBuffer.freeze = f;
}

float AudioEffectGrainer::bufferMS()
{
	return block2ms(mAudioBuffer.len);
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

	mConcurrentGrains = 0;
}
