#pragma once
#include "CoreMinimal.h"

// Server-local settled-world format. No controller, handle or bag is serialized.
namespace LDCheckpoint
{
    struct FObject
    {
        FString Id;int32 Kind=0;FVector Position=FVector::ZeroVector,Clear=FVector::ZeroVector;
        uint32 Revision=1;double Hp=80;bool Broken=false;
        FGuid Harvest,Yield,Burn,Physical;TArray<FGuid> Contents;
    };
    struct FItem
    {
        FGuid Id,Receipt;FString Origin,Location;int32 Kind=0;uint32 Revision=1;
        FVector Position=FVector::ZeroVector;int32 Fuel=0;bool Lit=false;
        int64 Ready=0,Proc=0;
    };
    struct FConsumed{FGuid Id,Receipt;FString Origin;int32 Kind=0;};
    struct FDoor{FString Id;uint32 Revision=1;uint8 State=0;};
    struct FData
    {
        TArray<FObject> Objects;TArray<FItem> Items;TArray<FConsumed> Consumed;TArray<FDoor> Doors;
        uint32 Committed=0,DoorMutation=1;
    };
    FString Hash(const FString& Text);
    FString Encode(const FData& Data);
    bool Decode(const FString& Text,FData& Data,FString& Error);
    FString Envelope(const FString& Payload,const FString& Objects,const FString& Doors);
    bool Open(const FString& Text,const FString& Objects,const FString& Doors,FData& Data,FString& Error);
}
