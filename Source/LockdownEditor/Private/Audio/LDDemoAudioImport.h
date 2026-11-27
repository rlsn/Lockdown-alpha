#pragma once
#include "CoreMinimal.h"

// Editor-only producer of the Demo audio assets from SourceData\Audio\audio_cues.json.
// Import creates or updates SC_DemoMaster with its bus classes, SMix_DemoUser, SW_<Cue>[_<n>] per cue take,
// SW_<ability>[_<n>] per skill release take and DA_DemoAudio.
// Validate rejects missing/unlicensed/changed sources, unlisted files and assets not imported from the current bytes.
namespace LDDemoAudioImport
{
    bool Import(FString& Error);
    bool Validate(FString& Error);
}
