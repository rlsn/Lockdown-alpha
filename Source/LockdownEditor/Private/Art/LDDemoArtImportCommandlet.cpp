#include "LDDemoArtImportCommandlet.h"
#include "LDDemoArtImport.h"
#include "Misc/Parse.h"

ULDDemoArtImportCommandlet::ULDDemoArtImportCommandlet()
{
    // A client: as a dedicated server the linker would drop the master materials' expressions.
    IsClient = true;
    IsEditor = true;
    LogToConsole = true;
}

int32 ULDDemoArtImportCommandlet::Main(const FString& Params)
{
    const bool bValidate = FParse::Param(*Params, TEXT("Validate"));
    FString Error;
    if (!(bValidate ? LDDemoArtImport::Validate(Error) : LDDemoArtImport::Import(Error)))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_ART_REJECTED %s"), *Error);
        return 1;
    }
    if (!bValidate && !LDDemoArtImport::Validate(Error))
    {
        UE_LOG(LogTemp, Error, TEXT("LD_DEMO_ART_REJECTED Saved art assets do not validate: %s"), *Error);
        return 1;
    }
    UE_LOG(LogTemp, Display, TEXT("LD_DEMO_ART_SUCCESS %s: CC0 sources, imported bodies, clips, props and catalog validated."),
        bValidate ? TEXT("Validate") : TEXT("Import"));
    return 0;
}
