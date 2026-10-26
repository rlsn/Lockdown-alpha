#include "Development/LDV08Loadout.h"
#include "Combat/LDCombatDirector.h"
#include "World/LDWorldDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Combat/LDCombatRules.h"
#include "Character/LDCharacter.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDTradeDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "Economy/LDProductionDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Region/LDRegionDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Navigation/LDNavigationComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
namespace
{
    FGuid Life(ALDCombatDirector* C,AActor* A){if(auto* B=Cast<ALDCharacter>(A)){const auto* S=C->StateFor(B);return S?S->Identity:FGuid();}const auto* F=Cast<ALDTacticalField>(A);return F?F->Identity:FGuid();}
    bool Major(const FString& Family){return Family==TEXT("major_protection");}
}
bool ALDCombatDirector::IsContentSkill(ELDSkill K) const
{if(K==ELDSkill::SmokeVeil){return GetWorld()->GetAuthGameMode<ALDGameMode>()->IsV08Profile();}switch(K){case ELDSkill::Firebolt:case ELDSkill::Gust:case ELDSkill::Entangle:case ELDSkill::ChainLightning:case ELDSkill::ShatterBurst:case ELDSkill::ConductiveField:case ELDSkill::Frostline:case ELDSkill::WaterJet:case ELDSkill::BraceWall:case ELDSkill::Hamstring:case ELDSkill::LedgerMark:case ELDSkill::FalseTrail:case ELDSkill::EmberLine:case ELDSkill::Grapple:case ELDSkill::NullSeal:case ELDSkill::GuardSwap:case ELDSkill::Slipstream:case ELDSkill::ArcanePrison:case ELDSkill::TemporalLock:case ELDSkill::SovereignWard:case ELDSkill::GrandExchange:case ELDSkill::GateWeave:return true;default:return false;}}
void ALDCombatDirector::QueueContent(ALDPlayerController* PC,const FLDTacticalRequest& R)
{
    FlushPastCommand();int32 Count=0;for(const auto& I:Intents){Count+=I.Controller==PC;}if(Count>=3){PC->CompleteCombatRequest(R.Token,TEXT("Too many queued actions"));return;}FIntent I;I.Controller=PC;I.Match=R.Match;I.Life=R.Life;I.Request=R.Token;I.Action=3+R.Slot;I.Direction=R.Direction;I.BuildRevision=R.BuildRevision;I.Content=R;I.IsContent=true;Intents.Add(I);}
void ALDCombatDirector::RefreshItemFamilies(ALDPlayerController* PC)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* O=M->StateFor(PC);const auto* C=ULDEquipmentCatalog::Load(O?O->Build.RulesVersion:7);if(!O||O->Build.RulesVersion<7||!C){return;}for(const auto& I:O->Inventory.Items){if(const auto* D=C->Find(I.Kind);D&&!D->Family.IsEmpty()){auto& End=O->ItemFamilies.FindOrAdd(D->CooldownKey());End=FMath::Max(End,I.ReadyAt);}}O->ItemFamilies.FindOrAdd(TEXT("cleanse"))=FMath::Max(O->ItemFamilies.FindRef(TEXT("cleanse")),LDTactical::Milliseconds(O->NullBellReadyAt));}
bool ALDCombatDirector::RequestContent(ALDCharacter* Body,const FLDTacticalRequest& Input,AActor* First,AActor* Second,FString& Why)
{
    auto R=Input;
    auto Reject=[&](const TCHAR* Text){Why=Text;return false;};auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* S=StateFor(Body);auto* B=BuildFor(Body);auto* PC=Body?Cast<ALDPlayerController>(Body->GetController()):nullptr;auto* O=PC?M->StateFor(PC):nullptr;const double Now=GetWorld()->GetTimeSeconds();const int64 At=LDTactical::Milliseconds(Now);
    if(!HasAuthority()||!M->IsTacticalProfile()||!M->IsPlaying()||!S||!B||S->Hp<=0||Now<S->ProtectionUntil||S->Action||Now<S->CoreRecoveryUntil||!R.Token||R.Token<=S->LastCoreToken||R.Token<=S->CancelledCoreThrough||B->Revision!=R.BuildRevision||R.ActionKind>4){return Reject(TEXT("Action unavailable, stale or occupied"));}
    if(B&&B->RulesVersion==8&&R.ActionKind==0&&B->Actions.IsValidIndex(R.Slot)&&B->Actions[R.Slot].Source==ELDActionSource::Gear){if(R.SourceKind!=1||!O||!LDV08::ItemSource(O->Inventory,*B,R.Item,R.Slot,false)){return Reject(TEXT("Equipped body action source changed"));}R.ActionKind=1;}
    ELDSkill Skill=R.ActionKind==0&&B->Slots.IsValidIndex(R.Slot)?B->Slots[R.Slot]:ELDSkill::None;
    if(R.ActionKind==4){if(!First||!LegalTarget(Body,First,B->Gear.Reach,nullptr,0)||!RequestBody(Body,0,R.Direction,Why)){return false;}S->bAttackOrder=true;S->AttackTarget=First;S->Target=First;S->AttackLife=Life(this,First);S->AttackToken=R.Token;return true;}
    const auto* Decoy=Cast<ALDTacticalField>(First);const bool IsDecoy=Decoy&&Decoy->Kind==ELDTacticalField::Decoy;
    if(R.ActionKind==0&&!IsContentSkill(Skill)&&!IsDecoy){return RequestEquipped(Body,R.BuildRevision,R.Slot,R.Token,R.Direction,Why,First,R.Point);}
    if(!Allows(Body,R.ActionKind==1?LDTactical::EAction::Item:R.ActionKind>=2?LDTactical::EAction::Dash:LDTactical::EAction::Skill,&Why)){return false;}
    FLDTacticalRequest Plan=R;ALDItem* Item=nullptr;const FLDEquipmentDefinition* ID=nullptr;const FLDTacticalDefinition* D=nullptr;
    auto Observe=[&](AActor* T,int32 Range,bool Own=false){if(!T||FVector::DistSquared2D(Body->GetActorLocation(),T->GetActorLocation())>FMath::Square(Range)){return false;}if(T==Body){return true;}if(Own){const auto* F=Cast<ALDTacticalField>(T);return F&&F->Source.Get()==Body&&F->Active(At)&&(!PC||M->Vision->CanObserve(PC,T));}return LegalTarget(Body,T,Range,nullptr,0);};
    auto Ground=[&](FVector Point,int32 Range){FVector Surface;return !Point.ContainsNaN()&&LDVision::InWorld(Point,M->Vision->Grid())&&FVector::DistSquared2D(Body->GetActorLocation(),Point)<=FMath::Square(Range)&&LDGround::MapSurface(GetWorld(),Point,Surface);};
    if(R.ActionKind==0)
    {
        D=M->TacticalCatalog->Find(Skill);if(!D||!LDDevelopment::Equipped(*B,Skill)||!ULDTacticalCatalog::NaturalGate(*D,*B)||B->Cooldowns[uint8(Skill)]>Now){return Reject(TEXT("Knowledge, natural gate, slot or cooldown unavailable"));}
        const int32 Range=D->Range+(D->Range&&Skill!=ELDSkill::GateWeave?B->Gear.CastRange:0);int32 Mana=D->Mana;
        if(D->Target==ELDTacticalTarget::Self){First=Body;}
        else if(Skill==ELDSkill::GrandExchange)
        {if(!First&&!R.Observed.IsValid()){First=Body;}if(!Second&&!R.SecondObserved.IsValid()){Second=Body;}if(!Observe(First,Range,Cast<ALDTacticalField>(First)!=nullptr)||!Observe(Second,Range,Cast<ALDTacticalField>(Second)!=nullptr)||!AtomicSwap(First,Second,Body,At,Why,false)){return Reject(TEXT("Two observable eligible swap targets required"));}}
        else if(Skill==ELDSkill::GateWeave)
        {if(!PC||!Ground(R.Point,650+B->Gear.CastRange)||!Ground(R.SecondPoint,5000)||FVector::DistSquared2D(R.Point,R.SecondPoint)>4000.*4000||!Explored(PC,R.Point)||!Explored(PC,R.SecondPoint)){return Reject(TEXT("Gate placement unavailable"));}}
        else if(Skill==ELDSkill::GuardSwap)
        {if(First){const auto* F=Cast<ALDTacticalField>(First);FVector End;if(!F||F->Kind!=ELDTacticalField::Marker||!Observe(First,Range,true)||!Allows(Body,LDTactical::EAction::Dash)||!MobilityEndpoint(Body,F->GetActorLocation(),End,F)){return Reject(TEXT("Owned observable marker or movement endpoint unavailable"));}}else{Mana=10;if(!Ground(R.Point,Range)){return Reject(TEXT("Marker placement unavailable"));}}}
        else if(Skill==ELDSkill::Grapple)
        {if(!Allows(Body,LDTactical::EAction::Dash)){return Reject(TEXT("Grapple mobility blocked"));}if(First){if(!Cast<ALDCharacter>(First)||First->IsA<ALDWildSentinel>()||(Cast<ALDWorldPawn>(First)&&Cast<ALDWorldPawn>(First)->Immune())||!Observe(First,Range)){return Reject(TEXT("Invalid grapple body"));}}else if(!Ground(R.Point,Range)||!LDGround::ClearSegment(GetWorld(),Body->GetActorLocation(),R.Point)){return Reject(TEXT("Choose an unobstructed terrain anchor"));}}
        else if(D->Target==ELDTacticalTarget::SelfOrUnit){if(!Observe(First,Range)||Skill==ELDSkill::RemedyBind){return Reject(TEXT("Observable eligible aid target required"));}}
        else if(D->Target==ELDTacticalTarget::Unit){if((!Cast<ALDCharacter>(First)&&!IsDecoy)||!Observe(First,Range)){return Reject(TEXT("Observable body required"));}}
        else{if(!Ground(R.Point,Range)){return Reject(TEXT("Ground placement unavailable"));}}
        if(Skill==ELDSkill::Slipstream&&!Allows(Body,LDTactical::EAction::Dash)){return Reject(TEXT("Dash movement blocked"));}
        if(Skill==ELDSkill::SovereignWard&&At<FMath::Max(S->Tactical.MajorReadyAt,O?O->MajorProtectionReadyAt:int64(0))){return Reject(TEXT("Major protection family locked"));}
        if(S->Tactical.MP<Mana){return Reject(TEXT("Insufficient MP"));}
    }
    else if(R.ActionKind==1)
    {
        RefreshItemFamilies(PC);Item=O&&M->Inventory?M->Inventory->Owned(PC,R.Item):nullptr;ID=Item?ULDEquipmentCatalog::Load(B->RulesVersion)->Find(Item->Kind):nullptr;
        if(!O||!ID||ID->Operation.IsEmpty()||!Item||Item->bReserved||Item->Revision!=R.ItemRevision||O->Inventory.Revision!=R.InventoryRevision||(B->RulesVersion==8?!LDV08::ItemSource(O->Inventory,*B,Item->Identity,R.Slot,R.SourceKind==2):!O->Inventory.Quick.Contains(Item->Identity)||(ID->Binding!=255&&!Item->BindingMask))||Item->ReadyAt>At||O->ItemFamilies.FindRef(ID->CooldownKey())>At){return Reject(TEXT("Item needs its current equipped/quick reference and ready family"));}
        if(Major(ID->Family)&&At<FMath::Max(S->Tactical.MajorReadyAt,O->MajorProtectionReadyAt)){return Reject(TEXT("Major protection locked"));}
        const auto& Op=ID->Operation;const int32 Range=ID->Param(TEXT("range_cm"));
        if(Op==TEXT("element_item_hit")||Op==TEXT("targeted_mute")||Op==TEXT("basic_dispel")){if(First&&((!Cast<ALDCharacter>(First)&&!IsDecoy)||!Observe(First,Range))){return Reject(TEXT("Observable target required"));}if((Op==TEXT("targeted_mute")||Op==TEXT("element_item_hit"))&&!First){return Reject(TEXT("Observable hostile target required"));}}
        else if(Op==TEXT("surface_clear")||Op==TEXT("element_item_field")||Op==TEXT("smoke")||Op==TEXT("spawn_ward")||Op==TEXT("spawn_trap")||Op==TEXT("terrain_grapple")||Op==TEXT("grapple"))
        {if(Op==TEXT("grapple")&&First){if(!Cast<ALDCharacter>(First)||First->IsA<ALDWildSentinel>()||(Cast<ALDWorldPawn>(First)&&Cast<ALDWorldPawn>(First)->Immune())||!Observe(First,Range)){return Reject(TEXT("Invalid grapple target"));}}else if(!Ground(R.Point,Range)){return Reject(TEXT("Item ground placement unavailable"));}}
        if(Op==TEXT("blink")){FVector End;if(At<O->DirectDamageUntil||!Allows(Body,LDTactical::EAction::Dash)||!Ground(R.Point,ID->Param(TEXT("distance_cm")))||!MobilityEndpoint(Body,R.Point,End)){return Reject(TEXT("Blink unavailable"));}}
        if((Op==TEXT("grapple")||Op==TEXT("terrain_grapple"))&&!Allows(Body,LDTactical::EAction::Dash)){return Reject(TEXT("Grapple movement blocked"));}
        if(Op==TEXT("recall"))
        {if(HasObjectiveCargo(PC)||!Allows(Body,LDTactical::EAction::Dash)){return Reject(TEXT("Recall mobility or cargo blocked"));}bool Found=false;const auto* Region=LDRegion::Definition(GetWorld());for(uint8 Site:{uint8(1),uint8(2),uint8(6)}){const FVector P=LDRegion::Site(GetWorld(),Site);if(FVector::DistSquared2D(P,R.Point)<=120*120&&Region&&(Region->NearestRegion(P)==Region->CityRegion||O->VisitedRegions.Contains(Region->NearestRegion(P)))){FVector End;if(MobilityEndpoint(Body,P+FVector(80,0,0),End)){Plan.Point=End;Found=true;break;}}}if(!Found){return Reject(TEXT("Choose a visited City/West/East service"));}}
    }
    else
    {
        const auto* F=Cast<ALDTacticalField>(First);if(!PC||!F||!F->Active(At)||FVector::DistSquared2D(Body->GetActorLocation(),F->GetActorLocation())>80*80||(R.ActionKind==2&&HasObjectiveCargo(PC))||M->Objectives->HasChannel(PC)||At<O->GateReadyAt){return Reject(TEXT("Traversal unavailable"));}
        if(R.ActionKind==2){if(F->Kind!=ELDTacticalField::Gate||!F->Partner.IsValid()||!F->Partner->Active(At)){return Reject(TEXT("Gate pair unavailable"));}Plan.SecondPoint=F->Partner->GetActorLocation();}
        else{if(F->Kind!=ELDTacticalField::Scramble||!B->Gear.Scramble){return Reject(TEXT("Cliffwalkers and an authored scramble link required"));}Plan.SecondPoint=F->Direction;}
        FVector End;if(!MobilityEndpoint(Body,Plan.SecondPoint,End)){return Reject(TEXT("Traversal placement unavailable"));}
    }
    if(PC){PC->Navigation->Stop();if(M->Trade){M->Trade->CancelFor(PC);}M->Development->Cancel(PC,TEXT("Action cancelled preparation"));if(M->Production){M->Production->Cancel(PC);}}Body->GetCharacterMovement()->StopMovementImmediately();
    S->bAttackOrder=false;if(O){Plan.InventoryRevision=O->Inventory.Revision;}S->ContentRequest=Plan;S->HadPositiveChannelDamage=false;S->CoreItem=Item?Item->Identity:FGuid();S->Target=First;S->CoreTargetLife=Life(this,First);S->CoreSecondTarget=Second;S->CoreSecondLife=Life(this,Second);S->CorePoint=Plan.Point;S->CoreSecondPoint=Plan.SecondPoint;S->CoreDirection=R.Direction;S->CoreBuildRevision=B->Revision;S->Skill=Skill;S->SkillToken=R.Token;S->LastCoreToken=R.Token;S->SkillAnchor=Body->GetActorLocation();S->Order=++NextOrder;S->LastSkillHold=Now;S->HoldSequence=0;
    if(R.ActionKind>=2){S->Action=10;S->ContentChannel=R.ActionKind==2?TEXT("gate"):TEXT("scramble");S->CoreEndsAt=At+1000;}
    else if(ID&&(ID->Operation==TEXT("restore_mp")||ID->Operation==TEXT("recall")))
    {
        S->Action=10;S->ContentChannel=ID->Operation;S->ChannelDestination=Plan.Point;S->CoreEndsAt=At+int64(ID->Param(TEXT("channel_ms")));S->HadPositiveChannelDamage=false;
        const int64 End=At+ID->Cooldown;O->ItemFamilies.FindOrAdd(ID->CooldownKey())=End;Item->ReadyAt=End;S->CoreItem.Invalidate();M->Inventory->Remove(PC,Item,true,TEXT("ChannelItemConsumed"));
    }
    else
    {S->Action=9;S->CoreEndsAt=At+(D?D->Windup:B->RulesVersion==8?int32(ID->Param(TEXT("windup_ms"),250)):ID&&ID->Kind==ELDItemKind::NullBell?0:250);if(Item){Item->bReserved=true;auto* E=const_cast<FLDInventoryItem*>(O->Inventory.Find(Item->Identity));E->bReserved=true;M->Inventory->Changed(PC);}if(Skill==ELDSkill::TemporalLock){auto* F=CreateField(Body,ELDTacticalField::Telegraph,Plan.Point,At,D->Windup,1,LDGear::Magnitude(*B,Skill,D->RankKey,D->Magnitudes));if(F){F->Skill=Skill;}}}
    S->ImpactAt=S->CoreEndsAt/1000.;Sync(Body,*S);Why=TEXT("Accepted; authoritative release/held channel revalidates legality");return true;
}
void ALDCombatDirector::ContentHold(ALDPlayerController* PC,uint32 LifeId,uint32 Token,uint32 Sequence,bool Moving,bool Release)
{
    FlushPastCommand();auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(PC);auto* S=PC?StateFor(Cast<ALDCharacter>(PC->GetPawn())):nullptr;if(!O||O->Life!=LifeId||!S||S->Action!=10||S->SkillToken!=Token||Sequence<=S->HoldSequence){return;}S->HoldSequence=Sequence;S->LastSkillHold=GetWorld()->GetTimeSeconds();if(Release||Moving){CancelTactical(Cast<ALDCharacter>(PC->GetPawn()),TEXT("Held action released"));}}
