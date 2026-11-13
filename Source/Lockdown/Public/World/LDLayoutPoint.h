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
