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
#define GRAINS_MAX_NUM 40

#define GRAIN_PLAYING 16
#define GRAIN_AVAILABLE 1
#define GRAIN_HAS_ENDED 32
#define GRAIN_TRIG 4
#define GRAIN_OVERLAP 8


static constexpr float MS_TO_BLOCK_SCALE =
		(AUDIO_SAMPLE_RATE_EXACT) / (AUDIO_BLOCK_SAMPLES * 1000.0);

inline __attribute__((always_inline)) uint32_t ms2block(float ms)
{
	return ms * MS_TO_BLOCK_SCALE +.5;
}

//For debug
#define ITER_TEST 0

#define PRINT_GRAIN 0
#define DEBUG_GRAIN_PRINT_DELAY_MEMBERS 0
#define DEBUG_GRAIN_PRINT_DELAY 0

struct GrainStruct
{
	uint16_t start = 0; //grain first position relative to head.
	uint16_t pos = 0; //next grain position in queue.
	uint16_t space = 0; //space between duration.
	uint16_t size = 50; //grain size in blocks
	int32_t magnitude = 1073741824;
	uint16_t sizePos = 0; //how many blocks have been played from start.
	uint16_t fade = 0;

	uint8_t state = GRAIN_AVAILABLE;

	GrainStruct * next = NULL;

#if PRINT_GRAIN
	#define DEBUG_GRAIN(N,G) { if(G) (G)->print(N); }
	void print(uint32_t index)
	{
		delay(DEBUG_GRAIN_PRINT_DELAY);
		Serial.print(index, DEC);
		Serial.print(": GrainStruct(");
		delay(DEBUG_GRAIN_PRINT_DELAY_MEMBERS);
		Serial.print(" state:");
		Serial.print(state, DEC);
		delay(DEBUG_GRAIN_PRINT_DELAY_MEMBERS);
		Serial.print(" , size:");
		Serial.print(size, DEC);
		delay(DEBUG_GRAIN_PRINT_DELAY_MEMBERS);
		Serial.print(" , sizePos:");
		Serial.print(sizePos, DEC);
		delay(DEBUG_GRAIN_PRINT_DELAY_MEMBERS);
		Serial.print(" , this:");
		Serial.print((size_t) this, HEX);
		delay(DEBUG_GRAIN_PRINT_DELAY_MEMBERS);
		Serial.print(" , next:");
		Serial.print((size_t) next, HEX);
		Serial.println(" )");
		delay(DEBUG_GRAIN_PRINT_DELAY_MEMBERS);
	}
#else
	#define DEBUG_GRAIN(G,N) {}
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
		mSender.state = GRAIN_TRIG;
		mAudioBuffer = NULL;
	}

	void init(AudioInputBuffer * aib)
	{
		mAudioBuffer = aib;
	}

	void pitch(float p);
	void durration(float ms);
	void pos(float ms);
	void space(float ms);
	void fade(float ms);
	void amplitude(float n);

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
		g.size = mResiver.size;
		g.start = mResiver.start;
		g.state = GRAIN_TRIG;
		g.sizePos = 0;
		g.magnitude = mResiver.magnitude;
		mResiving = false;
	}
private:

	GrainParameter( const GrainParameter& other ) : mAudioBuffer(other.mAudioBuffer) {} // non construction-copyable
	GrainParameter& operator=( const GrainParameter& ){} // non copyable
};

class AudioEffectGrainer : public AudioStream
{
private:

#if ITER_TEST
	int countTest = 1;
	void ZERO_COUNT() { countTest = 1; }
	void DEBUG_ADD_GRAIN(GrainStruct * grain)
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
	#define ZERO_COUNT() {}
	#define DEBUG_ADD_GRAIN(grain) {}
#endif

	uint32_t mTriggCount;
	uint32_t mTriggGrain;

	GrainStruct * mPlayGrain;
	GrainStruct * mFreeGrain;

	AudioInputBuffer mAudioBuffer;

	audio_block_t * mInputQueueArray[1];

	GrainStruct mGrains[GRAINS_MAX_NUM];

	GrainParameter mGrainParam;

	const int16_t * mWindow;

	void setBlock(audio_block_struct * out, GrainStruct* pGrain);

	inline __attribute__((always_inline))  GrainStruct * getFreeGrain();
	inline __attribute__((always_inline))  GrainStruct * freeGrain(GrainStruct * grain, GrainStruct* prev);

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
};

#endif /* EFFECT_PITCHSHIFTER_H_2_ */
