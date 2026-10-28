#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Character/LDCharacter.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"

bool ALDCombatDirector::IsB84Skill(ELDSkill K) const
{return uint8(K)>=44&&K!=ELDSkill::ShatterBurst&&K!=ELDSkill::ConductiveField&&uint8(K)<=61;}
bool ALDCombatDirector::ReleaseCatalog(ALDCharacter* Body,int64 At,TArray<FLDCombatHit>& Hits,FString& Why)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Body);auto* B=BuildFor(Body);const auto K=S->Skill;const auto* D=M->TacticalCatalog->Find(K);auto* PC=Cast<ALDPlayerController>(Body->GetController());auto* First=Cast<ALDCharacter>(S->Target.Get());const auto R=S->ContentRequest;
    auto Reject=[&](){Why=TEXT("Target, aid consent, geometry or field capacity changed; nothing spent");return false;};
    if(!D||!B||B->RulesVersion!=8||S->Tactical.MP<D->Mana){return Reject();}
    const bool Self=D->Target==ELDTacticalTarget::Self;FVector Point=Self?Body->GetActorLocation():FVector(R.Point),Ground;
    const int32 Range=D->Range+(D->Range?B->Gear.CastRange:0);const int32 Value=LDGear::Magnitude(*B,K,D->RankKey,D->Magnitudes);const double Scale=FMath::Min(1.25,1+.025*FMath::Max(0,LDDevelopment::Effective(*B)[3]-8))*(1+B->Gear.SpellPercent/100.);
    auto Aid=[&](ALDCharacter* T){if(!T){return false;}if(T==Body){return true;}auto* TP=Cast<ALDPlayerController>(T->GetController());const auto* O=TP?M->StateFor(TP):nullptr;return O&&O->bAidConsent&&LegalTarget(Body,T,Self?D->Area:Range,nullptr,0);};
    if(D->Target==ELDTacticalTarget::Unit&&!LegalTarget(Body,First,Range,nullptr,0)){return Reject();}
    if(D->Target==ELDTacticalTarget::SelfOrUnit&&!Aid(First)){return Reject();}
    ALDTacticalField* Construct=Cast<ALDTacticalField>(S->Target.Get());
    if(D->Target==ELDTacticalTarget::Construct&&(!Construct||(!(Construct->Source.Get()==Body&&FVector::DistSquared2D(Body->GetActorLocation(),Construct->GetActorLocation())<=FMath::Square(Range)&&(!PC||M->Vision->CanObserve(PC,Construct)))&&!LegalTarget(Body,Construct,Range,nullptr,0))||Construct->EnvironmentPool||Construct->Kind==ELDTacticalField::Scramble||Construct->Kind==ELDTacticalField::GuardianPulse)){return Reject();}
    if(D->Target==ELDTacticalTarget::Ground||D->Target==ELDTacticalTarget::Line||D->Target==ELDTacticalTarget::Cone||Self)
    {if(!LDGround::MapSurface(GetWorld(),Point,Ground)||(!Self&&FVector::DistSquared2D(Point,Body->GetActorLocation())>FMath::Square(Range))){return Reject();}Point=Ground;}
    ALDTacticalField* Field=nullptr;
    if(K==ELDSkill::DeployConduit||K==ELDSkill::ScoutingPulse)
    {Field=CreateField(Body,K==ELDSkill::DeployConduit?ELDTacticalField::Conduit:ELDTacticalField::Detector,Point+FVector(0,0,15),At,K==ELDSkill::DeployConduit?8000:3000,K==ELDSkill::DeployConduit?60:1,K==ELDSkill::DeployConduit?250:350);if(!Field){return Reject();}Field->NextTick=At+1000;}
    using LDEnvironment::EInput;bool Surface=false,Disk=true;double Width=D->Area,Length=D->Range;EInput Input=EInput::Water;int32 Duration=3000;
    switch(K){case ELDSkill::IceNova:Surface=true;Input=EInput::Frost;Duration=5000;break;case ELDSkill::RainCall:Surface=true;Duration=6000;break;case ELDSkill::Flameburst:Surface=true;Input=EInput::Fire;break;case ELDSkill::StaticLance:Surface=true;Input=EInput::Lightning;Duration=2000;Disk=false;break;case ELDSkill::Updraft:Surface=true;Input=EInput::Wind;Duration=0;break;case ELDSkill::ThawingTouch:Surface=true;Input=EInput::Thaw;break;case ELDSkill::ThermalVeil:Surface=true;Input=EInput::Steam;Duration=4000;break;default:break;}
    if(Surface&&!StageEnvironment(Body,Point,Input,Disk,Length,Width,At,Duration)){if(Field){DestroyField(Field);}return Reject();}
    // Every fallible allocation/target check precedes the one release-cost commit.
    S->Tactical.MP-=D->Mana;B->Cooldowns[uint8(K)]=At/1000.+(D->RankKey==TEXT("cooldown_ms")?Value:D->Cooldown)/1000.;
    auto Hit=[&](ALDCharacter* T,double Amount,LDTactical::EDamage Kind,bool Spell)->FLDCombatHit&{FLDCombatHit H{Body,T,Amount,Kind,Spell,0,R.Token,S->Identity};H.TargetLife=StateFor(T)->Identity;return Hits.Add_GetRef(H);};
    if(K==ELDSkill::VineLash||K==ELDSkill::DisarmingCut){auto& H=Hit(First,Value,LDTactical::EDamage::Physical,true);if(K==ELDSkill::VineLash){H.RootMS=900;}else{H.DisarmMS=1500;}}
    if(K==ELDSkill::CleanseRain){auto* T=StateFor(First);T->Tactical.Statuses.RemoveAll([](const auto& V){return V.Kind==LDTactical::EStatus::Wet||V.Kind==LDTactical::EStatus::Burning;});BurningUnits.Remove(First);}
    if(K==ELDSkill::RescueLine&&First!=Body){Push(First,(Body->GetActorLocation()-First->GetActorLocation()).GetSafeNormal2D(),FMath::Min(250.,FMath::Max(0.,FVector::Dist2D(Body->GetActorLocation(),First->GetActorLocation())-90)),At/1000.);}
    if(K==ELDSkill::Sabotage){Construct->SuppressedUntil=FMath::Max(Construct->SuppressedUntil,At+3000);if(Construct->Kind==ELDTacticalField::Prison){TInlineComponentArray<UBoxComponent*> Walls(Construct);for(auto* Wall:Walls){Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);}}M->Vision->RefreshEnvironment();}
    const FVector Direction=(FVector(R.Point)-Body->GetActorLocation()).GetSafeNormal2D();
    for(auto& Pair:Bodies)
    {
        auto* T=Pair.Key.Get();if(!T||Pair.Value.Hp<=0||At<LDTactical::Milliseconds(Pair.Value.ProtectionUntil)){continue;}
        if(K==ELDSkill::RallyGuard)
        {if(FVector::DistSquared2D(T->GetActorLocation(),Body->GetActorLocation())<=300.*300&&(T==Body||Aid(T))&&LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),T->GetActorLocation())){const auto* TB=BuildFor(T);LDTactical::Barrier(Pair.Value.Tactical,Value*Scale,TB?LDDevelopment::MaxHP(*TB):600,At+2500,At);}continue;}
        if(T==Body){continue;}const FVector Delta=T->GetActorLocation()-Body->GetActorLocation();bool Inside=false;
        if(K==ELDSkill::IceNova||K==ELDSkill::Flameburst||K==ELDSkill::Updraft){Inside=FVector::DistSquared2D(T->GetActorLocation(),Point)<=FMath::Square(D->Area);}
        if(K==ELDSkill::StaticLance){const double Along=FVector::DotProduct(Delta,Direction);Inside=Along>=0&&Along<=D->Range&&FVector::DistSquared2D(T->GetActorLocation(),Body->GetActorLocation()+Direction*Along)<=FMath::Square(D->Area);}
        if(K==ELDSkill::PowerStrike){Inside=Delta.SizeSquared2D()<=250.*250&&FVector::DotProduct(Delta.GetSafeNormal2D(),Direction)>=FMath::Cos(FMath::DegreesToRadians(25.));}
        if(!Inside||!LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),T->GetActorLocation())){continue;}
        if(K==ELDSkill::Updraft){auto& H=Hit(T,0,LDTactical::EDamage::Magical,false);H.SlowMS=1800;H.SlowMagnitude=35;}
        else{auto& H=Hit(T,K==ELDSkill::PowerStrike?Value:Value*Scale,K==ELDSkill::PowerStrike?LDTactical::EDamage::Physical:LDTactical::EDamage::Magical,false);H.DirectFrost=K==ELDSkill::IceNova;H.DirectFire=K==ELDSkill::Flameburst;H.BarrierBonus=K==ELDSkill::PowerStrike?30:0;}
    }
    BreakConcealment(Body,true);S->QuietSince=At/1000.;S->ExposedUntil=FMath::Max(S->ExposedUntil,At/1000.+4);Why=TEXT("Released once; source effects and costs committed");return true;
}
