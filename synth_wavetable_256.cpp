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
     audio_block_t *block, *modavetable;
     int16_t *bp, *mbp, *end;
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
     
    modavetable = receiveReadOnly(0);
 
    bp = block->data;
    if(modavetable) 
      mbp = modavetable->data;

    if (!arbdata1) {
        release(block);
        phase_accumulator += inc * AUDIO_BLOCK_SAMPLES;
        return;
    }
    // len = 256
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
        index = ph >> 24;
        index2 = index + 1;
        //if (index2 >= 256) index2 = 0;

        //DOING WAVE SCAN - START
        int32_t mod_scanw1 = scanw;
        if(modavetable) mod_scanw1 += int32_t(*mbp++) << 15; //wraping int
        int32_t indexw1 = multiply_32x32_rshift32(mod_scanw1, waveTableLength)+waveTableHalfLength;
        int32_t indexw2 = indexw1 + 1;
        if(indexw2>=88) indexw2 = 0;
        //int32_t interp_saw = (multiply_32x32_rshift32(mod_scanw1,2147483647)+1073741824) - indexw1 * index_to_trunc_scale;
        int32_t interp_full = int64_t(mod_scanw1 + 2147483648)/2;//  - uint32_t(indexw1 * index_to_trunc_scale);
        int32_t interp_trunc = uint32_t(indexw1 * index_to_trunc_scale);

        int32_t interp_saw = interp_full - interp_trunc;
        
        /*
        Serial.print("indexw1: ");
        Serial.println(indexw1);
        Serial.print("interp_full: ");
        Serial.println(interp_full);
        Serial.print("interp_trunc: ");
        Serial.println(interp_trunc);
        Serial.print("interp_saw: ");
        Serial.println(interp_saw);
        */


      
        
        
        /*
        //TODO: optimice
        if (indexw1 < 0) {
          indexw1 = waveTableLength + indexw1;
        }
        if( old_indexw1 != indexw1 ) {
          uint32_t indexw2 = indexw1+1;
          while (indexw2 >= waveTableLength) {
            indexw2 = indexw2 - waveTableLength;
          }
          arbdata1 = wave_tables[1 ];
          arbdata2 = wave_tables[1];
          old_indexw1 = indexw1;
        }
        */

        //DOING WAVE SCAN - END

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

      *bp++ = out2;
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

     if(modavetable) release(modavetable);
     transmit(block, 0);
     release(block);
 }
 
 //--------------------------------------------------------------------------------
 
 void AudioWavetable256Modulated::update(void)
 {
     audio_block_t *block, *moddata, *shapedata;
     int16_t *bp, *end;
     int32_t val11, val12, val21, val22;
     int16_t magnitude15;
     uint32_t i, ph, index, index2, scale, priorphase;
     const uint32_t inc = phase_increment;
 
     moddata = receiveReadOnly(0);
     shapedata = receiveReadOnly(1);
 
     // Pre-compute the phase angle for every output sample of this update
     ph = phase_accumulator;
     priorphase = phasedata[AUDIO_BLOCK_SAMPLES-1];
     if (moddata && modulation_type == 0) {
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
 
     // If the amplitude is zero, no output, but phase still increments properly
     if (magnitude == 0) {
         if (shapedata) release(shapedata);
         return;
     }
     
     block = allocate();
 
     if (!block) {
         if (shapedata) release(shapedata);
         return;
     }
     
     bp = block->data;
 
     // Now generate the output samples using the pre-computed phase angles

    if (!arbdata1) {
        release(block);
        if (shapedata) release(shapedata);
        return;
    }
    // len = 256
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
        ph = phasedata[i];
        index = ph >> 24;
        index2 = index + 1;
        if (index2 >= 256) index2 = 0;
        val11 = *(arbdata1 + index);
        val12 = *(arbdata1 + index2);
        val21 = *(arbdata1 + index);
        val22 = *(arbdata1 + index2);
        scale = (ph >> 8) & 0xFFFF;
        val12 *= scale;
        val11 *= 0x10000 - scale;
        val22 *= scale;
        val21 *= 0x10000 - scale;
        *bp++ = multiply_32x32_rshift32(val11 + val12, magnitude);
    }
 
     if (tone_offset) {
         bp = block->data;
         end = bp + AUDIO_BLOCK_SAMPLES;
         do {
             val11 = *bp;
             *bp++ = signed_saturate_rshift(val11 + tone_offset, 16, 0);
         } while (bp < end);
     }
     if (shapedata) release(shapedata);
     transmit(block, 0);
     release(block);
 }
 
 
 // BandLimitedWaveform256
 
 
 #define SUPPORT_SHIFT 4
 #define SUPPORT (1 << SUPPORT_SHIFT)
 #define PTRMASK ((2 << SUPPORT_SHIFT) - 1)
 
 #define SCALE 16
 #define SCALE_MASK (SCALE-1)
 #define N (SCALE * SUPPORT * 2)
 
 #define GUARD_BITS 8
 #define GUARD      (1 << GUARD_BITS)
 #define HALF_GUARD (1 << (GUARD_BITS-1))
 
 
 #define DEG180 0x80000000u
 
 #define PHASE_SCALE (0x100000000L / (2 * BASE_AMPLITUDE))
 
 
 extern "C"
 {
   extern const int16_t step_table [258] ;
 }
 
 int32_t BandLimitedWaveform256::lookup (int offset)
 {
   int off = offset >> GUARD_BITS ;
   int frac = offset & (GUARD-1) ;
 
   int32_t a, b ;
   if (off < N/2)   // handle odd symmetry by reflecting table
   {
     a = step_table [off+1] ;
     b = step_table [off+2] ;
   }
   else
   {
     a = - step_table [N-off] ;
     b = - step_table [N-off-1] ;
   }
   return  BASE_AMPLITUDE + ((frac * b + (GUARD - frac) * a + HALF_GUARD) >> GUARD_BITS) ; // interpolated
 }
 
 // create a new step, apply its past waveform into the cyclic sample buffer
 // and add a step_state256 object into active list so it can be added for the future samples
 void BandLimitedWaveform256::insert_step (int offset, bool rising, int i)
 {
   while (offset <= (N/2-SCALE)<<GUARD_BITS)
   {
     if (offset >= 0)
       cyclic [i & 15] += rising ? lookup (offset) : -lookup (offset) ;
     offset += SCALE<<GUARD_BITS ;
     i ++ ;
   }
 
   states[newptr].offset = offset ;
   states[newptr].positive = rising ;
   newptr = (newptr+1) & PTRMASK ;
 }
 
 // generate value for current sample from one active step, checking for the
 // dc_offset adjustment at the end of the table.
 int32_t BandLimitedWaveform256::process_step (int i)
 {
   int off = states[i].offset ;
   bool positive = states[i].positive ;
 
   int32_t entry = lookup (off) ;
   off += SCALE<<GUARD_BITS ;
   states[i].offset = off ;  // update offset in table for next sample
   if (off >= N<<GUARD_BITS)             // at end of step table we alter dc_offset to extend the step into future
     dc_offset += positive ? 2*BASE_AMPLITUDE : -2*BASE_AMPLITUDE ;
 
   return positive ? entry : -entry ;
 }
 
 // process all active steps for current sample, basically generating the waveform portion
 // due only to steps
 // square waves use this directly.
 int32_t BandLimitedWaveform256::process_active_steps (uint32_t new_phase)
 {
   int32_t sample = dc_offset ;
   
   int step_count = (newptr - delptr) & PTRMASK ;
   if (step_count > 0)        // for any steps in-flight we sum in table entry and update its state
   {
     int i = newptr ;
     do
     {
       i = (i-1) & PTRMASK ;
       sample += process_step (i) ;
     } while (i != delptr) ;
     if (states[delptr].offset >= N<<GUARD_BITS)  // remove any finished entries from the buffer.
     {
       delptr = (delptr+1) & PTRMASK ;
       // can be upto two steps per sample now for pulses
       if (newptr != delptr && states[delptr].offset >= N<<GUARD_BITS)
     delptr = (delptr+1) & PTRMASK ;
     }
   }
   return sample ;
 }
 
 BandLimitedWaveform256::BandLimitedWaveform256()
 {
   newptr = 0 ;
   delptr = 0 ;
   dc_offset = BASE_AMPLITUDE ;
   phase_word = 0 ;
 }
 