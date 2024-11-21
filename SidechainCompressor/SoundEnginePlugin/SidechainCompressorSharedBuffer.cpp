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

void SidechainCompressorSharedBuffer::resizeSharedBuffer(AkAudioBuffer* sourceBuffer)
{
    std::lock_guard<std::mutex> lock(mtx);

    if (sourceBuffer->NumChannels() != sharedBuffer.size() || sourceBuffer->uValidFrames != sharedBuffer[0].size())
    {
        sharedBuffer.resize(sourceBuffer->NumChannels(), std::vector<AkReal32>(sourceBuffer->uValidFrames));
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
        AkReal32* AK_RESTRICT sourceChannel = (AkReal32 * AK_RESTRICT)sourceBuffer->GetChannel(channel);
        while (frame < numFrames)
            {
                AkReal32 &thisSample = thisChannel[frame];

                thisSample += sourceBuffer->GetChannel(channel)[frame];
                
                frame++;
            }
    }

    numBuffersAdded++;
   
}



void SidechainCompressorSharedBuffer::AddToPriorityMap(AkUniqueID objectID, AkReal32 PriorityRank)
{
    std::lock_guard<std::mutex> lock(mtx);

   
    PriorityMap.insert_or_assign(objectID, PriorityRank);
  

}

void SidechainCompressorSharedBuffer::updatePriorityMap(AkUniqueID objectID, AkReal32 PriorityRank)
{
    std::lock_guard<std::mutex> lock(mtx);
    std::map<AkUniqueID, AkReal32>& m = PriorityMap;
    if (m.find(objectID) != m.end())
    {
        m.insert_or_assign(objectID, PriorityRank);
    }
}

void SidechainCompressorSharedBuffer::removeFromPriorityMap(AkUniqueID objectID)
{
    std::lock_guard<std::mutex> lock(mtx);
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
         return 1 - (1.0 / PriorityMap.size());
     }
         
     // Gives Percentile
     return 1 - ((PriorityMap[objectID] - minValue) / (maxValue - minValue));

}


void SidechainCompressorSharedBuffer::populateRMSTable(AkUInt32 frames10ms)
{
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<AkReal32> currentRMS = { lastbuffer_mRMS[0], lastbuffer_mRMS[1] };
    AkUInt16 numChannels = sharedBuffer.size();
    AkUInt32 numFrames = sharedBuffer[0].size();


    if (RMSTable.empty() && !sharedBuffer.empty())
    {
        if (RMSTable.size() < numChannels)
        {
            RMSTable.resize(numChannels);
        }
        
        for (AkUInt16 channel = 0; channel < numChannels; ++channel)
        {
            AkUInt16 frame = 0;
            std::vector<AkReal32>& currentChannel = sharedBuffer[channel];
            std::vector<AkReal32>& RMSChannel = RMSTable[channel];
            numFrames = currentChannel.size();
            // Prepare columns/frames
            if (RMSChannel.size() < numFrames)
            {
                RMSTable[channel].resize(numFrames);
            }
            
            while (frame < numFrames)
            {

                AkReal32& currentSample = currentChannel[frame];
                currentRMS[channel] = sqrtf(
                    (
                        (powf(currentRMS[channel], 2) * ((frames10ms * numChannels) - 1)     // a fake sum of previous frames' squares
                            ) + powf(currentSample, 2)                                      // add square of new sample
                        ) / (frames10ms * numChannels)                                      // divide by frames to get new average of squares
                );                                                                          // square root everything


                RMSChannel[frame] = currentRMS[channel];
                frame++;
            }
        }

    }

    // update lastbuffer_mRMS with above calculations
    for (AkUInt16 i = 0; i < 2; i++)
    {

        lastbuffer_mRMS[i] = currentRMS[i];
    }

}

void SidechainCompressorSharedBuffer::calculatedmRMS(AkUInt32 frames10ms)
{
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<AkReal32> currentRMS = { newbuffer_mRMS[0], newbuffer_mRMS[1] };
    AkUInt16 numChannels = sharedBuffer.size();
    AkUInt32 numFrames = sharedBuffer[0].size();

    // update lastbuffer_mRMS
    lastbuffer_mRMS[0] = newbuffer_mRMS[0];
    lastbuffer_mRMS[1] = newbuffer_mRMS[1];
    

    // calculated new mRMS
    if (!sharedBuffer.empty())
    {
        for (AkUInt16 channel = 0; channel < numChannels; ++channel)
        {
            AkUInt16 frame = 0;
            std::vector<AkReal32>& currentChannel = sharedBuffer[channel];
            numFrames = currentChannel.size();

            while (frame < numFrames)
            {

                AkReal32& currentSample = currentChannel[frame];
                currentRMS[channel] = sqrtf(
                    (
                        (powf(currentRMS[channel], 2) * ((frames10ms * numChannels) - 1)    // a fake sum of previous frames' squares
                            ) + powf(currentSample, 2)                                      // add square of new sample
                        ) / (frames10ms * numChannels)                                      // divide by frames to get new average of squares
                );                                                                          // square root everything

                frame++;
            }
        }
    }

    // update newbuffer_mRMS
    newbuffer_mRMS[0] = currentRMS[0];
    newbuffer_mRMS[1] = currentRMS[1];
}

void SidechainCompressorSharedBuffer::resetSharedBuffer()
{
    std::lock_guard<std::mutex> lock(mtx);
    if (numBuffersCalculated >= PriorityMap.size())
    {
        sharedBuffer.clear();
        numBuffersCalculated.store(0, std::memory_order_relaxed);
    }
    
}


void SidechainCompressorSharedBuffer::resetRMSTable()
{
    std::lock_guard<std::mutex> lock(mtx);
    if (numBuffersCalculated >= PriorityMap.size())
    {
        RMSTable.clear();
    }
    

}



