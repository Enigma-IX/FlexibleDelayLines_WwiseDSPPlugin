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

#ifndef FlexibleDelayLinesFX_H
#define FlexibleDelayLinesFX_H

#include "FlexibleDelayLinesFXParams.h"

#define MAX_BUFFER_LEN (131072) // 2^17 for ~2.73s at 48kHz
#define BIT_MASK (MAX_BUFFER_LEN - 1)

enum InterpolationType
{
    INTERP_LINEAR = 0,
    INTERP_POWER_COMPLEMENTARY = 1,
    INTERP_POLYNOMIAL_4POINT = 2,
    INTERP_HYBRID = 3
};

enum OversamplingFactor
{
    OVERSAMPLE_NONE = 1,
    OVERSAMPLE_2X = 2,
    OVERSAMPLE_4X = 4,
    OVERSAMPLE_8X = 8,
    OVERSAMPLE_16X = 16
};

enum UpSamplingMethod
{
    UPSAMPLE_LINEAR = 0,
    UPSAMPLE_SIMPLE_SINC = 1,
    UPSAMPLE_POLYPHASE = 2
};

/// See https://www.audiokinetic.com/library/edge/?source=SDK&id=soundengine__plugins__effects.html
/// for the documentation about effect plug-ins
class FlexibleDelayLinesFX : public AK::IAkInPlaceEffectPlugin
{
public:
    FlexibleDelayLinesFX();
    ~FlexibleDelayLinesFX();

    /// Plug-in initialization.
    /// Prepares the plug-in for data processing, allocates memory and sets up the initial conditions.
    AKRESULT Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat) override;

    /// Release the resources upon termination of the plug-in.
    AKRESULT Term(AK::IAkPluginMemAlloc* in_pAllocator) override;

    /// The reset action should perform any actions required to reinitialize the
    /// state of the plug-in to its original state (e.g. after Init() or on effect bypass).
    AKRESULT Reset() override;

    /// Plug-in information query mechanism used when the sound engine requires
    /// information about the plug-in to determine its behavior.
    AKRESULT GetPluginInfo(AkPluginInfo& out_rPluginInfo) override;

    /// Effect plug-in DSP execution.
    void Execute(AkAudioBuffer* io_pBuffer) override;

    /// Skips execution of some frames, when the voice is virtual playing from elapsed time.
    /// This can be used to simulate processing that would have taken place (e.g. update internal state).
    /// Return AK_DataReady or AK_NoMoreData, depending if there would be audio output or not at that point.
    AKRESULT TimeSkip(AkUInt32 in_uFrames) override;

private:
    // ==================== INTERPOLATION METHODS ====================
    
    // Linear interpolation (fastest, basic quality)
    inline float InterpolateLinear(float a, float b, float t) const
    {
        return a * (1.0f - t) + b * t;
    }
    
    // Power-complementary interpolation using Hanning window (better with noise)
    inline float InterpolatePowerComplementary(float a, float b, float t) const
    {
        int index = (int)(t * (float)(m_powerCompTableSize - 1)) & (m_powerCompTableSize - 1);
        float ampA = 1.0f - m_powerCompTable[index];
        float ampB = m_powerCompTable[index];
        return (a * ampA) + (b * ampB);
    }
    
    // 4-point polynomial interpolation (Lagrange, best for tones)
    inline float InterpolatePolynomial4Point(float* buffer, int baseIndex, float t) const
    {
        // Get 4 points: y[-1], y[0], y[1], y[2]
        float ym1 = buffer[(baseIndex - 1) & BIT_MASK];
        float y0  = buffer[baseIndex & BIT_MASK];
        float y1  = buffer[(baseIndex + 1) & BIT_MASK];
        float y2  = buffer[(baseIndex + 2) & BIT_MASK];
        
        // 4-point Lagrange interpolation
        float c0 = y0;
        float c1 = 0.5f * (y1 - ym1);
        float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
    
    // Hybrid: Oversampled + interpolation
    inline float InterpolateHybrid(float* buffer, int baseIndex, float t, int oversampleFactor) const
    {
        // Find closest oversampled index
        int oversampledIndex = (int)(t * (float)oversampleFactor);
        float subsampleT = (t * (float)oversampleFactor) - (float)oversampledIndex;
        
        int idxA = (baseIndex + oversampledIndex) & BIT_MASK;
        int idxB = (baseIndex + oversampledIndex + 1) & BIT_MASK;
        
        return InterpolateLinear(buffer[idxA], buffer[idxB], subsampleT);
    }
    
    // ==================== OVERSAMPLING ====================
    
    // Sinc-based upsampling with windowed sinc function
    void UpsampleBuffer(float* input, float* output, int inputLength, int factor);
    
    // Polyphase FIR filter for efficient upsampling
    void LinearUpsample(float* input, float* output, int inputLength, int factor);
    void SimpleSincUpsample(float* input, float* output, int inputLength, int factor);
    void PolyphaseUpsample(float* input, float* output, int inputLength, int factor);    
    
    // ==================== DELAY LINE CHANNEL ====================
    
    // Per-Channel delay line State
    struct DelayLineChannel
    {
        float* buffer;
        float* oversampledBuffer;
        float* tempUpsampledInput;
        float* tempDelayedOutput;
        int writePos;
        float lastDelayTime;
        int oversampleFactor;
        int effectiveBufferSize;
        
        DelayLineChannel()
            : buffer(nullptr)
            , oversampledBuffer(nullptr)
            , writePos(0)
            , lastDelayTime(0.0f)
            , oversampleFactor(OVERSAMPLE_NONE)
            , effectiveBufferSize(MAX_BUFFER_LEN)
        {}        
    };    
    
    FlexibleDelayLinesFXParams* m_pParams;
    AK::IAkPluginMemAlloc* m_pAllocator;
    AK::IAkEffectPluginContext* m_pContext;
    
    DelayLineChannel* m_pDelayLines;
    AkUInt32 m_uNumChannels;
    float m_fSampleRate;
    float m_fSamplesPerMeter;
    
    static constexpr int m_powerCompTableSize = 256;
    float m_powerCompTable[m_powerCompTableSize];
    
    float* m_pFIRCoefficients;
    int m_FIRLength;
    
    typedef void (FlexibleDelayLinesFX::*UpsampleFuncPtr)(float*, float*, int, int);
    UpsampleFuncPtr m_upsampleFunction;
    
    static constexpr float SPEED_OF_SOUND = 343.0f; // in m/s
    static constexpr float PI = 3.14159265358979323846f;
    
    void InitializePowerComplementaryTable();
    void InitializeFIRCoefficients(int oversampleFactor);
    float CalculateDopplerShift(float currentDelay, float previousDelay, float bufferDuration) const;
};

#endif // FlexibleDelayLinesFX_H
