#pragma once
#include "CoreMinimal.h"

// Editor-only producer of the Demo audio assets from SourceData\Audio\audio_cues.json.
// Import creates or updates SC_DemoMaster, SMix_DemoUser, one SW_<Cue> per cue and DA_DemoAudio.
// Validate rejects missing/unlicensed/changed sources, unlisted files and assets not imported from the current bytes.
namespace LDDemoAudioImport
{
    bool Import(FString& Error);
    bool Validate(FString& Error);
}
