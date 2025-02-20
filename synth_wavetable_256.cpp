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

     if(debugFlag == 0) debugFlag = 1;

     if(wave_tables==NULL) {
      debugFlag = 1111;
      return;
     }
 
     if (magnitude == 0) {
         phase_accumulator += inc * AUDIO_BLOCK_SAMPLES;
         debugFlag = 2222;
         return;
     }
     
     block = allocate();
     
     debugFlag = 2;

     if (!block) {
         phase_accumulator += inc * AUDIO_BLOCK_SAMPLES;
         return;
     }
     
    freqScanWavetable = receiveReadOnly(0);
 
    bp = block->data;

    if(freqScanWavetable) {
      mbp = freqScanWavetable->data;
    }

    
    if(debugFlag == 2) debugFlag = 3;

    // len = 256
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
        index = ph >> 24;
        index2 = index + 1;
        //if (index2 >= 256) index2 = 0; //wrap
        
        
        uint32_t val = scanw;
        /*if(mbp) {
          //TODO: just remake....
          val += multiply_32x32_rshift32(int32_t(*mbp++)*0x10000+0xFFFF,modMagnitude);
        }*/

        

        uint8_t wtIndex0 = val >> 24;
        uint8_t wtIndex2 = wtIndex0 + 1; //wrap
        //if (index2 >= 256) index2 = 0; //wrap
        uint32_t interpolate = val & 0xFFFFFF;
        //int32_t interpolate = (val >> 8) & 0xFFFF;

        //DOING WAVE SCAN - END

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

        if(debugFlag == 3) debugFlag = 4;

        int32_t out_scan_1 = multiply_32x32_rshift32( (0x1000000 - interpolate)*0x10,out1*0x10);
        int32_t out_scan_2 = multiply_32x32_rshift32(interpolate*0x10,out2*0x10);
        *bp++ = out_scan_2 + out_scan_1;
        //*bp++ = out1;
        ph += inc;
        
        if(debugFlag == 4) debugFlag = 5;
    }

     phase_accumulator = ph - phase_offset;
 

     if(debugFlag == 5) debugFlag = 6;

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
 