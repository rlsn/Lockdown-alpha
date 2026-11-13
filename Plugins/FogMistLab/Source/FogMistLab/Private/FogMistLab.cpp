#include "FogMistLab.h"
#include "Vision/LDFogDistanceField.h"
#include "Editor.h"
#include "GameMapsSettings.h"

#include "Camera/CameraComponent.h"
#include "Character/LDCharacter.h"
#include "Character/LDCameraSettings.h"
#include "Character/LDHumanoidVisualComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Development/LDDemoContentCatalog.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/LDDemoEntry.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Modules/ModuleManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Region/LDMapDefinition.h"
#include "Region/LDMapRegistry.h"
#include "World/LDNativeGround.h"

class FFogMistLabModule : public IModuleInterface
{
    FDelegateHandle Before, After, Cancel;
    TOptional<FSoftClassPath> SavedGameInstance;
    void Restore()
    {
        if (SavedGameInstance.IsSet())
        {
            GetMutableDefault<UGameMapsSettings>()->GameInstanceClass = SavedGameInstance.GetValue();
            SavedGameInstance.Reset();
        }
    }
public:
    void StartupModule() override
    {
        Before = FEditorDelegates::PreBeginPIE.AddLambda([this](bool)
        {
            Restore();
            UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
            if (World && World->GetOutermost()->GetName() == TEXT("/Game/FogMistPreview/L_FogMistLab"))
            {
                auto* Settings = GetMutableDefault<UGameMapsSettings>();
                SavedGameInstance = Settings->GameInstanceClass;
                Settings->GameInstanceClass = UFogMistLabGameInstance::StaticClass();
            }
        });
        After = FEditorDelegates::EndPIE.AddLambda([this](bool) { Restore(); });
        Cancel = FEditorDelegates::CancelPIE.AddLambda([this]() { Restore(); });
    }
    void ShutdownModule() override
    {
        FEditorDelegates::PreBeginPIE.Remove(Before);
        FEditorDelegates::EndPIE.Remove(After);
        FEditorDelegates::CancelPIE.Remove(Cancel);
        Restore();
    }
};
IMPLEMENT_MODULE(FFogMistLabModule, FogMistLab)
DEFINE_LOG_CATEGORY_STATIC(LogFogMistLab, Log, All);

namespace FogMistLab
{
bool IsLab(const UWorld* World)
{
    return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == TEXT("/Game/FogMistPreview/L_FogMistLab");
}
double Number(const FLDDemoRational& Value)
{
    check(Value.Denominator > 0);
    return double(Value.Numerator) / double(Value.Denominator);
}
FVector Foot(const ACharacter& Body)
{
    return Body.GetActorLocation() - FVector(0, 0, Body.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}
}

AGameModeBase* UFogMistLabGameInstance::CreateGameModeForURL(FURL URL, UWorld* World)
{
    // Only this editor lab bypasses the Demo lobby/session gate.
    if (FogMistLab::IsLab(World)) { return UGameInstance::CreateGameModeForURL(URL, World); }
    return Super::CreateGameModeForURL(URL, World);
}
void UFogMistLabGameInstance::OnStart()
{
    if (FogMistLab::IsLab(GetWorld())) { UGameInstance::OnStart(); return; }
    Super::OnStart();
}

AFogMistLabCharacter::AFogMistLabCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    bUseControllerRotationYaw = false;
    const auto* Native = GetDefault<ALDCharacter>();
    GetCapsuleComponent()->InitCapsuleSize(Native->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),
        Native->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    auto* Movement = GetCharacterMovement();
    const auto* Defaults = Native->GetCharacterMovement();
    Movement->MaxAcceleration = Defaults->MaxAcceleration;
    Movement->BrakingDecelerationWalking = Defaults->BrakingDecelerationWalking;
    Movement->MaxStepHeight = Defaults->MaxStepHeight;
    Movement->bMaintainHorizontalGroundVelocity = Defaults->bMaintainHorizontalGroundVelocity;
    Movement->SetWalkableFloorAngle(Defaults->GetWalkableFloorAngle());
    Movement->bCanWalkOffLedges = Defaults->bCanWalkOffLedges;
    Movement->bRunPhysicsWithNoController = true;
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0, 720, 0);
    Movement->NavAgentProps.bCanJump = false;
    Visual = CreateDefaultSubobject<ULDHumanoidVisualComponent>(TEXT("LabBody"));
    Visual->SetupAttachment(GetRootComponent());
    GetMesh()->SetHiddenInGame(true);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("LabCamera"));
    Camera->SetupAttachment(GetRootComponent());
    Camera->FieldOfView = LDCamera::FieldOfView;
    Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
    Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
    Camera->PostProcessSettings.AutoExposureMinBrightness = 0;
    Camera->PostProcessSettings.AutoExposureMaxBrightness = 0;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0;
    ArmLength = LDCamera::DefaultArmLength;
}
void AFogMistLabCharacter::BeginPlay()
{
    Super::BeginPlay();
    FString Error;
    TSharedPtr<const FLDDemoContentSnapshot> Loaded;
    FCollisionShape Capsule;
    if (!LDDemoEntry::LoadRuntimeContent(Loaded, Error) || !ULDMapRegistry::ContentCapsule(*Loaded, Capsule, Error))
    {
        UE_LOG(LogFogMistLab, Error, TEXT("LAB_BODY_FAILED %s"), *Error);
        Destroy();
        return;
    }
    auto* Movement = GetCharacterMovement();
    GetCapsuleComponent()->SetCapsuleSize(Capsule.GetCapsuleRadius(), Capsule.GetCapsuleHalfHeight());
    Movement->MaxWalkSpeed = float(FogMistLab::Number(Loaded->Get().Parameters.MoveSpeedCmPerSec.Base));
    Movement->NavAgentProps.AgentRadius = Capsule.GetCapsuleRadius();
    Movement->NavAgentProps.AgentHeight = Capsule.GetCapsuleHalfHeight() * 2;
    if (CreatureId) { Visual->ApplyMonsterLook(CreatureId); }
    else { Visual->ApplyPalette(0); }
    Visual->SetVisibility(true, true);
}
void AFogMistLabCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Visual->Animate(GetVelocity(), DeltaSeconds);
}
void AFogMistLabCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    Camera->GetCameraView(DeltaTime, OutResult);
    const FTransform Pose = ALDCharacter::DefaultArmCameraPose(GetActorLocation(), ArmLength);
    OutResult.Location = Pose.GetLocation();
    OutResult.Rotation = Pose.Rotator();
}
void AFogMistLabCharacter::Zoom(float Axis)
{
    ArmLength = ALDCharacter::ZoomedArmLength(ArmLength, Axis);
}

void AFogMistLabController::BeginPlay()
{
    Super::BeginPlay();
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
}
void AFogMistLabController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AFogMistLabController::NavigateUnderCursor);
    InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AFogMistLabController::StopMoving);
    InputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &AFogMistLabController::Zoom);
}
void AFogMistLabController::NavigateUnderCursor()
{
    FHitResult Hit;
    if (GetHitResultUnderCursor(ECC_GameTraceChannel1, false, Hit)) { NavigateTo(Hit.ImpactPoint); }
}
bool AFogMistLabController::NavigateTo(FVector Destination)
{
    auto* Body = Cast<AFogMistLabCharacter>(GetPawn());
    auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Body || !Navigation || Destination.ContainsNaN()) { return false; }
    FNavLocation Projected;
    if (!Navigation->ProjectPointToNavigation(Destination, Projected, FVector(100, 100, 300))) { return false; }
    UNavigationPath* Found = UNavigationSystemV1::FindPathToLocationSynchronously(this, Body->GetActorLocation(), Projected.Location, Body);
    if (!Found || !Found->IsValid() || Found->IsPartial() || Found->PathPoints.Num() < 2)
    {
        UE_LOG(LogFogMistLab, Warning, TEXT("LAB_PATH_UNAVAILABLE %s"), *Destination.ToString());
        return false;
    }
    Path = Found->PathPoints;
    PathIndex = 1;
    bFollowingPath = true;
    return true;
}

bool AFogMistLabGameMode::BuildField(const FVector& Foot, FString& Error)
{
    const double Started = FPlatformTime::Seconds();
    TArray<uint8> Bits;
    Sight.Coverage(Foot, Bits);
    if (Bits.Num() != LDObservation::CoverageBytes) { Error = TEXT("Lab native coverage has an invalid size."); return false; }
    const double CellX = (Sight.MaxCm().X - Sight.MinCm().X) / FieldSide;
    const double CellY = (Sight.MaxCm().Y - Sight.MinCm().Y) / FieldSide;
    LDFogDistanceField::Build(Bits, CellX, CellY, TargetField);
    if (ShownField.Num() != TargetField.Num()) { ShownField = TargetField; }
    LastFoot = Foot;
    ++FogUpdates;
    LastFogBuildMs = float((FPlatformTime::Seconds() - Started) * 1000);
    return true;
}
void AFogMistLabGameMode::UploadField(float DeltaSeconds)
{
    if (!FieldTexture || !FieldTexture->GetResource() || ShownField.Num() != TargetField.Num()) { return; }
    const float Alpha = 1.f - FMath::Exp(-FMath::Max(0.f, DeltaSeconds) / .06f);
    uint8* Pixels = new uint8[FieldSide * FieldSide * 4];
    for (int32 I = 0; I < ShownField.Num(); ++I)
    {
        ShownField[I] = FMath::Lerp(ShownField[I], TargetField[I], Alpha);
        const uint8 Value = uint8(FMath::Clamp(FMath::RoundToInt(ShownField[I] * 255), 0, 255));
        Pixels[4 * I] = Pixels[4 * I + 1] = Pixels[4 * I + 2] = Value;
        Pixels[4 * I + 3] = 255;
    }
    FieldTexture->UpdateTextureRegions(0, 1, new FUpdateTextureRegion2D(0, 0, 0, 0, FieldSide, FieldSide), FieldSide * 4, 4,
        Pixels, [](uint8* Data, const FUpdateTextureRegion2D* Region) { delete[] Data; delete Region; });
}
void AFogMistLabGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bLabReady) { return; }
    auto* Controller = UGameplayStatics::GetPlayerController(this, 0);
    auto* Player = Controller ? Cast<AFogMistLabCharacter>(Controller->GetPawn()) : nullptr;
    if (!Player) { return; }
    const FVector Foot = FogMistLab::Foot(*Player);
    LiveFog->SetVectorParameterValue(TEXT("ObserverPosition"), FLinearColor(Foot.X, Foot.Y, Foot.Z, 0));
    SampleElapsed += DeltaSeconds;
    if (SampleElapsed >= .1f && (TargetField.IsEmpty() || !LastFoot.Equals(Foot, 1.f) || SampleElapsed >= .5f))
    {
        FString Error;
        if (!BuildField(Foot, Error)) { Fail(Error); return; }
        SampleElapsed = 0;
    }
    UploadField(DeltaSeconds);
    float Radius = 1050;
    TuningMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RadiusCm")), Radius);
    VisibleMonsterCount = 0;
    for (const auto& Entry : Monsters)
    {
        auto* Monster = Entry.Get();
        if (!IsValid(Monster)) { continue; }
        const FVector Target = FogMistLab::Foot(*Monster);
        const bool Visible = FVector::DistSquared2D(Foot, Target) <= FMath::Square(double(Radius))
            && Sight.Trace(Foot, Target) == ELDSightResult::Visible;
        if (Monster->IsHidden() == Visible) { Monster->SetActorHiddenInGame(!Visible); }
        VisibleMonsterCount += Visible;
    }
}
void AFogMistLabController::StopMoving()
{
    Path.Reset();
    PathIndex = 0;
    bFollowingPath = false;
    if (auto* Body = Cast<ACharacter>(GetPawn())) { Body->GetCharacterMovement()->StopMovementImmediately(); }
}
void AFogMistLabController::Zoom(float Axis)
{
    if (auto* Body = Cast<AFogMistLabCharacter>(GetPawn())) { Body->Zoom(Axis); }
}
void AFogMistLabController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    auto* Body = Cast<AFogMistLabCharacter>(GetPawn());
    if (!Body) { return; }
    FVector Manual(IsInputKeyDown(EKeys::D) - IsInputKeyDown(EKeys::A),
        IsInputKeyDown(EKeys::S) - IsInputKeyDown(EKeys::W), 0);
    if (!Manual.IsNearlyZero())
    {
        Path.Reset(); bFollowingPath = false;
        Body->AddMovementInput(Manual.GetSafeNormal());
        return;
    }
    if (!bFollowingPath) { return; }
    while (Path.IsValidIndex(PathIndex) && FVector::DistSquared2D(Body->GetActorLocation(), Path[PathIndex]) < FMath::Square(30.f)) { ++PathIndex; }
    if (!Path.IsValidIndex(PathIndex)) { bFollowingPath = false; return; }
    Body->AddMovementInput((Path[PathIndex] - Body->GetActorLocation()).GetSafeNormal2D());
}

AFogMistLabGameMode::AFogMistLabGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AFogMistLabCharacter::StaticClass();
    PlayerControllerClass = AFogMistLabController::StaticClass();
    HUDClass = nullptr;
}
AActor* AFogMistLabGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
    {
        if (It->PlayerStartTag == TEXT("FogMistLabStart")) { return *It; }
    }
    return Super::ChoosePlayerStart_Implementation(Player);
}
void AFogMistLabGameMode::Fail(const FString& Error)
{
    StartupError = Error;
    bLabReady = false;
    UE_LOG(LogFogMistLab, Error, TEXT("LAB_START_FAILED %s"), *Error);
    if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 30, FColor::Red, Error); }
}
void AFogMistLabGameMode::BeginPlay()
{
    Super::BeginPlay();
    FString Error;
    if (!InitializeLab(Error)) { Fail(Error); return; }
    bLabReady = true;
    UE_LOG(LogFogMistLab, Display, TEXT("LAB_READY monsters=%d material=%s"), MonsterCount, *GetNameSafe(TuningMaterial));
}

bool AFogMistLabGameMode::InitializeLab(FString& Error)
{
    if (!FogMistLab::IsLab(GetWorld())) { Error = TEXT("Fog Mist Lab is only available in its lab level."); return false; }
    if (!LDDemoEntry::LoadRuntimeContent(Content, Error)) { return false; }
    const ULDMapDefinition* Definition = nullptr;
    for (TActorIterator<ALDMapAnchor> It(GetWorld()); It; ++It) { Definition = It->Definition; break; }
    if (!Definition || !Definition->Read(*Content, Geometry, Error)) { Error = TEXT("Lab map definition unavailable: ") + Error; return false; }
    FLDDemoRational Range;
    Range.Numerator = 2500; Range.Denominator = 1;
    const FLDWorldRevision Revision{FName(*Geometry->MapId), uint32(Geometry->LayoutRevision), uint32(Geometry->GeometryRevision)};
    LDDemoWorld::FTerrain LabTerrain;
    if (!LabTerrain.Build(*Geometry, Error)) { return false; }
    FBox NativeBounds(ForceInit);
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for (const auto* Part : Parts)
        {
            if (LDNativeGround::IsSupport(*Part)) { NativeBounds += Part->Bounds.GetBox(); }
        }
        if (It->ActorHasTag(TEXT("FogMistLabPreview"))) { It->SetActorHiddenInGame(true); }
    }
    if (!NativeBounds.IsValid) { Error = TEXT("Lab terrain has no native collision support."); return false; }
    LabTerrain.BindScene(*GetWorld(), NativeBounds);
    if (!Sight.Build(*Geometry, Revision, Range, Error, GetWorld(), &LabTerrain)) { return false; }
    TuningMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Lockdown/Art/Fog/MI_FogMist.MI_FogMist"));
    if (!TuningMaterial) { Error = TEXT("Live lab mist material is missing."); return false; }
    for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
    {
        if (It->ActorHasTag(TEXT("FogMistLabVolume"))) { FogVolume = *It; break; }
    }
    if (!FogVolume) { Error = TEXT("Lab fog post-process volume is missing."); return false; }
    FieldTexture = UTexture2D::CreateTransient(FieldSide, FieldSide, PF_B8G8R8A8);
    FieldTexture->SRGB = false;
    FieldTexture->Filter = TF_Bilinear;
    FieldTexture->AddressX = TA_Clamp;
    FieldTexture->AddressY = TA_Clamp;
    FieldTexture->NeverStream = true;
    FieldTexture->UpdateResource();
    LiveFog = UMaterialInstanceDynamic::Create(TuningMaterial, this);
    LiveFog->SetTextureParameterValue(TEXT("OcclusionDistance"), FieldTexture);
    LiveFog->SetVectorParameterValue(TEXT("FieldWorldMin"), FLinearColor(Sight.MinCm().X, Sight.MinCm().Y, 0, 0));
    LiveFog->SetVectorParameterValue(TEXT("FieldWorldSpan"), FLinearColor(Sight.MaxCm().X - Sight.MinCm().X, Sight.MaxCm().Y - Sight.MinCm().Y, 0, 0));
    FogVolume->Settings.WeightedBlendables.Array.Reset();
    FogVolume->Settings.AddBlendable(LiveFog, 1);
    const auto Query = LDNativeGround::Query(*GetWorld());
    for (const FLDDemoCampPlacement& Camp : Content->Get().Camps)
    {
        const auto* Anchor = Geometry->Anchors.FindByPredicate([&](const FLDDemoWorldAnchor& A) { return A.Id == Camp.AnchorId; });
        const FLDDemoMonsterDefinition* Monster = nullptr;
        if (!Anchor || !Content->FindMonster(Camp.MonsterId, Monster, Error)) { return false; }
        for (const FLDDemoWorldMemberHome& Member : Anchor->Members)
        {
            FVector Home;
            if (!Member.PositionCm.ToVector(Home, Error)) { return false; }
            FHitResult Hit;
            if (!LDNativeGround::Trace(*GetWorld(), Hit, Home + FVector(0, 0, 3000), Home - FVector(0, 0, 3000), Query))
            { Error = TEXT("Monster has no supporting ground: ") + Member.Id; return false; }
            Home.Z = Hit.ImpactPoint.Z + GetDefault<AFogMistLabCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 2;
            const FTransform Transform(FRotator::ZeroRotator, Home);
            auto* Body = GetWorld()->SpawnActorDeferred<AFogMistLabCharacter>(AFogMistLabCharacter::StaticClass(), Transform,
                this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
            if (!Body) { Error = TEXT("Monster spawn failed: ") + Member.Id; return false; }
            Body->CreatureId = Monster->NativeId;
            Body->FinishSpawning(Transform);
            Body->SetActorHiddenInGame(true);
            Monsters.Add(Body);
        }
    }
    MonsterCount = Monsters.Num();
    return true;
}
