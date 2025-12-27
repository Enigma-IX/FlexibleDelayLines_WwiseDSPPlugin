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

#include "FlexibleDelayLinesPlugin.h"
#include "../SoundEnginePlugin/FlexibleDelayLinesFXFactory.h"

FlexibleDelayLinesPlugin::FlexibleDelayLinesPlugin()
{
}

FlexibleDelayLinesPlugin::~FlexibleDelayLinesPlugin()
{
}

bool FlexibleDelayLinesPlugin::GetBankParameters(const GUID & in_guidPlatform, AK::Wwise::Plugin::DataWriter& in_dataWriter) const
{
    // Write bank data here
    in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "DelayTime"));
    in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "WetDryMix"));
    in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "Feedback"));
    in_dataWriter.WriteReal32(m_propertySet.GetReal32(in_guidPlatform, "Distance"));

    return true;
}

AK_DEFINE_PLUGIN_CONTAINER(FlexibleDelayLines);											// Create a PluginContainer structure that contains the info for our plugin
AK_EXPORT_PLUGIN_CONTAINER(FlexibleDelayLines);											// This is a DLL, we want to have a standardized name
AK_ADD_PLUGIN_CLASS_TO_CONTAINER(                                             // Add our CLI class to the PluginContainer
    FlexibleDelayLines,        // Name of the plug-in container for this shared library
    FlexibleDelayLinesPlugin,  // Authoring plug-in class to add to the plug-in container
    FlexibleDelayLinesFX       // Corresponding Sound Engine plug-in class
);
DEFINE_PLUGIN_REGISTER_HOOK

DEFINEDUMMYASSERTHOOK;							// Placeholder assert hook for Wwise plug-ins using AKASSERT (cassert used by default)
