#include "Modules/ModuleManager.h"
#include "LDEditorLog.h"
#include "LDSceneAuthoringLibrary.h"
#include "Navigation/LDDemoNavigationSystem.h"
#include "Editor.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogLDEditor);

class FLockdownEditorModule final : public FDefaultModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FEditorDelegates::PreBeginPIE.AddRaw(this, &FLockdownEditorModule::PrepareNavigation);
        FEditorDelegates::PostPIEStarted.AddRaw(this, &FLockdownEditorModule::FinishPreparation);
    }

    virtual void ShutdownModule() override
    {
        FEditorDelegates::PreBeginPIE.RemoveAll(this);
        FEditorDelegates::PostPIEStarted.RemoveAll(this);
    }

private:
    bool bNavigationFailed = false;
    void PrepareNavigation(bool bIsSimulating)
    {
        bNavigationFailed = false;
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World || !Cast<ULDDemoNavigationSystem>(World->GetNavigationSystem())) { return; }
        FString Error;
        if (!ULDSceneAuthoringLibrary::BakeNavigation(World, Error))
        {
            UE_LOG(LogLDEditor, Error, TEXT("Could not refresh navigation before Play: %s"), *Error);
            bNavigationFailed = true;
        }
    }

    void FinishPreparation(bool bIsSimulating)
    {
        // CancelRequestPlaySession is unsafe inside PreBeginPIE: UE still consumes the same request afterwards.
        if (bNavigationFailed && GEditor) { GEditor->RequestEndPlayMap(); }
        bNavigationFailed = false;
    }
};

IMPLEMENT_MODULE(FLockdownEditorModule, LockdownEditor);
