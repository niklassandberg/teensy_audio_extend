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
		neededSamples = mAudioBuffer.sampleSize - neededSamples;
		if(sampleStart > neededSamples)
			mResiver.sampleStart = neededSamples;
		else
			mResiver.sampleStart = sampleStart;
	}
}

void AudioEffectGrainer::pitch(float p)
{
	if (p > 2.f) p = 2.f;
	else if(p<0.0001f) p = 0.0001f;

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
	//1<<24 = 0x1000000 = 16777216
	mResiver.windowPhaseIncrement =
			33554432.f * (AUDIO_BLOCK_SAMPLES_FLOAT/float(samples)) + .5f;
}

void AudioEffectGrainer::interval(float ms)
{
	uint32_t samples = MS_TO_SAMPLE_SCALE * ms + .5;
	if (samples < AUDIO_BLOCK_SAMPLES)
		samples = AUDIO_BLOCK_SAMPLES;
	uint16_t blocks = samples >> ShiftOp<AUDIO_BLOCK_SAMPLES>::result;
	mTriggGrain = blocks;
}

void AudioEffectGrainer::adjustInterval()
{
	if( mResiver.size > 2560 )
	{
		uint32_t evenSpreadTrig =
			float(getBlockPosition( mResiver.size /*+ AUDIO_BLOCK_SAMPLES -1*/ )) *
				GRAINS_EVEN_SPREAD_TRIG_SCALE;
		if(mTriggGrain < evenSpreadTrig) mTriggGrain = evenSpreadTrig;
	}
}

void AudioEffectGrainer::pos(float ms)
{
	uint32_t samples = MS_TO_SAMPLE_SCALE * ms + .5;
	if ( samples  >= mAudioBuffer.sampleSize )
		samples = mAudioBuffer.sampleSize - 1;
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
	uint32_t grainBuffertPosition = pGrain->buffertPosition;
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

		*dst++ = val1 + val2;
	}

	pGrain->windowPhaseAccumulator = phaseAcc;

	// ---------------------------------------------
	// ------------------- Audio -------------------
	// ---------------------------------------------

	phaseAcc = pGrain->grainPhaseAccumulator;
	phaseIncr = pGrain->grainPhaseIncrement;

	dst = mGrainBlock;
	end = samplesToWrite;

	uint32_t toIndex;
	uint32_t prevIndex;

	while (end>-1)
	{
		interpolateIndex = getSampleIndex(grainBuffertPosition);
		prevIndex = interpolateIndex;

		if( interpolateIndex == AUDIO_BLOCK_SAMPLES-1 )
		{
//			//delay(10);
//			//Serial.println("toIndex = 128");

			toIndex = 128;

			uint32_t blockPos = getBlockPosition(grainBuffertPosition);
			inputSrc1 = mAudioBuffer.data[ blockPos ]->data;
			if( ++blockPos >= mAudioBuffer.len ) blockPos = 0;
			inputSrc2 = mAudioBuffer.data[blockPos]->data;
		}
		else
		{
//			//delay(10);
//			//Serial.println("toIndex = 127");

			toIndex = 127;

			if( grainBuffertPosition >= mAudioBuffer.sampleSize)
				grainBuffertPosition -= mAudioBuffer.sampleSize;
			inputSrc1 = mAudioBuffer.data[ getBlockPosition(grainBuffertPosition) ]->data;
			inputSrc2 = inputSrc1;
		}

		while(interpolateIndex < toIndex && end--)
		{
//			//delay(10);
//			//Serial.print("end: "); //Serial.println(end);

			val1 = inputSrc1[ interpolateIndex ];
			val2 = inputSrc2[ getSampleIndex(interpolateIndex+1) ];
			scale = (phaseAcc >> 8) & 0xFFFF;
			val1 *= 0x10000 - scale;
			val2 *= scale;
			*dst = multiply_32x32_rshift32(val1 + val2, *dst);

			phaseAcc += phaseIncr;
			interpolateIndex += (phaseAcc >> 24);
			phaseAcc &= 0xFFFFFF;
			++dst;

//			//delay(1);
//			//Serial.println("----");
//			//Serial.print("phaseIncr: "); //Serial.println(scale);
//			//Serial.print("phaseAcc: "); //Serial.println(scale);
//			//Serial.print("index1: "); //Serial.println(interpolateIndex);
//			//Serial.print("scale: "); //Serial.println(scale);
//			//Serial.println("----");
		}

		grainBuffertPosition += (interpolateIndex - prevIndex);

//		//delay(1);
//		//Serial.print("grainBuffertPosition: "); //Serial.println(grainBuffertPosition);

	}

	if( samplesLeft < AUDIO_BLOCK_SAMPLES )
			memset((void*)dst,0,(AUDIO_BLOCK_SAMPLES-samplesLeft)*sizeof(int32_t) );

	pGrain->grainPhaseAccumulator = phaseAcc;
	pGrain->buffertPosition = grainBuffertPosition;
	pGrain->position += samplesToWrite;



	if ( lastBlock )
	{
		//delay(1);
		//Serial.println("!!!! lastBlock !!!!");

		//delay(1);
		//Serial.print("phaseAcc: "); //Serial.println(phaseAcc);
		//Serial.print("buffertPosition: "); //Serial.println(grainBuffertPosition);
		//Serial.print("position: "); //Serial.println(pGrain->position);

		//Serial.println("----");

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
