#pragma once

#include <vector>
#include <mutex>
#include <iostream>
#include <map>
#include <future>
#include <condition_variable>
#include <AK/SoundEngine/Common/AkCommonDefs.h>
#include <AK/SoundEngine/Common/IAkPlugin.h>


class SidechainCompressorSharedBuffer
{
public:
    SidechainCompressorSharedBuffer(AK::IAkGlobalPluginContext* context, std::map<AkUniqueID, AkReal32>& myPriorityMap);
	~SidechainCompressorSharedBuffer();

    void Init(AK::IAkGlobalPluginContext* context);

    std::shared_ptr<AkAudioBuffer> sharedBuffer;
    AkUInt16 numBuffersAdded = 0;
    std::map<AkUniqueID, AkReal32> PriorityMap;
    std::vector<AkUniqueID> objectIDList;
    std::vector<std::vector<AkReal32>> RMSTable;        //This is a 2d-array. The outer vector (rows) is numChannels.  The inner vector (columns) is numSamples.

    void registerObjectID(AkUniqueID objectID);

    // TODO: unregisterObjectID for when plugin destructs

    void AddToSharedBuffer(AkAudioBuffer* sourceBuffer);

    void AddToPriorityMap(AkUniqueID objectID, AkReal32 PriorityRank);

    float getPercentile(AkUniqueID objectID);           // returns percentile in decimal form. (1.00 = 100%)

    void populateRMSTable(AkUInt32 frames10ms);         // Intended to be used per buffer. frames10ms is 10 ms worth of frames. RMS is in linear


    // Replace with spin locks or atomic counters
    void waitForSharedBufferAndPriorityMap();
    void waitForRMSTable();
    // TODO: resync all threads

    void resetRMSTable();
    void resetSharedBuffer();
    void resetPriorityMap();

private:
    
    std::mutex mtx; 
    AkReal32 lastbuffer_mRMS = 0.0f;                           // The moving RMS of the last sample of the previous buffer
    std::atomic<bool> isPriorityMapReady{ false };
    std::atomic<bool> isSharedBufferReady{ false };
    std::atomic<bool> isRMSTableReady{ false };
    
};
