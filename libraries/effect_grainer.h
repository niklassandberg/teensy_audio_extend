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
//becomes unstable if more like 40
#define GRAINS_MAX_NUM 20

//For debug
#define DEBUG_TRIG_ITER_MODE 0
#define DEBUG_PRINT_GRAIN_MODE 0

#define DEBUG_PRINT_GRAIN_DELAY_ON_MEMBERS 0
#define DEBUG_PRINT_GRAIN_DELAY 0

static constexpr float MS_TO_SAMPLE_SCALE = AUDIO_SAMPLE_RATE_EXACT / 1000.0;

static constexpr float MS_TO_BLOCK_SCALE =
		(AUDIO_SAMPLE_RATE_EXACT) / (float(AUDIO_BLOCK_SAMPLES) * 1000.0);
static constexpr float BLOCK_TO_MS_SCALE =
		(float(AUDIO_BLOCK_SAMPLES) * 1000.0) / (AUDIO_SAMPLE_RATE_EXACT);

static constexpr float MAX_BUFFERT_MS = BLOCK_TO_MS_SCALE * GRAIN_BLOCK_QUEUE_SIZE;

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
	return ms * MS_TO_BLOCK_SCALE +.5;
}

inline __attribute__((always_inline)) float block2ms(uint32_t blocks)
{
	return float(blocks) * BLOCK_TO_MS_SCALE;
}

inline __attribute__((always_inline)) uint32_t ms2sample(float ms)
{
	return MS_TO_SAMPLE_SCALE * ms + .5;
}

inline __attribute__((always_inline)) uint32_t blockPos(uint32_t samples)
{
	return (samples >> ShiftOp<AUDIO_BLOCK_SAMPLES>::result) >> 14;
}

inline __attribute__((always_inline)) uint32_t samplePos(uint32_t block)
{
	// Note: sample pos is from 0-127.. now it is 0. therefore 2^16
	return (block << ShiftOp<AUDIO_BLOCK_SAMPLES>::result) << 14;
}

inline __attribute__((always_inline)) uint32_t sampleIndex(uint32_t samples)
{
	return (samples >> 14) & (AUDIO_BLOCK_SAMPLES-1);
}


struct GrainStruct
{
	uint32_t start = 0; //grain first position relative to head.
	uint32_t buffSamplePos = 0; //next grain position in queue.
	uint32_t size = 50; //grain size
	int32_t magnitude[4];
	//how many blocks have been played from start.
	//Zero sizePos indicates the start.
	uint32_t sizePos = 0;

	uint32_t window_phase_accumulator;
	uint32_t window_phase_increment;

	//uint32_t grain_phase_accumulator;
	uint32_t grain_phase_increment;

	GrainStruct * next = NULL;

	GrainStruct()
	{
		for (uint8_t ch = 0; ch < 4; ++ch)
			magnitude[ch] = 0;
	}

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
		Serial.print(sizePos, DEC);
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
	uint16_t head;
	uint16_t tail;
	uint16_t len;
	audio_block_t *data[GRAIN_BLOCK_QUEUE_SIZE];

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

class GrainParameter {
public:
	const AudioInputBuffer * mAudioBuffer;
	GrainStruct mResiver;
	GrainStruct mSender;
	bool mResiving;
	GrainParameter()
	{
		delay(10); Serial.println("GrainParameter: created!!!!");
		mResiving = false;
		mAudioBuffer = NULL;
	}

	void init(AudioInputBuffer * aib)
	{
		mAudioBuffer = aib;
	}

	void pitch(float p);
	void durration(float ms);
	void pos(float ms);
	void amplitude(uint8_t ch, float n);

	bool send()
	{
		//TODO: that about polling in teensy or interrupts?
		if(mResiving) return false;
		mResiver = mSender;
		return true;
	}

	void resive(GrainStruct & g)
	{
		mResiving = true;
		g.start = mResiver.start;
		g.grain_phase_increment = mResiver.grain_phase_increment;
		g.window_phase_increment = mResiver.window_phase_increment;
		g.size = mResiver.size;
		g.sizePos = 0;
		g.window_phase_accumulator = 0;
		for ( uint8_t ch = 0; ch < 3; ++ch )
			g.magnitude[ch] = mResiver.magnitude[ch];

		mResiving = false;
	}
private:

	GrainParameter( const GrainParameter& other ); // non construction-copyable
	GrainParameter& operator=( const GrainParameter& grain); // non copyable
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

	uint32_t mTriggCount;
	uint32_t mTriggGrain;

	GrainStruct * mPlayGrain;
	GrainStruct * mFreeGrain;

	AudioInputBuffer mAudioBuffer;

	audio_block_t * mInputQueueArray[1];

	GrainStruct mGrains[GRAINS_MAX_NUM];

	int32_t mGrainBlock[AUDIO_BLOCK_SAMPLES];

	GrainParameter mGrainParam;

	const int16_t * mWindow;

	bool setGrainBlock(GrainStruct* pGrain);

	inline __attribute__((always_inline))  GrainStruct * getFreeGrain();
	inline __attribute__((always_inline))  GrainStruct * freeGrain(GrainStruct * grain, GrainStruct* prev);

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
	GrainParameter * next();
	void grainFreeze(bool f);

	//void numberOfGrains(uint8_t n);
	void queueLength(uint16_t l);
	void interval(float ms);

	float bufferMS();
};

#endif /* EFFECT_PITCHSHIFTER_H_2_ */
