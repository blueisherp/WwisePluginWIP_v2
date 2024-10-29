#include "SidechainCompressorSharedBuffer.h"

SidechainCompressorSharedBuffer::SidechainCompressorSharedBuffer(std::map<AkUniqueID, AkReal32>& myPriorityMap)
    : PriorityMap(myPriorityMap)
{
}

SidechainCompressorSharedBuffer::~SidechainCompressorSharedBuffer()
{

}


void SidechainCompressorSharedBuffer::Init()
{
    
}

void SidechainCompressorSharedBuffer::registerObjectID(AkUniqueID objectID)
{
    std::lock_guard<std::mutex> lock(mtx);
    objectIDList.push_back(objectID);

}

void SidechainCompressorSharedBuffer::removeObjectID(AkUniqueID objectID)
{
    std::lock_guard<std::mutex> lock(mtx);

    
    std::vector<AkUniqueID>& v = objectIDList;

    if (std::find(v.begin(), v.end(), objectID) != v.end())
    {
        v.erase(std::remove(v.begin(), v.end(), objectID));
    }
    
}

void SidechainCompressorSharedBuffer::AddToSharedBuffer(AkAudioBuffer* sourceBuffer)
{
    std::lock_guard<std::mutex> lock(mtx);
    AkUInt32 numChannels = sharedBuffer.size();
    AkUInt32 numFrames = sharedBuffer[0].size();

    for (AkUInt32 channel = 0; channel < numChannels; channel++)
    {
        AkUInt32 frame = 0;
        numFrames = AkMin(sharedBuffer[channel].size(), sourceBuffer->uValidFrames);
        auto& thisChannel = sharedBuffer[channel];
        while (frame < numFrames)
            {
                AkReal32 &pData = thisChannel[frame];

                pData += sourceBuffer->GetChannel(channel)[frame];
                frame++;
            }
    }

    
    numBuffersAdded++;
   
    if (objectIDList.size() == numBuffersAdded)
    {
        isSharedBufferReady.store(true, std::memory_order_relaxed);
    }
  
}

void SidechainCompressorSharedBuffer::AddToPriorityMap(AkUniqueID objectID, AkReal32 PriorityRank)
{
    std::lock_guard<std::mutex> lock(mtx);

    if (PriorityMap.find(objectID) == PriorityMap.end())
    {
        PriorityMap.emplace(objectID, PriorityRank);
    }
    else
    {
        PriorityMap[objectID] = PriorityRank;
    }

    if (objectIDList.size() == PriorityMap.size())
    {
        isPriorityMapReady.store(true, std::memory_order_release);
    }

}

void SidechainCompressorSharedBuffer::addToPriorityList(AkUniqueID objectID, AkReal32 PriorityRank)
{
    std::lock_guard<std::mutex> lock(mtx);
    using pairtype = std::pair<AkUniqueID, AkReal32>;
    std::vector<pairtype>& v = PriorityList;

    auto myPriority = std::find_if(v.begin(), v.end(),
        [objectID](const auto& pair) {return pair.first == objectID; });

    if (myPriority == v.end()) // if this object is NOT already listed
    {
        v.push_back(std::make_pair(objectID, PriorityRank));
    }
    else if (myPriority->second != PriorityRank)
    {
        v.erase(myPriority);
        v.push_back(std::make_pair(objectID, PriorityRank));
    }

    if (objectIDList.size() == PriorityList.size())
    {
        isPriorityListReady.store(true, std::memory_order_release);
    }
    
}

void SidechainCompressorSharedBuffer::removeFromPriorityMap(AkUniqueID objectID)
{
    std::lock_guard<std::mutex> lock(mtx);
    using pairtype = std::pair<AkUniqueID, AkReal32>;
    std::map<AkUniqueID, AkReal32>& m = PriorityMap;

    if (m.find(objectID) != m.end())
    {
        m.erase(objectID);
    }
}

float SidechainCompressorSharedBuffer::getPercentile(AkUniqueID objectID)
{
    std::shared_lock<std::shared_mutex> lock(s_mtx);

    using pairtype = std::pair<AkUniqueID, AkReal32>;

    // Find Minimum
    pairtype min = *min_element(PriorityMap.begin(), PriorityMap.end(),
        [](const pairtype& p1, const pairtype& p2)
        { return p1.second < p2.second; });
    AkReal32 minValue = min.second;

    // Find Maximum
    pairtype max = *max_element(PriorityMap.begin(), PriorityMap.end(),
        [](const pairtype& p1, const pairtype& p2)
        { return p1.second < p2.second; });
    AkReal32 maxValue = max.second;


    // handles error and avoids dividing by zero
     if (PriorityMap.find(objectID) == PriorityMap.end() || minValue == maxValue)
     {
         return 0.5f;
     }
         
     // Gives Percentile
     return (PriorityMap[objectID] - minValue) / (maxValue - minValue);

}

float SidechainCompressorSharedBuffer::getPercentile2(AkUniqueID objectID)
{
    std::shared_lock<std::shared_mutex> lock(s_mtx);
    if (objectIDList.size() != PriorityMap.size())
    {
        return 0.5f;
    }

    using pairtype = std::pair<AkUniqueID, AkReal32>;

    // Find Minimum
    pairtype min = *min_element(PriorityList.cbegin(), PriorityList.cend(),
        [](const pairtype& left, const pairtype& right)
        { return left.second < right.second; });
    AkReal32 minValue = min.second;

    // Find Maximum
    pairtype max = *max_element(PriorityList.cbegin(), PriorityList.cend(),
        [](const pairtype& left, const pairtype& right)
        { return left.second < right.second; });
    AkReal32 maxValue = max.second;

    // Gives Percentile
    auto myPriority = std::find_if(PriorityList.begin(), PriorityList.end(),
        [objectID] (const auto& pair) {return pair.first == objectID; });

    if (myPriority != PriorityList.end() && minValue != maxValue)
    {
        return (myPriority->second - minValue) / (maxValue - minValue);
    }
    else // handles error and avoids dividing by zero
    {
        return 0.5f;
    }

    
}

void SidechainCompressorSharedBuffer::populateRMSTable(AkUInt32 frames10ms)
{
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<AkReal32> currentRMS = { lastbuffer_mRMS[0], lastbuffer_mRMS[1] };
    AkUInt16 numChannels = sharedBuffer.size();
    AkUInt32 numFrames = 0;

    
    if (RMSTable.empty() && !sharedBuffer.empty())
    {
        // Prepare rows/channels
        RMSTable.resize(numChannels);
        
        for (AkUInt16 channel = 0; channel < numChannels; ++channel)
        {
            AkUInt16 frame = 0;
            numFrames = sharedBuffer[channel].size();
            // Prepare columns/frames
            RMSTable[channel].resize(numFrames);

            while (frame < numFrames)
            {

                AkReal32 currentSample = sharedBuffer[channel][frame];
                currentRMS[channel] = sqrtf(
                    (
                        (powf(currentRMS[channel], 2) * ((frames10ms * numChannels) - 1)     // a fake sum of previous frames' squares
                        ) + powf(currentSample, 2)                                  // add square of new sample
                    ) / (frames10ms * numChannels)                                  // divide by frames to get new average of squares
                );                                                                  // square root everything

                RMSTable[channel][frame] = currentRMS[channel];
                frame++;
            }
        }
        
    }
    
    // update lastbuffer_mRMS with above calculations
    for (AkUInt16 i = 0; i < 2; i++)
    {
        lastbuffer_mRMS[i] = currentRMS[i];
    }

    if (!RMSTable.empty())
    {
        isRMSTableReady.store(true, std::memory_order_release);
    }
    

}



void SidechainCompressorSharedBuffer::resetSharedBuffer(AkAudioBuffer* sourceBuffer)
{
    std::lock_guard<std::mutex> lock(mtx);

    sharedBuffer.clear();
    numBuffersAdded = 0;
    isSharedBufferReady.store(false, std::memory_order_relaxed);

    // Resize rows to match sourceBuffer
    sharedBuffer.resize(sourceBuffer->NumChannels(), std::vector<AkReal32>(sourceBuffer->uValidFrames));
}

void SidechainCompressorSharedBuffer::resetPriorityMap()
{
    std::lock_guard<std::mutex> lock(mtx);

    PriorityMap.clear();
    PriorityList.clear();
    isPriorityMapReady.store(false, std::memory_order_release);
}

void SidechainCompressorSharedBuffer::globalCallback(AkGlobalCallbackLocation in_eLocation, void* in_pCallbackInfo, void* in_pUserData, AkAudioBuffer* sourceBuffer, AkUniqueID objectID, AkReal32 PriorityRank)
{
    if (in_eLocation == AkGlobalCallbackLocation_BeginRender)
    {
        for (auto& plugin : objectIDList)
        {
            AddToSharedBuffer(sourceBuffer);
            AddToPriorityMap(objectID, PriorityRank);
        }
    }
}

void SidechainCompressorSharedBuffer::resetRMSTable()
{
    std::lock_guard<std::mutex> lock(mtx);

    RMSTable.clear();
    isRMSTableReady.store(false, std::memory_order_release);

}


void SidechainCompressorSharedBuffer::waitForSharedBuffer()
{
    
    using namespace std::chrono;
    auto startTime = steady_clock::now();
    int timeoutInMilliseconds = 50; // timeout in miliseconds

    while (!isSharedBufferReady)
    {
        if (duration_cast<milliseconds>(steady_clock::now() - startTime).count() > timeoutInMilliseconds)
        {
            break; // Timeout exceeded, break the loop
        }

    }

}


void SidechainCompressorSharedBuffer::waitForRMSTable() 
{
    // pseudo-spinlock

    using namespace std::chrono;
    auto startTime = steady_clock::now();
    int timeoutInMilliseconds = 50; // timeout in miliseconds

    while (!isRMSTableReady)
    {
        if (duration_cast<milliseconds>(steady_clock::now() - startTime).count() > timeoutInMilliseconds)
        {
            break; // Timeout exceeded, break the loop
        }
    }
}


