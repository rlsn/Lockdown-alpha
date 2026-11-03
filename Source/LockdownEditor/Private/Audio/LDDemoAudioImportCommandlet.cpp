#include "LDDemoAudioImportCommandlet.h"
#include "LDDemoAudioImport.h"
#include "Misc/Parse.h"

ULDDemoAudioImportCommandlet::ULDDemoAudioImportCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 ULDDemoAudioImportCommandlet::Main(const FString& Params)
{
    const bool bValidate = FParse::Param(*Params, TEXT("Validate"));
    FString Error;
    if (!(bValidate ? LDDemoAudioImport::Validate(Error) : LDDemoAudioImport::Import(Error)))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_AUDIO_REJECTED %s"), *Error);
        return 1;
    }
    if (!bValidate && !LDDemoAudioImport::Validate(Error))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_AUDIO_REJECTED Saved audio assets do not validate: %s"), *Error);
        return 1;
    }
    UE_LOG(LogTemp, Display, TEXT("LD_DEMO_AUDIO_SUCCESS %s: CC0 sources, catalog, master class and user mix validated."),
        bValidate ? TEXT("Validate") : TEXT("Import"));
    return 0;
}
