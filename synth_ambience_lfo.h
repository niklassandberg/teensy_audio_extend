#ifndef AudioSynthAmbienceLfo_h
#define AudioSynthAmbienceLfo_h

#include <Audio.h>

class AudioSynthAmbienceLfo : public AudioStream
{
public:
    AudioSynthAmbienceLfo() : AudioStream(0, NULL), mFloor(0), mAcc(0), mOrder(0), mIncr(0), mDataPrev(0), mFlipp(false) {}

    void increment(uint16_t incr)
    {
    	mIncr = incr;
    }

    void order(uint16_t order) {
    	mOrder = order;
    }

    void floor(uint16_t floor) {
    	mFloor = floor;
    }

    virtual void update(void)
    {
        audio_block_t *block;
        block = allocate();

        if (!block) return;

        uint16_t count;
        int16_t *bp = block->data;

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
		{
        	mAcc += mIncr;
        	count = mAcc >> order;
        	if(count) {
        		mAcc -= count << order;
        	}
        	if(mFlipp) {
        		*bp++ = mDataPrev - count;
        	}
        	else {
        		*bp++ = mDataPrev + count;
        	}
        	mDataPrev = (*bp);
        	if( abs(mDataPrev) > mFloor ) {
        		mFlipp = !mFlipp;
        	}
		}

		transmit(block);
		release(block);
    }

private:
    uint16_t mFloor;
    uint16_t mAcc;
    uint16_t mOrder;
    uint16_t mIncr;

    int16_t mDataPrev;
    boolean mFlipp;
};

#endif
