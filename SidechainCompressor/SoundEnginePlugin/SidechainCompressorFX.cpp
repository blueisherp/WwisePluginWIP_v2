/*******************************************************************************
The content of this file includes portions of the AUDIOKINETIC Wwise Technology
released in source code form as part of the SDK installer package.

Commercial License Usage

Licensees holding valid commercial licenses to the AUDIOKINETIC Wwise Technology
may use this file in accordance with the end user license agreement provided
with the software or, alternatively, in accordance with the terms contained in a
written agreement between you and Audiokinetic Inc.

Apache License Usage

Alternatively, this file may be used under the Apache License, Version 2.0 (the
"Apache License"); you may not use this file except in compliance with the
Apache License. You may obtain a copy of the Apache License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed
under the Apache License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
OR CONDITIONS OF ANY KIND, either express or implied. See the Apache License for
the specific language governing permissions and limitations under the License.

  Copyright (c) 2024 Audiokinetic Inc.
*******************************************************************************/

#include "SidechainCompressorFX.h"
#include "../SidechainCompressorConfig.h"

#include <AK/AkWwiseSDKVersion.h>

AK::IAkPlugin* CreateSidechainCompressorFX(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, SidechainCompressorFX());
}

AK::IAkPluginParam* CreateSidechainCompressorFXParams(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, SidechainCompressorFXParams());
}

AK_IMPLEMENT_PLUGIN_FACTORY(SidechainCompressorFX, AkPluginTypeEffect, SidechainCompressorConfig::CompanyID, SidechainCompressorConfig::PluginID)

SidechainCompressorFX::SidechainCompressorFX()
    : m_pParams(nullptr)
    , m_pAllocator(nullptr)
    , m_pContext(nullptr)
{
}

SidechainCompressorFX::~SidechainCompressorFX()
{
    /**/
    auto& v = m_sharedBuffer->objectIDList;
    if (std::find(v.begin(), v.end(), objectID) != v.end())
    {
        m_sharedBuffer->removeObjectID(objectID);
    }

    m_sharedBuffer->removeFromPriorityMap(objectID);
    /**/
}

AKRESULT SidechainCompressorFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{

    m_pParams = (SidechainCompressorFXParams*)in_pParams;
    m_pAllocator = in_pAllocator;
    m_pContext = in_pContext;
    SampleRate = in_rFormat.uSampleRate;
    priorityRank = m_pParams->RTPC.fPriorityRank;

    
    // Register object to sharedBuffer's list of objects
    if (in_pContext != nullptr)
    {
        objectID = in_pContext->GetAudioNodeID();
    }
    m_sharedBuffer->registerObjectID(objectID);
    m_sharedBuffer->AddToPriorityMap(objectID, priorityRank);
    

    return AK_Success;
}

#define AK_LINTODB( __lin__ ) (log10f(__lin__) * 20.f)

AKRESULT SidechainCompressorFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    // Unregister from list of objects
    auto& v = m_sharedBuffer->objectIDList;
    if (std::find(v.begin(), v.end(), objectID) != v.end())
    {
        m_sharedBuffer->removeObjectID(objectID);
    }

    m_sharedBuffer->removeFromPriorityMap(objectID);

    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT SidechainCompressorFX::Reset()
{
    return AK_Success;
}

AKRESULT SidechainCompressorFX::GetPluginInfo(AkPluginInfo& out_rPluginInfo)
{
    out_rPluginInfo.eType = AkPluginTypeEffect;
    out_rPluginInfo.bIsInPlace = false;
	out_rPluginInfo.bCanProcessObjects = false;
    out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
    
    return AK_Success;
}


void SidechainCompressorFX::Execute(AkAudioBuffer* in_pBuffer, AkUInt32 in_ulnOffset, AkAudioBuffer* out_pBuffer)
{
    const AkUInt32 uNumChannels = in_pBuffer->NumChannels();

    AkUInt16 uFramesConsumed;
    AkUInt16 uFramesProduced;

    const AkUInt32 frames10ms = SampleRate / 100;
    AkReal32 threshold = m_pParams->RTPC.fThreshold;


    // Reset Atomic counters
    if (m_sharedBuffer->numBuffersProcessed == m_sharedBuffer->objectIDList.size())
    {
        m_sharedBuffer->numBuffersProcessed.store(0, std::memory_order_relaxed);
        m_sharedBuffer->numBuffersCalculated.store(0, std::memory_order_relaxed);
    }

    // AkGlobalCallbackLocation_BeginRender;


    // SharedBuffer and RMSTable need to be reset
    if (m_sharedBuffer->isSharedBufferReady || 
        m_sharedBuffer->sharedBuffer.empty() || 
        m_sharedBuffer->numBuffersAdded == m_sharedBuffer->numBuffersProcessed)
    {
        m_sharedBuffer->resetSharedBuffer(in_pBuffer);
    }
    
    
    if (m_sharedBuffer->isRMSTableReady ||
        m_sharedBuffer->numBuffersProcessed == m_sharedBuffer->objectIDList.size() || 
        m_sharedBuffer->RMSTable.empty())
    {
        m_sharedBuffer->resetRMSTable();
    }
    
    // Add buffer to global shared buffer
    m_sharedBuffer->AddToSharedBuffer(in_pBuffer);
    
    // Add/update object ID and Priority Rank to PriorityMap
    m_sharedBuffer->AddToPriorityMap(objectID, priorityRank);

    // Buffer finished Calculating
    m_sharedBuffer->numBuffersCalculated.fetch_add(1, std::memory_order_relaxed);



    // Populate RMStable
    if (m_sharedBuffer->RMSTable.empty())
    {
        m_sharedBuffer->populateRMSTable(frames10ms);
    }

    // Get Percentile from Priority Map

    AkReal32 Percentile = m_sharedBuffer->getPercentile(objectID);
    

    AkReal32 realRatio = 0.0f;
    AkReal32 gainDB = 0.0f;

    for (AkUInt32 i = 0; i < uNumChannels; ++i)
    {
        AkReal32* AK_RESTRICT pInBuf = (AkReal32* AK_RESTRICT)in_pBuffer->GetChannel(i) + in_ulnOffset;
        AkReal32* AK_RESTRICT pOutBuf = (AkReal32* AK_RESTRICT)out_pBuffer->GetChannel(i) +  out_pBuffer->uValidFrames;

        
        auto& channelRMSTable = m_sharedBuffer->RMSTable[i];

        uFramesConsumed = 0;
        uFramesProduced = 0;
        while (uFramesConsumed < in_pBuffer->uValidFrames
            && uFramesProduced < out_pBuffer->MaxFrames())
        {

             // Get sharedMovingRMS from RMSTable

            AkReal32 sharedMovingRMS = m_sharedBuffer->lastbuffer_mRMS[i];
            if (!m_sharedBuffer->RMSTable.empty())
            {
                sharedMovingRMS = channelRMSTable[uFramesConsumed];
            }
            AkReal32 excessDB = AK_LINTODB(sharedMovingRMS) - threshold;
            // Calculate real Ratio

            realRatio = ((1 - Percentile) * (m_pParams->RTPC.fMaxRatio - 1)) + 1;

            // Do compression DSP

            if (AK_LINTODB(sharedMovingRMS) > threshold)
            {
               
                /* db to be compressed, a negative float
                gainDB =
                    (threshold - AK_LINTODB(sharedMovingRMS))        // difference between threshold and RMS
                    * (1 - (1 / realRatio));                       // apply ratio */
                    
                // alternate dsp
                AkReal32 y = (1 / realRatio) * excessDB;  // the new dB above the threshold, a positive float. excessDB is the x variable in the graph

                // db to be compressed, a negative float
                gainDB = y - excessDB;
                //
                
                // apply gain
                pOutBuf[uFramesConsumed] = pInBuf[uFramesConsumed] * AK_DBTOLIN(gainDB);
            }
            else
            {
                *pOutBuf++ = *pInBuf++;
            }
            
            ++uFramesConsumed;
            ++uFramesProduced;
        }
    }

    in_pBuffer->uValidFrames -= uFramesConsumed;
    out_pBuffer->uValidFrames += uFramesProduced;

    if (in_pBuffer->eState == AK_NoMoreData && in_pBuffer->uValidFrames == 0)
        out_pBuffer->eState = AK_NoMoreData;
    else if (out_pBuffer->uValidFrames == out_pBuffer->MaxFrames())
        out_pBuffer->eState = AK_DataReady;
    else
        out_pBuffer->eState = AK_DataNeeded;

    m_sharedBuffer->numBuffersProcessed.fetch_add(1, std::memory_order_relaxed);

#ifndef AK_OPTIMIZED
    if (m_pContext->CanPostMonitorData())
    {
        if (m_sharedBuffer->PriorityMap.size() == m_sharedBuffer->objectIDList.size())
        {
            errorMsg2 = "Map Complete";
        }


        // Monitor Data 1
        std::ostringstream reformat1, reformat2, reformat3;
        reformat1 << std::fixed << std::setprecision(2) << m_sharedBuffer->lastbuffer_mRMS[0];
        reformat2 << std::fixed << std::setprecision(2) << m_sharedBuffer->lastbuffer_mRMS[1];
        std::stringstream sstream1, sstream2;
        sstream1 << reformat1.str() << ", " << reformat2.str();
        std::string monitorData1 = sstream1.str();

        // Monitor Data 2 
        sstream2 << m_sharedBuffer->objectIDList.size() << ", " << m_sharedBuffer->numBuffersCalculated << ", " << m_sharedBuffer->numBuffersProcessed;
        std::string monitorData2 = sstream2.str();

        // Serialize
        std::string monitorData[2] = { monitorData1, monitorData2};

        m_pContext->PostMonitorData((void*)monitorData, sizeof(monitorData));
    }

#endif // !AK_OPTIMIZED
    

}

AKRESULT SidechainCompressorFX::TimeSkip(AkUInt32 &io_uFrames)
{
    return AK_DataReady;
}

void SidechainCompressorFX::doCalculations(AkAudioBuffer* sourceBuffer, AkGlobalCallbackLocation in_pCallback)
{
    if (in_pCallback == AkGlobalCallbackLocation_BeginRender)
    {
        // m_sharedBuffer->AddToSharedBuffer(sourceBuffer);
        // m_sharedBuffer->AddToPriorityMap(objectID, priorityRank);
        m_sharedBuffer->numBuffersCalculated.fetch_add(1, std::memory_order_relaxed);
    }
}


void SidechainCompressorFX::resetCalcs(AkGlobalCallbackLocation in_pCallback)
{
    if (in_pCallback == AkGlobalCallbackLocation_EndRender)
    {
        if (m_sharedBuffer->numBuffersProcessed == m_sharedBuffer->objectIDList.size())
        {
            // Reset stuff
        }
    }
}


void AKSOUNDENGINE_CALL SidechainCompressorFX::MyGlobalCallbackFunction(AK::IAkGlobalPluginContext* in_pContext, AkGlobalCallbackLocation in_eLocation, void* in_pCookie)
{
    
}


void SidechainCompressorFX::registerCallbacks()
{
    void* myCookie = sourceBuffer;
    AKRESULT result = AK::SoundEngine::RegisterGlobalCallback(&MyGlobalCallbackFunction, AkGlobalCallbackLocation_BeginRender, myCookie, AkPluginTypeNone, 0, 0);
}
