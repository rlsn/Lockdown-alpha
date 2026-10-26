#include "Development/LDV08Modifiers.h"
#include "Combat/LDCombatDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Region/LDRegionDefinition.h"
#include "World/LDWorldDirector.h"
#include "Region/LDRegionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
bool ALDCombatDirector::HasModifier(const ALDCharacter* Body,const TCHAR* Id,int64 At) const
{const auto* B=const_cast<ALDCombatDirector*>(this)->BuildFor(const_cast<ALDCharacter*>(Body));const auto* S=StateFor(Body);return B&&S&&B->RulesVersion==8&&(B->Gear.ModifierMask&LDV08Modifiers::Bit(Id))&&LDTactical::PassiveActive(S->Tactical,FCString::Strcmp(Id,TEXT("survey_overlay"))!=0,At);}
bool ALDCombatDirector::ClaimProc(ALDCharacter* Body,const TCHAR* Family,int64 At,int64 Cooldown)
{
    auto* S=StateFor(Body);auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;auto* O=PC?M->StateFor(PC):nullptr;const FName Key(Family);ALDItem* Instance=nullptr;
    if(O){for(const auto& Ref:O->Inventory.Bindings){auto* I=M->Inventory->Owned(PC,Ref);const auto* D=I?ULDEquipmentCatalog::Load(8)->Find(I->Kind):nullptr;if(D&&(D->Stats.ModifierMask&LDV08Modifiers::Bit(Family))){Instance=I;break;}}if(!Instance){return false;}}
    if((O?O->ProcReady.FindRef(Key):S->ProcReady.FindRef(Key))>At||(Instance&&Instance->ProcReadyAt>At)){return false;}if(O){O->ProcReady.FindOrAdd(Key)=At+Cooldown;}else{S->ProcReady.Add(Key,At+Cooldown);}
    if(Instance){Instance->ProcReadyAt=At+Cooldown;++Instance->Revision;if(auto* I=O->Inventory.Items.FindByPredicate([&](const auto& V){return V.Id==Instance->Identity;})){I->ProcReadyAt=Instance->ProcReadyAt;I->Revision=Instance->Revision;}++O->Inventory.Revision;M->Inventory->Changed(PC);}return true;
}
void ALDCombatDirector::AugmentBasic(FLDCombatHit& H,int64 At)
{
    auto* T=Cast<ALDCharacter>(H.Target);auto* S=StateFor(T);if(!H.Basic||H.Damage<=0||!T||!S||S->Hp<=0||At<LDTactical::Milliseconds(S->ProtectionUntil)||!H.Source){return;}
    if(HasModifier(H.Source,TEXT("isolation_strike"),At))
    {bool Isolated=true;for(const auto& P:Bodies){auto* Other=P.Key.Get();if(Other&&Other!=T&&Other!=H.Source&&P.Value.Hp>0&&!Other->IsA<ALDWildSentinel>()&&!(Cast<ALDWorldPawn>(Other)&&Cast<ALDWorldPawn>(Other)->Immune())&&FVector::DistSquared2D(T->GetActorLocation(),Other->GetActorLocation())<=300.*300){Isolated=false;break;}}const auto* B=BuildFor(H.Source);if(Isolated&&B&&!B->Gear.Ranged&&ClaimProc(H.Source,TEXT("isolation_strike"),At,4000)){H.Damage+=12;}}
    if(HasModifier(H.Source,TEXT("conductive_hit"),At)&&S->Tactical.Has(LDTactical::EStatus::Wet,At)&&ClaimProc(H.Source,TEXT("conductive_hit"),At,4000)){H.ExtraMagical=15;}
    if(HasModifier(H.Source,TEXT("ember_hit"),At)&&ClaimProc(H.Source,TEXT("ember_hit"),At,5000)){H.DirectFire=true;}
}
double ALDCombatDirector::AuraArmor(ALDCharacter* Body,int64 At) const
{
    const auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;const auto* O=PC?const_cast<ALDGameMode*>(M)->StateFor(const_cast<ALDPlayerController*>(PC)):nullptr;
    for(const auto& P:Bodies){auto* Source=P.Key.Get();if(Source&&P.Value.Hp>0&&HasModifier(Source,TEXT("consented_armor_aura"),At)&&(Source==Body||(O&&O->bAidConsent))&&FVector::DistSquared2D(Source->GetActorLocation(),Body->GetActorLocation())<=300.*300&&LDGround::ClearSegment(GetWorld(),Source->GetActorLocation(),Body->GetActorLocation())){return 4;}}return 0;
}
void ALDCombatDirector::SampleCatalog(int64 At)
{
    const auto* Region=LDRegion::Definition(GetWorld());if(!Region){return;}
    for(auto& P:Bodies)
    {
        auto* B=P.Key.Get();auto& S=P.Value;if(!B||S.Hp<=0){continue;}bool Cover=false;const auto* Build=BuildFor(B);if(Build&&Build->RulesVersion==8&&(Build->Gear.ModifierMask&LDV08Modifiers::Bit(TEXT("cover_concealment")))){for(const auto& R:Region->Routes){if(!R.Covered){continue;}for(int32 I=1;I<R.Points.Num();++I){FVector A=R.Points[I-1],End=R.Points[I];A.Z=End.Z=B->GetActorLocation().Z;if(FMath::PointDistToSegment(B->GetActorLocation(),A,End)<=70){Cover=true;break;}}if(Cover){break;}}}
        if(!Cover||B->GetVelocity().SizeSquared2D()>1||FVector::DistSquared2D(S.CoverAnchor,B->GetActorLocation())>4){S.CoverSince=At;S.CoverAnchor=B->GetActorLocation();S.CoverConcealed=false;}
        else if(!S.Action&&At-S.CoverSince>=2000){S.CoverConcealed=true;}
    }
}
