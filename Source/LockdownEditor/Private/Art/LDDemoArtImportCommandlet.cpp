#include "LDDemoArtImportCommandlet.h"
#include "LDEditorLog.h"
#include "LDDemoArtImport.h"
#include "LDDemoUIImageImport.h"
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
    // -Backgrounds imports only the UI background images; the CC0 art is left as it is and still validated.
    const bool bBackgrounds = FParse::Param(*Params, TEXT("Backgrounds"));
    FString Error;
    if (bValidate && bBackgrounds)
    {
        UE_LOG(LogLDEditor, Error, TEXT("LD_DEMO_ART_REJECTED -Validate and -Backgrounds are exclusive."));
        return 1;
    }
    const bool bImported = bValidate || (bBackgrounds ? LDDemoUIImageImport::Import(Error) : LDDemoArtImport::Import(Error));
    if (!bImported || (bValidate && !(LDDemoArtImport::Validate(Error) && LDDemoUIImageImport::Validate(Error))))
    {
        UE_LOG(LogLDEditor, Error, TEXT("LD_DEMO_ART_REJECTED %s"), *Error);
        return 1;
    }
    if (!bValidate && !(LDDemoArtImport::Validate(Error) && LDDemoUIImageImport::Validate(Error)))
    {
        UE_LOG(LogLDEditor, Error, TEXT("LD_DEMO_ART_REJECTED Saved art assets do not validate: %s"), *Error);
        return 1;
    }
    UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_ART_SUCCESS %s: CC0 sources, imported bodies, clips, props, catalog and UI backgrounds validated."),
        bValidate ? TEXT("Validate") : bBackgrounds ? TEXT("ImportBackgrounds") : TEXT("Import"));
    return 0;
}
