#include "Development/LDWorldChecks.h"
#include "Framework/LDGameMode.h"
#include "Framework/LDPlayerController.h"
#include "Character/LDCharacter.h"
#include "Combat/LDCombatDirector.h"
#include "Vision/LDVisionDirector.h"
#include "Core/LDGroundQueries.h"
#include "Combat/LDCombatRules.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ALDCharterTask::ALDCharterTask(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
ALDWorldChecks::ALDWorldChecks(){bReplicates=false;}
void ALDWorldChecks::BeginPlay()
{
    Super::BeginPlay();check(HasAuthority());const FGuid PrivateEntropy=FGuid::NewGuid();Theft.Initialize(int32(PrivateEntropy.A^PrivateEntropy.B^PrivateEntropy.C^PrivateEntropy.D));
    auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    for(uint8 I=0;I<4;++I)
    {
        auto* Task=GetWorld()->SpawnActor<ALDCharterTask>(LDCharter::Position(I),FRotator::ZeroRotator);check(Task);Task->Service=I;
        Mode->Combat->RegisterSkillTask(Task,{LDCharter::Skill(I)},[this,Weak=TWeakObjectPtr<ALDCharterTask>(Task)](ALDCharacter* Source,ELDSkill Skill,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Result)
        {return UseCharter(Source,Weak.Get(),Build,Skill,Token,Phase,Result);});
    }
}
void ALDWorldChecks::EnsureTargets()
{
    auto* C=GetWorld()->GetAuthGameMode<ALDGameMode>()->Combat.Get();
    for(TActorIterator<ALDCharacter> It(GetWorld());It;++It)
    {
        if(!C->HasSkillTask(*It))
        {C->RegisterSkillTask(*It,{ELDSkill::Pickpocket},[this,Weak=TWeakObjectPtr<ALDCharacter>(*It)](ALDCharacter* Source,ELDSkill,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Result){return UseWallet(Source,Weak.Get(),Build,Token,Phase,Result);});}
    }
    for(auto It=BodyWallets.CreateIterator();It;++It){if(!It.Key().IsValid()){It.RemoveCurrent();}}
}
FGuid ALDWorldChecks::WalletId(ALDCharacter* Body)
{
    if(!IsValid(Body)){return FGuid();}auto* Mode=GetWorld()->GetAuthGameMode<ALDGameMode>();
    if(auto* PC=Cast<ALDPlayerController>(Body->GetController()))
    {
        const FGuid Id=Mode->ContenderId(PC);if(!Id.IsValid()){return FGuid();}
        auto& W=Wallets.FindOrAdd(Id);W.Human=true;W.Owner=PC;W.Gold=0;BodyWallets.Add(Body,Id);return Id;
    }
    if(const auto* Id=BodyWallets.Find(Body)){return *Id;}
    const FGuid Id=FGuid::NewGuid();Wallets.Add(Id,FWallet());BodyWallets.Add(Body,Id);return Id;
}
void ALDWorldChecks::BindReturnedBody(ALDCharacter* Body,FGuid Id)
{if(HasAuthority()&&IsValid(Body)&&Wallets.Contains(Id)){BodyWallets.Add(Body,Id);}}
void ALDWorldChecks::CancelAttempt(ALDCharacter* Source,uint32 Token)
{
    const auto* Id=BodyWallets.Find(Source);auto* W=Id?Wallets.Find(*Id):nullptr;auto* R=W?W->Receipts.Find(Token):nullptr;
    if(!R||R->Committed||R->Cancelled){return;}R->Cancelled=true;R->Feedback=TEXT("Purse attempt interrupted; skill cooldown and pair retry retained. No roll drawn.");
    if(auto* PC=Cast<ALDPlayerController>(Source->GetController())){PC->CompleteCombatRequest(Token,R->Feedback);}
}
int64 ALDWorldChecks::Gold(const FWallet& W) const
{if(!W.Human){return W.Gold;}const auto* O=GetWorld()->GetAuthGameMode<ALDGameMode>()->StateFor(W.Owner.Get());return O?O->Gold:0;}
void ALDWorldChecks::SetGold(FWallet& W,int64 Value)
{check(Value>=0);if(!W.Human){W.Gold=Value;}else{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(auto* O=M->StateFor(W.Owner.Get())){O->Gold=Value;M->SyncOwner(W.Owner.Get());}}}
int64 ALDWorldChecks::Balance(ALDCharacter* Body){const FGuid Id=WalletId(Body);const auto* W=Wallets.Find(Id);return W?Gold(*W):0;}
double ALDWorldChecks::RetryAt(ALDCharacter* Source,ALDCharacter* Target)
{const FGuid S=WalletId(Source),T=WalletId(Target);const auto* W=Wallets.Find(S);return W?W->Retry.FindRef(T):0;}
double ALDWorldChecks::AlertUntil(ALDCharacter* Target){const auto* W=Wallets.Find(WalletId(Target));return W?W->Alert:0;}
int32 ALDWorldChecks::Wisdom(ALDCharacter* Body) const
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(auto* PC=Cast<ALDPlayerController>(Body->GetController())){if(auto* O=M->StateFor(PC)){return O->Build.Attributes[LDDevelopment::WIS];}}
    const auto* S=M->Combat->StateFor(Body);return S&&S->bHasNPCBuild?S->NPCBuild.Attributes[LDDevelopment::WIS]:10;
}
bool ALDWorldChecks::LiveWallet(ALDCharacter* Body,const FWallet& W) const
{
    if(!W.Human){return true;}auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();const auto* O=M->StateFor(W.Owner.Get());
    return W.Owner.IsValid()&&W.Owner->GetPawn()==Body&&O&&O->bActive;
}
int32 ALDWorldChecks::Draw()
{
    ++Draws;
#if !UE_BUILD_SHIPPING
    if(!TestRolls.IsEmpty()){const int32 Roll=TestRolls[0];TestRolls.RemoveAt(0);return Roll;}
#endif
    return Theft.RandRange(1,20);
}
#if !UE_BUILD_SHIPPING
bool ALDWorldChecks::InjectTestRolls(const TArray<int32>& Rolls)
{if(!HasAuthority()||!FParse::Param(FCommandLine::Get(),TEXT("LDWorldCheckProbe"))||Rolls.ContainsByPredicate([](int32 R){return R<1||R>20;})){return false;}TestRolls=Rolls;return true;}
bool ALDWorldChecks::SetTestBalance(ALDCharacter* Body,int64 Value)
{if(!HasAuthority()||!FParse::Param(FCommandLine::Get(),TEXT("LDWorldCheckProbe"))||Value<0){return false;}const FGuid Id=WalletId(Body);if(auto* W=Wallets.Find(Id)){SetGold(*W,Value);return true;}return false;}
#endif
bool ALDWorldChecks::UseWallet(ALDCharacter* Source,ALDCharacter* Target,const FLDCharacterBuild& Build,uint32 Token,ELDSkillTaskPhase Phase,FString& Result)
{
    auto Reject=[&](){Result=TEXT("No eligible purse in reach, or retry unavailable.");return false;};
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* C=M->Combat.Get();const double Now=GetWorld()->GetTimeSeconds();
    auto* S=C->StateFor(Source);const auto* T=C->StateFor(Target);
    if(!HasAuthority()||!M->IsPlaying()||!Source||!Target||Source==Target||!S||!T||S->Hp<=0||T->Hp<=0||Now<S->ProtectionUntil||Now<T->ProtectionUntil||Now<T->ExposedUntil||!Token
        ||!LDDevelopment::Equipped(Build,ELDSkill::Pickpocket)||!LDDevelopment::Valid(Build)
        ||FVector::DistSquared2D(Source->GetActorLocation(),Target->GetActorLocation())>100*100
        ||!LDCombat::IsRear(Source->GetActorLocation(),Target->GetActorLocation(),Target->GetActorForwardVector())||!LDGround::ClearSegment(GetWorld(),Source->GetActorLocation(),Target->GetActorLocation())){return Reject();}
    if(auto* PC=Cast<ALDPlayerController>(Source->GetController())){if(!M->Vision->CanSee(PC,Target->GetActorLocation())){return Reject();}}
    else if(!M->Vision->Ray({Source->GetActorLocation(),720,M->Vision->LevelAt(Source->GetActorLocation())},Target->GetActorLocation())){return Reject();}
    const FGuid SourceId=WalletId(Source),TargetId=WalletId(Target);if(!SourceId.IsValid()||!TargetId.IsValid()||SourceId==TargetId){return Reject();}
    auto& SW=Wallets.FindChecked(SourceId);auto& TW=Wallets.FindChecked(TargetId);
    if(!LiveWallet(Source,SW)||!LiveWallet(Target,TW)||Gold(SW)<0||Gold(TW)<0||Gold(SW)>MAX_int64-12){return Reject();}
    auto* Existing=SW.Receipts.Find(Token);
    if(Existing&&Existing->Cancelled){Result=Existing->Feedback;return false;}
    if(Existing&&Existing->Committed){Result=Existing->Feedback;return Phase==ELDSkillTaskPhase::Commit;}
    if(Phase==ELDSkillTaskPhase::Validate)
    {
        if(Existing){if(Existing->SourceBody!=S->Identity||Existing->Source!=Source||Existing->Target!=Target||Existing->TargetWallet!=TargetId||S->Action!=7||S->SkillToken!=Token){return Reject();}}
        else if(Now<SW.Retry.FindRef(TargetId)){return Reject();}
        return true;
    }
    if(Phase==ELDSkillTaskPhase::Accept)
    {
        if(Existing||Now<SW.Retry.FindRef(TargetId)||S->Action){return Reject();}
        FReceipt Receipt;Receipt.Id=FGuid::NewGuid();Receipt.SourceBody=S->Identity;Receipt.Source=Source;Receipt.Target=Target;Receipt.TargetWallet=TargetId;SW.Receipts.Add(Token,Receipt);SW.Retry.Add(TargetId,Now+90);
        while(SW.Receipts.Num()>64){uint32 Oldest=MAX_uint32;for(const auto& Pair:SW.Receipts){Oldest=FMath::Min(Oldest,Pair.Key);}SW.Receipts.Remove(Oldest);}
        if(auto* PC=Cast<ALDPlayerController>(Source->GetController())){auto* O=M->StateFor(PC);O->TheftRetryAt=Now+90;M->SyncOwner(PC);}
        return true;
    }
    if(!Existing||Existing->SourceBody!=S->Identity||Existing->Source!=Source||Existing->Target!=Target||Existing->TargetWallet!=TargetId||S->Action!=7||S->SkillToken!=Token){return Reject();}
    auto& R=*Existing;R.Roll=Draw();R.Success=LDDevelopment::TheftSucceeds(R.Roll,Build.Attributes[LDDevelopment::DEX],LDDevelopment::Rank(Build,ELDSkill::Pickpocket),Build.Subclass==3,Wisdom(Target),Now<TW.Alert);R.Committed=true;
    R.Amount=R.Success?FMath::Min<int64>(12,Gold(TW)):0;
    if(R.Success){const int64 SourceGold=Gold(SW),TargetGold=Gold(TW);SetGold(TW,TargetGold-R.Amount);SetGold(SW,SourceGold+R.Amount);}
    else{TW.Alert=FMath::Max(TW.Alert,Now+30);S->ExposedUntil=FMath::Max(S->ExposedUntil,Now+4);}
    R.Feedback=R.Success?FString::Printf(TEXT("Purse attempt succeeded: %lld gold transferred. Receipt %s."),R.Amount,*R.Id.ToString(EGuidFormats::Short)):FString::Printf(TEXT("Purse attempt failed; target alerted and you are exposed. Receipt %s."),*R.Id.ToString(EGuidFormats::Short));Result=R.Feedback;
    if(auto* PC=Cast<ALDPlayerController>(Source->GetController())){auto* O=M->StateFor(PC);O->TheftReceipt=R.Id;O->TheftOutcome=R.Success?1:2;O->TheftAmount=R.Amount;M->SyncOwner(PC);}
    if(TW.Human&&TW.Owner.IsValid()){auto* O=M->StateFor(TW.Owner.Get());O->AlertUntil=TW.Alert;++O->TamperSerial;M->SyncOwner(TW.Owner.Get());TW.Owner->ClientIntentResult(0,R.Success?TEXT("Your purse balance changed."):TEXT("Your purse was disturbed."));}
    UE_LOG(LogTemp,Display,TEXT("LD_THEFT_RECEIPT request=%u receipt=%s roll=%d success=%d transfer=%lld"),Token,*R.Id.ToString(),R.Roll,R.Success,R.Amount);
    return true;
}
bool ALDWorldChecks::UseCharter(ALDCharacter* Source,ALDCharterTask* Task,const FLDCharacterBuild& Build,ELDSkill Skill,uint32 Token,ELDSkillTaskPhase Phase,FString& Result)
{
    auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();auto* PC=Source?Cast<ALDPlayerController>(Source->GetController()):nullptr;auto* O=M->StateFor(PC);const auto* S=M->Combat->StateFor(Source);const double Now=GetWorld()->GetTimeSeconds();
    if(!HasAuthority()||!Task||!O||!O->bActive||!S||S->Hp<=0||Now<S->ProtectionUntil||!M->IsPlaying()||Skill!=LDCharter::Skill(Task->Service)||!LDDevelopment::Equipped(Build,Skill)||!Token
        ||FVector::DistSquared2D(Source->GetActorLocation(),Task->GetActorLocation())>120*120||!LDGround::ClearSegment(GetWorld(),Source->GetActorLocation(),Task->GetActorLocation())||!M->Vision->CanSee(PC,Task->GetActorLocation())){Result=TEXT("Approach a visible Charter service with its skill equipped.");return false;}
    if(O->Witness.IsValid()){Result=TEXT("Charter Witness already recorded; no duplicate reward.");return false;}
    if(LDDevelopment::SkillValue(Build,Skill)<12){Result=TEXT("Your check score is below the public threshold 12; no cooldown spent.");return false;}
    if(Phase==ELDSkillTaskPhase::Commit)
    {
        if(S->Action!=7||S->SkillToken!=Token){return false;}
        O->Witness=FGuid::NewGuid();M->SyncOwner(PC);Result=FString::Printf(TEXT("Charter Witness recorded. Receipt %s. No gold or XP awarded."),*O->Witness.ToString(EGuidFormats::Short));
        UE_LOG(LogTemp,Display,TEXT("LD_WITNESS_RECEIPT request=%u receipt=%s service=%d"),Token,*O->Witness.ToString(),Task->Service);
    }
    return true;
}
