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

void AudioEffectGrainer::adjustPosition()
{
	uint32_t sampleStart = mResiver.saved_sampleStart;
	uint32_t neededSamples;
	float p = mResiver.saved_pitchRatio;


	/*
	 * TODO: not thinking of head writing in block
	 * 			but variable sample writing based on pitch pitch.
	 * 			working in the most cases maybe...
	 */

	if( p > 1.f )
	{
		neededSamples = float(mResiver.size) * (p-1.f);
		if(sampleStart < neededSamples)
			mResiver.sampleStart = neededSamples;
		else
			mResiver.sampleStart = sampleStart;
	}
	else if( p > 0.1f )
	{
		//TODO: opt (1.f/p) and/or is it (1.f/p - 1.f) even if it gives crackle?
		neededSamples = float(mResiver.size) * (1.f/p);
		neededSamples = samplePos(mAudioBuffer.len) - neededSamples;
		if(sampleStart > neededSamples)
			mResiver.sampleStart = neededSamples;
		else
			mResiver.sampleStart = sampleStart;
	}
}

void AudioEffectGrainer::pitch(float p)
{
	if (p > 2.f) p = 2.f;
	else if(p<0.f) p = 0.f;

	//1<<24 = 0x1000000 = 16777216
	mResiver.grainPhaseIncrement = 16777216.f * p;
	mResiver.saved_pitchRatio = p;
}

void AudioEffectGrainer::durration(float ms)
{
	uint32_t samples = MS_TO_SAMPLE_SCALE * ms;
	if (samples < (AUDIO_BLOCK_SAMPLES<<1) )
		samples = AUDIO_BLOCK_SAMPLES<<1;

	mResiver.size = samples;
	//2*(1<<24) = 0x2000000 = 33554432
	mResiver.windowPhaseIncrement =
			33554432.f * (AUDIO_BLOCK_SAMPLES_FLOAT/float(samples)) + .5f;
}

void AudioEffectGrainer::interval(float ms)
{
	uint32_t interv = MS_TO_SAMPLE_SCALE * ms + .5;
	if (interv < AUDIO_BLOCK_SAMPLES)
		interv = AUDIO_BLOCK_SAMPLES;
	//interval is on block position.
	interv = getBlockPosition( interv );
	mTriggGrain = mSavedTriggGrain = interv;
}

void AudioEffectGrainer::adjustInterval()
{
	if( mResiver.size > 2560 )
	{
		uint32_t evenSpreadTrig =
			float(getBlockPosition( mResiver.size /*+ AUDIO_BLOCK_SAMPLES -1*/ )) *
				GRAINS_EVEN_SPREAD_TRIG_SCALE;
		if(mSavedTriggGrain < evenSpreadTrig)
			mTriggGrain = evenSpreadTrig;
		else
			mTriggGrain = mSavedTriggGrain;
	}
}

void AudioEffectGrainer::pos(float ms)
{
	uint32_t samples = MS_TO_SAMPLE_SCALE * ms + .5;
	if ( getBlockPosition(samples) >= mAudioBuffer.len )
		samples = samplePos(mAudioBuffer.len) - 1;
	mResiver.sampleStart = samples;
	mResiver.saved_sampleStart = samples;
}

void AudioEffectGrainer::amplitude(uint8_t ch, float n)
{
	if (n < 0.f)
		n = 0.f;
	else if (n > 1.f)
		n = 1.f;
	if(ch>3) ch = 3;
	mResiver.magnitude[ch] = n * 65536.f;
}

bool AudioEffectGrainer::writeGrainBlock(GrainStruct* pGrain)
{
	//Grain variables

	uint32_t grainPosition = pGrain->position;
	uint32_t grainSize = pGrain->size;
	uint32_t blockPosition = pGrain->blockPosition;
	const int16_t * inputSrc1;
	const int16_t * inputSrc2;

	//Shared variables.

	uint32_t phaseAcc, phaseIncr;
	uint8_t interpolateIndex; //need to be uint8_t !!!
	int32_t val1, val2, scale;

	int32_t end;
	int32_t * dst;
	int32_t samplesLeft = grainSize - grainPosition;
	bool lastBlock = samplesLeft <= AUDIO_BLOCK_SAMPLES;
	int32_t samplesToWrite;
	samplesToWrite = (lastBlock) ? samplesLeft : AUDIO_BLOCK_SAMPLES;


	// ---------------------------------------------
	// ------------------- Audio -------------------
	// ---------------------------------------------

	phaseAcc = pGrain->grainPhaseAccumulator;
	phaseIncr = pGrain->grainPhaseIncrement;

	dst = mGrainBlock;
	end = samplesToWrite;

	uint32_t toIndex;
	uint32_t nextPhaseAcc;

	interpolateIndex = phaseAcc>>24;
	inputSrc1 = mAudioBuffer.data[ blockPosition ]->data;

	toIndex = (AUDIO_BLOCK_SAMPLES-1) << 24;
	inputSrc2 = inputSrc1;

	while (end>-1)
	{
		nextPhaseAcc = phaseAcc & AUDIO_BLOCK_SAMPLES_24_BITOP;
		if(nextPhaseAcc<phaseAcc)
		{
			toIndex = (AUDIO_BLOCK_SAMPLES-1) << 24;
			if( ++blockPosition >= mAudioBuffer.len)
				blockPosition -= mAudioBuffer.len;
			inputSrc1 = mAudioBuffer.data[ blockPosition ]->data;
			inputSrc2 = inputSrc1;
		}
		else if( interpolateIndex >= AUDIO_BLOCK_SAMPLES-1 )
		{
			toIndex = AUDIO_BLOCK_SAMPLES << 24;
			uint32_t blockPos = blockPosition;
			inputSrc1 = mAudioBuffer.data[ blockPos ]->data;
			if( ++blockPos >= mAudioBuffer.len ) blockPos = 0;
			inputSrc2 = mAudioBuffer.data[blockPos]->data;
		}

		phaseAcc = nextPhaseAcc;
		interpolateIndex = phaseAcc>>24;

		while(phaseAcc < toIndex && end--)
		{
			val1 = inputSrc1[ interpolateIndex ];
			val2 = inputSrc2[ getSampleIndex(interpolateIndex+1) ];
			scale = (phaseAcc >> 8) & 0xFFFF;
			val1 *= 0x10000 - scale;
			val2 *= scale;
			*dst++ = val1 + val2;

			phaseAcc += phaseIncr;
			interpolateIndex = phaseAcc>>24;
		}
	}

	pGrain->grainPhaseAccumulator = phaseAcc;
	pGrain->blockPosition = blockPosition;
	pGrain->position += samplesToWrite;

	// ---------------------------------------------
	// ------------- Window Interpolate ------------
	// ---------------------------------------------

	phaseAcc = pGrain->windowPhaseAccumulator;
	phaseIncr = pGrain->windowPhaseIncrement;

	dst = mGrainBlock;
	end = samplesToWrite;

	while (end--)
	{
		interpolateIndex = phaseAcc >> 24;
		val1 = mWindow[interpolateIndex];
		val2 = mWindow[interpolateIndex+1];
		scale = (phaseAcc >> 8) & 0xFFFF;
		val1 *= 0x10000 - scale;
		val2 *= scale;
		phaseAcc += phaseIncr;

		*dst = multiply_32x32_rshift32(val1 + val2, *dst);
		++dst;
	}

	pGrain->windowPhaseAccumulator = phaseAcc;


	if( samplesLeft < AUDIO_BLOCK_SAMPLES )
			memset((void*)dst,0,(AUDIO_BLOCK_SAMPLES-samplesLeft)*sizeof(int32_t) );


	if ( lastBlock )
	{
		pGrain->position = 0;
		pGrain->dead = true;
		return true;
	}

	return false;
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

	size_t freeGrainIndex = GRAINS_MAX_NUM;
	mConcurrentGrains = 0;
	size_t index = GRAINS_MAX_NUM;

	GrainStruct ** grainSrc = mPlayGrains;

	//Fill playing grains and find a free grain.
	while(index--)
	{
		if( mGrains[index].dead )
			freeGrainIndex = index;
		else
		{
			*grainSrc++ = &(mGrains[index]);
			mConcurrentGrains++;
		}
	}

	if( ++mTriggCount >= mTriggGrain )
	{
		mTriggCount = 0;
		if(freeGrainIndex!=GRAINS_MAX_NUM)
		{
			//Fetch grain parameter if grain is new.
			resive(&(mGrains[freeGrainIndex]));
			*grainSrc++ = &(mGrains[freeGrainIndex]);
			mConcurrentGrains++;
		}
	}

	audio_block_t * outs[4];
	for(uint8_t ch = 0 ; ch < 4 ; ++ch)
		outs[ch] = NULL;

	if( mConcurrentGrains )
	{
		if( ! allocateOutputs(outs) ) return;
	}

	GrainStruct * grain;

	size_t end = mConcurrentGrains;

	//Start DSP.
	while ( end-- )
	{
		grain = *(--grainSrc);
		DEBUG_PRINT_GRAIN(0, grain);
		writeGrainBlock(grain);
		setOutputs(outs, grain);
	}

	transmitOutputs(outs);

}

void AudioEffectGrainer::resive(GrainStruct * g)
{
//	g->grainPhaseIncrement = mResiver.grainPhaseIncrement;
//	g->windowPhaseIncrement = mResiver.windowPhaseIncrement;
//	g->size = mResiver.size;
//
//	g->magnitude[0] = mResiver.magnitude[0];
//	g->magnitude[1] = mResiver.magnitude[1];
//	g->magnitude[2] = mResiver.magnitude[2];
//	g->magnitude[3] = mResiver.magnitude[3];

	*g = mResiver;

	//set position relative to head block.
	uint32_t samples = getBlockPosition(mResiver.sampleStart);
	if (samples <= mAudioBuffer.head)
		samples = mAudioBuffer.head - samples;
	else
		samples = mAudioBuffer.len - samples + mAudioBuffer.head;
	g->blockPosition = samples;
	g->grainPhaseAccumulator = getSampleIndex( mResiver.sampleStart ) << 24 ;

	g->dead = false;
	g->position = 0;
	g->windowPhaseAccumulator = 0;
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

AudioEffectGrainer::AudioEffectGrainer() :
		AudioStream(1, mInputQueueArray),
		mWindow(AudioWindowNuttall256),
		mTriggCount(0), mConcurrentGrains(0)
{}
