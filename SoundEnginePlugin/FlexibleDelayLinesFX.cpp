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
    , m_pDelayLines(nullptr)
    , m_uNumChannels(0)
    , m_fSampleRate(48000.0f)
    , m_fSamplesPerMeter(0.0f)
    , m_pFIRCoefficients(nullptr)
    , m_FIRLength(0)
{
}

FlexibleDelayLinesFX::~FlexibleDelayLinesFX()
{
}

void FlexibleDelayLinesFX::InitializePowerComplementaryTable()
{
    float oneOverTwoNminusOne = 1.0f / (float)((m_powerCompTableSize - 1) << 1);
    
    for (int i = 0; i < m_powerCompTableSize; ++i)
    {
        float val = sinf((float)i * PI * oneOverTwoNminusOne);
        m_powerCompTable[i] = val * val;
    }
}

void FlexibleDelayLinesFX::InitializeFIRCoefficients(int oversampleFactor)
{
    if (oversampleFactor <= 1)
        return;
    
    m_FIRLength = 8 * oversampleFactor;
    
    m_pFIRCoefficients = (float*)AK_PLUGIN_ALLOC(m_pAllocator, sizeof(float) * m_FIRLength);
    if (!m_pFIRCoefficients)
    {
        m_FIRLength = 0;
        return;
    }
    
    float cutoff = 1.0f / (float)oversampleFactor;
    int center = m_FIRLength / 2;
    
    for (int i =0; i < m_FIRLength; ++i)
    {
        int n = i - center;
        if (n == 0)
        {
            m_pFIRCoefficients[i] = 2.0f * cutoff;
        }
        else
        {
            float sinc = sinf(2.0f * PI * cutoff * (float)n) / (PI * (float)n);
            
            float window = 0.42f - 0.5f * cosf(2.0f * PI * (float)i / (float)(m_FIRLength - 1))
                         + 0.08f * cosf(4.0f * PI * (float)i / (float)(m_FIRLength - 1));
            
            m_pFIRCoefficients[i] = sinc * window;
        }
    }
    
    float sum = 0.0f;
    for (int i = 0; i < m_FIRLength; ++i)
        sum += m_pFIRCoefficients[i];
    
    if (sum > 0.0f)
    {
        for (int i = 0; i < m_FIRLength; ++i)
            m_pFIRCoefficients[i] /= sum;
    }
}

AKRESULT FlexibleDelayLinesFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{
    m_pParams = (FlexibleDelayLinesFXParams*)in_pParams;
    m_pAllocator = in_pAllocator;
    m_pContext = in_pContext;
    
    m_uNumChannels = in_rFormat.GetNumChannels();
    m_fSampleRate = (float)in_rFormat.uSampleRate;
    m_fSamplesPerMeter = m_fSampleRate / SPEED_OF_SOUND;
    
    InitializePowerComplementaryTable();
    
    int oversampleFactor = m_pParams->NonRTPC.oversamplingFactor;
    InitializeFIRCoefficients(oversampleFactor);
    
    // Allocate delay line array
    m_pDelayLines = (DelayLineChannel*)AK_PLUGIN_ALLOC(in_pAllocator, sizeof(DelayLineChannel) * m_uNumChannels);
    if (m_pDelayLines == nullptr)
        return AK_InsufficientMemory;
    
    // Initialize each channel's delay line
    for (AkUInt32 i = 0; i < m_uNumChannels; ++i)
    {
        m_pDelayLines[i].buffer = nullptr;
        m_pDelayLines[i].oversampledBuffer = nullptr;
        m_pDelayLines[i].writePos = 0;
        m_pDelayLines[i].lastDelayTime = 0.0f;
        m_pDelayLines[i].oversampleFactor = oversampleFactor;
        m_pDelayLines[i].effectiveBufferSize = MAX_BUFFER_LEN * oversampleFactor;
        
        m_pDelayLines[i].buffer = (float*)AK_PLUGIN_ALLOC(in_pAllocator, sizeof(float) * MAX_BUFFER_LEN);
        if (m_pDelayLines[i].buffer == nullptr)
            return AK_InsufficientMemory;
        
        memset(m_pDelayLines[i].buffer, 0, sizeof(float) * MAX_BUFFER_LEN);
        
        if (oversampleFactor > OVERSAMPLE_NONE)
        {
            m_pDelayLines[i].oversampledBuffer = (float*)AK_PLUGIN_ALLOC(in_pAllocator,
                sizeof(float) * MAX_BUFFER_LEN * oversampleFactor);
            
            if (m_pDelayLines[i].oversampledBuffer == nullptr)
                return AK_InsufficientMemory;
            
            memset(m_pDelayLines[i].oversampledBuffer, 0, 
                sizeof(float) * MAX_BUFFER_LEN * oversampleFactor);
        }
    }

    return AK_Success;
}

AKRESULT FlexibleDelayLinesFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    if (m_pFIRCoefficients != nullptr)
    {
        AK_PLUGIN_FREE(in_pAllocator, m_pFIRCoefficients);
        m_pFIRCoefficients = nullptr;
    }
    
    if (m_pDelayLines != nullptr)
    {
        for (AkUInt32 i = 0; i < m_uNumChannels; ++i)
        {
            if (m_pDelayLines[i].buffer)
                AK_PLUGIN_FREE(in_pAllocator, m_pDelayLines[i].buffer);
            
            if (m_pDelayLines[i].oversampledBuffer)
                AK_PLUGIN_FREE(in_pAllocator, m_pDelayLines[i].oversampledBuffer);
        }
        AK_PLUGIN_FREE(in_pAllocator, m_pDelayLines);
    }
    
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT FlexibleDelayLinesFX::Reset()
{
    for (AkUInt32 i = 0; i < m_uNumChannels; ++i)
    {
        if (m_pDelayLines[i].buffer != nullptr)
            memset(m_pDelayLines[i].buffer, 0, sizeof(float) * MAX_BUFFER_LEN);
        
        if (m_pDelayLines[i].oversampledBuffer != nullptr)
            memset(m_pDelayLines[i].oversampledBuffer, 0, 
                sizeof(float) * MAX_BUFFER_LEN * m_pDelayLines[i].oversampleFactor);
        
        m_pDelayLines[i].writePos = 0;
        m_pDelayLines[i].lastDelayTime = m_pParams->RTPC.fDelayTime;
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

void FlexibleDelayLinesFX::PolyphaseUpsample(float* input, float* output, int inputLength, int factor)
{
    if (factor <= 1 || !m_pFIRCoefficients)
        return;
    
    for (int i = 0; i < inputLength; ++i)
    {
        output[i * factor] = input[i];
        for (int j = 1; j < factor; ++j)
            output[i * factor + j] = 0.0f;
    }
    
    // Apply polyphase FIR filter
    int halfLength = m_FIRLength / 2;
    
    for (int i = 0; i < inputLength * factor; ++i)
    {
        float sum = 0.0f;
        for (int j = 0; j < m_FIRLength; ++j)
        {
            int sampleIndex = i - halfLength + j;
            if (sampleIndex >= 0 && sampleIndex < inputLength * factor)
                if (sampleIndex % factor == 0)
                    sum += output[sampleIndex] * m_pFIRCoefficients[j] * (float)factor;
        }
            
        output[i] = sum;
    }
}

float FlexibleDelayLinesFX::CalculateDopplerShift(float currentDelay, float lastDelay, float bufferDuration) const
{
    // Doppler shift is implicit in the time gradient!
    // The rate of change of delay time creates pitch shift
    float delayChange = currentDelay - lastDelay;
    float relativeVelocity = (delayChange / bufferDuration) * SPEED_OF_SOUND;
    
    // Doppler factor = c / (c + v) where v is relative velocity
    // But we get this "for free" through the time-varying delay!
    return relativeVelocity; // Just for monitoring
}

void FlexibleDelayLinesFX::Execute(AkAudioBuffer* io_pBuffer)
{
    const AkUInt16 uValidFrames = io_pBuffer->uValidFrames;
    
    // Get parameters
    float currentDelayTime;
    if (m_pParams->RTPC.fDistance > 0.0f)
    {
        currentDelayTime = (m_pParams->RTPC.fDistance * 2.0f) / SPEED_OF_SOUND;
    }
    else
    {
        currentDelayTime = m_pParams->RTPC.fDelayTime;
    }
    
    float wetDryMix = m_pParams->RTPC.fWetDryMix;
    float feedback = m_pParams->RTPC.fFeedback;
    
    InterpolationType interpType = (InterpolationType)m_pParams->NonRTPC.interpolationType;
    int oversampleFactor = m_pParams->NonRTPC.oversamplingFactor;
    
    // Process each channel
    for (AkUInt32 chan = 0; chan < m_uNumChannels; ++chan)
    {
        DelayLineChannel& delayLine = m_pDelayLines[chan];
        float* pChannel = io_pBuffer->GetChannel(chan);
        
        // Calculate Doppler shift (for monitoring/debugging)
        float bufferDuration = (float)uValidFrames / m_fSampleRate;
        float dopplerVelocity = CalculateDopplerShift(currentDelayTime, delayLine.lastDelayTime, bufferDuration);
        (void)dopplerVelocity; // Suppress unused warning - Doppler is implicit!
        
        // Time gradient for smooth delay changes
        float timeGradient = (currentDelayTime - delayLine.lastDelayTime) / (float)uValidFrames;
        float currDelayTime = delayLine.lastDelayTime;
        
        // Choose processing path based on oversampling
        if (oversampleFactor > 1 && delayLine.oversampledBuffer)
        {
            // ==================== OVERSAMPLED PATH ====================
            
            // Upsample input buffer
            PolyphaseUpsample(pChannel, delayLine.oversampledBuffer, uValidFrames, oversampleFactor);
            
            int oversampledFrames = uValidFrames * oversampleFactor;
            float oversampledTimeGradient = timeGradient / (float)oversampleFactor;
            
            // Process oversampled samples
            for (int frame = 0; frame < oversampledFrames; ++frame)
            {
                float samplesDelayed = currDelayTime * m_fSampleRate * (float)oversampleFactor;
                int wholeSampleDelay = (int)samplesDelayed;
                float subSampleDelay = samplesDelayed - (float)wholeSampleDelay;
                
                int readPosA = (delayLine.writePos - wholeSampleDelay) & (delayLine.effectiveBufferSize - 1);
                int readPosB = (delayLine.writePos - wholeSampleDelay - 1) & (delayLine.effectiveBufferSize - 1);
                
                float valueA = delayLine.oversampledBuffer[readPosA];
                float valueB = delayLine.oversampledBuffer[readPosB];
                float delayedSample;
                
                // Simple linear for oversampled (Hybrid approach)
                if (interpType == INTERP_HYBRID)
                {
                    delayedSample = InterpolateLinear(valueA, valueB, subSampleDelay);
                }
                else
                {
                    // Pick nearest oversampled value (quantized to oversample grid)
                    delayedSample = valueA;
                }
                
                // Apply feedback
                float inputWithFeedback = delayLine.oversampledBuffer[frame] + (delayedSample * feedback);
                delayLine.oversampledBuffer[delayLine.writePos] = inputWithFeedback;
                
                delayLine.writePos = (delayLine.writePos + 1) & (delayLine.effectiveBufferSize - 1);
                currDelayTime += oversampledTimeGradient;
            }
            
            // Downsample back to original rate (simple decimation)
            for (AkUInt16 frame = 0; frame < uValidFrames; ++frame)
            {
                int oversampledIdx = frame * oversampleFactor;
                float delayedSample = delayLine.oversampledBuffer[oversampledIdx];
                pChannel[frame] = pChannel[frame] * (1.0f - wetDryMix) + delayedSample * wetDryMix;
            }
        }
        else
        {
            // ==================== STANDARD PATH (NO OVERSAMPLING) ====================
            
            for (AkUInt16 frame = 0; frame < uValidFrames; ++frame)
            {
                float samplesDelayed = currDelayTime * m_fSampleRate;
                int wholeSampleDelay = (int)samplesDelayed;
                float subSampleDelay = samplesDelayed - (float)wholeSampleDelay;
                
                int readPosA = (delayLine.writePos - wholeSampleDelay) & BIT_MASK;
                int readPosB = (delayLine.writePos - wholeSampleDelay - 1) & BIT_MASK;
                
                float delayedSample;
                
                // Choose interpolation method
                switch (interpType)
                {
                case INTERP_LINEAR:
                {
                    float valueA = delayLine.buffer[readPosA];
                    float valueB = delayLine.buffer[readPosB];
                    delayedSample = InterpolateLinear(valueA, valueB, subSampleDelay);
                    break;
                }
                
                case INTERP_POWER_COMPLEMENTARY:
                {
                    float valueA = delayLine.buffer[readPosA];
                    float valueB = delayLine.buffer[readPosB];
                    delayedSample = InterpolatePowerComplementary(valueA, valueB, subSampleDelay);
                    break;
                }
                
                case INTERP_POLYNOMIAL_4POINT:
                {
                    delayedSample = InterpolatePolynomial4Point(delayLine.buffer, 
                        readPosA, subSampleDelay);
                    break;
                }
                
                case INTERP_HYBRID:
                default:
                {
                    // Fallback to linear if hybrid without oversampling
                    float valueA = delayLine.buffer[readPosA];
                    float valueB = delayLine.buffer[readPosB];
                    delayedSample = InterpolateLinear(valueA, valueB, subSampleDelay);
                    break;
                }
                }
                
                // Apply feedback
                float inputWithFeedback = pChannel[frame] + (delayedSample * feedback);
                delayLine.buffer[delayLine.writePos] = inputWithFeedback;
                
                delayLine.writePos = (delayLine.writePos + 1) & BIT_MASK;
                
                // Output with wet/dry mix
                pChannel[frame] = pChannel[frame] * (1.0f - wetDryMix) + delayedSample * wetDryMix;
                
                currDelayTime += timeGradient;
            }
        }
        
        delayLine.lastDelayTime = currentDelayTime;
    }
}

AKRESULT FlexibleDelayLinesFX::TimeSkip(AkUInt32 in_uFrames)
{
    for (AkUInt32 chan = 0; chan < m_uNumChannels; ++chan)
    {
        if (m_pDelayLines[chan].oversampleFactor > 1)
        {
            m_pDelayLines[chan].writePos = (m_pDelayLines[chan].writePos + 
                in_uFrames * m_pDelayLines[chan].oversampleFactor) & 
                (m_pDelayLines[chan].effectiveBufferSize - 1);
        }
        else
        {
            m_pDelayLines[chan].writePos = (m_pDelayLines[chan].writePos + in_uFrames) & BIT_MASK;
        }
    }
    return AK_DataReady;
}
