/* Audio Library for Teensy 3.X
 * Copyright (c) 2018, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

 #include <Arduino.h>
 #include "synth_wavetable_256.h"
 #include "arm_math.h"
 #include "utility/dspinst.h"
 
 // uncomment for more accurate but more computationally expensive frequency modulation
 //#define IMPROVE_EXPONENTIAL_ACCURACY
 #define BASE_AMPLITUDE 0x6000  // 0x7fff won't work due to Gibb's phenomenon, so use 3/4 of full range.
 


 void AudioWavetable256::update(void)
 {
     audio_block_t *block, *freqScanWavetable, *ampScanWavetable;
     int16_t *bp, *mbp=0, *end;
     int32_t val11, val12, val21, val22;
     int16_t magnitude15;
     uint32_t i, ph, scale;
     uint8_t index, index2; //wraps value. No need to do //if (index2 >= 256) index2 = 0;
     const uint32_t inc = phase_increment;
 
     ph = phase_accumulator + phase_offset;

     if(wave_tables==NULL) {
      return;
     }
 
     if (magnitude == 0) {
         phase_accumulator += inc * AUDIO_BLOCK_SAMPLES;
         return;
     }
     
     block = allocate();
     
     if (!block) {
         phase_accumulator += inc * AUDIO_BLOCK_SAMPLES;
         return;
     }
     
    freqScanWavetable = receiveReadOnly(0);
 
    bp = block->data;

    if(freqScanWavetable) {
      mbp = freqScanWavetable->data;
    }

    // len = 256
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
        index = ph >> 24;
        index2 = index + 1;
        //if (index2 >= 256) index2 = 0; //wrap
        
        
        uint32_t val = scanw;
        if(mbp) {
          int16_t modVal = *mbp++;
          int32_t positive = uint32_t(32768+int32_t(modVal)) << 16;
          if(modVal>=0) {
            val += multiply_32x32_rshift32( positive,modMagnitude);
          } else {
            val -= multiply_32x32_rshift32( positive,modMagnitude);
          }
        }
        uint8_t wtIndex0 = val >> 24;
        uint8_t wtIndex2 = wtIndex0 + 1; //wrap
    //if (index2 >= 256) index2 = 0; //wrap
    uint32_t interpolate = val & 0xFFFFFF;

    int16_t *arbdata1 = wave_tables[wtIndex0];
    int16_t *arbdata2 = wave_tables[wtIndex2];

    val11 = *(arbdata1 + index);
    val12 = *(arbdata1 + index2);
    val21 = *(arbdata2 + index); //TODO
    val22 = *(arbdata2 + index2); //TODO
    scale = (ph >> 8) & 0xFFFF;
    val12 *= scale;
    val11 *= 0x10000 - scale;
    val22 *= scale;
    val21 *= 0x10000 - scale;
    int32_t out1 = multiply_32x32_rshift32(val11 + val12, magnitude);
    int32_t out2 = multiply_32x32_rshift32(val21 + val22, magnitude);

    int32_t out_scan_1 = multiply_32x32_rshift32( (0x1000000 - interpolate)*0x10,out1*0x10);
    int32_t out_scan_2 = multiply_32x32_rshift32(interpolate*0x10,out2*0x10);
        *bp++ = out_scan_2 + out_scan_1;
        //*bp++ = out1;
        ph += inc;
    }

     phase_accumulator = ph - phase_offset;

     if (tone_offset) {
         bp = block->data;
         end = bp + AUDIO_BLOCK_SAMPLES;
         do {
             val11 = *bp;
             *bp++ = signed_saturate_rshift(val11 + tone_offset, 16, 0);
         } while (bp < end);
     }

     if(freqScanWavetable) release(freqScanWavetable);
     transmit(block, 0);
     release(block);
 }
 
 void AudioWavetable256Modulated::update(void)
{
  audio_block_t *block, *freqScanWavetable, *ampScanWavetable, *moddata;
	int16_t *bp, *mbp=0, *end;
  int32_t val11, val12, val21, val22;
	int16_t magnitude15;
	uint32_t i, ph, index, index2, scale, priorphase;
	const uint32_t inc = phase_increment;

	moddata = receiveReadOnly(1);

	// Pre-compute the phase angle for every output sample of this update
	ph = phase_accumulator;
	priorphase = phasedata[AUDIO_BLOCK_SAMPLES-1];
	if (moddata && modulation_type == 0) {
    debugFlag = 1;
		// Frequency Modulation
		bp = moddata->data;
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
			int32_t n = (*bp++) * modulation_factor; // n is # of octaves to mod
			int32_t ipart = n >> 27; // 4 integer bits
			n &= 0x7FFFFFF;          // 27 fractional bits
			#ifdef IMPROVE_EXPONENTIAL_ACCURACY
			// exp2 polynomial suggested by Stefan Stenzel on "music-dsp"
			// mail list, Wed, 3 Sep 2014 10:08:55 +0200
			int32_t x = n << 3;
			n = multiply_accumulate_32x32_rshift32_rounded(536870912, x, 1494202713);
			int32_t sq = multiply_32x32_rshift32_rounded(x, x);
			n = multiply_accumulate_32x32_rshift32_rounded(n, sq, 1934101615);
			n = n + (multiply_32x32_rshift32_rounded(sq,
				multiply_32x32_rshift32_rounded(x, 1358044250)) << 1);
			n = n << 1;
			#else
			// exp2 algorithm by Laurent de Soras
			// https://www.musicdsp.org/en/latest/Other/106-fast-exp2-approximation.html
			n = (n + 134217728) << 3;

			n = multiply_32x32_rshift32_rounded(n, n);
			n = multiply_32x32_rshift32_rounded(n, 715827883) << 3;
			n = n + 715827882;
			#endif
			uint32_t scale = n >> (14 - ipart);
			uint64_t phstep = (uint64_t)inc * scale;
			uint32_t phstep_msw = phstep >> 32;
			if (phstep_msw < 0x7FFE) {
				ph += phstep >> 16;
			} else {
				ph += 0x7FFE0000;
			}
			phasedata[i] = ph;
		}
		release(moddata);
	} else if (moddata) {
		// Phase Modulation
		bp = moddata->data;
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
			// more than +/- 180 deg shift by 32 bit overflow of "n"
		        uint32_t n = ((uint32_t)(*bp++)) * modulation_factor;
			phasedata[i] = ph + n;
			ph += inc;
		}
		release(moddata);
	} else {
		// No Modulation Input
		for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
			phasedata[i] = ph;
			ph += inc;
		}
	}
	phase_accumulator = ph;

  if(wave_tables==NULL) {
  return;
  }

  if (magnitude == 0) {
      return;
  }
  
  block = allocate();
  
  if (!block) {
      return;
  }
  
freqScanWavetable = receiveReadOnly(0);

bp = block->data;

if(freqScanWavetable) {
  mbp = freqScanWavetable->data;
}

// len = 256
for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
    ph = phasedata[i];
    index = ph >> 24;
    index2 = index + 1;
    //if (index2 >= 256) index2 = 0; //wrap
    
    
    uint32_t val = scanw;
    
    if(mbp) {
      int16_t modVal = *mbp++;
      int32_t positive = uint32_t(32768+int32_t(modVal)) << 16;
      if(modVal>=0) {
        val += multiply_32x32_rshift32( positive,modMagnitude);
      } else {
        val -= multiply_32x32_rshift32( positive,modMagnitude);
      }
    }
      
    uint8_t wtIndex0 = val >> 24;

    int debugNewIndex = wtIndex0;
    if(debugOldIndex != debugNewIndex) {
      Serial.print("new index:");
      Serial.println(debugNewIndex);
      debugOldIndex = debugNewIndex;
    }

    uint8_t wtIndex2 = wtIndex0 + 1; //wrap
    //if (index2 >= 256) index2 = 0; //wrap
    uint32_t interpolate = val & 0xFFFFFF;

    int16_t *arbdata1 = wave_tables[wtIndex0];
    int16_t *arbdata2 = wave_tables[wtIndex2];

    val11 = *(arbdata1 + index);
    val12 = *(arbdata1 + index2);
    val21 = *(arbdata2 + index); //TODO
    val22 = *(arbdata2 + index2); //TODO
    scale = (ph >> 8) & 0xFFFF;
    val12 *= scale;
    val11 *= 0x10000 - scale;
    val22 *= scale;
    val21 *= 0x10000 - scale;
    int32_t out1 = multiply_32x32_rshift32(val11 + val12, magnitude);
    int32_t out2 = multiply_32x32_rshift32(val21 + val22, magnitude);

    int32_t out_scan_1 = multiply_32x32_rshift32( (0x1000000 - interpolate)*0x10,out1*0x10);
    int32_t out_scan_2 = multiply_32x32_rshift32(interpolate*0x10,out2*0x10);
   // *bp++ = out_scan_2 + out_scan_1;
    *bp++ = out1;
}

  if (tone_offset) {
      bp = block->data;
      end = bp + AUDIO_BLOCK_SAMPLES;
      do {
          val11 = *bp;
          *bp++ = signed_saturate_rshift(val11 + tone_offset, 16, 0);
      } while (bp < end);
  }

  if(freqScanWavetable) release(freqScanWavetable);
  transmit(block, 0);
  release(block);
}