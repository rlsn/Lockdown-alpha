#include "UI/LDBuildRingModel.h"
#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatTypes.h"
#include "UI/LDBuildRingPaint.h"
#include "UI/LDComponentStorage.h"
#include "UI/LDWorkshop.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Development/LDDemoContentCatalog.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "LDTestApprovedDisplay.h"
#include "LDTestApprovedFixture.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace
{
// The approved display names, registered for the faces when this editor session has none (like the HUD tests).
bool RingRegisterNames(FAutomationTestBase& Test,const FLDDemoContentDefinitions& Content,FName& OutTable)
{
    OutTable=NAME_None;
    TMap<FString,FString> Names;
    if(!LDTestApprovedDisplay::Names(Test,Names)||Content.Display.IsEmpty()){return false;}
    const FName Namespace(*Content.Display[0].Namespace);
    if(FStringTableRegistry::Get().FindStringTable(Namespace).IsValid()){return true;}
    OutTable=Namespace;
    FStringTableRef Table=FStringTable::NewStringTable();Table->SetNamespace(OutTable.ToString());
    for(const FLDDemoDisplayDefinition& Row:Content.Display)
    {
        const FString* Name=Names.Find(Row.Id);
        if(!Name||Row.Namespace!=OutTable.ToString()){continue;}
        const auto Put=[&](const FString& Key,const FString& Value)
        {
            if(Key.IsEmpty()){return;}
#if WITH_EDITORONLY_DATA
            Table->SetSourceString(FTextKey(*Key),Value,FString());
#else
            Table->SetSourceString(FTextKey(*Key),Value);
#endif
        };
        Put(Row.NameKey,*Name);Put(Row.DescriptionKey,Row.DescriptionText);
    }
    FStringTableRegistry::Get().RegisterStringTable(OutTable,Table);
    return true;
}
// A level-16 own view over the approved catalog: three learned skills, body cards installed (one twice), a few stored
// cards and two element medals, one of them in the body socket.
FLDProgressionView RingFixtureView(const FLDDemoContentDefinitions& Content)
{
    FLDProgressionView View;View.bValid=true;View.Generation=1;View.Life=1;View.LifePhase=ELDLifePhase::Alive;
    View.Level=16;View.MaxLevel=18;View.BuildRevision=5;View.UnspentPoints=4;View.CardCapacity=11;
    View.CatalogDigest=Content.Identity.Root;
    for(const FLDDemoStatDefinition* Stat:Content.StatsInDesignOrder()){View.StatPoints.Add({FName(*Stat->Id),View.StatPoints.Num()==2?18:3});}
    int32 Learned=0;
    for(const FLDDemoAbilityDefinition& Ability:Content.Abilities)
    {
        if(Learned==3){break;}
        FLDActiveSlotView& Active=View.Actives.AddDefaulted_GetRef();Active.DefinitionId=FName(*Ability.Id);Active.Slot=uint8(Learned++);
    }
    uint16 Serial=1;int32 Body=0,Stored=0,Medals=0;
    for(const FLDDemoCardDefinition& Card:Content.Cards)
    {
        const bool bMedal=Card.Form==ELDCardForm::Medal;
        if(bMedal&&Medals<2)
        {
            FLDCardInstanceView& Owned=View.Cards.AddDefaulted_GetRef();Owned.Serial=Serial++;Owned.DefinitionId=FName(*Card.Id);
            if(Medals==0){Owned.Target.Kind=ELDCardTargetKind::Body;Owned.Tier=2;}
            ++Medals;continue;
        }
        if(Card.Form==ELDCardForm::Body&&Body<4)
        {
            for(int32 Copy=0;Copy<(Body==0?2:1);++Copy)
            {
                FLDCardInstanceView& Owned=View.Cards.AddDefaulted_GetRef();Owned.Serial=Serial++;Owned.DefinitionId=FName(*Card.Id);
                Owned.Target.Kind=ELDCardTargetKind::Body;
            }
            ++Body;continue;
        }
        if(!bMedal&&Card.Form!=ELDCardForm::Skill&&Stored<5)
        {
            FLDCardInstanceView& Owned=View.Cards.AddDefaulted_GetRef();Owned.Serial=Serial++;Owned.DefinitionId=FName(*Card.Id);
            ++Stored;
        }
    }
    return View;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildRingGeometryTest,"Lockdown.Rules.BuildRingGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBuildRingGeometryTest::RunTest(const FString& Parameters)
{
    // The approved scale rules: ring never under 0.8, text never under 0.875 (times the player's size), layout H / 1080.
    const LDBuildRing::FScale Full=LDBuildRing::ScaleFor(1080.f,1.f),Small=LDBuildRing::ScaleFor(720.f,1.f),Large=LDBuildRing::ScaleFor(720.f,1.25f);
    TestTrue(TEXT("1080p scales are one"),FMath::IsNearlyEqual(Full.Layout,1.f)&&FMath::IsNearlyEqual(Full.Ring,1.f)&&FMath::IsNearlyEqual(Full.Text,1.f));
    TestTrue(TEXT("720p keeps the ring at 0.8 and the text at 0.875"),FMath::IsNearlyEqual(Small.Layout,720.f/1080.f)
        &&FMath::IsNearlyEqual(Small.Ring,.8f)&&FMath::IsNearlyEqual(Small.Text,.875f));
    TestTrue(TEXT("The text size multiplies the text floor"),FMath::IsNearlyEqual(Large.Text,.875f*1.25f));
    TestTrue(TEXT("Ring icons stay at least 36 px at 720p"),LDBuildRing::NodeSize*Small.Ring>=36.f);
    TestTrue(TEXT("The smallest label stays at least 12 px at 720p"),14.f*Small.Text>=12.f);
    // The 3 + 4 + 5 places fill from the middle outwards and never move when another card comes or goes.
    TestEqual(TEXT("One place takes one row"),LDBuildRing::RowsFor(1),1);
    TestEqual(TEXT("Twelve places take three rows"),LDBuildRing::RowsFor(12),3);
    TestEqual(TEXT("A thirteenth place opens the fourth row"),LDBuildRing::RowsFor(13),4);
    const FVector2D First=LDBuildRing::SlotPolar(0,0.f);
    TestTrue(TEXT("The first place is the middle of the first row"),FMath::IsNearlyEqual(First.X,LDBuildRing::RowRadius[0])&&FMath::IsNearlyZero(First.Y));
    TSet<FIntPoint> Seen;
    for(int32 Index=0;Index<12;++Index)
    {
        const FVector2D Polar=LDBuildRing::SlotPolar(Index,44.f);
        const FIntPoint Key(FMath::RoundToInt(Polar.X),FMath::RoundToInt(Polar.Y*100.));
        TestFalse(FString::Printf(TEXT("Place %d is its own"),Index),Seen.Contains(Key));Seen.Add(Key);
        TestTrue(FString::Printf(TEXT("Place %d stays inside its sector"),Index),FMath::Abs(Polar.Y-44.f)<LDBuildRing::SectorHalf);
        for(int32 Other=0;Other<Index;++Other)
        {
            const FVector2D A=LDRingPaint::Polar(FVector2D::ZeroVector,float(Polar.X),float(Polar.Y));
            const FVector2D B0=LDBuildRing::SlotPolar(Other,44.f);
            const FVector2D B=LDRingPaint::Polar(FVector2D::ZeroVector,float(B0.X),float(B0.Y));
            TestTrue(FString::Printf(TEXT("Places %d and %d do not overlap"),Index,Other),FVector2D::Distance(A,B)>=LDBuildRing::NodeSize-.5f);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildRingLayoutTest,"Lockdown.Rules.BuildRingLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBuildRingLayoutTest::RunTest(const FString& Parameters)
{
    // The open ring over a level-16 build at 1920x1080 and 1280x720 with 100% and 125% text: every part on screen, no part
    // under the inspector's corner, the hand above the band, the wing rows apart, icons at least 36 physical pixels, and
    // links only between parts sharing a vocabulary key.
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if(!LDTestApprovedFixture::Read(*this,Catalog)){return false;}
    const FLDDemoContentDefinitions& Content=Catalog->Get();
    FName Table;
    if(!RingRegisterNames(*this,Content,Table)){return false;}
    ON_SCOPE_EXIT{if(!Table.IsNone()){FStringTableRegistry::Get().UnregisterStringTable(Table);}};
    if(!FSlateApplication::IsInitialized()){AddError(TEXT("The ring layout needs Slate."));return false;}
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("The layout fixture has a world"),World)){return false;}
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    ULDWorkshop* Ring=NewObject<ULDWorkshop>(World);Ring->Initialize();
    UWidgetTree* Tree=NewObject<UWidgetTree>();
    auto* Root=Tree->ConstructWidget<UOverlay>();Tree->RootWidget=Root;
    {auto* Slot=Root->AddChildToOverlay(Ring);Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);}
    const TSharedRef<SWidget> Slate=Root->TakeWidget();
    const FLDProgressionView View=RingFixtureView(Content);
    ULDWorkshop::FInput Input;Input.Mode=ULDWorkshop::EMode::Open;Input.bShown=true;Input.Progression=&View;Input.Match=1;Input.Content=&Content;
    Input.Stage=LDProgression::EMatchStage::Live;
    const FLDAttributeProjectionResult Projected=LDAttributeProjection::Project(FLDAttributeBuildValues(16,{}),*Catalog);
    if(!TestTrue(TEXT("The level-16 build projects"),Projected.IsSuccess())){return false;}
    for(const FLDAttributeContributions& Contribution:Projected.Value->Contributions)
    {
        FLDCombatAttributeRowView& Row=Input.Attributes.AddDefaulted_GetRef();Row.Attribute=uint8(Contribution.Attribute);Row.Final=Contribution.Final;
    }
    struct FCase{FVector2D Pixels;float Text;};
    for(const FCase& Case:{FCase{FVector2D(1920,1080),1.f},FCase{FVector2D(1920,1080),1.25f},FCase{FVector2D(1280,720),1.f},FCase{FVector2D(1280,720),1.25f}})
    {
        // The HUD lays out at 1080 units tall with the DPI scale the viewport gives.
        const float Scale=float(Case.Pixels.Y)/1080.f;
        const FVector2D Local=Case.Pixels/Scale;
        const FVector2D Origin(-1000000.,-1000000.);
        const FGeometry Geometry=FGeometry::MakeRoot(Local,FSlateLayoutTransform(Scale,Origin));
        Ring->ApplyTextScale(Case.Text);
        for(int32 Tick=0;Tick<3;++Tick)
        {
            Ring->Present(Input);
            Slate->SlatePrepass(Scale);
            FHittestGrid HitTest;FSlateWindowElementList Draw{TSharedPtr<SWindow>()};
            Slate->Paint(FPaintArgs(nullptr,HitTest,FVector2D::ZeroVector,1.,1.f/60.f),Geometry,FSlateRect(Origin.X,Origin.Y,Origin.X+Case.Pixels.X,Origin.Y+Case.Pixels.Y),Draw,0,FWidgetStyle(),true);
            Ring->NativeTick(Ring->GetCachedGeometry(),1.f/60.f);
        }
        const FString At=FString::Printf(TEXT("%.0fx%.0f text %.2f"),Case.Pixels.X,Case.Pixels.Y,Case.Text);
        const auto& L=Ring->Layout;
        TestTrue(At+TEXT(": the ring has its parts"),Ring->View().bValid&&L.Boxes.Num()>20);
        TestEqual(At+TEXT(": the wing lists the derived attributes"),Ring->View().Derived.Num(),LDBuildRing::WingAttributes().Num());
        TestTrue(At+TEXT(": card icons are at least 36 physical pixels"),LDBuildRing::NodeSize*L.R*L.K>=35.9f);
        TArray<FBox2D> WingRows;
        for(const ULDWorkshop::FShape& Box:L.Boxes)
        {
            if(Box.bWedge){continue;}
            FBox2D Bounds(ForceInit);
            for(const FVector2D& Point:Box.Shape){Bounds+=Point;}
            const FString Part=FString::Printf(TEXT(" part %d/%d/%d"),int32(Box.Hit.Kind),Box.Hit.Host,Box.Hit.Index);
            TestTrue(At+TEXT(": on screen")+Part,Bounds.Min.X>=-1.&&Bounds.Min.Y>=-1.&&Bounds.Max.X<=L.Size.X+1.&&Bounds.Max.Y<=L.Size.Y+1.);
            if(Box.Hit.Kind==ULDWorkshop::EHit::Wing)
            {
                TestTrue(At+TEXT(": a wing row stays under the inspector's corner")+Part,Bounds.Min.Y>=L.InspectorBottom);
                TestTrue(At+TEXT(": a wing row stays above the hand")+Part,Bounds.Max.Y<=L.HandTop);
                for(const FBox2D& Other:WingRows)
                {
                    const bool bApart=Bounds.Max.X<=Other.Min.X+.5||Other.Max.X<=Bounds.Min.X+.5||Bounds.Max.Y<=Other.Min.Y+.5||Other.Max.Y<=Bounds.Min.Y+.5;
                    TestTrue(At+TEXT(": wing rows do not overlap")+Part,bApart);
                }
                WingRows.Add(Bounds);
            }
            if(Box.Hit.Kind==ULDWorkshop::EHit::Hand){TestTrue(At+TEXT(": the hand stands above the band line")+Part,Bounds.Max.Y<=L.HandBottom+1.);}
        }
        // Links: every lit part shares a vocabulary key with the hovered one.
        for(int32 Host=0;Host<Ring->View().Hosts.Num();++Host)
        {
            for(int32 Index=0;Index<Ring->View().Hosts[Host].Cards.Num();++Index)
            {
                const uint32 Mask=LDBuildRing::CardVocab(&Content,Ring->View().Hosts[Host].Cards[Index].Definition);
                for(const uint64 Key:Ring->LinkedTo(ULDWorkshop::FHit{ULDWorkshop::EHit::Installed,Host,Index}))
                {
                    const int32 OtherHost=int32((Key>>32)&0xFFFF)-1,OtherIndex=int32(Key&0xFFFFFFFF)-1;
                    const ULDWorkshop::EHit Kind=static_cast<ULDWorkshop::EHit>(Key>>48);
                    const uint32 Other=Kind==ULDWorkshop::EHit::Host?LDBuildRing::ActiveVocab(&Content,Ring->View().Hosts[OtherHost].Active)
                        :LDBuildRing::CardVocab(&Content,Ring->View().Hosts[OtherHost].Cards[OtherIndex].Definition);
                    TestTrue(At+TEXT(": a link shares a vocabulary key"),(Mask&Other)!=0);
                }
            }
        }
    }
    TestTrue(TEXT("The fixture installs two copies of one body card as one face"),Ring->View().Hosts[0].Cards.ContainsByPredicate([](const LDBuildRing::FCard& Card){return Card.Copies==2;}));
    TestTrue(TEXT("The body socket holds the tier-2 medal"),Ring->View().Hosts[0].bHasMedal&&Ring->View().Hosts[0].Medal.Tier==2);
    TestTrue(TEXT("The hand lists the stored cards with the medal last"),!Ring->View().Hand.IsEmpty()&&Ring->View().Hand.Last().Form==ELDCardForm::Medal);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildRingIntentTest,"Lockdown.Rules.BuildRingIntent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBuildRingIntentTest::RunTest(const FString& Parameters)
{
    // A click is an intent through the existing command owner: a hand card is selected, a click on its host sends the
    // storage's InstallCard, a click on an installed card sends UninstallCard, and a card of another form is refused
    // locally with one line and no command.
    TSharedPtr<const FLDDemoContentSnapshot> Catalog;
    if(!LDTestApprovedFixture::Read(*this,Catalog)){return false;}
    const FLDDemoContentDefinitions& Content=Catalog->Get();
    FName Table;
    if(!RingRegisterNames(*this,Content,Table)){return false;}
    ON_SCOPE_EXIT{if(!Table.IsNone()){FStringTableRegistry::Get().UnregisterStringTable(Table);}};
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("The intent fixture has a world"),World)){return false;}
    ON_SCOPE_EXIT{World->DestroyWorld(false);};
    ULDWorkshop* Ring=NewObject<ULDWorkshop>(World);Ring->Initialize();Ring->TakeWidget();
    ULDComponentStorage* Storage=NewObject<ULDComponentStorage>(World);Storage->Initialize();Storage->TakeWidget();
    Storage->SetHeadless(true);
    TArray<FLDBuildCommand> Sent;
    Storage->SubmitBuild=[&Sent](const FLDBuildCommand& Command,ELDGrowthReason& Reason){Sent.Add(Command);Reason=ELDGrowthReason::None;return uint64(Sent.Num());};
    Storage->PeekRequestId=[&Sent](){return uint64(Sent.Num()+1);};
    TArray<FString> Lines;
    Ring->Notify=[&Lines](const FString& Line){Lines.Add(Line);};
    Ring->Storage=Storage;
    FLDProgressionView View=RingFixtureView(Content);
    // Make room: capacity above what is installed.
    View.CardCapacity=11;
    Storage->Present(&View,1,LDProgression::EMatchStage::Live,ELDLifePhase::Alive,&Content,LDHUD::FCardStats(),ULDComponentStorage::FAnchors(),true);
    ULDWorkshop::FInput Input;Input.Mode=ULDWorkshop::EMode::Open;Input.bShown=true;Input.Progression=&View;Input.Match=1;Input.Content=&Content;
    Input.Stage=LDProgression::EMatchStage::Live;
    Ring->Present(Input);
    const int32 Body=Ring->View().Hand.IndexOfByPredicate([](const LDBuildRing::FCard& Card){return Card.Form==ELDCardForm::Body;});
    if(!TestTrue(TEXT("The fixture stores a body card"),Body!=INDEX_NONE)){return false;}
    Ring->Click(ULDWorkshop::FHit{ULDWorkshop::EHit::Hand,INDEX_NONE,Body},false);
    TestTrue(TEXT("A hand click selects the card"),Ring->SelectedCard()&&Ring->SelectedCard()->Form==ELDCardForm::Body);
    Ring->Click(ULDWorkshop::FHit{ULDWorkshop::EHit::Host,2,INDEX_NONE},false);
    TestTrue(TEXT("A body card on a skill host is refused locally without a command"),Sent.IsEmpty()&&!Lines.IsEmpty());
    Ring->Click(ULDWorkshop::FHit{ULDWorkshop::EHit::Host,0,INDEX_NONE},false);
    TestTrue(TEXT("A body card on the body host sends the storage's install"),Sent.Num()==1&&Sent[0].Kind==ELDBuildCommandKind::InstallCard
        &&Sent[0].CardTarget==ELDCardTargetKind::Body&&Sent[0].ExpectedBuildRevision==View.BuildRevision);
    // The storage answers; a click on an installed card (nothing selected) takes one copy back.
    FLDSubmitResult Answer;Answer.Family=ELDGrowthFamily::Build;Answer.Generation=1;Answer.RequestId=1;Answer.Status=ELDAdmissionStatus::AdmissionRejected;
    Storage->HandleSubmit(Answer);
    Ring->Selected=NAME_None;
    Ring->Click(ULDWorkshop::FHit{ULDWorkshop::EHit::Installed,0,0},false);
    TestTrue(TEXT("A click on an installed card sends the storage's uninstall"),Sent.Num()==2&&Sent[1].Kind==ELDBuildCommandKind::UninstallCard);
    return true;
}
