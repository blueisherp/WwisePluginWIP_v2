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
    m_sharedBuffer->removeFromPriorityMap(objectID);
}

AKRESULT SidechainCompressorFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{

    m_pParams = (SidechainCompressorFXParams*)in_pParams;
    m_pAllocator = in_pAllocator;
    m_pContext = in_pContext;
    SampleRate = in_rFormat.uSampleRate;
    priorityRank = m_pParams->RTPC.fPriorityRank;

    /**/
    registerCallbacks();

    // Register object to sharedBuffer's list of objects
    if (in_pContext != nullptr)
    {
        objectID = in_pContext->GetAudioNodeID();
    }
    m_sharedBuffer->AddToPriorityMap(objectID, priorityRank);
    /**/
    

    return AK_Success;
}

#define AK_LINTODB( __lin__ ) (log10f(__lin__) * 20.f)

AKRESULT SidechainCompressorFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    // Unregister from list of objects
    
    m_sharedBuffer->removeFromPriorityMap(objectID);

    if (m_sharedBuffer->PriorityMap.empty())
    {
        unregisterCallbacks();
    }
    
    

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
    AkUInt32 uFramesConsumed;
    AkUInt32 uFramesProduced;
    AkReal32 threshold = m_pParams->RTPC.fThreshold;
    AkReal32 Percentile = m_sharedBuffer->getPercentile(objectID);
    AkReal32 realRatio = (Percentile * (m_pParams->RTPC.fMaxRatio - 1)) + 1;
    AkReal32 gainDB[2] = { 0.0f, 0.0f };
    AkReal32 knee = 1.0f;
    AkReal32 myRMS[2] = { 0.0f, 0.0f };
    auto& oldRMS = m_sharedBuffer->lastbuffer_mRMS;
    auto& newRMS = m_sharedBuffer->newbuffer_mRMS;
    AkReal32 rmsDiff[2] = { m_sharedBuffer->diff_mRMS[0], m_sharedBuffer->diff_mRMS[1] };


    m_sharedBuffer->updatePriorityMap(objectID, priorityRank);

    m_sharedBuffer->resetSharedBuffer();
    
    m_sharedBuffer->resizeSharedBuffer(in_pBuffer);

    m_sharedBuffer->AddToSharedBuffer(in_pBuffer);

    m_sharedBuffer->numBuffersCalculated.fetch_add(1, std::memory_order_relaxed);

    for (AkUInt32 i = 0; i < uNumChannels; ++i)
    {
        AkReal32* AK_RESTRICT pInBuf = (AkReal32* AK_RESTRICT)in_pBuffer->GetChannel(i) + in_ulnOffset;
        AkReal32* AK_RESTRICT pOutBuf = (AkReal32* AK_RESTRICT)out_pBuffer->GetChannel(i) +  out_pBuffer->uValidFrames;
        
        uFramesConsumed = 0;
        uFramesProduced = 0;
        auto& frame = uFramesConsumed;
        const auto& maxFrames = in_pBuffer->uValidFrames;
        while (uFramesConsumed < in_pBuffer->uValidFrames
            && uFramesProduced < out_pBuffer->MaxFrames())
        {
            // current RMS is somewhere between oldRMS and newRMS, based on the % of progress through the total amount of frames in the buffer
            myRMS[i] = oldRMS[i] + ((frame / maxFrames) * (newRMS[i] - oldRMS[i]));

            
            AkReal32 mySlope = newRMS[i] - oldRMS[i];
                
            // if difference is negligible, rmsDiff can match it
            if (abs(mySlope - rmsDiff[i]) < 0.0001){
                rmsDiff[i] = mySlope;}
            else{
                //shift rmsSlope toward the next RMS, at 50% strength
                rmsDiff[i] += (mySlope - rmsDiff[i]) * (0.5);}

            //update current RMS to follow rmsDiff
            myRMS[i] += (rmsDiff[i]/maxFrames);


            AkReal32 x = AK_LINTODB(myRMS[i]);
            AkReal32 y = x;

            // TODO: dsp in DB
            if (x > threshold + (knee / 2))
            {
                y = ((x - threshold) / realRatio) + threshold;
            }
            else if (x > threshold - (knee / 2))
            {
                AkReal32 m = ((1 / realRatio) - 1) / (2 * knee);
                y = x + (m * powf(x - (threshold - (knee / 2)), 2));
            }
            
            gainDB[i] = y - x;

            pOutBuf[frame] = pInBuf[frame] * AK_DBTOLIN(gainDB[i]);

            // make output same as input, effectively y = x
            //*pOutBuf++ = *pInBuf++;

            std::ostringstream reformat1, reformat2;
            if (i == 0){
                reformat1 << std::fixed << std::setprecision(2) << gainDB[i];
                errorMsg1 = reformat1.str();}
            else if (i == 1){
                reformat2 << std::fixed << std::setprecision(2) << gainDB[i];
                errorMsg2 = reformat2.str();}

 
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

    // do RMS table
    if (m_sharedBuffer->numBuffersCalculated >= m_sharedBuffer->PriorityMap.size())
    {
        m_sharedBuffer->diff_mRMS[0] = rmsDiff[0];
        m_sharedBuffer->diff_mRMS[1] = rmsDiff[1];
        m_sharedBuffer->calculatedmRMS(SampleRate / 100);
    }

    // Post Monitor Data
    monitorData();
}

AKRESULT SidechainCompressorFX::TimeSkip(AkUInt32 &io_uFrames)
{
    return AK_DataReady;
}


void SidechainCompressorFX::resetCalcs()
{
}

void SidechainCompressorFX::doCalcs()
{
    
}

void SidechainCompressorFX::doDSP()
{
    std::lock_guard<std::mutex> lock(mtx);
    /*
    const AkUInt32 uNumChannels = sourceBuffer->NumChannels();
    AkUInt16 uFramesProcessed;
    AkReal32 threshold = m_pParams->RTPC.fThreshold;
    AkReal32 Percentile = m_sharedBuffer->getPercentile(objectID);
    AkReal32 realRatio = 0.0f;
    AkReal32 gainDB = 0.0f;

    for (AkUInt32 i = 0; i < uNumChannels; ++i)
    {
        AkReal32* pBuf = (AkReal32 * )sourceBuffer->GetChannel(i);
        auto& channelRMSTable = m_sharedBuffer->RMSTable[i];

        uFramesProcessed = 0;
        while (uFramesProcessed < sourceBuffer->uValidFrames)
        {

            // Get sharedMovingRMS from RMSTable
            AkReal32 sharedMovingRMS = m_sharedBuffer->lastbuffer_mRMS[i];
            if (!m_sharedBuffer->RMSTable.empty())
            {
                sharedMovingRMS = channelRMSTable[uFramesProcessed];
            }
            AkReal32 excessDB = AK_LINTODB(sharedMovingRMS) - threshold;

            // Calculate real Ratio

            realRatio = ((1 - Percentile) * (m_pParams->RTPC.fMaxRatio - 1)) + 1;

            // Do compression DSP

            if (AK_LINTODB(sharedMovingRMS) > threshold)
            {
                

                AkReal32 y = (1 / realRatio) * excessDB;  // the new dB above the threshold, a positive float. excessDB is the x variable in the graph

                // db to be compressed, a negative float
                gainDB = y - excessDB;

                // apply gain
                pBuf[uFramesProcessed] = pBuf[uFramesProcessed] * AK_DBTOLIN(gainDB);
                
                pBuf[uFramesProcessed] *= 0.5f;
            }
            
            ++uFramesProcessed;
        }

    }
    */
    
}

void SidechainCompressorFX::monitorData()
{
#ifndef AK_OPTIMIZED

    
    if (m_pContext->CanPostMonitorData())
    {

        // Monitor Data 1
        std::ostringstream reformat1, reformat2, reformat3, reformat4;
        reformat1 << std::fixed << std::setprecision(2) << AK_LINTODB(m_sharedBuffer->lastbuffer_mRMS[0]);
        reformat2 << std::fixed << std::setprecision(2) << AK_LINTODB(m_sharedBuffer->lastbuffer_mRMS[1]);
        std::stringstream sstream1, sstream2;
        sstream1 << reformat1.str() << ", " << reformat2.str();

        std::string monitorData1 = sstream1.str();

        // Monitor Data 2 
        AkUInt32 data1 = 0;
        AkUInt32 data2 = 0;
        if (!m_sharedBuffer->sharedBuffer.empty())
        {
            reformat3 << std::fixed << std::setprecision(3) << m_sharedBuffer->diff_mRMS[0] * 100;
            reformat4 << std::fixed << std::setprecision(3) << m_sharedBuffer->diff_mRMS[1] * 100;
        }
        sstream2 << errorMsg1 << ", " << errorMsg2;
        
        std::string monitorData2 = sstream2.str();
        
        // Serialize
        std::string monitorData[2] = { monitorData1, monitorData2 };

        m_pContext->PostMonitorData((void*)monitorData, sizeof(monitorData));

    }
    

#endif // !AK_OPTIMIZED

    

}

void AKSOUNDENGINE_CALL SidechainCompressorFX::BeginRenderCallback(AK::IAkGlobalPluginContext* in_pContext, AkGlobalCallbackLocation in_eLocation, void* in_pCookie)
{
    SidechainCompressorFX* me = (SidechainCompressorFX*)in_pCookie;
}

void AKSOUNDENGINE_CALL SidechainCompressorFX::EndCallback(AK::IAkGlobalPluginContext* in_pContext, AkGlobalCallbackLocation in_eLocation, void* in_pCookie)
{
    
    SidechainCompressorFX* me = (SidechainCompressorFX*)in_pCookie;

}


void SidechainCompressorFX::registerCallbacks()
{
    m_pContext->GlobalContext()->RegisterGlobalCallback(AkPluginTypeEffect, 64, 25358, BeginRenderCallback, AkGlobalCallbackLocation_BeginRender, this);
    m_pContext->GlobalContext()->RegisterGlobalCallback(AkPluginTypeEffect, 64, 25358, EndCallback, AkGlobalCallbackLocation_End, this);
    
}

void SidechainCompressorFX::unregisterCallbacks()
{
    m_pContext->GlobalContext()->UnregisterGlobalCallback(BeginRenderCallback, AkGlobalCallbackLocation_BeginRender);
    m_pContext->GlobalContext()->UnregisterGlobalCallback(EndCallback, AkGlobalCallbackLocation_EndRender);
    
}