#include "Combat/LDCombatDirector.h"
#include "Combat/LDRiverRules.h"
#include "Character/LDCharacter.h"
#include "Region/LDRegionDirector.h"
#include "Components/CapsuleComponent.h"
#include "Core/LDGroundQueries.h"
#include "Development/LDV08Loadout.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "World/LDWorldDirector.h"
#include "Engine/World.h"

void ALDCombatDirector::ElementContact(const FLDCombatHit& Hit,ALDCharacter* Victim,int64 At)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* T=StateFor(Victim);const auto* S=StateFor(Hit.Source);
    if(!T||(Hit.TargetLife.IsValid()&&Hit.TargetLife!=T->Identity)||Hit.Source==Victim||Hit.Periodic||At<LDTactical::Milliseconds(T->ProtectionUntil)){return;}
    const FGuid CreditId=Hit.CreditOwner.IsValid()?Hit.CreditOwner:Hit.Source?M->ContenderId(Cast<ALDPlayerController>(Hit.Source->GetController())):FGuid();
    if(Hit.DirectWater){T->Tactical.Statuses.RemoveAll([](const auto& V){return V.Kind==LDTactical::EStatus::Burning;});BurningUnits.Remove(Victim);}
    if(Hit.DirectFire){if(T->Tactical.Has(LDTactical::EStatus::Wet,At)){T->Tactical.Statuses.RemoveAll([](const auto& V){return V.Kind==LDTactical::EStatus::Wet;});}else if(T->Hp>0){T->Tactical.Statuses.RemoveAll([](const auto& V){return V.Kind==LDTactical::EStatus::Burning;});LDTactical::Apply(T->Tactical,LDTactical::EStatus::Burning,At+3000,Hit.SourceIdentity);FRiverState R;R.Owner=CreditId;R.Life=Hit.SourceIdentity.IsValid()?Hit.SourceIdentity:S?S->Identity:FGuid();R.Cast=FGuid::NewGuid();R.Created=At;R.Until=At+3000;R.Magnitude=12;BurningUnits.Add(Victim,R);}}
    if(Hit.DirectWater){LDTactical::Apply(T->Tactical,LDTactical::EStatus::Wet,At+3000,FGuid());}
    if(!Hit.DirectFrost||!S){return;}
    const FString Receipt=S->Identity.ToString()+TEXT(":")+FString::FromInt(Hit.Token)+TEXT(":")+T->Identity.ToString();
    if(FrostReceipts.Contains(Receipt)){return;}FrostReceipts.Add(Receipt,At+10000);
    auto* PC=Cast<ALDPlayerController>(Victim->GetController());auto* O=PC?M->StateFor(PC):nullptr;int64& Ready=O?O->WetFrostReadyAt:T->WetFrostReadyAt;
    if(LDRiver::AttemptFrost(T->Tactical,Ready,At))
    {
        T->WetFrostReadyAt=Ready;ApplyStatus(Victim,LDTactical::EStatus::Stun,At+600,S->Identity);
        UE_LOG(LogTemp,Display,TEXT("LD_B82_FROST at=%lld ready=%lld applied=%d token=%u"),At,Ready,T->Tactical.Has(LDTactical::EStatus::Stun,At),Hit.Token);
    }
}
