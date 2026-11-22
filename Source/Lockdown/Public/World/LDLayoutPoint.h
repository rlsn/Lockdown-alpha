#pragma once

#include "CoreMinimal.h"
#include "Engine/TargetPoint.h"
#include "LDLayoutPoint.generated.h"

// A saved layout point with editor-only previews. Its existing LDSceneAnchor/Member identity drives runtime placement.
UCLASS()
class LOCKDOWN_API ALDLayoutPoint : public ATargetPoint
{
    GENERATED_BODY()
public:
    ALDLayoutPoint();
    virtual void OnConstruction(const FTransform& Transform) override;
    // Additional shared targets use saved actor names, independently of the eight roster-owned slots.
    UPROPERTY(EditAnywhere, Category="Training", meta=(ToolTip="Additional attackable shared training target. Duplicating a training point enables this automatically."))
    bool bSharedTrainingDummy = false;
    // Opt in to the actual saved child roster rather than the template's default count.
    UPROPERTY(EditAnywhere, Category="Camp", meta=(ToolTip="Camp centre: use every attached member slot, preserving authored quantities and per-member approved tiers. Each whole-group spawn draws one species per tier present. The centre's prototype still supplies the whole-group refresh delay."))
    bool bUseAuthoredCampMembers = false;
    UPROPERTY(EditAnywhere, Category="Camp", meta=(ToolTip="Camp member: approved camp prototype providing this member's tier and AI; its species is drawn from that tier on each whole-group spawn. Empty inherits the parent centre's prototype. Layout Preview CharacterId is visual only."))
    FString CampMemberPrototypeId;
    FString SharedTrainingId() const { return TEXT("training_shared_") + GetName(); }
#if WITH_EDITOR
    virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
    virtual void PostEditImport() override;
private:
    void ConfigureTrainingCopy();
public:
#endif
#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category="Layout Preview", meta=(ToolTip="Editor preview only. Move this actor to change the runtime spawn position."))
    FString CharacterId;
    UPROPERTY(EditAnywhere, Category="Layout Preview") FString PropId;
    UPROPERTY(EditAnywhere, Category="Layout Preview") FString BasicShape;
    UPROPERTY(EditAnywhere, Category="Layout Preview") FVector PropScale = FVector::OneVector;
    UPROPERTY(EditAnywhere, Category="Layout Preview") FVector PropOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, Category="Layout Preview") float RadiusCm = 0;
    UPROPERTY(EditAnywhere, Category="Layout Preview") FLinearColor PreviewTint = FLinearColor::White;
    UPROPERTY() TObjectPtr<class ULDArtCatalog> PreviewCatalog;
    UPROPERTY(VisibleAnywhere, Category="Layout Preview") TObjectPtr<class ULDHumanoidVisualComponent> BodyPreview;
    UPROPERTY(VisibleAnywhere, Category="Layout Preview") TObjectPtr<class UStaticMeshComponent> PropPreview;
    UPROPERTY(VisibleAnywhere, Category="Layout Preview") TObjectPtr<class UInstancedStaticMeshComponent> RangePreview;
#endif
};
