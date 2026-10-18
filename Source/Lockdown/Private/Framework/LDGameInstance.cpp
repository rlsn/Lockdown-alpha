#include "Framework/LDGameInstance.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"

void ULDGameInstance::Init()
{
    Super::Init();
    NetworkHandle = GEngine->OnNetworkFailure().AddUObject(this, &ULDGameInstance::NetworkFailed);
    TravelHandle = GEngine->OnTravelFailure().AddUObject(this, &ULDGameInstance::TravelFailed);
}
void ULDGameInstance::Shutdown()
{
    GEngine->OnNetworkFailure().Remove(NetworkHandle);
    GEngine->OnTravelFailure().Remove(TravelHandle);
    Super::Shutdown();
}
void ULDGameInstance::NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
    // Pending connections have no world on the driver yet. Resolve their actual
    // owning context rather than losing the message or leaking it to another PIE instance.
    if (!World && Driver)
    {
        if (const FWorldContext* Context = GEngine->GetWorldContextFromPendingNetGameNetDriver(Driver))
        {
            if (Context->OwningGameInstance != this) { return; }
            World = Context->World();
        }
    }
    ReturnAfterFailure(World, FString::Printf(TEXT("%s. Check the address and that the host is running."), ENetworkFailure::ToString(Type)));
}
void ULDGameInstance::TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Error) { ReturnAfterFailure(World, Error); }
void ULDGameInstance::ReturnAfterFailure(UWorld* World, const FString& Error)
{
    if (!World || World->GetGameInstance() != this) { return; }
    ConnectionMessage = TEXT("Connection failed: ") + Error;
    if (!World->GetMapName().Contains(TEXT("L_Frontend")))
    {
        World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            UGameplayStatics::OpenLevel(this, TEXT("/Game/Lockdown/Generated/Maps/L_Frontend"));
        }));
    }
}
