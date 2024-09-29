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
    , g_sharedBuffer(nullptr)
{
}

SidechainCompressorFX::~SidechainCompressorFX()
{
    
}

AKRESULT SidechainCompressorFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{

    m_pParams = (SidechainCompressorFXParams*)in_pParams;
    m_pAllocator = in_pAllocator;
    m_pContext = in_pContext;
    SampleRate = in_rFormat.uSampleRate;


    if (in_pContext == nullptr || in_pContext->GlobalContext() == nullptr)
    {
        errorMsg = "Context Null";
        return AK_Fail;
    }
    
    
    if (!g_sharedBuffer)
    {
        
        g_sharedBuffer = std::make_shared<SidechainCompressorSharedBuffer>(in_pContext->GlobalContext(), priorityMapTemp);
        if (!g_sharedBuffer)
        {
            errorMsg = "Could not initiate Shared Buffer";
            return AK_Fail;
        }
       
    }
 

    
    objectID = m_pContext->GetAudioNodeID();

    // Register object to g_sharedBuffer's list of objects
    g_sharedBuffer->registerObjectID(objectID);
    
    return AK_Success;
}

#define AK_LINTODB( __lin__ ) (log10f(__lin__) * 20.f)

AKRESULT SidechainCompressorFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    // TODO unregister object ID

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

    AkReal32 priorityRank = m_pParams->RTPC.fPriorityRank;
    const AkReal32 frames10ms = SampleRate / 100;
    AkReal32 threshold = m_pParams->RTPC.fThreshold;

    
    // SharedBuffer, PriorityMap, PercentileMap, and RMSTable need to be reset
    g_sharedBuffer->resetSharedBuffer();
    g_sharedBuffer->resetPriorityMap();
    g_sharedBuffer->resetRMSTable();

    /*
    // Add buffer to global shared buffer
    g_sharedBuffer->AddToSharedBuffer(in_pBuffer);

    // Add object ID and Priority Rank to PriorityMap
    g_sharedBuffer->AddToPriorityMap(objectID, priorityRank);

    // TODO: Wait for SharedBuffer and Priority Map to be done
    g_sharedBuffer->waitForPriorityMap();
    g_sharedBuffer->waitForSharedBuffer();

    // Populate RMStable

    g_sharedBuffer->populateRMSTable(frames10ms);

    // Get Percentile from Priority Map

    AkReal32 Percentile = g_sharedBuffer->getPercentile(objectID);

    // TODO: Wait for RMSTable to finish
    g_sharedBuffer->waitForRMSTable();

    */

    for (AkUInt32 i = 0; i < uNumChannels; ++i)
    {
        AkReal32* AK_RESTRICT pInBuf = (AkReal32* AK_RESTRICT)in_pBuffer->GetChannel(i) + in_ulnOffset;
        AkReal32* AK_RESTRICT pOutBuf = (AkReal32* AK_RESTRICT)out_pBuffer->GetChannel(i) +  out_pBuffer->uValidFrames;

        //auto& channelRMSTable = g_sharedBuffer->RMSTable[i];

        uFramesConsumed = 0;
        uFramesProduced = 0;
        while (uFramesConsumed < in_pBuffer->uValidFrames
            && uFramesProduced < out_pBuffer->MaxFrames())
        {
            /*
            AkReal32& AFSample = pInBuf[uFramesConsumed];
 
             // Get sharedMovingRMS from RMSTable

            AkReal32 sharedMovingRMS = channelRMSTable[uFramesConsumed];

            // Calculate real Ratio multiplier

            AkReal32 realRatioMulti = (1 - Percentile) * (1 - (1 / m_pParams->RTPC.fMaxRatio));
    
             // Do compression DSP

            if (AK_LINTODB(sharedMovingRMS) > threshold)
            {
                AkReal32 dbCompress = realRatioMulti * (threshold - AK_LINTODB(sharedMovingRMS));

                AFSample *= AK_DBTOLIN(dbCompress);
            }
            
            pOutBuf[uFramesProduced] = AFSample;

            */

            *pOutBuf++ = *pInBuf++;
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

    // TODO: resync all threads
}

AKRESULT SidechainCompressorFX::TimeSkip(AkUInt32 &io_uFrames)
{
    return AK_DataReady;
}
