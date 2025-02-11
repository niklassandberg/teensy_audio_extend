#ifndef effect_sinwavefolder_h_
#define effect_sinwavefolder_h_
#include "Arduino.h"
#include "AudioStream.h"
#include "utility/dspinst.h"
#include "ns_utility.h"

class AudioEffectSinWaveFolder : public AudioStream
{
public:
	AudioEffectSinWaveFolder() : mStages(1), mOffset(0), AudioStream(3, inputQueueArray) { }
	virtual void update(void);

	void setStages(uint32_t stages)
	{
		if(stages<1) mStages = 1;
		else mStages = stages;
	}

	void setAmp(int32_t amp)
	{
		Serial.println(amp);
		if(amp<0) mAmp = 0;
		else mAmp = amp;
	}


	void setOffset(int32_t offset)
	{
		if(offset > 1073741823) mOffset = 1073741823;
		else if(offset < -1073741823) mOffset = -1073741823;
		else mOffset = offset;
	}
private:

	uint32_t mStages;
	KnobsFilter<int32_t,67108856> mOffset;
	KnobsFilter<int32_t,67108856> mAmp;

//	KnobsFilter<uint32_t,4294967295> mKnopStages;
//	KnobsFilter<int32_t,67108856> mKnopOffset;
//	KnobsFilter<int32_t,67108856> mKnopAmp;


	//Taken from: https://www.pjrc.com/high-precision-sine-wave-synthesis-using-taylor-series/

	// High accuracy 11th order Taylor Series Approximation
	// input is 0 to 0xFFFFFFFF, representing 0 to 360 degree phase
	// output is 32 bit signed integer, top 25 bits should be very good
	static int32_t taylorSine(uint32_t ph)
	{
	        int32_t angle, sum, p1, p2, p3, p5, p7, p9, p11;

	        if (ph >= 0xC0000000 || ph < 0x40000000) {
	                angle = (int32_t)ph; // valid from -90 to +90 degrees
	        } else {
	                angle = (int32_t)(0x80000000u - ph);
	        }
	        p1 =  multiply_32x32_rshift32_rounded(angle << 1, 1686629713);
	        p2 =  multiply_32x32_rshift32_rounded(p1, p1) << 3;
	        p3 =  multiply_32x32_rshift32_rounded(p2, p1) << 3;
	        sum = multiply_subtract_32x32_rshift32_rounded(p1 << 1, p3, 1431655765);
	        p5 =  multiply_32x32_rshift32_rounded(p3, p2) << 1;
	        sum = multiply_accumulate_32x32_rshift32_rounded(sum, p5, 286331153);
	        p7 =  multiply_32x32_rshift32_rounded(p5, p2);
	        sum = multiply_subtract_32x32_rshift32_rounded(sum, p7, 54539267);
	        p9 =  multiply_32x32_rshift32_rounded(p7, p2);
	        sum = multiply_accumulate_32x32_rshift32_rounded(sum, p9, 6059919);
	        p11 = multiply_32x32_rshift32_rounded(p9, p2);
	        sum = multiply_subtract_32x32_rshift32_rounded(sum, p11, 440721);
	        return sum <<= 1;
	}



	static uint32_t int16ToUint32(int32_t value)
	{
		return static_cast<uint32_t>( value + 32768 ) * 65537;
	}

	static int32_t int16ToInt32Possitive(int32_t value)
	{
		return (value + 32768) * 32769;
	}

	static int32_t int16ToInt32(int16_t value)
	{
		return static_cast<int32_t>(value) * 65538;
	}

	static int16_t int32ToInt16(int32_t value)
	{
		return signed_saturate_rshift(value, 17, 16);
		//return static_cast<int16_t>(float(value)/65538.f);
	}

	audio_block_t *inputQueueArray[3];
};

#endif
