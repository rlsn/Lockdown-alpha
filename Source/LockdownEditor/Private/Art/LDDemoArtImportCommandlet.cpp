#include "LDDemoArtImportCommandlet.h"
#include "LDEditorLog.h"
#include "LDDemoArtImport.h"
#include "LDDemoUIImageImport.h"
#include "LDStaticSceneEditor.h"
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
    // -UI imports only the UI images (backgrounds and icons); the body and prop art is left as it is and still validated.
    const bool bUI = FParse::Param(*Params, TEXT("UI"));
    FString Error;
    FString ExportPath;
    if (FParse::Value(*Params, TEXT("ExportStaticScene="), ExportPath))
    {
        if (bValidate || bUI || ExportPath.IsEmpty()) { UE_LOG(LogLDEditor, Error, TEXT("LD_STATIC_SCENE_REJECTED Export is exclusive and needs an output path.")); return 1; }
        if (!LDStaticSceneEditor::Export(ExportPath, Error)) { UE_LOG(LogLDEditor, Error, TEXT("LD_STATIC_SCENE_REJECTED %s"), *Error); return 1; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_STATIC_SCENE_EXPORTED %s"), *ExportPath); return 0;
    }
    if (bValidate && bUI)
    {
        UE_LOG(LogLDEditor, Error, TEXT("LD_DEMO_ART_REJECTED -Validate and -UI are exclusive."));
        return 1;
    }
    const bool bImported = bValidate || (bUI ? LDDemoUIImageImport::Import(Error) : LDDemoArtImport::Import(Error));
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
    UE_LOG(LogLDEditor, Display, TEXT("LD_DEMO_ART_SUCCESS %s: licensed sources, imported and authored bodies, clips, props, catalog, UI backgrounds and icons validated."),
        bValidate ? TEXT("Validate") : bUI ? TEXT("ImportUI") : TEXT("Import"));
    return 0;
}
