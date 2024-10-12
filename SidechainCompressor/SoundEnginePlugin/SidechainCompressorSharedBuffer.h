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
#include <AK/SoundEngine/Common/AkCommonDefs.h>
#include <AK/SoundEngine/Common/IAkPlugin.h>


class SidechainCompressorSharedBuffer
{
public:
    SidechainCompressorSharedBuffer(std::map<AkUniqueID, AkReal32>& myPriorityMap);
	~SidechainCompressorSharedBuffer();

    void Init();

    std::vector<std::vector<AkReal32>> sharedBuffer; // 2-d array of Channels (outer vector) and Frames (inner vector)
    AkUInt16 numBuffersAdded = 0;
    std::map<AkUniqueID, AkReal32> PriorityMap = {};
    std::vector<AkUniqueID> objectIDList;
    std::vector<std::vector<AkReal32>> RMSTable;        //This is a 2d-array. The outer vector (rows) is numChannels.  The inner vector (columns) is numSamples.
    std::atomic<bool> isPriorityMapReady{ false };
    std::atomic<bool> isRMSTableReady{ false };
    std::atomic<bool> isSharedBufferReady{ false };
    std::string errorMsg = "Default Error Message";
    AkReal32 lastbuffer_mRMS[2] = { 0.0f, 0.0f };       // The moving RMS of the last L and R samples of the previous buffer

    void registerObjectID(AkUniqueID objectID);

    void removeObjectID(AkUniqueID objectID);

    void AddToSharedBuffer(AkAudioBuffer* sourceBuffer);

    void AddToPriorityMap(AkUniqueID objectID, AkReal32 PriorityRank);

    float getPercentile(AkUniqueID objectID);           // returns percentile in decimal form. (1.00 = 100%)

    void populateRMSTable(AkUInt32 frames10ms);         // Intended to be used per buffer. frames10ms is 10 ms worth of frames. RMS is in linear


    void waitForSharedBufferAndPriorityMap(); 
    void waitForRMSTable(); 

    void resetRMSTable();
    void resetSharedBuffer(AkAudioBuffer* sourceBuffer);
    void resetPriorityMap();

private:
    
    std::mutex mtx; 
  
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

