/*
 * effect_pitchshifter.h
 *
 *  Created on: 10 feb. 2018
 *      Author: niksan
 */

#ifndef EFFECT_PITCHSHIFTER_H_2_
#define EFFECT_PITCHSHIFTER_H_2_

#include "Arduino.h"
#include "AudioStream.h"
#include "utility/dspinst.h"

#if defined(__MK66FX1M0__)
  // 2.41 second maximum on Teensy 3.6
  #define GRAIN_BLOCK_QUEUE_SIZE  (106496 / AUDIO_BLOCK_SAMPLES)

  //2,9661538461538461538461538461538 second maximum on Teensy 3.6
  //#define GRAIN_BLOCK_QUEUE_SIZE 1024
#elif defined(__MK64FX512__)
  // 1.67 second maximum on Teensy 3.5
  #define GRAIN_BLOCK_QUEUE_SIZE  (73728 / AUDIO_BLOCK_SAMPLES)
#elif defined(__MK20DX256__)
  // 0.45 second maximum on Teensy 3.1 & 3.2
  #define GRAIN_BLOCK_QUEUE_SIZE  (19826 / AUDIO_BLOCK_SAMPLES)
#else
  // 0.14 second maximum on Teensy 3.0
  #define GRAIN_BLOCK_QUEUE_SIZE  (6144 / AUDIO_BLOCK_SAMPLES)
#endif

#define WINDOW_SIZE 256

// windows.c
extern "C" {
extern const int16_t AudioWindowHanning256[];
extern const int16_t AudioWindowBartlett256[];
extern const int16_t AudioWindowBlackman256[];
extern const int16_t AudioWindowFlattop256[];
extern const int16_t AudioWindowBlackmanHarris256[];
extern const int16_t AudioWindowNuttall256[];
extern const int16_t AudioWindowBlackmanNuttall256[];
extern const int16_t AudioWindowWelch256[];
extern const int16_t AudioWindowHamming256[];
extern const int16_t AudioWindowCosine256[];
extern const int16_t AudioWindowTukey256[];
}

//BUGG
//becomes unstable if more like 50
#define GRAINS_MAX_NUM 50

static constexpr float GRAINS_EVEN_SPREAD_TRIG_SCALE = 1.f/float(GRAINS_MAX_NUM);

//For debug
#define DEBUG_TRIG_ITER_MODE 0
#define DEBUG_PRINT_GRAIN_MODE 0

#define DEBUG_PRINT_GRAIN_DELAY_ON_MEMBERS 0
#define DEBUG_PRINT_GRAIN_DELAY 0

static constexpr float AUDIO_BLOCK_SAMPLES_FLOAT = float(AUDIO_BLOCK_SAMPLES);

static constexpr float MS_TO_SAMPLE_SCALE = AUDIO_SAMPLE_RATE_EXACT / 1000.f;

static constexpr float MS_TO_BLOCK_SCALE =
		(AUDIO_SAMPLE_RATE_EXACT) / (float(AUDIO_BLOCK_SAMPLES) * 1000.f);
static constexpr float BLOCK_TO_MS_SCALE =
		(float(AUDIO_BLOCK_SAMPLES) * 1000.f) / (AUDIO_SAMPLE_RATE_EXACT);

static constexpr float MAX_BUFFERT_MS = BLOCK_TO_MS_SCALE * GRAIN_BLOCK_QUEUE_SIZE;

static constexpr uint32_t AUDIO_BLOCK_SAMPLES_24_BITOP = (uint32_t) ((uint64_t)AUDIO_BLOCK_SAMPLES << 24) - 1;
//static constexpr uint32_t AUDIO_BLOCK_SAMPLES_24_BITOP = UINT32_MAX >> 1;
//static constexpr uint32_t AUDIO_BLOCK_SAMPLES_24_BITOP = UINT32_MAX >> 1;

template<int i> struct ShiftOp
{
	static const int result = 1 + ShiftOp<(i-1)/2>::result;
};

template<> struct ShiftOp<0>
{
	static const int result = 0;
};

inline __attribute__((always_inline)) uint32_t ms2block(float ms)
{
	return ms * MS_TO_BLOCK_SCALE +.5f;
}

inline __attribute__((always_inline)) float block2ms(uint32_t blocks)
{
	return float(blocks) * BLOCK_TO_MS_SCALE;
}

inline __attribute__((always_inline)) uint32_t ms2sample(float ms)
{
	return MS_TO_SAMPLE_SCALE * ms + .5;
}

inline __attribute__((always_inline)) uint32_t getBlockPosition(uint32_t samples)
{
	return samples >> ShiftOp<AUDIO_BLOCK_SAMPLES>::result;
}

inline __attribute__((always_inline)) uint32_t samplePos(uint32_t block)
{
	// Note: sample pos is from 0-127.. now it is 0. therefore 2^16
	return block << ShiftOp<AUDIO_BLOCK_SAMPLES>::result;
}

inline __attribute__((always_inline)) uint32_t getSampleIndex(uint32_t samples)
{
	return samples & (AUDIO_BLOCK_SAMPLES-1);
}

struct GrainStruct
{
	float saved_pitchRatio = 0.f;
	uint32_t saved_sampleStart = 0;

	uint32_t sampleStart = 0; //grain first position relative to head.
	uint32_t buffertPosition = 0; //next grain position in queue.
	uint32_t size = ms2sample(100); //grain size
	int32_t magnitude[4]={0,0,0,0}; //volume per channel
	uint32_t position = 0; //position relative to size

	uint32_t windowPhaseAccumulator=0;
	uint32_t windowPhaseIncrement=0;
	uint32_t grainPhaseAccumulator=0;
	uint32_t grainPhaseIncrement=0;

	bool dead = true;

#if PRINT_GRAIN
	#define DEBUG_PRINT_GRAIN(N,G) { if(G) (G)->print(N); }
	void print(uint32_t index)
	{
		delay(DEBUG_PRINT_GRAIN_DELAY);
		Serial.print(index, DEC);
		Serial.print(": GrainStruct(");
		delay(DEBUG_PRINT_GRAIN_DELAY_ON_MEMBERS);
		Serial.print(" , size:");
		Serial.print(size, DEC);
		delay(DEBUG_PRINT_GRAIN_DELAY_ON_MEMBERS);
		Serial.print(" , sizePos:");
		Serial.print(position, DEC);
		delay(DEBUG_PRINT_GRAIN_DELAY_ON_MEMBERS);
		Serial.print(" , this:");
		Serial.print((size_t) this, HEX);
		delay(DEBUG_PRINT_GRAIN_DELAY_ON_MEMBERS);
		Serial.print(" , next:");
		Serial.print((size_t) next, HEX);
		Serial.println(" )");
		delay(DEBUG_PRINT_GRAIN_DELAY_ON_MEMBERS);
	}
#else
	#define DEBUG_PRINT_GRAIN(G,N) {}
	void print(uint8_t index) {}
#endif

//	GrainStruct(){}
private:
//
//	GrainStruct( const GrainStruct& other ){} // non construction-copyable
//	GrainStruct& operator=( const GrainStruct& ){} // non copyable

};

struct AudioInputBuffer
{
	bool isFilled;
	bool freeze;
	uint32_t head;
	uint32_t tail;
	uint32_t len;
	audio_block_t *data[GRAIN_BLOCK_QUEUE_SIZE];

	uint32_t sampleSize = samplePos(GRAIN_BLOCK_QUEUE_SIZE);

	AudioInputBuffer()
	{
		len = GRAIN_BLOCK_QUEUE_SIZE;
		head = 0;
		tail = 0;
		isFilled = false;
		freeze = false;
		memset(data, 0, sizeof(data));
	}
};

class AudioEffectGrainer : public AudioStream
{
private:

#if DEBUG_TRIG_ITER_MODE
	int countTest = 1;
	void DEBUG_TRIG_ITER_ZERO_COUNT() { countTest = 1; }
	void DEBUG_TRIG_ITER_ADD_GRAIN(GrainStruct * grain)
	{
		Serial.print("added grain, iter(");
		Serial.print(countTest++, DEC);
		Serial.print("), grain(");
		Serial.print((size_t) (grain), HEX);
		Serial.print(", next: ");
		Serial.println((size_t) (grain->next), HEX);
		++countTest; \
	}
#else
	#define DEBUG_TRIG_ITER_ZERO_COUNT() {}
	#define DEBUG_TRIG_ITER_ADD_GRAIN(grain) {}
#endif

	bool mDisableChannel[4]={false,false,false,false};

	uint32_t mTriggCount;
	uint32_t mTriggGrain;
	uint32_t mSavedTriggGrain;

	AudioInputBuffer mAudioBuffer;

	audio_block_t * mInputQueueArray[1];

	GrainStruct mGrains[GRAINS_MAX_NUM];

	GrainStruct * mPlayGrains[GRAINS_MAX_NUM];

	int32_t mGrainBlock[AUDIO_BLOCK_SAMPLES];

	GrainStruct mResiver;

	const int16_t * mWindow;

	uint32_t mConcurrentGrains;

	bool writeGrainBlock(GrainStruct* pGrain);
	void resive(GrainStruct * g);

	inline __attribute__((always_inline))
		bool allocateOutputs(audio_block_t* out[4]);
	inline __attribute__((always_inline))
		void setOutputs(audio_block_t* out[4], GrainStruct* grain);
	inline __attribute__((always_inline))
		void transmitOutputs(audio_block_t* out[4]);

public:

	AudioEffectGrainer();

	bool fillAudioBuffer();

	virtual void update(void);

	void freezer(bool f);

	//TODO: IMPL!!
	//void numberOfGrains(uint8_t n);


	void pitch(float p);
	void durration(float ms);
	void pos(float ms);
	void amplitude(uint8_t ch, float n);


	void queueLength(uint16_t l);
	void interval(float ms);

	float bufferMS();

	inline __attribute__((always_inline)) uint32_t concurrentGrains()
	{
		return mConcurrentGrains;
	}

	void disable(uint8_t ch)
	{
		if(ch>3) return;
		mDisableChannel[ch] = true;
	}

	void adjustPosition();
	void adjustInterval();
};

#endif /* EFFECT_PITCHSHIFTER_H_2_ */
