#include "Modules/ModuleManager.h"
#include "LDEditorLog.h"
#include "Art/LDArtTestCharacter.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameMapsSettings.h"

DEFINE_LOG_CATEGORY(LogLDEditor);

class FLockdownEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        PrePIE = FEditorDelegates::PreBeginPIE.AddRaw(this,&FLockdownEditorModule::BeginArtPreview);
        PostPIE = FEditorDelegates::PostPIEStarted.AddRaw(this,&FLockdownEditorModule::RestoreAfterPIE);
        EndPIE = FEditorDelegates::EndPIE.AddRaw(this,&FLockdownEditorModule::RestoreAfterPIE);
        CancelPIE = FEditorDelegates::CancelPIE.AddRaw(this,&FLockdownEditorModule::RestoreSettings);
    }
    virtual void ShutdownModule() override
    {
        FEditorDelegates::PreBeginPIE.Remove(PrePIE);
        FEditorDelegates::PostPIEStarted.Remove(PostPIE);
        FEditorDelegates::EndPIE.Remove(EndPIE);
        FEditorDelegates::CancelPIE.Remove(CancelPIE);
        RestoreSettings();
    }
private:
    void BeginArtPreview(bool)
    {
        RestoreSettings();
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World || World->GetOutermost()->GetName() != TEXT("/Game/Lockdown/Generated/Maps/L_ArtTest")) { return; }
        if (!World->GetWorldSettings()->DefaultGameMode
            || !World->GetWorldSettings()->DefaultGameMode->IsChildOf(ALDArtTestGameMode::StaticClass())) { return; }
        // Override in memory only while PIE constructs its instance. Never change project configuration or packaged entry rules.
        UGameMapsSettings* Settings = GetMutableDefault<UGameMapsSettings>();
        SavedGameInstance = Settings->GameInstanceClass;
        Settings->GameInstanceClass = FSoftClassPath(UGameInstance::StaticClass());
        bOverridden = true;
    }
    void RestoreAfterPIE(bool) { RestoreSettings(); }
    void RestoreSettings()
    {
        if (!bOverridden) { return; }
        GetMutableDefault<UGameMapsSettings>()->GameInstanceClass = SavedGameInstance;
        bOverridden = false;
    }
    FSoftClassPath SavedGameInstance;
    bool bOverridden = false;
    FDelegateHandle PrePIE,PostPIE,EndPIE,CancelPIE;
};

IMPLEMENT_MODULE(FLockdownEditorModule, LockdownEditor);
