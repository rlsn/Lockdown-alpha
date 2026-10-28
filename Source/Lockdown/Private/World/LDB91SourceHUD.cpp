#include "World/LDEnvironmentDirector.h"
#include "World/LDPortableTools.h"
#include "UI/LDControlSettings.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

void ALDB91HUD::Command(const FString& Label,FName Name,int32 Value,FVector2D P,FVector2D Size)
{
    const FName Key(*FString::Printf(TEXT("%s_%d"),*Name.ToString(),Value));
    const TArray<FName> Names={TEXT("Select"),TEXT("Equip"),TEXT("Unequip"),TEXT("Use"),TEXT("Alternate"),TEXT("Drop"),TEXT("Store"),TEXT("Take"),TEXT("Place"),TEXT("Deploy"),TEXT("Retrieve"),TEXT("Stop"),TEXT("Carry")};
    Commands.Add(Key,FIntPoint(Names.Find(Name),Value));AddHitBox(P,Size,Key,true,100);DrawRect(FLinearColor(.08f,.19f,.23f,.96f),P.X,P.Y,Size.X,Size.Y);DrawText(Label,FLinearColor::White,P.X+5,P.Y+5,nullptr,.85f);
}
bool ALDB91HUD::PointerOverControls() const
{float X,Y;return PlayerOwner&&PlayerOwner->GetMousePosition(X,Y)&&GetHitBoxAtCoordinates(FVector2D(X,Y),true)!=nullptr;}
void ALDB91HUD::NotifyHitBoxClick(FName Name)
{Super::NotifyHitBoxClick(Name);const auto* C=Commands.Find(Name);auto* PC=Cast<ALDB91Controller>(PlayerOwner);if(!C||!PC){return;}const TArray<FName> Names={TEXT("Select"),TEXT("Equip"),TEXT("Unequip"),TEXT("Use"),TEXT("Alternate"),TEXT("Drop"),TEXT("Store"),TEXT("Take"),TEXT("Place"),TEXT("Deploy"),TEXT("Retrieve"),TEXT("Stop"),TEXT("Carry")};if(Names.IsValidIndex(C->X)){if(Names[C->X]==TEXT("Stop")){PC->StopSourceOrder();}else{PC->ItemCommand(Names[C->X],C->Y);}}}
void ALDB91HUD::DrawHUD()
{
    Super::DrawHUD();auto* PC=Cast<ALDB91Controller>(PlayerOwner);if(!PC||!Canvas){return;}Commands.Reset();
    DrawRect(FLinearColor(0,0,0,.85f),10,10,430,156);DrawText(TEXT("B91 / source-based actions"),FLinearColor::White,20,18);
    DrawText(TEXT("RMB: pickup / doors / move | LMB: storage / target"),FLinearColor::White,20,42,nullptr,.85f);
    DrawText(TEXT("A: basic attack | Q W E R D G: abilities | 1-4: items"),FLinearColor::White,20,62,nullptr,.85f);
    DrawText(TEXT("Confirm once. Release keeps channeling. S stops."),FLinearColor::White,20,82,nullptr,.85f);
    DrawText(PC->IntentFeedback,FLinearColor::Yellow,20,106,nullptr,.8f);Command(TEXT("Stop [S]"),TEXT("Stop"),0,FVector2D(20,132),FVector2D(100,26));
    const auto& Bag=PC->OwnerState.Inventory;const auto* Selected=Bag.Items.IsValidIndex(PC->BagSelection)?&Bag.Items[PC->BagSelection]:nullptr;
    const float Top=180;DrawRect(FLinearColor(0,0,0,.85f),10,Top,430,215);DrawText(FString::Printf(TEXT("Bag %d/24 · select an item"),Bag.Items.Num()),FLinearColor::White,20,Top+6,nullptr,.9f);
    for(int N=0;N<Bag.Items.Num();++N){FString Label=LDInventory::Name(Bag.Items[N].Kind,Bag.Items[N].Skill);if(Label.Len()>16){Label=Label.Left(15)+TEXT("…");}if(N==PC->BagSelection){Label=TEXT("> ")+Label;}Command(Label,TEXT("Select"),N,FVector2D(20+(N%3)*137,Top+30+(N/3)*19),FVector2D(132,18));}
    for(int N=0;N<4;++N){Command(FString::Printf(TEXT("Bind %d (2s)"),N+1),TEXT("Equip"),N,FVector2D(20+N*103,Top+185),FVector2D(98,25));}
    if(Selected){const int Binding=Bag.Bindings.Find(Selected->Id);const FString BindingName=Binding>=7?FString::Printf(TEXT("Accessory%d"),Binding-6):Binding>=0?TEXT("Body equipment"):TEXT("Unbound");DrawText(LDInventory::Name(Selected->Kind,Selected->Skill)+TEXT(" · ")+BindingName+(Selected->bReserved?TEXT(" · in use"):TEXT("")),FLinearColor::White,20,404,nullptr,.8f);Command(TEXT("Unbind (2s)"),TEXT("Unequip"),0,FVector2D(20,428),FVector2D(128,26));Command(TEXT("Drop item"),TEXT("Drop"),0,FVector2D(155,428),FVector2D(128,26));if(PC->ContainerHandle.IsValid()){Command(TEXT("Store item"),TEXT("Store"),0,FVector2D(290,428),FVector2D(128,26));}}
    const float Row=Canvas->SizeY-100;for(int N=0;N<4;++N)
    {const auto* I=Bag.Find(Bag.Bindings.IsValidIndex(7+N)?Bag.Bindings[7+N]:FGuid());const float X=460+N*182;Command(FString::Printf(TEXT("%d: %s"),N+1,I?*LDInventory::Name(I->Kind,I->Skill):TEXT("empty")),TEXT("Use"),N,FVector2D(X,Row),FVector2D(176,30));
     if(I&&(I->Kind==ELDItemKind::Torch||I->Kind==ELDItemKind::Lantern||I->Kind==ELDItemKind::ForestryKnife)){Command(I->Kind==ELDItemKind::ForestryKnife?TEXT("Chop (no material)"):I->bLit?TEXT("Extinguish light"):TEXT("Light"),TEXT("Alternate"),N,FVector2D(X,Row+34),FVector2D(176,26));}}
    if(PC->ContainerHandle.IsValid()){DrawText(TEXT("Storage · click an item to take it"),FLinearColor::White,460,180,nullptr,.9f);for(int N=0;N<PC->ContainerView.Num();++N){Command(LDInventory::Name(PC->ContainerView[N].Kind,ELDSkill::None),TEXT("Take"),N,FVector2D(460+(N%3)*180,206+(N/3)*30),FVector2D(172,27));}}
    if(const auto* O=PC->ObjectView.FindByPredicate([&](const auto& V){return V.Handle==PC->SelectedObject;});O&&(O->Kind==ELDObjectKind::Crate||O->Kind==ELDObjectKind::Barrel||O->Kind==ELDObjectKind::Ladder)){Command(TEXT("Carry selected prop"),TEXT("Carry"),0,FVector2D(460,110));}
    if(PC->HasObjectCargo()){Command(TEXT("Place carried item"),TEXT("Place"),0,FVector2D(460,140));Command(TEXT("Deploy Ladder"),TEXT("Deploy"),0,FVector2D(630,140));}
    if(const auto* O=PC->ObjectView.FindByPredicate([&](const auto& V){return V.Handle==PC->SelectedObject;});O&&O->Kind==ELDObjectKind::Anchor&&(O->State&16)){Command(TEXT("Retrieve installed tool"),TEXT("Retrieve"),0,FVector2D(800,140),FVector2D(195,28));}
    const double Now=GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    double Start=PC->Channel.Started,End=PC->Channel.Ends;FString Action=PC->Channel.Label;bool Active=PC->ChannelActive;
    if(PC->OwnerState.DevelopmentKind){Active=true;End=PC->OwnerState.DevelopmentEndsAt;Start=End-2;Action=TEXT("Preparing item binding");}
    if(Active){const float X=460,Y=Canvas->SizeY-145,W=Canvas->SizeX-X-20;const double T=FMath::Clamp((Now-Start)/FMath::Max(.01,End-Start),0.,1.);DrawRect(FLinearColor(.02f,.05f,.06f,.95f),X,Y,W,36);DrawRect(FLinearColor(.15f,.65f,.5f,.8f),X,Y+28,W*T,8);DrawText(Action+FString::Printf(TEXT(" · %.1fs · S stops"),FMath::Max(0.,End-Now)),FLinearColor::White,X+8,Y+4,nullptr,.9f);}
}
