#pragma once

#include <vector>
#include <mutex>
#include <iostream>
#include <map>
#include <future>
#include <chrono>
#include <atomic>
#include <string>
#include <sstream>
#include <iomanip>
#include <shared_mutex>
#include <algorithm>
#include <thread>
#include <AK/SoundEngine/Common/AkCommonDefs.h>
#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <AK/SoundEngine/Common/AkSoundEngine.h>
#include <AK/SoundEngine/Common/AkCallback.h>


class SidechainCompressorSharedBuffer
{
public:
    SidechainCompressorSharedBuffer(std::map<AkUniqueID, AkReal32>& myPriorityMap);
	~SidechainCompressorSharedBuffer();

    void Init();

    std::vector<std::vector<AkReal32>> sharedBuffer;    // 2-d array of Channels (outer vector) and Frames (inner vector)
    AkUInt16 numBuffersAdded = 0;
    
    
    std::atomic<AkInt16> numBuffersCalculated = 0;
    std::map<AkUniqueID, AkReal32> PriorityMap;
    std::vector<std::vector<AkReal32>> RMSTable;        //This is a 2d-array. The outer vector (rows) is numChannels.  The inner vector (columns) is numSamples.


    std::string errorMsg = "Default Error Message";
    AkReal32 lastbuffer_mRMS[2] = { 0.0f, 0.0f };       // The moving RMS of the last L and R samples of the previous buffer
    AkReal32 newbuffer_mRMS[2] = { 0.0f, 0.0f };
    AkReal32 diff_mRMS[2] = { 0.0f, 0.0f };
    AkReal32 lastRMSAdded[2] = { 0.1f, 0.1f };

    void resizeSharedBuffer(AkAudioBuffer* sourceBuffer);


    void AddToSharedBuffer(AkAudioBuffer* sourceBuffer);

    void AddToPriorityMap(AkUniqueID objectID, AkReal32 PriorityRank);

    void updatePriorityMap(AkUniqueID objectID, AkReal32 PriorityRank);

    void removeFromPriorityMap(AkUniqueID objectID);



    float getPercentile(AkUniqueID objectID);           // returns percentile in decimal form. (1.00 = 100%)

    void populateRMSTable(AkUInt32 frames10ms);         // Intended to be used per buffer. frames10ms is 10 ms worth of frames. RMS is in linear

    void calculatedmRMS(AkUInt32 frames10ms);

    void resetRMSTable();
    void resetSharedBuffer();

private:
    
    std::mutex mtx; 
    std::shared_mutex s_mtx;
  
};

class GlobalManager
{
public:

    static std::shared_ptr<SidechainCompressorSharedBuffer>& getGlobalBuffer(std::map<AkUniqueID, AkReal32>& myPriorityMap)
    {
        static std::shared_ptr<SidechainCompressorSharedBuffer> g_ptr = std::make_shared<SidechainCompressorSharedBuffer>(myPriorityMap);
        return g_ptr;
    }

};

class SpinLock
{
private:
    std::atomic_flag locked = ATOMIC_FLAG_INIT;

public:
    void timedLock() {
        using namespace std::chrono;
        auto startTime = steady_clock::now();
        AkUInt16 timeout = 50; // timeout in miliseconds

        while (locked.test_and_set(std::memory_order_acquire)) 
        {
            std::this_thread::yield();

            if (duration_cast<milliseconds>(steady_clock::now() - startTime).count() > timeout)
            {
                break;
            }
        }
    }

    void unlock() {
        locked.clear(std::memory_order_release);
    }
};
