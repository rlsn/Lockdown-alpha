#include "Population/LDB92Roster.h"
#include "Economy/LDB92Services.h"
#include "Region/LDMapDefinition.h"
#include "Navigation/LDNavigationComponent.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void ALDB92Controller::ServerB92Open_Implementation(uint32 Match,uint32 Life,uint32 Sequence,FName POI)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Population&&M->Population->Services){M->Population->Services->Open(this,Match,Life,Sequence,POI);}}
void ALDB92Controller::ServerB92Order_Implementation(FLDB92MarketOrder Order)
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Population&&M->Population->Services){M->Population->Services->Order(this,Order);}}
void ALDB92Controller::ServerB92Close_Implementation()
{auto* M=GetWorld()->GetAuthGameMode<ALDGameMode>();if(M&&M->Population&&M->Population->Services){M->Population->Services->Close(this);}}
void ALDB92Controller::ClientB92Market_Implementation(const FLDOwnerState& Snapshot,const FLDB92MarketView& Terms,uint32 Sequence,FGuid Receipt,bool Accepted,const FString& Message)
{
    if(Snapshot.Match<OwnerState.Match||(Snapshot.Match==OwnerState.Match&&Snapshot.Revision<OwnerState.Revision)){return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("LDB92ServicesProbe"))&&Sequence&&Receipt.IsValid()){if(const auto* Old=ServicesProbeReceipts.Find(Sequence)){ServicesProbeReplaySame&=*Old==Receipt;}else{ServicesProbeReceipts.Add(Sequence,Receipt);}++ServicesProbeReceiptCounts.FindOrAdd(Sequence);}
    ClientLifeReceipt_Implementation(Snapshot);MarketView=Terms;if(Sequence){MarketReceiptSequence=Sequence;MarketReceipt=Receipt;MarketAccepted=Accepted;++MarketReceiptSerial;}MarketMessage=Message;
}
void ALDB92Controller::OpenMarket(FName POI)
{
    if(!CanWorldCommand()||!ALDB92Services::Endpoints().Contains(POI)){return;}StopSourceOrder();Navigation->Stop();const FVector P=ALDB92Services::Counter(GetWorld(),POI);
    if(FVector::Dist2D(GetPawn()->GetActorLocation(),P)>120){Navigation->Navigate(P);PendingMarket=POI;PendingMarketOrder=Navigation->LocalIdentity().Order;PendingMarketAt=GetWorld()->GetTimeSeconds();return;}
    ServerB92Open(OwnerState.Match,OwnerState.Life,++MarketSequence,POI);
}
FLDB92MarketOrder ALDB92Controller::MarketOrder(bool Buy,ELDItemKind Kind,FGuid Item)
{
    FLDB92MarketOrder R;R.Match=OwnerState.Match;R.Life=OwnerState.Life;R.Sequence=++MarketSequence;R.Session=MarketView.Session;R.Revision=MarketView.Revision;R.WalletRevision=MarketView.WalletRevision;R.InventoryRevision=OwnerState.Inventory.Revision;R.Kind=Kind;R.Buy=Buy;R.Item=Item;
    if(const auto* I=OwnerState.Inventory.Find(Item)){R.ItemRevision=I->Revision;}
    if(const auto* Offer=MarketView.Offers.FindByPredicate([&](const auto& O){return O.Kind==Kind;})){R.Price=Buy?Offer->Ask:Offer->Bid;}return R;
}
void ALDB92Controller::SubmitMarket(bool Buy,ELDItemKind Kind)
{
    if(!MarketView.Session.IsValid()){return;}FGuid Item;
    if(!Buy){for(const auto& I:OwnerState.Inventory.Items){if(I.Kind==Kind&&!I.BindingMask&&!I.bReserved){Item=I.Id;break;}}if(!Item.IsValid()){MarketMessage=TEXT("No available unit in your bag");return;}}
    ServerB92Order(MarketOrder(Buy,Kind,Item));
}
void ALDB92Controller::SubmitBank(bool Deposit,int64 Amount)
{if(!MarketView.Session.IsValid()){return;}auto R=MarketOrder(true,ELDItemKind::Fiber);R.BankAction=Deposit?1:2;R.Amount=Amount;R.BankRevision=MarketView.BankRevision;R.Price=0;R.Revision=0;ServerB92Order(R);}
void ALDB92Controller::SubmitSupply(int32 Index)
{if(!MarketView.Session.IsValid()||!MarketView.Offers.IsValidIndex(Index)){return;}const auto& V=MarketView.Offers[Index];auto R=MarketOrder(true,V.Kind);R.Skill=V.Skill;R.Price=V.Ask;R.Supply=true;ServerB92Order(R);}
bool ALDB92Controller::PointerService(FVector Point)
{
    if(!IsLocalController()||!GetPawn()){return false;}float X,Y;if(!GetMousePosition(X,Y)){return false;}const auto* E=ALDEnvironmentDirector::Get(GetWorld());
    for(FName POI:ALDB92Services::Endpoints())
    {
        const FVector P=ALDB92Services::Counter(GetWorld(),POI);FVector2D Screen;
        if(ProjectWorldLocationToScreen(P+FVector(0,0,60),Screen)&&FVector2D::DistSquared(Screen,FVector2D(X,Y))<32*32&&E&&E->Ray(GetPawn()->GetActorLocation(),P+FVector(0,0,90),nullptr,E->SightFor(this)))
        {OpenMarket(POI);return true;}
    }return false;
}
void ALDB92Controller::PlayerTick(float Delta)
{
    Super::PlayerTick(Delta);if(!IsLocalController()){return;}TickServicesProbe();
    if(MarketMarkers.IsEmpty()&&ULDMapDefinition::Get(GetWorld()))
    {
        for(FName POI:ALDB92Services::Endpoints())
        {auto* Marker=GetWorld()->SpawnActor<AStaticMeshActor>();Marker->SetReplicates(false);auto* Mesh=Marker->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);Mesh->SetWorldScale3D(FVector(.55,.55,.7));Marker->SetActorLocation(ALDB92Services::Counter(GetWorld(),POI)+FVector(0,0,35));MarketMarkers.Add(Marker);}
    }
    if(!PendingMarket.IsNone())
    {
        const FVector P=ALDB92Services::Counter(GetWorld(),PendingMarket);
        if(!CanWorldCommand()||Navigation->LocalIdentity().Order!=PendingMarketOrder||GetWorld()->GetTimeSeconds()-PendingMarketAt>20){PendingMarket=NAME_None;}
        else if(Navigation->Status==TEXT("Arrived")){const FName POI=PendingMarket;PendingMarket=NAME_None;OpenMarket(POI);}
    }
}
void ALDB92HUD::DrawHUD()
{
    Super::DrawHUD();auto* PC=Cast<ALDB92Controller>(PlayerOwner);if(!PC||!Canvas){return;}const auto& Q=PC->MarketView;if(!Q.Session.IsValid()){return;}
    const float X=FMath::Max(460.f,float(Canvas->SizeX)-400.f),Y=180;DrawRect(FLinearColor(0,.03f,.05f,.96f),X,Y,390,365);
    const auto* Map=ULDMapDefinition::Get(GetWorld());const auto* P=Map?Map->Find(Q.POI):nullptr;DrawText(P?P->Name:TEXT("Regional market"),FLinearColor::White,X+10,Y+8,nullptr,.85f);
    DrawText(FString::Printf(TEXT("Wallet: %lld gold | one unit per confirmation"),PC->OwnerState.Gold),FLinearColor::White,X+10,Y+30,nullptr,.8f);
    if(ALDB92Services::Official(Q.POI))
    {
        const int32 Pages=FMath::Max(1,FMath::DivideAndRoundUp(Q.Offers.Num(),5));PC->SupplyPage=FMath::Clamp(PC->SupplyPage,0,Pages-1);
        for(int32 I=PC->SupplyPage*5;I<FMath::Min(Q.Offers.Num(),PC->SupplyPage*5+5);++I){const auto& O=Q.Offers[I];const float Row=Y+60+(I%5)*39;DrawText(LDInventory::Name(O.Kind,O.Skill).Left(34),FLinearColor::White,X+10,Row,nullptr,.75f);const FName Name(*FString::Printf(TEXT("Supply_%d"),I));AddHitBox(FVector2D(X+270,Row-3),FVector2D(105,28),Name,true,200);DrawRect(FLinearColor(.08f,.2f,.23f,1),X+270,Row-3,105,28);DrawText(FString::Printf(TEXT("Buy %d%s"),O.Ask,O.Stock==0?TEXT(" SOLD OUT"):TEXT("")),FLinearColor::White,X+273,Row+3,nullptr,.7f);if(O.Stock>=0){DrawText(FString::Printf(TEXT("Remaining: %d"),O.Stock),FLinearColor::Yellow,X+185,Row+15,nullptr,.65f);}}
        for(int32 N=0;N<2;++N){const float Left=X+10+N*120;AddHitBox(FVector2D(Left,Y+263),FVector2D(110,25),N?TEXT("Supply_Next"):TEXT("Supply_Previous"),true,200);DrawText(N?TEXT("Next"):TEXT("Previous"),FLinearColor::White,Left,Y+265,nullptr,.8f);}DrawText(FString::Printf(TEXT("Page %d / %d"),PC->SupplyPage+1,Pages),FLinearColor::White,X+250,Y+265,nullptr,.8f);DrawText(PC->MarketMessage.Left(55),FLinearColor::Yellow,X+10,Y+305,nullptr,.75f);AddHitBox(FVector2D(X+270,Y+330),FVector2D(105,25),TEXT("Market_Close"),true,200);DrawText(TEXT("Close"),FLinearColor::White,X+290,Y+332);return;
    }
    for(int32 I=0;I<Q.Offers.Num();++I)
    {
        const auto& O=Q.Offers[I];const float Row=Y+60+I*40;DrawText(FString::Printf(TEXT("%s (%d)"),*LDInventory::Name(O.Kind,ELDSkill::None),O.Stock),FLinearColor::White,X+10,Row,nullptr,.85f);
        for(int32 Action=0;Action<2;++Action)
        {const FName Name(*FString::Printf(TEXT("Market_%d_%d"),Action,uint8(O.Kind)));const float Left=X+150+Action*112;AddHitBox(FVector2D(Left,Row-3),FVector2D(105,27),Name,true,200);DrawRect(FLinearColor(.08f,.2f,.23f,1),Left,Row-3,105,27);DrawText(FString::Printf(TEXT("%s1: %d"),Action?TEXT("Sell "):TEXT("Buy "),Action?O.Bid:O.Ask),FLinearColor::White,Left+5,Row+2,nullptr,.8f);}
    }
    DrawText(FString::Printf(TEXT("Bank: %lld | Deposit fee: ceil(2%%), no withdrawal fee"),PC->OwnerState.BankGold),FLinearColor::White,X+10,Y+220,nullptr,.73f);
    for(int32 Row=0;Row<2;++Row){for(int32 Col=0;Col<3;++Col){const int32 Amount=Col==0?2:Col==1?10:50;const float Left=X+10+Col*125,Top=Y+244+Row*28;const FName Name(*FString::Printf(TEXT("Bank_%d_%d"),Row,Amount));AddHitBox(FVector2D(Left,Top),FVector2D(120,24),Name,true,200);DrawRect(FLinearColor(.08f,.2f,.23f,1),Left,Top,120,24);DrawText(FString::Printf(TEXT("%s %d"),Row?TEXT("Withdraw"):TEXT("Deposit"),Amount),FLinearColor::White,Left+4,Top+3,nullptr,.8f);}}
    DrawText(PC->MarketMessage.Left(55),FLinearColor::Yellow,X+10,Y+305,nullptr,.75f);AddHitBox(FVector2D(X+270,Y+330),FVector2D(105,25),TEXT("Market_Close"),true,200);DrawText(TEXT("Close"),FLinearColor::White,X+290,Y+332);
}
void ALDB92HUD::NotifyHitBoxClick(FName Name)
{
    auto* PC=Cast<ALDB92Controller>(PlayerOwner);const FString Text=Name.ToString();if(PC&&Text.StartsWith(TEXT("Supply_"))){if(Text==TEXT("Supply_Next")){++PC->SupplyPage;}else if(Text==TEXT("Supply_Previous")){--PC->SupplyPage;}else{PC->SubmitSupply(FCString::Atoi(*Text.Mid(7)));}return;}if(PC&&Text.StartsWith(TEXT("Bank_"))){TArray<FString> Fields;Text.ParseIntoArray(Fields,TEXT("_"));if(Fields.Num()==3){PC->SubmitBank(FCString::Atoi(*Fields[1])==0,FCString::Atoi64(*Fields[2]));}return;}if(PC&&Text.StartsWith(TEXT("Market_")))
    {if(Text==TEXT("Market_Close")){PC->ServerB92Close();return;}TArray<FString> Fields;Text.ParseIntoArray(Fields,TEXT("_"));if(Fields.Num()==3){PC->SubmitMarket(FCString::Atoi(*Fields[1])==0,ELDItemKind(FCString::Atoi(*Fields[2])));}return;}Super::NotifyHitBoxClick(Name);
}
