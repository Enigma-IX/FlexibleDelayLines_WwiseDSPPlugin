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

  Copyright (c) 2025 Audiokinetic Inc.
*******************************************************************************/

#include "FlexibleDelayLinesFX.h"
#include "../FlexibleDelayLinesConfig.h"

#include <AK/AkWwiseSDKVersion.h>

AK::IAkPlugin* CreateFlexibleDelayLinesFX(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, FlexibleDelayLinesFX());
}

AK::IAkPluginParam* CreateFlexibleDelayLinesFXParams(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, FlexibleDelayLinesFXParams());
}

AK_IMPLEMENT_PLUGIN_FACTORY(FlexibleDelayLinesFX, AkPluginTypeEffect, FlexibleDelayLinesConfig::CompanyID, FlexibleDelayLinesConfig::PluginID)

FlexibleDelayLinesFX::FlexibleDelayLinesFX()
    : m_pParams(nullptr)
    , m_pAllocator(nullptr)
    , m_pContext(nullptr)
    , m_delayLines(nullptr)
    , m_numChannels(0)
    , m_sampleRate(48000.0f)
    , m_SamplesPerMeter(0.0f)
{
}

FlexibleDelayLinesFX::~FlexibleDelayLinesFX()
{
}

AKRESULT FlexibleDelayLinesFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{
    m_pParams = (FlexibleDelayLinesFXParams*)in_pParams;
    m_pAllocator = in_pAllocator;
    m_pContext = in_pContext;
    
    m_numChannels = in_rFormat.GetNumChannels();
    m_sampleRate = (float)in_rFormat.uSampleRate;
    m_SamplesPerMeter = m_sampleRate / SPEED_OF_SOUND;
    
    // Allocate delay line array
    m_delayLines = (DelayLineChannel*)AK_PLUGIN_ALLOC(in_pAllocator, sizeof(DelayLineChannel) * m_numChannels);
    if (m_delayLines == nullptr)
        return AK_InsufficientMemory;
    
    // Initialize each channel's delay line
    for (AkUInt32 i = 0; i < m_numChannels; ++i)
    {
        // Manual initialization instead of placement new
        m_delayLines[i].buffer = nullptr;
        m_delayLines[i].writePos = 0;
        m_delayLines[i].lastDelayTime = 0.0f;
        
        m_delayLines[i].buffer = (float*)AK_PLUGIN_ALLOC(in_pAllocator, sizeof(float) * MAX_BUFFER_LEN);
        if (m_delayLines[i].buffer == nullptr)
            return AK_InsufficientMemory;
        
        // Clear buffer
        memset(m_delayLines[i].buffer, 0, sizeof(float) * MAX_BUFFER_LEN);
        m_delayLines[i].writePos = 0;
        m_delayLines[i].lastDelayTime = m_pParams->RTPC.fDelayTime;
    }

    return AK_Success;
}

AKRESULT FlexibleDelayLinesFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    if (m_delayLines != nullptr)
    {
        for (AkUInt32 i = 0; i < m_numChannels; ++i)
        {
            if (m_delayLines[i].buffer)
            {
                AK_PLUGIN_FREE(in_pAllocator, m_delayLines[i].buffer);
            }
        }
        AK_PLUGIN_FREE(in_pAllocator, m_delayLines);
    }
    
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT FlexibleDelayLinesFX::Reset()
{
    for (AkUInt32 i = 0; i < m_numChannels; ++i)
    {
        if (m_delayLines[i].buffer != nullptr)
        {
            memset(m_delayLines[i].buffer, 0, sizeof(float) * MAX_BUFFER_LEN);
            m_delayLines[i].writePos = 0;
            m_delayLines[i].lastDelayTime = m_pParams->RTPC.fDelayTime;
        }
    }
    
    return AK_Success;
}

AKRESULT FlexibleDelayLinesFX::GetPluginInfo(AkPluginInfo& out_rPluginInfo)
{
    out_rPluginInfo.eType = AkPluginTypeEffect;
    out_rPluginInfo.bIsInPlace = true;
	out_rPluginInfo.bCanProcessObjects = false;
    out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
    return AK_Success;
}

void FlexibleDelayLinesFX::Execute(AkAudioBuffer* io_pBuffer)
{
    const AkUInt16 uValidFrames = io_pBuffer->uValidFrames;
    
    float currentDelayTime;
    if (m_pParams->RTPC.fDistance > 0.0f)
        currentDelayTime = (m_pParams->RTPC.fDistance * 2.0f) / SPEED_OF_SOUND;
    else
        currentDelayTime = m_pParams->RTPC.fDelayTime;
    
    float wetDryMix = m_pParams->RTPC.fWetDryMix;
    
    for (AkUInt32 chan = 0; chan < m_numChannels; ++chan)
    {
        DelayLineChannel& delayLine = m_delayLines[chan];
        float* pChannel = io_pBuffer->GetChannel(chan);
        
        float timeGradient = (currentDelayTime - delayLine.lastDelayTime) / (float)uValidFrames;
        float currDelayTime = delayLine.lastDelayTime;
        
        for (AkUInt16 frame = 0; frame < uValidFrames; ++frame)
        {
            
            float samplesDelayed = currDelayTime * m_sampleRate;
            int wholeSampleDelay = (int)samplesDelayed;
            float subSampleDelay = samplesDelayed - (float)wholeSampleDelay;
            
            int readPosA = (delayLine.writePos - wholeSampleDelay) & BIT_MASK;
            int readPosB = (delayLine.writePos - wholeSampleDelay - 1) & BIT_MASK;
            
            float valueA = delayLine.buffer[readPosA];
            float valueB = delayLine.buffer[readPosB];
            float delayedSample = Interpolate(valueA, valueB, subSampleDelay);
            
            float feedback = m_pParams->RTPC.fFeedback;
            float inputWithFeedback = pChannel[frame] + delayedSample * feedback;
            
            delayLine.buffer[delayLine.writePos] = inputWithFeedback;
            
            delayLine.writePos = (delayLine.writePos + 1) & BIT_MASK;
            
            pChannel[frame] = pChannel[frame] * (1.0f - wetDryMix) + delayedSample * wetDryMix;
            
            currDelayTime += timeGradient;
        }
        
        delayLine.lastDelayTime = currentDelayTime;
    }
}

AKRESULT FlexibleDelayLinesFX::TimeSkip(AkUInt32 in_uFrames)
{
    for (AkUInt32 chan = 0; chan < m_numChannels; ++chan)
    {
        m_delayLines[chan].writePos = (m_delayLines[chan].writePos + in_uFrames) & BIT_MASK;
    }
    
    return AK_DataReady;
}
