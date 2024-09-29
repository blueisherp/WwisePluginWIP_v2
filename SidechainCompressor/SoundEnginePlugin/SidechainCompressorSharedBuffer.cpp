#include "SidechainCompressorSharedBuffer.h"

SidechainCompressorSharedBuffer::SidechainCompressorSharedBuffer(AK::IAkGlobalPluginContext* context
    , std::map<AkUniqueID, AkReal32>& myPriorityMap)
{
    if (!context)
    {
        return;
    }
	Init(context);
    
}

SidechainCompressorSharedBuffer::~SidechainCompressorSharedBuffer()
{
}


void SidechainCompressorSharedBuffer::Init(AK::IAkGlobalPluginContext* context)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (!context)
    {
        // Error: context is null
        return;
    }

    sharedBuffer = std::make_shared<AkAudioBuffer>();

    // Initializing buffer variables
    sharedBuffer->uValidFrames = context->GetMaxBufferLength();
    sharedBuffer->eState = AK_DataReady;
    sharedBuffer->ZeroPadToMaxFrames();
    
}

void SidechainCompressorSharedBuffer::registerObjectID(AkUniqueID objectID)
{
    std::lock_guard<std::mutex> lock(mtx);
    objectIDList.push_back(objectID);

}

void SidechainCompressorSharedBuffer::AddToSharedBuffer(AkAudioBuffer* sourceBuffer)
{
    std::lock_guard<std::mutex> lock(mtx);

    for (AkUInt16 channel = 0; channel < sourceBuffer->NumChannels(); ++channel)
    {
        AkUInt16 uFrames = 0;
        AkUInt16 minFrames = AkMin(sourceBuffer->uValidFrames, sharedBuffer->uValidFrames);

        while (uFrames < minFrames)
        {
            sharedBuffer->GetChannel(channel)[uFrames] += sourceBuffer->GetChannel(channel)[uFrames];
            uFrames++;
        }
    }

    numBuffersAdded++;

    if (objectIDList.size() == numBuffersAdded)
    {
        isSharedBufferReady.store(true, std::memory_order_release);
 
    }
}

void SidechainCompressorSharedBuffer::AddToPriorityMap(AkUniqueID objectID, AkReal32 PriorityRank)
{
    std::lock_guard<std::mutex> lock(mtx);

    PriorityMap.insert_or_assign(objectID,PriorityRank);

    if (objectIDList.size() == PriorityMap.size())
    {
        isPriorityMapReady.store(true, std::memory_order_release);


    }
}

float SidechainCompressorSharedBuffer::getPercentile(AkUniqueID objectID)
{
    if (objectIDList.size() != PriorityMap.size())
    {
        return 0.0f;
    }

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
         return 0.0f;
     }
         
     // Gives Percentile
     return (PriorityMap[objectID] - minValue) / (maxValue - minValue);

}

void SidechainCompressorSharedBuffer::populateRMSTable(AkUInt32 frames10ms)
{
    std::lock_guard<std::mutex> lock(mtx);
    AkReal32 currentRMS = lastbuffer_mRMS;
    AkUInt16 numChannels = sharedBuffer->NumChannels();
    AkUInt32 numSamples = sharedBuffer->uValidFrames;

    if (RMSTable.empty() && sharedBuffer->HasData())
    {
        RMSTable.resize(numChannels, std::vector<AkReal32>(numSamples));

        for (AkUInt16 channel = 0; channel < sharedBuffer->NumChannels(); ++channel)
        {
            AkUInt16 sample = 0;

            while (sample < sharedBuffer->uValidFrames)
            {
                std::vector<AkReal32> RMSTableSample;
                AkReal32 ampfactor = sharedBuffer->GetChannel(channel)[sample];
                currentRMS = sqrtf(
                    (
                        (powf(currentRMS, 2) * ((frames10ms * numChannels) - 1)     // a fake sum of previous frames' squares
                        ) + powf(ampfactor, 2)                                      // add square of new sample
                    ) / (frames10ms * numChannels)                                  // divide by frames to get new average of squares
                );                                                                  // square root everything

                RMSTable[channel][sample] = currentRMS;
                sample++;
            }

        }
    }

    isRMSTableReady.store(true, std::memory_order_release);
    lastbuffer_mRMS = currentRMS;
}



void SidechainCompressorSharedBuffer::resetSharedBuffer()
{
    std::lock_guard<std::mutex> lock(mtx);
    if (sharedBuffer->HasData())
    {
        sharedBuffer->ClearData();
        numBuffersAdded = 0;
        sharedBuffer->ZeroPadToMaxFrames();
        isSharedBufferReady.store(false, std::memory_order_release);
    }
}

void SidechainCompressorSharedBuffer::resetPriorityMap()
{
    std::lock_guard<std::mutex> lock(mtx);
    if (!PriorityMap.empty())
    {
        PriorityMap.clear();
        isPriorityMapReady.store(false, std::memory_order_release);
    }

}

void SidechainCompressorSharedBuffer::resetRMSTable()
{
    std::lock_guard<std::mutex> lock(mtx);
    if (!RMSTable.empty())
    {
        RMSTable.clear();
        isRMSTableReady.store(false, std::memory_order_release);
    }
}


void SidechainCompressorSharedBuffer::waitForSharedBufferAndPriorityMap()
{
    while (!isSharedBufferReady || !isPriorityMapReady)
    {
        // pseudo-spinlock
    }
}


void SidechainCompressorSharedBuffer::waitForRMSTable()
{
    while (!isRMSTableReady)
    {
        // pseudo-spinlock
    }
}




