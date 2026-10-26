#include "Tests/LDWorldProbe.h"
#include "Inventory/LDTradeDirector.h"
#include "Inventory/LDVendorDirector.h"
#include "World/LDWorldDirector.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Combat/LDCombatDirector.h"
#include "Economy/LDEconomyDirector.h"
#include "Economy/LDCommerceDirector.h"
#include "Economy/LDBankDirector.h"
#include "Economy/LDProductionDirector.h"
#include "Inventory/LDInventoryDirector.h"
#include "Inventory/LDAcquisitionDirector.h"
#include "Inventory/LDAcquisitionCatalog.h"
#include "Development/LDDevelopmentDirector.h"
#include "Development/LDWorldChecks.h"
#include "Objectives/LDObjectiveDirector.h"
#include "Region/LDRegionDefinition.h"
#include "Navigation/LDNavigationComponent.h"
#include "Vision/LDVisionDirector.h"
#include "Vision/LDWorldViewRelay.h"
#include "Core/LDGroundQueries.h"
#include "Character/LDCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
namespace LDWorldDemo {ALDCharacter* Body(ALDPlayerController*);FGuid Observe(ALDGameMode*,ALDPlayerController*,AActor*);FGuid Find(ALDGameMode*,ALDPlayerController*,ELDItemKind);}

void ALDWorldProbe::Boundary()
{
#if !UE_BUILD_SHIPPING
    FString Case,Why;FParse::Value(FCommandLine::Get(),TEXT("LDWorldCase="),Case);B=Cast<ALDPlayerController>(UGameplayStatics::CreatePlayer(this,1,true));if(!B){Check(false,TEXT("Boundary second owner"));Finish();return;}Mode->SetLobbyChoice(A,1,true,Why);Mode->SetLobbyChoice(B,3,true,Why);if(!Mode->StartDevelopmentMatch(A,Why)||!Mode->WorldRuntime){Check(false,Why);Finish();return;}auto* W=GetWorld();auto* C=Mode->Combat.Get();auto* O=Mode->Objectives.Get();auto* I=Mode->Inventory.Get();auto* E=Mode->Economy.Get();auto* World=Mode->WorldRuntime.Get();auto* G=Mode->GetGameState<ALDGameState>();C->SetActorTickEnabled(false);Check(E->AllowDiagnosticItems(),TEXT("Boundary lane explicitly permits labelled fixture sources; normal route lane forbids them"));
    auto Advance=[&](double At){W->TimeSeconds=At;C->AdvanceAuthority(At);Mode->Vision->Tick(0);};
    auto Place=[&](ALDPlayerController* PC,FVector Point){O->Interrupt(PC,W->GetTimeSeconds(),true);PC->Navigation->Stop();LDWorldDemo::Body(PC)->GetCharacterMovement()->StopMovementImmediately();LDWorldDemo::Body(PC)->SetActorLocation(Point+FVector(0,0,92));};
    auto Start=[&](ALDPlayerController* PC,uint8 Target,FGuid Seen=FGuid()){const auto* S=Mode->StateFor(PC);O->Queue(PC,S->Match,S->Life,++Token,Target,Seen);O->Sample(W->GetTimeSeconds());return Token;};
    auto Held=[&](ALDPlayerController* PC,uint32 Id,double Through){while(W->GetTimeSeconds()+1.e-7<Through&&!Mode->IsFinished()){const double Next=FMath::Min(Through,W->GetTimeSeconds()+.1);W->TimeSeconds=Next;O->RefreshHold(PC,Mode->StateFor(PC)->Life,Id,++Sequence,false,Next);C->AdvanceAuthority(Next);}};
    auto Grant=[&](ALDPlayerController* PC,ELDItemKind Kind){auto* Item=W->SpawnActor<ALDItem>();Item->Kind=Kind;Check(I->Adopt(PC,Item,TEXT("DiagnosticItemCreation")),TEXT("Labelled boundary physical source"));return Item;};
    Advance(Mode->StartedAt()+7);Place(A,FVector(0,-40,0));Place(B,FVector(0,170,0));Advance(W->GetTimeSeconds()+.1);
    if(Case==TEXT("NPC"))
    {
        Check(Mode->IsV08Profile(),TEXT("Elemental camp policy belongs to explicit schema8 profile"));
        ALDWorldPawn* P=nullptr;for(const auto& V:World->Pawns){if(V.IsValid()&&V->Kind==0&&V->Index==0){P=V.Get();break;}}
        if(!P){Check(false,TEXT("Actual finite camp actor"));Finish();return;}
        auto* S=C->StateFor(P);Check(S->bHasNPCBuild&&S->NPCBuild.RulesVersion==8&&LDDevelopment::Valid(S->NPCBuild),TEXT("Camp has valid declared six-slot Arcanist build"));
        Place(A,P->Home+FVector(400,0,-92));Place(B,FVector(1800,1000,0));P->GetCharacterMovement()->DisableMovement();
        const auto Receipts=E->Accounting().ReceiptCount();TArray<FLDCombatHit> Hits;World->Prepare(W->GetTimeSeconds(),Hits);
        Check(!S->Action,TEXT("Dry visible target does not trigger elemental response"));
        C->ApplyStatus(LDWorldDemo::Body(A),LDTactical::EStatus::Wet,LDTactical::Milliseconds(W->GetTimeSeconds()+5),FGuid::NewGuid());
        const double MP=S->Tactical.MP;
        const int64 At=LDTactical::Milliseconds(W->GetTimeSeconds());
        Check(C->StageEnvironment(LDWorldDemo::Body(A),(P->GetActorLocation()+LDWorldDemo::Body(A)->GetActorLocation())*.5,LDEnvironment::EInput::Smoke,true,0,100,At,500,16),TEXT("Labelled local Smoke fixture reserves bounded cells"));C->CommitRiver(At);
        World->Prepare(W->GetTimeSeconds(),Hits);Check(!S->Action&&S->Tactical.MP==MP,TEXT("Visible field mask occludes Wet target before AI action or cost"));
        Advance(W->GetTimeSeconds()+.6);World->Prepare(W->GetTimeSeconds(),Hits);
        Check(S->Action==9&&S->Skill==ELDSkill::Frostline,TEXT("Live camp AI chooses ordinary Frostline for visible Wet body"));
        Advance(W->GetTimeSeconds()+.5);
        Check(S->Tactical.MP<MP-25&&S->NPCBuild.Cooldowns[uint8(ELDSkill::Frostline)]>W->GetTimeSeconds(),TEXT("AI release pays real MP and cooldown"));
        Check(Mode->StateFor(A)->WetFrostReadyAt>LDTactical::Milliseconds(W->GetTimeSeconds()),TEXT("AI direct Frost commits real victim-wide reaction endpoint"));
        Advance(W->GetTimeSeconds()+1);World->Prepare(W->GetTimeSeconds(),Hits);Check(!S->Action,TEXT("Wet does not bypass AI cooldown"));
        Advance(W->GetTimeSeconds()+15);Check(S->Hp==P->Maximum&&S->Hp==260,TEXT("Spell kit cannot introduce generic600HP regeneration into finite260HP camp"));
        C->ApplyStatus(P,LDTactical::EStatus::Silence,LDTactical::Milliseconds(W->GetTimeSeconds()+5),FGuid::NewGuid());
        C->ApplyStatus(LDWorldDemo::Body(A),LDTactical::EStatus::Wet,LDTactical::Milliseconds(W->GetTimeSeconds()+5),FGuid::NewGuid());World->Prepare(W->GetTimeSeconds(),Hits);
        Check(!S->Action,TEXT("Actual Silence blocks AI skill through normal request boundary"));
        Check(E->Accounting().ReceiptCount()==Receipts&&!Mode->StateFor(A)->Gold,TEXT("NPC kit and choices mint no gold, items or rewards"));
    }
    else if(Case==TEXT("Sources"))
    {
        const TArray<FVector> Flank={World->Position(TEXT("HeartAntechamber")),FVector(-430,-500,0),FVector(-430,0,0),FVector(-100,100,0),FVector::ZeroVector};auto Path=[&](FVector From,FVector To){auto* P=UNavigationSystemV1::FindPathToLocationSynchronously(W,From,To,LDWorldDemo::Body(A));return P&&P->IsValid()&&!P->IsPartial()&&P->PathPoints.Num()>1;};Check(Path(Flank[0],Flank.Last()),TEXT("Native complete direct antechamber to Heart path"));bool ValidFlank=true;for(int32 N=1;N<Flank.Num();++N){ValidFlank&=Path(Flank[N-1],Flank[N]);}Check(ValidFlank,TEXT("Independent complete native flank through authored cover"));bool Occluded=true;for(uint8 N=0;N<3;++N){Occluded&=!LDGround::ClearSegment(W,World->Position(FString::Printf(TEXT("GrandTower%c"),'A'+N)),Flank[3]);}Check(Occluded,TEXT("Native wall traces occlude the inner flank from each surviving tower"));
        for(const auto& V:World->Pawns){auto* P=V.Get();if(!P||!P->Immune()){continue;}const int64 At=LDTactical::Milliseconds(W->GetTimeSeconds());for(auto Status:{LDTactical::EStatus::Stun,LDTactical::EStatus::Hex,LDTactical::EStatus::Break,LDTactical::EStatus::Silence,LDTactical::EStatus::Mute}){C->ApplyStatus(P,Status,At+2000,FGuid::NewGuid());Check(!C->StateFor(P)->Tactical.Has(Status,At),TEXT("Declared boss/tower category immunity"));}const FVector Before=P->GetActorLocation();Check(!C->Push(P,FVector::ForwardVector,100,W->GetTimeSeconds())&&P->GetActorLocation().Equals(Before),TEXT("Boss/tower displacement rejected"));C->ApplyStatus(P,LDTactical::EStatus::Root,At+100,FGuid::NewGuid());C->ApplyStatus(P,LDTactical::EStatus::Disarm,At+100,FGuid::NewGuid());Check(C->StateFor(P)->Tactical.Has(LDTactical::EStatus::Root,At)&&P->GetCharacterMovement()->GetMaxSpeed()==0&&!C->Allows(P,LDTactical::EAction::Attack),TEXT("Undeclared root/disarm immunities are not invented"));}Advance(W->GetTimeSeconds()+.2);
        TSet<FGuid> Seen;for(int32 Ordinal=0;Ordinal<12;++Ordinal){if(Ordinal){Advance(FMath::Max3(World->CampNext[0],World->CampNext[1],World->CampNext[2])+.1);}for(uint8 Camp=0;Camp<3;++Camp){ALDWorldPawn* P=nullptr;for(const auto& V:World->Pawns){if(V.IsValid()&&V->Kind==0&&V->Index==Camp&&!V->Paid){P=V.Get();break;}}Check(P&&!Seen.Contains(P->SpawnId),TEXT("One fresh authored camp identity, never a restored body"));if(!P){continue;}Seen.Add(P->SpawnId);World->Contact({LDWorldDemo::Body(A),P,1},1);C->StateFor(P)->Hp=0;Check(World->Defeated(P,W->GetTimeSeconds()),TEXT("One finite camp reward commits"));const auto Before=E->Accounting().ReceiptCount();Check(!World->Defeated(P,W->GetTimeSeconds())&&E->Accounting().ReceiptCount()==Before,TEXT("Repeated death cannot repay"));P->Destroy();}if(Ordinal==0){Advance(Mode->StartedAt()+149.9);int32 Living=0;for(const auto& P:World->Pawns){Living+=P.IsValid()&&P->Kind==0&&!P->Paid;}Check(Living==0,TEXT("No camp respawn before150s"));}}
        Check(Seen.Num()==36&&E->Accounting().Minted()==432,TEXT("Exactly36 camp deaths mint432g"));auto Rest=World->Pawns;for(const auto& V:Rest){auto* P=V.Get();if(!P||P->Paid||P->Kind==0){continue;}World->Contact({LDWorldDemo::Body(A),P,1},1);C->StateFor(P)->Hp=0;Check(World->Defeated(P,W->GetTimeSeconds()),TEXT("Guardian/tower bounded death transaction"));P->Destroy();}Check(E->Accounting().Minted()==642&&G->DefensiveFlags==0,TEXT("World maximum642g includes guardian90 and three towers40 each; siege permanent"));Check(World->TowerSight().IsEmpty(),TEXT("All destroyed surveillance sources removed"));int32 Relics=0,Fragments=0,Books=0;for(TActorIterator<ALDItem> It(W);It;++It){if(It->State==0){Relics+=It->Kind==ELDItemKind::TowerRelic;Fragments+=It->Kind==ELDItemKind::HeartFragment;Books+=It->Kind==ELDItemKind::Book&&It->Skill==ELDSkill::SovereignWard;}}Check(Relics==3&&Fragments==1&&Books==1,TEXT("Exact contestable physical drops, no exclusive equipment grant"));World->Initialize();Advance(W->GetTimeSeconds()+1);Check(E->Accounting().Minted()==642&&World->TowerSight().IsEmpty(),TEXT("Reinitialization and late samples never refill source budgets"));Check(World->AttributedXP.FindRef(Mode->ContenderId(A))==36*90+3*150+250,TEXT("Every bounded world death attributes exact XP to persistent contender"));
    }
    else if(Case==TEXT("Leases"))
    {
        Place(A,World->Position(TEXT("HeartAntechamber"))+FVector(-50,0,0));Place(B,World->Position(TEXT("HeartAntechamber"))+FVector(50,0,0));const int32 StarterCount=Mode->StateFor(A)->Inventory.Items.Num();auto* Key=Grant(A,ELDItemKind::AncientKeyA);Grant(A,ELDItemKind::AncientKeyB);Grant(A,ELDItemKind::AncientKeyC);auto* Spare=Grant(A,ELDItemKind::Heartbreaker);Grant(B,ELDItemKind::Heartbreaker);Advance(W->GetTimeSeconds()+.1);
        auto Id=Start(A,10);Held(A,Id,W->GetTimeSeconds()+4.1);auto* OA=Mode->StateFor(A);Check(OA->BreachRoute==5&&OA->Inventory.Items.Num()==StarterCount+4&&Key->ReadyAt==OA->KeysReadyAt,TEXT("Keys retained; instance and owner endpoints commit together"));const int64 Ready=Key->ReadyAt;if(!World->Leases.Contains(A)){Finish();return;}const FGuid OldLease=World->Leases.FindChecked(A).Id;FString Reason;Check(!World->CanActivate(A,6,W->GetTimeSeconds()+6,Reason)&&I->Owned(A,Spare->Identity),TEXT("One live private lease per owner; rejected artifact remains physical"));
        Id=Start(B,11);Held(B,Id,W->GetTimeSeconds()+6.1);Check(Mode->StateFor(B)->BreachRoute==6&&!LDWorldDemo::Find(Mode,B,ELDItemKind::Heartbreaker).IsValid()&&G->Breaches.Num()==2,TEXT("Concurrent different-owner leases; artifact consumes once; two anonymous warnings"));
        const FVector Drop=LDWorldDemo::Body(A)->GetActorLocation()+FVector(0,-40,0);I->Queue(A,OA->Match,OA->Life,++Token,OA->Inventory.Revision,LDInventory::Drop,Key->Identity,Key->Revision,0,Drop);Advance(W->GetTimeSeconds()+.1);Check(!World->Leases.Contains(A)&&Mode->StateFor(B)->BreachRoute==6&&Key->State==0,TEXT("Actual key drop immediately revokes old lease without closing another owner"));
        Place(B,Key->GetActorLocation()+FVector(50,0,-15));Advance(W->GetTimeSeconds()+.1);const FGuid Borrowed=LDWorldDemo::Observe(Mode,B,Key);Id=Start(B,14,Borrowed);Held(B,Id,W->GetTimeSeconds()+3.1);Check(I->Owned(B,Key->Identity)&&Key->ReadyAt==Ready&&Mode->StateFor(B)->KeysReadyAt==Ready,TEXT("Different holder inherits the same physical reuse endpoint"));auto* OldB=LDWorldDemo::Body(B);Mode->EndContenderLife(B,W->GetTimeSeconds());OldB->Destroy();Check(!World->Leases.Contains(B)&&!LDWorldDemo::Find(Mode,B,ELDItemKind::Heartbreaker).IsValid()&&Key->State==0,TEXT("Artifact death cancels lease without refund and drops newly acquired key once"));Advance(W->GetTimeSeconds()+.1);
        const FGuid Seen=LDWorldDemo::Observe(Mode,A,Key);Id=Start(A,14,Seen);Held(A,Id,W->GetTimeSeconds()+3.1);Check(I->Owned(A,Key->Identity)&&Key->ReadyAt==Ready&&OA->KeysReadyAt==Ready&&!World->Leases.Contains(A),TEXT("Quiet held re-pickup retains same ID/cooldown and never resurrects old lease"));Check(!World->CanActivate(A,5,W->GetTimeSeconds()+4,Reason),TEXT("Reacquired keys cannot bypass persistent90s reuse"));
        Advance(Ready/1000.+.1);Place(A,World->Position(TEXT("HeartAntechamber"))+FVector(-50,0,0));Advance(W->GetTimeSeconds()+.1);Id=Start(A,10);Held(A,Id,W->GetTimeSeconds()+4.1);Check(World->Leases.Contains(A)&&World->Leases.FindChecked(A).Id!=OldLease,TEXT("Only a fresh activation after reuse creates another lease"));auto* OldA=LDWorldDemo::Body(A);Mode->EndContenderLife(A,W->GetTimeSeconds());OldA->Destroy();Check(!World->Leases.Contains(A)&&Key->State==0&&OA->KeysReadyAt>Ready,TEXT("Key owner death drops physical keys, revokes lease and preserves owner cooldown"));
        if(Mode->IsV08Profile())
        {
            const FGuid KeyId=Key->Identity;const auto Cooldown=OA->KeysReadyAt;const uint32 Life=OA->Life;const double ReturnAt=OA->ReturnAt;
            Advance(ReturnAt+.01);Mode->ReturnDueContenders(ReturnAt+.01);auto* New=LDWorldDemo::Body(A);const auto* Returned=C->StateFor(New);
            Check(New&&OA->bActive&&OA->Life==Life+1&&OA->Hp==LDDevelopment::MaxHP(OA->Build)&&OA->Mp==LDTactical::MaxMP(OA->Build),TEXT("Real return creates next life at full current HP/MP"));
            Check(OA->ProtectedUntil>=ReturnAt+3&&Returned&&Returned->ProtectionUntil>=ReturnAt+3,TEXT("Returned body receives actual three-second protection"));
            Check(Key->Identity==KeyId&&Key->State==0&&OA->KeysReadyAt==Cooldown&&!World->Leases.Contains(A),TEXT("Return never clones dropped key, resets reuse, or restores expired lease"));
        }
    }
    else if(Case==TEXT("Shrines"))
    {
        Place(A,World->Position(TEXT("ShrineWest")));Advance(W->GetTimeSeconds()+.1);auto* S=C->StateFor(LDWorldDemo::Body(A));S->Hp=20;S->Tactical.MP=0;const auto XP=Mode->StateFor(A)->Xp,Minted=E->Accounting().Minted();auto Id=Start(A,12);Held(A,Id,W->GetTimeSeconds()+4.1);auto* OA=Mode->StateFor(A);Check(S->Hp==LDDevelopment::MaxHP(OA->Build)&&S->Tactical.MP==LDTactical::MaxMP(OA->Build)&&OA->ShrineReadyAt>W->GetTimeSeconds(),TEXT("Quiet4s shrine restores full HP/MP with owner90s endpoint"));Check(OA->Xp==XP&&E->Accounting().Minted()==Minted,TEXT("Recovery mints no gold/XP"));Place(A,World->Position(TEXT("ShrineEast")));Advance(W->GetTimeSeconds()+.1);Id=Start(A,13);Check(!O->HasChannel(A),TEXT("Another shrine cannot bypass same owner recovery endpoint"));
    }
    else if(Case==TEXT("SelfAction"))
    {
        G->CharterWindow=FGuid::NewGuid();G->CharterOpenAt=W->GetTimeSeconds();G->CharterCloseAt=W->GetTimeSeconds()+200;
        auto Exercise=[&](const FString& Name,TFunction<void()> Request){Advance(W->GetTimeSeconds()+.6);const auto Claim=Start(A,3);Held(A,Claim,W->GetTimeSeconds()+.1);Check(O->HasChannel(A),TEXT("Fresh claim before ")+Name);Request();Advance(W->GetTimeSeconds()+.1);Check(!O->HasChannel(A),Name+TEXT(" cancels before another action or rejection"));};
        Exercise(TEXT("Inventory organization"),[&](){const auto* S=Mode->StateFor(A);I->Queue(A,S->Match,S->Life,++Token,S->Inventory.Revision,LDInventory::Sort,{},0,0,{});});
        Exercise(TEXT("Preparation"),[&](){const auto* S=Mode->StateFor(A);auto P=LDDevelopment::PlanFrom(S->Build);if(Mode->IsV08Profile()){P.InventoryRevision=S->Inventory.Revision;P.Bindings=S->Inventory.Bindings;}Mode->Development->Queue(A,S->Match,S->Life,++Token,LDDevelopmentTransaction::Prepare,ELDSkill::None,{},P);});
        Exercise(TEXT("Production"),[&](){const auto* S=Mode->StateFor(A);FLDProductionPlan P;P.Site=2;P.Recipe=1;P.InventoryRevision=S->Inventory.Revision;P.BuildRevision=S->Build.Revision;Mode->Production->Queue(A,S->Match,S->Life,++Token,P);});
        Exercise(TEXT("Bank commit"),[&](){const auto* S=Mode->StateFor(A);Mode->Bank->Queue(A,S->Match,S->Life,++Token,FGuid::NewGuid(),0,false,1,1);});
        Exercise(TEXT("Market commit"),[&](){const auto* S=Mode->StateFor(A);Mode->Commerce->Queue(A,S->Match,S->Life,++Token,FGuid::NewGuid(),1,1,ELDItemKind::Fiber,{},0,S->Inventory.Revision,4);});
        Exercise(TEXT("Vendor commit"),[&](){const auto* S=Mode->StateFor(A);Mode->Vendor->Queue(A,S->Match,S->Life,++Token,FGuid::NewGuid(),1,40,S->Inventory.Revision,ELDSkill::Step);});
        Exercise(TEXT("Trade invitation"),[&](){const auto* S=Mode->StateFor(A);Mode->Trade->Queue(A,S->Match,S->Life,++Token,LDTrade::Invite,{},0,TEXT(""),FGuid::NewGuid(),0);});
        Exercise(TEXT("Equipment supply request"),[&](){const auto* S=Mode->StateFor(A);Mode->Acquisition->Purchase(A,S->Match,S->Life,++Token,2,ELDItemKind::SurveyorBow,ELDSkill::None,S->Inventory.Revision,ULDAcquisitionCatalog::Load(Mode->RulesVersion())->Checksum);});
        Exercise(TEXT("Another world service"),[&](){const auto* S=Mode->StateFor(A);O->Queue(A,S->Match,S->Life,++Token,10,{});});
        Exercise(TEXT("Combat command"),[&](){const auto* S=Mode->StateFor(A);FLDTacticalRequest R;R.ActionKind=4;R.Match=S->Match;R.Life=S->Life;R.Token=++Token;R.BuildRevision=S->Build.Revision;R.CatalogDigest=S->Build.CatalogDigest;R.Observed=LDWorldDemo::Observe(Mode,A,LDWorldDemo::Body(B));R.Direction=FVector(0,1,0);if(Mode->IsV08Profile()){R.Schema=8;R.WireVersion=4;R.SourceKind=3;R.InventoryRevision=S->Inventory.Revision;}A->ServerTacticalAction(R);});
        Check(!G->ResultSerial&&E->Accounting().Minted()==0,TEXT("Self cancellation creates no winner or reward"));
    }
    else if(Case==TEXT("ActivationDeadline"))
    {
        Place(A,World->Position(TEXT("HeartAntechamber")));Grant(A,ELDItemKind::AncientKeyA);Grant(A,ELDItemKind::AncientKeyB);Grant(A,ELDItemKind::AncientKeyC);auto* Artifact=Grant(A,ELDItemKind::Heartbreaker);Advance(G->MatchDeadline-94+.001);const auto Before=E->Accounting().ReceiptCount();Start(A,10);Check(!O->HasChannel(A)&&!World->Leases.Contains(A),TEXT("Keys cannot start unless full4+30+60 fits deadline"));Advance(W->GetTimeSeconds()+.1);Start(A,11);Check(!O->HasChannel(A)&&I->Owned(A,Artifact->Identity)&&E->Accounting().ReceiptCount()==Before,TEXT("Late artifact cannot reserve, consume, warn or debit"));Advance(G->MatchDeadline+1);Check(G->EndReason==2&&G->ResultSerial==1&&!Mode->StateFor(A)->bWinner&&!Mode->StateFor(B)->bWinner,TEXT("Cap without a valid due claim produces NoAcquisition, never hidden-score victory"));
    }
    else
    {
        const bool Deadline=Case==TEXT("Deadline"),Tie=Case==TEXT("TieA")||Case==TEXT("TieB"),Cancel=Case==TEXT("Death")||Case==TEXT("Stun")||Case==TEXT("Interrupt")||Case==TEXT("Displacement")||Case==TEXT("Release");
        if(Deadline){Advance(G->MatchDeadline-10);}const double Began=W->GetTimeSeconds();double End=Began+10;G->CharterWindow=FGuid::NewGuid();G->CharterOpenAt=Began;G->CharterCloseAt=End;G->CharterPhase=2;
        if(Case==TEXT("Handoff")||Case==TEXT("Gap")){G->CharterCloseAt=Began+5;ALDWorldDirector::FLease L;L.Id=FGuid::NewGuid();L.Route=6;L.Life=Mode->StateFor(A)->Life;L.Open=Began+(Case==TEXT("Gap")?5.001:4);L.Close=Began+15;World->Leases.Add(A,L);}
        if(Case==TEXT("Late")){Advance(Began+.001);End+=.001;}
        if(Case==TEXT("Tags")){for(auto Tag:{LDTactical::EStatus::Root,LDTactical::EStatus::Silence,LDTactical::EStatus::Mute}){C->ApplyStatus(LDWorldDemo::Body(A),Tag,LDTactical::Milliseconds(End+2),FGuid::NewGuid());}}
        uint32 First=0,Second=0;if(Tie){Place(B,FVector(0,60,0));if(Case==TEXT("TieB")){Second=Start(B,3);First=Start(A,3);}else{First=Start(A,3);Second=Start(B,3);}}else{First=Start(A,3);}const auto* Claim=O->StateFor(A);
        if(Case==TEXT("Late")||Case==TEXT("Gap")){Check(!Claim||!Claim->Channel,TEXT("A1ms late endpoint or uncovered gap refuses claim before progress"));}
        else
        {
            Check(Claim&&Claim->Channel==3,TEXT("Eligible full scheduled claim accepted"));ALDPlayerController* Expected=A;if(Tie){Expected=O->StateFor(A)->Ordinal<O->StateFor(B)->Ordinal?A.Get():B.Get();Check(O->StateFor(A)->Ordinal!=O->StateFor(B)->Ordinal,TEXT("Concurrent accepts receive unique authority ordinals"));}
            const double Event=End-(Case==TEXT("Damage")?2:0);while(W->GetTimeSeconds()+.11<Event){const double Next=FMath::Min(Event-.1,W->GetTimeSeconds()+.1);W->TimeSeconds=Next;O->RefreshHold(A,Mode->StateFor(A)->Life,First,++Sequence,false,Next);if(Tie){O->RefreshHold(B,Mode->StateFor(B)->Life,Second,++Sequence,false,Next);}C->AdvanceAuthority(Next);}
            if(Case==TEXT("Death")||Case==TEXT("Damage")){auto* S=C->StateFor(LDWorldDemo::Body(B));S->Action=1;S->CommittedDamage=Case==TEXT("Death")?10000:1;S->ImpactAt=Event;S->BasicReady=End+100;S->bAttackOrder=true;S->AttackTarget=LDWorldDemo::Body(A);S->AttackLife=C->StateFor(LDWorldDemo::Body(A))->Identity;Mode->StateFor(B)->Build.Gear.Reach=300;}
            if(Case==TEXT("Stun")){Advance(End-1.e-6);// Labelled committed disable at exactly the claim boundary, not an earlier command.
                W->TimeSeconds=End;C->ApplyStatus(LDWorldDemo::Body(A),LDTactical::EStatus::Stun,LDTactical::Milliseconds(End+1),FGuid::NewGuid());}
            if(Case==TEXT("Interrupt")){Advance(End-1.e-6);W->TimeSeconds=End;C->ExplicitInterrupt(LDWorldDemo::Body(A),End);}
            if(Case==TEXT("Displacement")){Advance(End-1.e-6);W->TimeSeconds=End;Check(C->Push(LDWorldDemo::Body(A),FVector(0,-1,0),30,End),TEXT("Actual nonzero swept displacement occurs before claim completion"));}
            if(Case==TEXT("Release")){Advance(End-1.e-6);W->TimeSeconds=End;O->Release(A,Mode->StateFor(A)->Life,First);}
            if(Case==TEXT("Damage")){Held(A,First,End-.05);}else if(!Cancel){Held(A,First,End-.05);if(Tie){W->TimeSeconds=End-.05;O->RefreshHold(B,Mode->StateFor(B)->Life,Second,++Sequence,false,W->GetTimeSeconds());}}
            Advance(End+1);if(Cancel){Check(G->EndReason==0&&G->ResultSerial==0&&!O->HasChannel(A),TEXT("Same-boundary death/disable/interrupt/displacement/release beats winner resolution"));}else{Check(G->EndReason==1&&G->ResultSerial==1&&Mode->StateFor(Expected)->bWinner&&FMath::IsNearlyEqual(double(G->MatchEndedAt),End,.001),TEXT("One winner at inclusive scheduled endpoint on stalled sample"));const auto Receipts=E->Accounting().ReceiptCount();Advance(End+100);Check(G->ResultSerial==1&&E->Accounting().ReceiptCount()==Receipts,TEXT("Terminal state rejects later events and duplicate receipts"));}
        }
    }
    Check(E->Audit(Why)&&E->AuditFailures==0,TEXT("Boundary exact physical sources and gold ledger reconcile: ")+Why);Finish();
#endif
}
