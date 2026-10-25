#include "Tests/LDContentProbe.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDTacticalCatalog.h"
#include "Character/LDCharacterMovementComponent.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Inventory/LDEquipmentCatalog.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDVendorDirector.h"
#include "Development/LDDevelopmentDirector.h"
#include "UI/LDInventoryPanel.h"
#include "Economy/LDEconomyDirector.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Region/LDRegionDefinition.h"
#include "Navigation/LDNavigationComponent.h"
#include "Core/LDGroundQueries.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

ALDContentProbe::ALDContentProbe(){bReplicates=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostUpdateWork;}
void ALDContentProbe::Tick(float Delta)
{
#if !UE_BUILD_SHIPPING
    if(Done){return;}auto* W=GetWorld();auto* M=W->GetAuthGameMode<ALDGameMode>();auto* A=Cast<ALDPlayerController>(W->GetFirstPlayerController());if(!A||!A->GetPawn()){return;}Done=true;
    int32 Failures=0;TArray<TSharedPtr<FJsonValue>> Checks;auto Check=[&](bool Good,const FString& Text){Failures+=!Good;Checks.Add(MakeShared<FJsonValueString>((Good?TEXT("PASS "):TEXT("FAIL "))+Text));UE_LOG(LogTemp,Display,TEXT("LD_B75 %s %s"),Good?TEXT("PASS"):TEXT("FAIL"),*Text);};
    auto* B=Cast<ALDPlayerController>(UGameplayStatics::CreatePlayer(this,1,true));FString Why;M->SetLobbyChoice(A,3,true,Why);if(B){M->SetLobbyChoice(B,4,true,Why);}if(!B||!M->StartDevelopmentMatch(A,Why)){Check(false,Why);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto* C=M->Combat.Get();auto* I=M->Inventory.Get();auto* E=M->Economy.Get();auto* OA=M->StateFor(A);auto* OB=M->StateFor(B);auto* BA=CastChecked<ALDCharacter>(A->GetPawn());auto* BB=CastChecked<ALDCharacter>(B->GetPawn());auto* SA=C->StateFor(BA);auto* SB=C->StateFor(BB);const auto* Catalog=M->TacticalCatalog.Get();const auto* Gear=ULDEquipmentCatalog::Load();uint32 Token=50000,Sequence=0;
    C->SetActorTickEnabled(false);M->GetGameState<ALDGameState>()->MatchDeadline=100000;
    auto Now=[&](){return LDTactical::Milliseconds(W->GetTimeSeconds());};
    auto Place=[&](ALDCharacter* Body,FVector P){FVector End;if(C->MobilityEndpoint(Body,P,End)){Body->SetActorLocation(End,false,nullptr,ETeleportType::TeleportPhysics);}Body->GetCharacterMovement()->StopMovementImmediately();};
    auto Advance=[&](double Seconds,bool Hold=false){for(double Left=Seconds;Left>1.e-7;){const double Step=FMath::Min(.05,Left);W->TimeSeconds+=Step;if(Hold){C->ContentHold(A,OA->Life,SA->SkillToken,++Sequence,false);}C->Sample(W->GetTimeSeconds());Left-=Step;}};
    auto Fields=[&](ELDTacticalField K){TArray<ALDTacticalField*> Out;for(TActorIterator<ALDTacticalField> It(W);It;++It){if(It->Kind==K&&It->Active(Now())){Out.Add(*It);}}return Out;};
    auto Clear=[&](){C->EndContentLife(BA);C->EndContentLife(BB);for(TActorIterator<ALDArcProjectile> It(W);It;++It){It->Destroy();}for(auto* PC:{A,B}){for(auto& V:M->StateFor(PC)->Inventory.Items){V.BindingMask=0;if(auto* Item=I->Owned(PC,V.Id)){Item->BindingMask=0;}}M->StateFor(PC)->Inventory.Bindings.Init(FGuid(),6);const auto Copy=M->StateFor(PC)->Inventory.Items;for(const auto& V:Copy){if(auto* Item=I->Owned(PC,V.Id)){I->Remove(PC,Item,true,TEXT("B75FixtureCleanup"));}}M->StateFor(PC)->Inventory.Bindings.Init(FGuid(),6);M->StateFor(PC)->Inventory.Quick.Init(FGuid(),3);M->StateFor(PC)->ItemFamilies.Entries.Reset();M->StateFor(PC)->MajorProtectionReadyAt=0;M->StateFor(PC)->NullBellReadyAt=0;M->StateFor(PC)->DirectDamageUntil=0;M->StateFor(PC)->GateReadyAt=0;}
        for(auto* PC:{A,B}){auto* O=M->StateFor(PC);auto* Body=CastChecked<ALDCharacter>(PC->GetPawn());auto* S=C->StateFor(Body);Body->GetCharacterMovement()->RemoveRootMotionSource(TEXT("LD_AuthorityDash"));Body->GetCharacterMovement()->SetMovementMode(MOVE_Walking);PC->Navigation->ResetLife();O->Build=LDDevelopment::TacticalStart(0,Catalog->Checksum);O->Build.Attributes.Init(16,6);O->Build.Ranks.Init(1,38);O->Build.Ranks[0]=0;O->Build.Slots.Init(ELDSkill::None,5);O->Build.Cooldowns.Init(0,38);O->ProtectedUntil=0;S->Tactical=LDTactical::FState();S->Tactical.MP=LDTactical::MaxMP(O->Build);S->Tactical.LastRegen=Now();S->Hp=LDDevelopment::MaxHP(O->Build);S->Action=0;S->CoreRecoveryUntil=0;S->ProtectionUntil=0;S->LastDamage=S->LastDealt=-1000;S->LastFootstep=Now();S->DaggerItem.Invalidate();S->WatchHits.Reset();S->SalveEnd=0;S->bAttackOrder=false;}
        // Deterministic geometry/resources are explicitly diagnostic, never an earned route.
        BA->SetActorLocation(FVector(-1800,-900,500));BB->SetActorLocation(FVector(-1600,-900,500));Place(BA,FVector(-1800,-900,0));Place(BB,FVector(-1600,-900,0));BA->SetFacing(0);BB->SetFacing(180);M->Vision->Tick(0);};
    auto Skill=[&](ELDSkill K,AActor* Target=nullptr,AActor* Second=nullptr,FVector Point=FVector(-1600,-900,0),FVector Point2=FVector(-1400,-1050,0)){OA->Build.Slots[0]=K;FLDTacticalRequest R;R.Token=++Token;R.BuildRevision=OA->Build.Revision;R.CatalogDigest=Catalog->Checksum;R.Direction=FVector::ForwardVector;R.Point=Point;R.SecondPoint=Point2;R.Observed=Target?FGuid::NewGuid():FGuid();R.SecondObserved=Second?FGuid::NewGuid():FGuid();const bool Accepted=C->RequestContent(BA,R,Target,Second,Why);Check(Accepted,FString(LDDevelopment::SkillName(K))+TEXT(" authoritative acceptance: ")+Why);return Accepted;};
    auto Grant=[&](ELDItemKind K){auto* Item=W->SpawnActor<ALDItem>();Item->Kind=K;Item->State=0;Check(I->Adopt(A,Item,TEXT("DiagnosticItemCreation")),TEXT("Labelled journalled fixture item: ")+LDInventory::Name(K,ELDSkill::None));return Item;};
    auto Bind=[&](ALDItem* Item){const auto* D=Gear->Find(Item->Kind);if(D&&D->Binding!=255){FLDInventory Out;Check(LDGear::Propose(OA->Inventory,OA->Build,LDInventory::Equip,Item->Identity,D->Binding,Out,Why),D->Name+TEXT(" real binding proposal"));OA->Inventory=Out;Item->BindingMask=Out.Find(Item->Identity)->BindingMask;}FString Error;if(D&&!D->Operation.IsEmpty()){Check(LDInventory::BindQuick(OA->Inventory,Item->Identity,0,Error),D->Name+TEXT(" bound quick reference"));}I->Changed(A);M->RefreshBuild(A);};
    auto Use=[&](ALDItem* Item,AActor* Target=nullptr,FVector Point=FVector(-1700,-1050,0)){FLDTacticalRequest R;R.ActionKind=1;R.Item=Item->Identity;R.ItemRevision=Item->Revision;R.InventoryRevision=OA->Inventory.Revision;R.Token=++Token;R.BuildRevision=OA->Build.Revision;R.Point=Point;R.Direction=FVector::ForwardVector;Sequence=0;const bool Good=C->RequestContent(BA,R,Target,nullptr,Why);Check(Good,LDInventory::Name(Item->Kind,ELDSkill::None)+TEXT(" authoritative active acceptance: ")+Why);return Good;};
    Check(OA->Build.Preset==3&&OB->Build.Preset==4&&OA->Inventory.Items.IsEmpty()&&OB->Inventory.Items.IsEmpty()&&OA->Gold==0&&OB->Gold==0,TEXT("Actual new-preset match starts with adopted skills and no ward/item/wallet grant"));
    Clear();
    // Paid Circle-I source verification uses two actual courier rewards, no money/item setter.
    auto Context=[&](uint8 Site){Sequence=0;OA->ProtectedUntil=SA->ProtectionUntil=0;W->TimeSeconds+=.4;M->Objectives->Queue(A,OA->Match,OA->Life,++Token,Site,FGuid());M->Objectives->Sample(W->GetTimeSeconds());return Token;};
    auto CourierHold=[&](uint32 T){for(int32 N=0;N<31;++N){W->TimeSeconds+=.1;M->Objectives->RefreshHold(A,OA->Life,T,++Sequence,false,W->GetTimeSeconds());M->Objectives->Sample(W->GetTimeSeconds());}};
    for(int32 Job=0;Job<2;++Job){W->TimeSeconds=FMath::Max(W->GetTimeSeconds()+.4,OA->WorkReadyAt+.01);Place(BA,LDRegion::Site(W,1)+FVector(50,0,0));Context(1);const auto* Destination=LDRegion::Definition(W)->Node(OA->ContractDestination);if(!Destination){Check(false,TEXT("Actual courier destination assigned"));break;}Place(BA,LDRegion::Site(W,Destination->Site)+FVector(50,0,0));const uint32 Collect=Context(Destination->Site);CourierHold(Collect);Place(BA,LDRegion::Site(W,1)+FVector(50,0,0));const uint32 Deliver=Context(1);CourierHold(Deliver);}
    Check(OA->Gold==50&&OA->PaidJobs==2,TEXT("Two normal courier holds earn50gold before Circle-I purchase"));
    A->OpenVendor();const auto Quote=A->VendorQuote;Check(Quote.Session.IsValid()&&Quote.Offers.Num()==20&&Quote.Offers.ContainsByPredicate([](const auto& V){return V.Skill==ELDSkill::EmberLine&&V.Price==40;})&&Quote.Offers.ContainsByPredicate([](const auto& V){return V.Skill==ELDSkill::Grapple&&V.Price==40;}),TEXT("Actual Guild quote exposes all16 Circle-I plus four legacy books at40gold"));
    A->BuyVendorBook(ELDSkill::EmberLine,Quote.CatalogRevision,40);M->Vendor->Sample(W->GetTimeSeconds());Check(OA->Gold==10&&OA->Inventory.Items.ContainsByPredicate([](const auto& V){return V.Kind==ELDItemKind::Book&&V.Skill==ELDSkill::EmberLine;}),TEXT("Actual forty-gold Ember Line purchase burns earnings and creates a physical book"));A->CloseVendor();if(A->InventoryUI){A->InventoryUI->Close();}
    OA->Build.Ranks[24]=0;const auto Books=OA->Inventory.Items;for(const auto& Book:Books){if(Book.Kind!=ELDItemKind::Book){continue;}Sequence=0;const uint32 Read=++Token;I->Queue(A,OA->Match,OA->Life,Read,OA->Inventory.Revision,LDInventory::Read,Book.Id,Book.Revision,0,FVector::ZeroVector);I->Sample(W->GetTimeSeconds());M->Development->Sample(W->GetTimeSeconds());for(int32 N=0;N<31;++N){W->TimeSeconds+=.1;M->Development->RefreshHold(A,OA->Life,Read,++Sequence,false);M->Development->Sample(W->GetTimeSeconds());}Check(OA->Build.Ranks[24]==1&&!OA->Inventory.Find(Book.Id),TEXT("Paid new Circle-I book reads through actual quiet held transaction"));}
    Check(Catalog->Abilities.Num()==28&&Gear->Items.Num()==40,TEXT("Native complete catalogs 28 spells /40 items"));
    for(uint8 N=3;N<9;++N){const auto Kit=LDDevelopment::TacticalStart(N,Catalog->Checksum);int32 Budget=0;for(int32 V:Kit.Attributes){Budget+=V;}Check(Budget==53&&LDDevelopment::Valid(Kit)&&Kit.Slots[0]!=ELDSkill::None&&Kit.Slots[1]!=ELDSkill::None,TEXT("53-point two-skill starter: ")+FString(LDDevelopment::PresetName(Kit)));}
    for(const auto& D:Catalog->Abilities)
    {
        Clear();const auto K=D.Skill;AActor* Target=D.Target==ELDTacticalTarget::Unit||K==ELDSkill::Grapple?BB:nullptr;
        if(K==ELDSkill::PurgeTouch){LDTactical::Apply(SA->Tactical,LDTactical::EStatus::Silence,Now()+5000,SA->Identity);SA->Tactical.Statuses.Reset();LDTactical::Apply(SA->Tactical,LDTactical::EStatus::Mute,Now()+5000,SA->Identity);}
        if(K==ELDSkill::RemedyBind){OA->MajorProtectionReadyAt=0;}
        if(K==ELDSkill::GrandExchange){Target=BA;}
        if(!Skill(K,Target,K==ELDSkill::GrandExchange?BB:nullptr,K==ELDSkill::GuardSwap||K==ELDSkill::GateWeave?FVector(-1700,-1050,0):FVector(-1600,-900,0))){continue;}
        if(K==ELDSkill::TemporalLock){Check(Fields(ELDTacticalField::Telegraph).Num()==1,TEXT("Temporal Lock warning exists during1200ms windup"));}
        const FVector OldA=BA->GetActorLocation(),OldB=BB->GetActorLocation();Advance(D.Windup/1000.+.001);
        Check(OA->Build.Cooldowns[uint8(K)]>W->GetTimeSeconds()||K==ELDSkill::GuardSwap,D.Id+TEXT(" release commits actual cooldown"));
        bool Effect=false;
        switch(K)
        {
        case ELDSkill::AnchorBash:Effect=SB->Hp<LDDevelopment::MaxHP(OB->Build)&&SB->Tactical.Has(LDTactical::EStatus::Stun,Now());break;
        case ELDSkill::BraceWall:Effect=SA->Tactical.Directional&&SA->Tactical.Barrier>140;break;
        case ELDSkill::VeilStep:Effect=C->Concealed(BA);break;
        case ELDSkill::Hamstring:Effect=SB->Hp<LDDevelopment::MaxHP(OB->Build)&&SB->Tactical.Has(LDTactical::EStatus::Slow,Now())&&OB->SlowPercent==40;break;
        case ELDSkill::ArcBolt:Advance(.3);Effect=SB->Hp<LDDevelopment::MaxHP(OB->Build)-70;break;
        case ELDSkill::Counterpulse:Effect=SB->Tactical.Has(LDTactical::EStatus::Silence,Now());break;
        case ELDSkill::FarwatchWard:Effect=Fields(ELDTacticalField::Ward).Num()==1;break;
        case ELDSkill::WireSnare:Advance(1.01);Effect=SB->Tactical.Has(LDTactical::EStatus::Root,Now())&&Fields(ELDTacticalField::Trap).IsEmpty();break;
        case ELDSkill::LedgerMark:Effect=SB->Tactical.Has(LDTactical::EStatus::Mark,Now());M->Vision->Tick(0);Advance(.1);Check(!Fields(ELDTacticalField::Trail).IsEmpty(),TEXT("Ledger Mark emits a source-limited stale clue in sight"));LDTactical::Dispel(SB->Tactical,false);break;
        case ELDSkill::SuretyShell:Effect=SA->Tactical.Barrier>140;break;
        case ELDSkill::FalseTrail:Effect=Fields(ELDTacticalField::Decoy).Num()==1;break;
        case ELDSkill::SmokeVeil:Effect=Fields(ELDTacticalField::Smoke).Num()==1;break;
        case ELDSkill::PurgeTouch:Effect=!SA->Tactical.Has(LDTactical::EStatus::Mute,Now());break;
        case ELDSkill::SeersFlare:Effect=Fields(ELDTacticalField::Detector).Num()==1;break;
        case ELDSkill::EmberLine:{const double HP=SB->Hp;Advance(3.01);Effect=SB->Hp<HP-95&&Fields(ELDTacticalField::Ember).IsEmpty();break;}
        case ELDSkill::Grapple:Effect=FVector::DistSquared2D(BA->GetActorLocation(),OldA)>1||FVector::DistSquared2D(BB->GetActorLocation(),OldB)>1;break;
        case ELDSkill::NullSeal:Effect=SB->Tactical.Has(LDTactical::EStatus::Mute,Now());break;
        case ELDSkill::GroundingField:Advance(.1);Effect=SB->Tactical.GroundedUntil>Now();break;
        case ELDSkill::ForceWave:Effect=FVector::DistSquared2D(BB->GetActorLocation(),OldB)>100;break;
        case ELDSkill::GuardSwap:{auto F=Fields(ELDTacticalField::Marker);Effect=F.Num()==1;Advance(1.01);if(Effect&&Skill(K,F[0])){Advance(.61);Effect=Fields(ELDTacticalField::Marker).IsEmpty()&&FVector::DistSquared2D(BA->GetActorLocation(),OldA)>100&&OA->Build.Cooldowns[uint8(K)]>W->GetTimeSeconds();}break;}
        case ELDSkill::Slipstream:Effect=BA->GetCharacterMovement()->HasRootMotionSources()&&!BA->GetCharacterMovement()->bCanWalkOffLedges;break;
        case ELDSkill::RemedyBind:Effect=SA->Tactical.GuardUntil>Now();break;
        case ELDSkill::BreakSigil:Effect=SB->Tactical.Has(LDTactical::EStatus::Break,Now());break;
        case ELDSkill::ArcanePrison:{const auto F=Fields(ELDTacticalField::Prison);Effect=F.Num()==1&&F[0]->GetComponents().Num()>=25;break;}
        case ELDSkill::TemporalLock:Effect=SB->Tactical.Has(LDTactical::EStatus::Stun,Now())&&Fields(ELDTacticalField::Telegraph).IsEmpty();break;
        case ELDSkill::SovereignWard:Effect=Fields(ELDTacticalField::Dome).Num()==1&&OA->MajorProtectionReadyAt>Now();break;
        case ELDSkill::GrandExchange:Effect=FVector::DistSquared2D(BA->GetActorLocation(),OldB)<1&&FVector::DistSquared2D(BB->GetActorLocation(),OldA)<1;break;
        case ELDSkill::GateWeave:{const auto F=Fields(ELDTacticalField::Gate);Effect=F.Num()==2&&F[0]->Partner.IsValid()&&F[1]->Partner.IsValid();break;}
        default:break;
        }
        Check(Effect,D.Id+TEXT(" real effect outcome"));
    }
    // Atomic packet defenses, endpoint staging, fresh held travel and owner-life cleanup.
    Clear();SA->Tactical.BlockUntil=SB->Tactical.BlockUntil=Now()+10000;const FVector A0=BA->GetActorLocation(),B0=BB->GetActorLocation();Skill(ELDSkill::GrandExchange,BB,BA);Advance(1.11);Check(!SB->Tactical.BlockUntil&&SA->Tactical.BlockUntil>Now()&&BA->GetActorLocation().Equals(A0)&&BB->GetActorLocation().Equals(B0),TEXT("First selected block consumed; entire swap cancelled; other charge retained"));
    Clear();LDTactical::Apply(SB->Tactical,LDTactical::EStatus::Root,Now()+5000,SA->Identity);OA->Build.Slots[0]=ELDSkill::GrandExchange;const double BeforeMP=SA->Tactical.MP;FLDTacticalRequest Invalid;Invalid.Token=++Token;Invalid.BuildRevision=OA->Build.Revision;Check(!C->RequestContent(BA,Invalid,BA,BB,Why)&&SA->Tactical.MP==BeforeMP,TEXT("Rooted participant invalidates both swap endpoints before costs"));
    Clear();Skill(ELDSkill::GateWeave,nullptr,nullptr,FVector(-1750,-1050,0),FVector(-1400,-1050,0));Advance(1.41);auto Gates=Fields(ELDTacticalField::Gate);if(Gates.Num()==2){Place(BA,Gates[0]->GetActorLocation());FLDTacticalRequest Travel;Travel.ActionKind=2;Travel.Token=++Token;Travel.BuildRevision=OA->Build.Revision;const FVector Arrival=Gates[1]->GetActorLocation();Check(C->RequestContent(BA,Travel,Gates[0],nullptr,Why),TEXT("Gate admits fresh ordinary held traversal"));Sequence=0;Advance(1.01,true);Check(FVector::DistSquared2D(BA->GetActorLocation(),Arrival)<1&&OA->GateReadyAt>Now(),TEXT("One-second gate completion moves once and starts3s arrival lock"));C->DestroyField(Gates[0]);Check(Fields(ELDTacticalField::Gate).IsEmpty(),TEXT("Destroying one gate closes both emitters"));}
    Clear();OA->Build.Slots[0]=ELDSkill::GateWeave;Invalid.Token=++Token;Invalid.BuildRevision=OA->Build.Revision;Invalid.Point=FVector(-1750,-1050,0);Invalid.SecondPoint=FVector(25000,0,0);Check(!C->RequestContent(BA,Invalid,nullptr,nullptr,Why)&&Fields(ELDTacticalField::Gate).IsEmpty(),TEXT("Invalid gate pair creates neither half"));
    // Every item is a real carried actor; all active operations run through RequestContent.
    for(const auto& D:Gear->Items)
    {
        Clear();auto* Item=Grant(D.Kind);Bind(Item);const auto Stats=LDGear::Derive(OA->Inventory);bool Effect=false;
        if(D.Operation.IsEmpty())
        {
            switch(D.Kind)
            {
            case ELDItemKind::IronHalberd:Effect=Stats.WeaponBase==50&&Stats.Interval==1450&&Stats.Reach==280;break;
            case ELDItemKind::Watchblade:{FLDCombatHit H{BA,BB,42};H.Basic=true;C->WeaponContact(H,BB,42,Now());C->WeaponContact(H,BB,42,Now());C->WeaponContact(H,BB,42,Now());Effect=SB->Tactical.Has(LDTactical::EStatus::Mark,Now());break;}
            case ELDItemKind::RunicStaff:Effect=Stats.WeaponBase==30&&Stats.SpellPercent==5&&Stats.Primary[3]==2;break;
            case ELDItemKind::SurveyorBow:{Effect=Stats.Ranged&&Stats.Reach==600&&Stats.SightBonus==50;Place(BB,FVector(-1050,-900,0));M->Vision->Tick(0);Check(M->Vision->CanObserve(A,BB),TEXT("Bound bow extends geometric body observation beyond720cm"));Place(BB,FVector(-1300,-900,0));SA->BasicReady=0;Check(C->RequestBody(BA,0,FVector::ForwardVector,Why),TEXT("Bow basic begins real ranged windup"));Advance(.31);int32 Projectiles=0;for(TActorIterator<ALDArcProjectile> It(W);It;++It){Projectiles+=It->Basic;}Check(Projectiles==1,TEXT("Bow launches one physical non-homing snapshot projectile"));Advance(.5);Effect&=SB->Hp<LDDevelopment::MaxHP(OB->Build)-40;break;}
            case ELDItemKind::OathMace:{LDTactical::Barrier(SB->Tactical,200,1000,Now()+3000,Now());FLDCombatHit H{BA,BB,50};H.Basic=true;C->WeaponContact(H,BB,50,Now());Effect=SB->Tactical.Barrier==175;break;}
            case ELDItemKind::BastionPlate:Effect=Stats.Armor==18&&Stats.Movement==-10&&Stats.Primary[2]==2;break;
            case ELDItemKind::ShadeMantle:Effect=Stats.Armor==4&&Stats.FootstepReduction==20;break;
            case ELDItemKind::ArchivistRobe:Effect=Stats.Armor==3&&Stats.MPRegen==50;break;
            case ELDItemKind::MerchantHarness:Effect=Stats.Armor==4&&Stats.Capacity==10;break;
            case ELDItemKind::PathfinderBoots:Effect=Stats.RoadMovement==12;break;
            case ELDItemKind::SilentSoles:Effect=Stats.FootstepReduction==20&&Stats.Primary[1]==1;break;
            case ELDItemKind::Cliffwalkers:{const auto Links=Fields(ELDTacticalField::Scramble);Effect=Stats.Scramble&&Links.Num()==2;if(Effect){Place(BA,Links[0]->GetActorLocation());FLDTacticalRequest Climb;Climb.ActionKind=3;Climb.Token=++Token;Climb.BuildRevision=OA->Build.Revision;Sequence=0;const FVector Destination=Links[0]->Direction;Check(C->RequestContent(BA,Climb,Links[0],nullptr,Why),TEXT("Cliffwalkers admits the actual authored held link"));Advance(1.01,true);Effect=FVector::DistSquared2D(BA->GetActorLocation(),Destination)<1;}break;}
            case ELDItemKind::ScholarsSigil:Effect=Stats.ControlPotency==1;break;
            case ELDItemKind::BrokersSeal:Effect=Stats.FeeDiscount==1;break;
            case ELDItemKind::WardingEye:Effect=Stats.Detector==250&&Stats.Primary[4]==1;break;
            case ELDItemKind::IronOath:{Place(BB,FVector(-1710,-900,0));SB->BasicReady=0;const double Before=SA->Hp;C->RequestBody(BB,0,-FVector::ForwardVector,Why);Advance(.31);const double GuardLoss=Before-SA->Hp;Effect=Stats.DirectionalGuard==10&&GuardLoss>47&&GuardLoss<50;C->ApplyStatus(BA,LDTactical::EStatus::Break,Now()+5000,SB->Identity);Advance(1.21);const double Broken=SA->Hp;C->RequestBody(BB,0,-FVector::ForwardVector,Why);Advance(.31);Check(Broken-SA->Hp>GuardLoss+4&&LDDevelopment::Effective(OA->Build)[0]==17,TEXT("Break removes directional guard but retains Iron Oath STR"));break;}
            case ELDItemKind::WeightedSatchel:Effect=Stats.Capacity==10;break;
            case ELDItemKind::SimpleStaff:Effect=Stats.WeaponBase==32&&Stats.Reach==220;break;
            case ELDItemKind::ConduitStaff:Effect=Stats.WeaponBase==34&&Stats.CastRange==50&&Stats.SpellPercent==5;break;
            default:break;
            }
        }
        else
        {
            SA->Hp-=200;SA->Tactical.MP=100;LDTactical::Apply(SA->Tactical,LDTactical::EStatus::Silence,Now()+5000,SB->Identity);const double HP=SA->Hp;const auto Id=Item->Identity;const FVector Old=BA->GetActorLocation();AActor* Target=D.Operation==TEXT("targeted_mute")||D.Operation==TEXT("grapple")?BB:nullptr;FVector Point=D.Operation==TEXT("recall")?LDRegion::Site(W,1):FVector(-1700,-1050,0);
            if(!Use(Item,Target,Point)){continue;}const bool Channel=D.Operation==TEXT("restore_mp")||D.Operation==TEXT("recall");Advance(Channel?D.Param(TEXT("channel_ms"))/1000.+.01:.251,Channel);
            const auto& Op=D.Operation;
            if(Op==TEXT("basic_dispel_self")||Op==TEXT("basic_dispel")){Effect=!SA->Tactical.Has(LDTactical::EStatus::Silence,Now());}
            if(Op==TEXT("targeted_mute")){Effect=SB->Tactical.Has(LDTactical::EStatus::Mute,Now());}
            if(Op==TEXT("arm_next_hit")){FLDCombatHit H{BA,BB,38};H.Basic=true;C->WeaponContact(H,BB,38,Now());Effect=!SA->DaggerItem.IsValid()&&SB->Tactical.Has(LDTactical::EStatus::Silence,Now());}
            if(Op==TEXT("reflect_next_projectile")){auto* P=W->SpawnActor<ALDArcProjectile>(BB->GetActorLocation(),FRotator::ZeroRotator);P->Source=BB;P->CasterIdentity=SB->Identity;P->Victim=BA;P->VictimLife=SA->Identity;P->Endpoint=BA->GetActorLocation();P->Amount=77;Effect=C->ProjectileIntercept(P,BB->GetActorLocation(),BA->GetActorLocation(),Now())&&P->Reflected&&P->Source.Get()==BA&&P->Victim.Get()==BB&&P->Amount==77&&!SA->Tactical.ReflectUntil;Check(!C->ProjectileIntercept(P,BA->GetActorLocation(),BB->GetActorLocation(),Now()),TEXT("Reflected packet cannot reflect again"));P->Destroy();}
            if(Op==TEXT("spell_block")){Effect=LDTactical::Block(SA->Tactical,Now())&&!LDTactical::Block(SA->Tactical,Now());}
            if(Op==TEXT("control_guard")){Effect=LDTactical::GuardControl(SA->Tactical,LDTactical::EStatus::Stun,Now())&&!LDTactical::GuardControl(SA->Tactical,LDTactical::EStatus::Hex,Now());}
            if(Op==TEXT("anchor")){Effect=!C->Push(BA,FVector::ForwardVector,100,W->GetTimeSeconds())&&OA->SlowPercent==30;}
            if(Op==TEXT("barrier")){Effect=SA->Tactical.Barrier==D.Param(TEXT("amount"));}
            if(Op==TEXT("heal_over_time")){Advance(8.01);Effect=SA->Hp>=HP+160&&SA->Hp<HP+170&&SA->SalveTicks==8;}
            if(Op==TEXT("restore_mp")){Effect=SA->Tactical.MP>=180&&SA->Tactical.MP<185&&SA->Action==0;}
            if(Op==TEXT("recall")||Op==TEXT("blink")){Effect=FVector::DistSquared2D(Old,BA->GetActorLocation())>100;}
            if(Op==TEXT("grapple")||Op==TEXT("terrain_grapple")){Effect=FVector::DistSquared2D(Old,BA->GetActorLocation())>1;}
            if(Op==TEXT("smoke")){Effect=Fields(ELDTacticalField::Smoke).Num()==1;}
            if(Op==TEXT("detector")){Effect=Fields(ELDTacticalField::Detector).Num()==1;}
            if(Op==TEXT("spawn_ward")||Op==TEXT("spawn_trap")){const auto F=Fields(Op==TEXT("spawn_ward")?ELDTacticalField::Ward:ELDTacticalField::Trap);Effect=F.Num()==1&&F[0]->ChargeIdentity==Id;Check(E->Audit(Why),TEXT("Consumed charge transfers into one active source: ")+Why);if(Effect){C->DestroyField(F[0]);Check(E->Audit(Why),TEXT("Source expiration retires charge exactly once: ")+Why);}}
            Check(D.Consumable?!I->Owned(A,Id):I->Owned(A,Id)!=nullptr,D.Name+TEXT(" physical consumption/retention"));
            Check(OA->ItemFamilies.FindRef(D.CooldownKey())>Now()||Op==TEXT("heal_over_time"),D.Name+TEXT(" contender cooldown survives instance consumption"));
            if(D.Family==TEXT("major_protection")){Check(OA->MajorProtectionReadyAt>Now(),D.Name+TEXT(" starts8s major activation lock"));}
        }
        Check(Effect,D.Id+TEXT(" real item effect outcome"));Check(ULDAcquisitionCatalog::Load()->Find(D.Kind)!=nullptr,D.Id+TEXT(" checked physical acquisition definition"));
    }
    Clear();int32 Emitters=0;for(int32 N=0;N<80;++N){if(!C->CreateField(BA,ELDTacticalField::Footstep,BA->GetActorLocation(),Now(),60000,1,0)){break;}++Emitters;}Check(Emitters>=70,TEXT("Labelled emitter-cap fixture fills bounded world capacity"));const double CapacityMP=SA->Tactical.MP;Skill(ELDSkill::FarwatchWard);Advance(.66);Check(SA->Tactical.MP>=CapacityMP&&OA->Build.Cooldowns[16]==0&&Fields(ELDTacticalField::Ward).IsEmpty(),TEXT("Native allocation-cap refusal releases neither mana nor cooldown"));
    Clear();auto* Rift=Grant(ELDItemKind::RiftStaff);Bind(Rift);Use(Rift);Advance(.26);const int64 RiftEnd=Rift->ReadyAt;auto* SecondBlink=Grant(ELDItemKind::BlinkCharm);Bind(SecondBlink);FLDTacticalRequest Reuse;Reuse.ActionKind=1;Reuse.Item=SecondBlink->Identity;Reuse.ItemRevision=SecondBlink->Revision;Reuse.InventoryRevision=OA->Inventory.Revision;Reuse.BuildRevision=OA->Build.Revision;Reuse.Token=++Token;Reuse.Point=FVector(-1800,-1050,0);Check(!C->RequestContent(BA,Reuse,nullptr,nullptr,Why)&&SecondBlink->ReadyAt==0&&OA->ItemFamilies.FindRef(TEXT("blink"))==RiftEnd,TEXT("Fresh Blink Charm cannot bypass Rift's full family endpoint"));
    FLDInventory Unbound;Check(LDGear::Propose(OA->Inventory,OA->Build,LDInventory::Unequip,Rift->Identity,0,Unbound,Why),TEXT("Cooldown transfer first unbinds its physical weapon"));OA->Inventory=Unbound;Rift->BindingMask=0;I->Remove(A,Rift,false,TEXT("B75FixtureTransferDrop"));Check(I->Adopt(B,Rift)&&Rift->ReadyAt==RiftEnd&&OB->ItemFamilies.FindRef(TEXT("blink"))==RiftEnd,TEXT("Native adoption transfers full instance endpoint into recipient family"));
    Clear();auto* Relic=Grant(ELDItemKind::TowerRelic);Bind(Relic);Use(Relic);Advance(.26);const int64 RelicEnd=Relic->ReadyAt,MajorEnd=OA->MajorProtectionReadyAt;auto* Retained=Grant(ELDItemKind::SimpleStaff);Bind(Retained);I->EndLife(A);Check(Relic->State==0&&!Relic->Holder.IsValid()&&!Relic->BindingMask&&!OA->Inventory.Find(Relic->Identity)&&Retained->State==1&&Retained->BindingMask,TEXT("Native death policy drops equipped Relic once and retains ordinary bound staff"));Check(Relic->Quality==0&&Relic->ReadyAt==RelicEnd&&OA->MajorProtectionReadyAt==MajorEnd,TEXT("Always-drop Relic remains Standard and retains instance/owner locks"));
    Clear();Skill(ELDSkill::GuardSwap,nullptr,nullptr,FVector(-1700,-1050,0));const double PlaceMP=SA->Tactical.MP;Advance(.61);auto Marker=Fields(ELDTacticalField::Marker);Check(Marker.Num()==1&&SA->Tactical.MP>PlaceMP-10.1&&SA->Tactical.MP<PlaceMP-8&&OA->Build.Cooldowns[29]==0&&SA->CoreRecoveryUntil>W->GetTimeSeconds(),TEXT("Marker placement costs10MP and1s recovery without full cooldown"));if(Marker.Num()==1){Marker[0]->Hp=0;Marker[0]->EnemyDestroyed=true;Advance(.05);Check(OA->Build.Cooldowns[29]>W->GetTimeSeconds()+30,TEXT("Enemy marker destruction starts full31s cooldown"));}
    Clear();Skill(ELDSkill::GateWeave,nullptr,nullptr,FVector(-1750,-1050,0),FVector(-1400,-1050,0));Advance(1.41);Gates=Fields(ELDTacticalField::Gate);if(Gates.Num()==2){Place(BA,Gates[0]->GetActorLocation());Grant(ELDItemKind::AncientKeyA);FLDTacticalRequest Travel;Travel.ActionKind=2;Travel.Token=++Token;Travel.BuildRevision=OA->Build.Revision;Check(!C->RequestContent(BA,Travel,Gates[0],nullptr,Why),TEXT("A physical ancient key prevents gate travel"));C->EndContentLife(BA);Check(Fields(ELDTacticalField::Gate).IsEmpty(),TEXT("Owner-life loss closes both linked emitters"));}
    Clear();Skill(ELDSkill::BraceWall);Advance(.26);Place(BB,FVector(-1710,-900,0));const double FrontHP=SA->Hp,FrontPool=SA->Tactical.Barrier;SB->BasicReady=0;Check(C->RequestBody(BB,0,-FVector::ForwardVector,Why),TEXT("Real front basic attack accepted"));Advance(.31);Check(SA->Hp>=FrontHP&&SA->Tactical.Barrier<FrontPool&&OA->DirectDamageUntil>Now(),TEXT("Front contact damages barrier and starts Blink lock without HP loss"));const double RearPool=SA->Tactical.Barrier;Place(BB,FVector(-1890,-900,0));Advance(.91);Check(C->RequestBody(BB,0,FVector::ForwardVector,Why),TEXT("Real rear basic attack accepted"));Advance(.31);Check(SA->Hp<FrontHP&&FMath::IsNearlyEqual(SA->Tactical.Barrier,RearPool),TEXT("Rear hit bypasses Brace's captured120-degree pool"));
    Clear();Place(BB,FVector(-1710,-900,0));FLDTacticalRequest Attack;Attack.ActionKind=4;Attack.Token=++Token;Attack.BuildRevision=OA->Build.Revision;SA->BasicReady=0;Check(C->RequestContent(BA,Attack,BB,nullptr,Why),TEXT("Observed target starts maintained basic order"));Advance(.31);const double FirstHit=SB->Hp;Advance(1.21);Check(SB->Hp<FirstHit-20&&SA->bAttackOrder,TEXT("Maintained attack releases a second interval-legal contact"));A->ServerCancelTactical(OA->Match,OA->Life,Token);const double StopHP=SB->Hp;Advance(1.3);Check(!SA->bAttackOrder&&SB->Hp>=StopHP,TEXT("Stop cancels maintained violence before a further hit"));
    Clear();auto* Mana=Grant(ELDItemKind::ManaDraught);Bind(Mana);SA->Tactical.MP=0;Use(Mana);SA->HadPositiveChannelDamage=true;Advance(.1,true);Check(SA->Action==0&&SA->Tactical.MP<1&&!I->Owned(A,Mana->Identity)&&OA->ItemFamilies.FindRef(TEXT("mana"))>Now(),TEXT("Damaged Mana channel cancels with no restore; consumed charge and family cost remain"));
    Clear();auto* Blink=Grant(ELDItemKind::BlinkCharm);Bind(Blink);OA->DirectDamageUntil=Now()+3000;FLDTacticalRequest Blocked;Blocked.ActionKind=1;Blocked.Item=Blink->Identity;Blocked.ItemRevision=Blink->Revision;Blocked.InventoryRevision=OA->Inventory.Revision;Blocked.BuildRevision=OA->Build.Revision;Blocked.Token=++Token;Blocked.Point=FVector(-1700,-1050,0);Check(!C->RequestContent(BA,Blocked,nullptr,nullptr,Why)&&Blink->ReadyAt==0,TEXT("Positive direct-damage lock refuses Blink before spending"));
    Clear();Skill(ELDSkill::SovereignWard,nullptr,nullptr,FVector(-1800,-900,0));Advance(1.01);auto Dome=Fields(ELDTacticalField::Dome);if(Dome.Num()==1){auto* P=W->SpawnActor<ALDArcProjectile>();P->Source=BB;P->Victim=BA;P->CasterIdentity=SB->Identity;P->VictimLife=SA->Identity;Check(C->ProjectileIntercept(P,FVector(-2300,-900,92),FVector(-1700,-900,92),Now())&&P->IsActorBeingDestroyed(),TEXT("Hostile projectile intercepted at dome boundary"));C->EndContentLife(BA);Check(Fields(ELDTacticalField::Dome).IsEmpty(),TEXT("Owner life cleanup removes persistent emitter"));}
    Clear();Check(E->Audit(Why)&&E->AuditFailures==0,TEXT("All fixture item sources and sinks reconcile without audit failures: ")+Why);
    auto Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("failures"),Failures);Report->SetArrayField(TEXT("checks"),Checks);Report->SetStringField(TEXT("scope"),TEXT("B75 native authority effect fixtures, all28 spells/40 items. Actual new-preset empty start and two-job paid Circle-I acquisition/read; effect cases explicitly use diagnostic items, attributes, skills, resources, clock/placement. Real authority requests/releases/held channels, geometry, defenses and charged-source ledger. Not an earned acquisition, human, remote or cooked route."));FString Json,Path;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FParse::Value(FCommandLine::Get(),TEXT("LDProbeReport="),Path);FFileHelper::SaveStringToFile(Json,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);FFileHelper::SaveStringToFile(E->Accounting().ExportJson(TEXT("B75Effects")),*(FPaths::GetPath(Path)/TEXT("Ledger.json")));FPlatformMisc::RequestExitWithStatus(false,Failures?1:0);
#endif
}
