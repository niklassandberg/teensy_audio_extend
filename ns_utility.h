/*
 * ns_utility.h
 *
 *      Author: niksan
 */

#ifndef ns_utility_h_
#define ns_utility_h_

#include <stdint.h>
#include <utility/dspinst.h>

// computes sum + (((int64_t)a[31:0] * (int64_t)b[31:0] + 0x8000000) >> 32)
/*static inline int32_t multiply_accumulate_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t multiply_accumulate_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b)
{
#if defined (__ARM_ARCH_7EM__)
	int32_t out;
	asm volatile("smmlar %0, %2, %3, %1" : "=r" (out) : "r" (sum), "r" (a), "r" (b));
	return out;
#elif defined(KINETISL)
	return 0; // TODO....
#endif
}*/

// computes (a + b), result saturated to 32 bit unsigned integer range
static inline uint32_t signed_add_32_saturate(uint32_t a, uint32_t b) __attribute__((always_inline, unused));
static inline uint32_t signed_add_32_saturate(uint32_t a, uint32_t b)
{
	uint32_t out;
	asm volatile("qsub %0, %1, %2" : "=r" (out) : "r" (a), "r" (b));
	return out;
}

template<typename T, T WLPF_DIV>
struct KnobsFilter
{
	T mOut = 0;
	T mValue = 0;

	KnobsFilter() : mOut(0), mValue(0)
	{

	}

	KnobsFilter(KnobsFilter const& knob)
	{
		mOut = knob.mOut;
		mValue = knob.mValue;
	}

	void operator=(KnobsFilter const& knob)
	{
		if(knob==this) return;
		knob.mOut = mOut;
		knob.mValue = mValue;
	}

	KnobsFilter(T const& value) : mOut(value), mValue(value)
	{

	}

	inline T filter()
	{
		mOut = (T) multiply_accumulate_32x32_rshift32_rounded(mOut,mValue - mOut,WLPF_DIV);
		return mOut;
	}

	void operator=(T const& value)
	{
		mValue = value;
	}

	operator T()
	{
		return filter();
	}
};

// computes sum + (((int64_t)a[31:0] * (int64_t)b[31:0]) >> 32)
static inline int32_t multiply_accumulate_32x32_rshift32(int32_t sum, int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t multiply_accumulate_32x32_rshift32(int32_t sum, int32_t a, int32_t b)
{
#if defined (__ARM_ARCH_7EM__)
	int32_t out;
	asm volatile("smmla %0, %2, %3, %1" : "=r" (out) : "r" (sum), "r" (a), "r" (b));
	return out;
#elif defined(KINETISL)
	return sum + ((((int64_t)a * (int64_t)b)) >> 32);
#endif
}

// computes sum - (((int64_t)a[31:0] * (int64_t)b[31:0]) >> 32)
static inline int32_t multiply_subtract_32x32_rshift32(int32_t sum, int32_t a, int32_t b) __attribute__((always_inline, unused));
static inline int32_t multiply_subtract_32x32_rshift32(int32_t sum, int32_t a, int32_t b)
{
#if defined (__ARM_ARCH_7EM__)
	int32_t out;
	asm volatile("smmls %0, %2, %3, %1" : "=r" (out) : "r" (sum), "r" (a), "r" (b));
	return out;
#elif defined(KINETISL)
	return sum - ((((int64_t)a * (int64_t)b)) >> 32);
#endif
}

#endif