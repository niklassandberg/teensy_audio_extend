
#include <Arduino.h>
#include "effect_sinwavefolder.h"

uint32_t prevMs2 = 0;


void AudioEffectSinWaveFolder::update(void)
{
//#if defined(__ARM_ARCH_7EM__)

	audio_block_t *blocka, *blockb, *blockc;
	int16_t *pa, *pb, *pc, *end;
	uint32_t in, multi, offset;
	int16_t out;

	blocka = receiveWritable(0);
	blockb = receiveReadOnly(1);
	blockc = receiveReadOnly(2);

	if (!blocka) {
		if (blockb) release(blockb);
		return;
	}
	if (!blockb) {
		release(blocka);
		return;
	}
	if (!blockc) {
		release(blocka);
		release(blockb);
		return;
	}

	pa = blocka->data;
	pb = blockb->data;
	pc = blockc->data;
	end = pa + AUDIO_BLOCK_SAMPLES;

	while (pa < end) {

		in = int16ToInt32((*pa));
		multi = int16ToInt32(*pb);
		offset = int16ToInt32(*pc);

		int32_t smoothAmp = signed_add_32_saturate(multi,mAmp);
		uint32_t angle = multiply_32x32_rshift32_rounded(smoothAmp,in);
		//TODO: this or this????
		//out = int32ToInt16( taylorSine( angle*mStages + signed_add_32_saturate(offset,mOffset) ) );
		out = int32ToInt16( taylorSine( angle*mStages + offset + mOffset + (0xFFFFFFFF>>1)) );
/*
		if(millis() - prevMs2 > 1000)
		{
			prevMs2 = millis();
			Serial.print("mAmp: ");
			Serial.println(mAmp);
			Serial.print("angle: ");
			Serial.println(angle);
			Serial.print("angle2: ");
			Serial.println(angle2);
			Serial.println("------");
		}
*/
		++pc;
		++pb;
		*pa++ = out;
	}
	transmit(blocka);
	release(blocka);
	release(blockb);
	release(blockc);

	/*
#elif defined(KINETISL)
	audio_block_t *block;

	block = receiveReadOnly(0);
	if (block) release(block);
	block = receiveReadOnly(1);
	if (block) release(block);
	block = receiveReadOnly(2);
	if (block) release(block);
#endif
*/
}
