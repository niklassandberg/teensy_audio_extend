/*
 * effect_pitchshifter.cpp
 *
 *  Created on: 10 feb. 2018
 *      Author: niksan
 */

#include "effect_grainer.h"

bool AudioEffectGrainer::fillAudioBuffer()
{
	if (mAudioBuffer.freeze) return true;

	uint32_t head = mAudioBuffer.head;
	audio_block_struct ** data = mAudioBuffer.data;

	if (++head >= mAudioBuffer.len) head = 0;

	if( data[head] )
	{
		mAudioBuffer.isFilled = true;
		release(data[head]);
		data[head] = NULL;
	}

	data[head] = receiveReadOnly();
	if (!data[head])
	{
		mAudioBuffer.isFilled = false;
		return false;
	}

	mAudioBuffer.head = head;

	return mAudioBuffer.isFilled;
}

void AudioEffectGrainer::releaseBlockOver(uint32_t index)
{
	for (size_t i = index; i < GRAIN_BLOCK_QUEUE_SIZE; ++i)
	{
		audio_block_struct* block = mAudioBuffer.data[i];
		if (block) release(block);
		mAudioBuffer.data[i] = NULL;
	}
}

void AudioEffectGrainer::audioBufferBlockLength(uint32_t l)
{
	if (l > GRAIN_BLOCK_QUEUE_SIZE)
	{
		l = GRAIN_BLOCK_QUEUE_SIZE;
	}
	else if( l < 64)
	{
		l = 64;
	}

	if(l == mAudioBuffer.len) return;

	//we need to release block
	if( l < mAudioBuffer.len )
	{
		//record head outside of new length.
		if( l < mAudioBuffer.head )
			mAudioBuffer.head = 0;

		//Alter parameter that is outside of new buffer length
		if( mResiver.saved_sampleStart > samplePos(mAudioBuffer.len))
			mResiver.saved_sampleStart = mResiver.sampleStart = 0;
		if(mResiver.size > samplePos(mAudioBuffer.len))
			mResiver.size = samplePos(mAudioBuffer.len-1);

		//kill all grains.
		uint32_t index = GRAINS_MAX_NUM;
		GrainStruct * grain = mGrains;
		while(index--)
		{
			(grain++)->dead = true;
		}

		//free blocks.
		releaseBlockOver(l);
	}
	else
	{
		//Remove everything over head position.
		releaseBlockOver(mAudioBuffer.head);
		//Just forget to play any grains until audio buffer is full
		mAudioBuffer.isFilled = false;
	}

	mAudioBuffer.len = l;
}

void AudioEffectGrainer::adjustPosition()
{
	uint32_t sampleStart = mResiver.saved_sampleStart;
	uint32_t neededSamples;
	float p = mResiver.saved_pitchRatio;

	/*
	 * TODO: not thinking of head writing in block
	 * 			but variable sample writing based on pitch.
	 * 			working in the most cases maybe...
	 */

	/*
	 * if: Pitch is higher =>
	 * 		if: Need to check that the buffer write head does not eat grains read tail.
	 * 		else if: need to check that the grains read head does not eat buffer write tail.
	 */
	if( p > 1.f )
	{
		//needed samples so the grains readHead does not eat the buffer writeTail.
//		neededSamples = (float(mResiver.size) * (p-1.f)) + .5f;
//		neededSamples = mAudioBuffer.len - neededSamples;
//		//if: grains read head eats buffer write tail
//		if( sampleStart > neededSamples)
//		{
//			mResiver.sampleStart = neededSamples;
//		}
//		else
//		{
//			mResiver.sampleStart = sampleStart;
//		}

		neededSamples = float(mResiver.size) * (p-1.f);

		Serial.print("neededSamples: ");
		Serial.println(neededSamples);

		Serial.print("sampleStart: ");
		Serial.println(sampleStart);


		//To avoid crackles in grains attack/release.
//		if(neededSamples<AUDIO_BLOCK_SAMPLES)
//			neededSamples = AUDIO_BLOCK_SAMPLES;

		mResiver.sampleStart = sampleStart;
//		if(sampleStart < neededSamples)
//		{
////			mResiver.sampleStart = neededSamples + AUDIO_BLOCK_SAMPLES*2;
//		}
//		else
//		{
//			mResiver.sampleStart = sampleStart;
//		}
	}
	else if( p > 0.1f ) //to avoid div by zero.
	{
		neededSamples = float(mResiver.size) * (1.f/p);

		//To avoid crackles in grains attack/release.
		if(neededSamples<AUDIO_BLOCK_SAMPLES)
			neededSamples = AUDIO_BLOCK_SAMPLES;

		neededSamples = samplePos(mAudioBuffer.len) - neededSamples;
		if(sampleStart > neededSamples)
			mResiver.sampleStart = neededSamples;
		else
			mResiver.sampleStart = sampleStart;
	}
//	else
//	{
//		if( sampleStart < AUDIO_BLOCK_SAMPLES )
//			mResiver.sampleStart = AUDIO_BLOCK_SAMPLES;
//		else
//			mResiver.sampleStart = sampleStart;
//	}
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
	else if(samples>samplePos(mAudioBuffer.len))
		samples = samplePos(mAudioBuffer.len-1);

	mResiver.size = samples;
	//2*(1<<24) = 0x2000000 = 33554432
	mResiver.windowPhaseIncrement =
			33554432.f * (AUDIO_BLOCK_SAMPLES_FLOAT/float(samples)) + .5f;
}

void AudioEffectGrainer::interval(float ms)
{
	uint32_t interval = MS_TO_SAMPLE_SCALE * ms + .5;
	if (interval < AUDIO_BLOCK_SAMPLES)
		interval = AUDIO_BLOCK_SAMPLES;
	//interval is on block position.
	interval = getBlockPosition( interval );
	mTriggGrain = mSavedTriggGrain = interval;
}

void AudioEffectGrainer::adjustInterval()
{
	if( mResiver.size > 2560 )
	{
		size_t evenSpreadTrig =
			float(getBlockPosition( mResiver.size )) *
				mEvenSpreadTrigScale;
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

void AudioEffectGrainer::window(WINDOW_TYPE w)
{
	switch (w) {
		case HANNING :
			mWindow = AudioWindowHanning256;
			break;
		case BARTLETT :
			mWindow = AudioWindowBartlett256;
			break;
		case BLACKMAN :
			mWindow = AudioWindowBlackman256;
			break;
		case FLATTOP :
			mWindow = AudioWindowFlattop256;
			break;
		case BLACKMAN_HARRIS :
			mWindow = AudioWindowBlackmanHarris256;
			break;
		case NUTTALL :
			mWindow = AudioWindowNuttall256;
			break;
		case BLACKMAN_NUTTALL :
			mWindow = AudioWindowBlackmanNuttall256;
			break;
		case WELCH :
			mWindow = AudioWindowWelch256;
			break;
		case HAMMING :
			mWindow = AudioWindowHamming256;
			break;
		case COSINE :
			mWindow = AudioWindowCosine256;
			break;
		case TUKEY :
			mWindow = AudioWindowTukey256;
			break;
		default:
			break;
	}
}

void AudioEffectGrainer::writeGrainBlock(GrainStruct* pGrain)
{
	//Grain variables

	uint32_t grainPosition = pGrain->position;
	uint32_t grainSize = pGrain->size;
	uint32_t blockPosition = pGrain->blockPosition;
	const int16_t * inputSrc1;
	const int16_t * inputSrc2;

	//Shared variables.

	uint32_t phaseAcc, phaseIncr;
	uint8_t interpolateIndex; //need to be uint8_t to wrap!!!
	int32_t val1, val2, scale;

	int32_t end;
	int32_t * dst;
	int32_t samplesLeft = grainSize - grainPosition;
	int32_t samplesToWrite;

	if(samplesLeft <= AUDIO_BLOCK_SAMPLES)
	{
		pGrain->dead = true; //grain will be dead.
		samplesToWrite = samplesLeft;
	}
	else
	{
		samplesToWrite = AUDIO_BLOCK_SAMPLES;
	}

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
		} //if: interpolate in the same block
		else if( interpolateIndex >= AUDIO_BLOCK_SAMPLES-1 )
		{
			toIndex = AUDIO_BLOCK_SAMPLES << 24;
			uint32_t blockPos = blockPosition;
			inputSrc1 = mAudioBuffer.data[ blockPos ]->data;
			if( ++blockPos >= mAudioBuffer.len ) blockPos = 0;
			inputSrc2 = mAudioBuffer.data[blockPos]->data;
		} //else if: interpolate in different block

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
	int32_t magnitude;
	for (uint8_t ch = 0; ch < 4; ++ch)
	{
		magnitude = grain->magnitude[ch];
		if (outs[ch] == NULL || magnitude == 0)
			continue;

		int32_t* src = mGrainBlock;
		int16_t* dst = outs[ch]->data;
		int16_t* end = dst + AUDIO_BLOCK_SAMPLES;
		while (dst < end)
		{
			*dst++ += multiply_32x32_rshift32(*src++, magnitude);
		}
	}
}

void AudioEffectGrainer::transmitOutputs(audio_block_t** outs)
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

	GrainStruct ** grainSrc;
	grainSrc = mPlayGrains;
	size_t freeGrainIndex = GRAINS_MAX_NUM;
	uint32_t concurrentGrains = mConcurrentGrains = 0;
	size_t index = mMaxNumOfGrains;

	//Fill playing grains and find a free grain.

	while (index--)
	{
		if (mGrains[index].dead)
		{
			freeGrainIndex = index;
		}
		else
		{
			*grainSrc++ = &(mGrains[index]);
			++concurrentGrains;
		}
	}

	if (++mTriggCount >= mTriggGrain)
	{
		mTriggCount = 0;
		if (freeGrainIndex != GRAINS_MAX_NUM)
		{
			//Fetch grain parameter if grain is new.
			resive(&(mGrains[freeGrainIndex]));
			*grainSrc++ = &(mGrains[freeGrainIndex]);
			concurrentGrains++;
		}
	}

	mConcurrentGrains = concurrentGrains;

	audio_block_t * outs[4];
	outs[0] = NULL;
	outs[1] = NULL;
	outs[2] = NULL;
	outs[3] = NULL;

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
		writeGrainBlock(grain);
		setOutputs(outs, grain);
	}

	transmitOutputs(outs);

}

void AudioEffectGrainer::resive(GrainStruct * g)
{
	*g = mResiver;

	//set position relative to head block.
	uint32_t blocks = getBlockPosition(mResiver.sampleStart);
	if (blocks <= mAudioBuffer.head)
		blocks = mAudioBuffer.head - blocks;
	else
		blocks = mAudioBuffer.len - blocks + mAudioBuffer.head;
	g->blockPosition = blocks;
	g->grainPhaseAccumulator = getSampleIndex( mResiver.sampleStart ) << 24 ;

	g->position = 0;
	g->windowPhaseAccumulator = 0;

	g->dead = false;

}

void AudioEffectGrainer::freezer(bool f)
{
	mAudioBuffer.freeze = f;
}

float AudioEffectGrainer::bufferMS()
{
	return block2ms(mAudioBuffer.len);
}

void AudioEffectGrainer::numberOfGrains(uint8_t n)
{
	if(n>GRAINS_MAX_NUM)
		n = GRAINS_MAX_NUM;
	if(!n)
		n = 1;

	mMaxNumOfGrains = n;
	mEvenSpreadTrigScale = 1.f/float(n);
}

AudioEffectGrainer::AudioEffectGrainer() :
		AudioStream(1, mInputQueueArray),
		mWindow(AudioWindowHanning256),
		mTriggCount(0), mConcurrentGrains(0), mMaxNumOfGrains(GRAINS_MAX_NUM)
{
}
