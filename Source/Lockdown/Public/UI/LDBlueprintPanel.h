#pragma once
#include "CoreMinimal.h"
#include "UI/LDWidgets.h"
#include "Development/LDBuildBlueprint.h"
#include "LDBlueprintPanel.generated.h"
class UBorder;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class FLDDemoContentSnapshot;
class ULDBuildBlueprintComponent;

// #198 §10 构筑蓝图 page: the owner's read-only record of the last match (ULDBuildBlueprintComponent), its explicit
// export / open-folder actions, and a read-only view of a strictly validated file from Saved/Blueprints. It formats the
// record only: it never writes runtime state, grants an ability or restores a forgotten active, and a fact that was
// not collected shows as 未采集, never as 0.
UCLASS()
class LOCKDOWN_API ULDBlueprintPanel : public ULDPanel
{
    GENERATED_BODY()
public:
    virtual bool IsPanelOpen() const override { return bOpen; }
    virtual void OpenPanel() override;
    virtual bool ClosePanel() override;
    // Player-facing text of one valid record (also used for an imported file).
    static FString Describe(const FLDBlueprintRecord& Record, const FLDDemoContentDefinitions* Definitions);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual UWidget* FocusScope() const override;
    virtual UWidget* HitArea() const override;
private:
    enum class EMode : uint8 { Match, List, Imported };
    void ShowMatch();
    void ShowList();
    void ShowImported(const FString& Path);
    void RefreshMatch();
    void SetAction(const FString& Text);
    ULDBuildBlueprintComponent* Channel() const;
    const FLDDemoContentDefinitions* Definitions();
    UPROPERTY() TObjectPtr<UBorder> MainPanel;
    UPROPERTY() TObjectPtr<UTextBlock> SourceText;
    UPROPERTY() TObjectPtr<UTextBlock> StatusText;
    UPROPERTY() TObjectPtr<UTextBlock> NoticeText;
    UPROPERTY() TObjectPtr<UTextBlock> BodyText;
    UPROPERTY() TObjectPtr<UVerticalBox> FileList;
    UPROPERTY() TObjectPtr<UScrollBox> Scroll;
    UPROPERTY() TObjectPtr<ULDCommandButton> ExportButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> ImportButton;
    UPROPERTY() TObjectPtr<ULDCommandButton> BackButton;
    UPROPERTY() TObjectPtr<UTextBlock> ActionText;
    EMode Mode = EMode::Match;
    bool bOpen = false;
    // What the match view last showed, so a tick without a change rewrites nothing.
    uint8 ShownState = MAX_uint8;
    FGuid ShownResult;
    bool bShownSaved = false;
    FString ShownProblem;
    TSharedPtr<const FLDDemoContentSnapshot> Content;
    bool bContentTried = false;
    FString LocalProduct;
};
