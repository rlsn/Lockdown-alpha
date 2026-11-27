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
    // The scale rules: layout H / 1080; ring and text never under 2/3 (a card icon keeps 36 px and 18 px text keeps 12 px),
    // the text times the player's size.
    const LDBuildRing::FScale Full=LDBuildRing::ScaleFor(1080.f,1.f),Small=LDBuildRing::ScaleFor(720.f,1.f),Large=LDBuildRing::ScaleFor(720.f,1.25f);
    TestTrue(TEXT("1080p scales are one"),FMath::IsNearlyEqual(Full.Layout,1.f)&&FMath::IsNearlyEqual(Full.Ring,1.f)&&FMath::IsNearlyEqual(Full.Text,1.f));
    TestTrue(TEXT("720p scales the ring and the text with the layout"),FMath::IsNearlyEqual(Small.Layout,720.f/1080.f)
        &&FMath::IsNearlyEqual(Small.Ring,2.f/3.f)&&FMath::IsNearlyEqual(Small.Text,2.f/3.f));
    TestTrue(TEXT("The text size multiplies the text floor"),FMath::IsNearlyEqual(Large.Text,2.f/3.f*1.25f));
    TestTrue(TEXT("Card icons (the tile less its 2 px rules) stay at least 36 px at 720p"),(LDBuildRing::NodeSize-4.f)*Small.Ring>=35.9f);
    TestTrue(TEXT("The smallest text (18 design px) stays at least 12 px at 720p"),18.f*Small.Text>=11.9f);
    // The 3 + 4 + 5 places fill from the middle outwards and never move when another card comes or goes.
    TestEqual(TEXT("One place takes one row"),LDBuildRing::RowsFor(1),1);
    TestEqual(TEXT("Seven places take two rows"),LDBuildRing::RowsFor(7),2);
    TestEqual(TEXT("Twelve places take three rows"),LDBuildRing::RowsFor(12),3);
    TestEqual(TEXT("A thirteenth place opens the fourth row"),LDBuildRing::RowsFor(13),4);
    TestTrue(TEXT("Two rows keep the band's own edge"),FMath::IsNearlyEqual(LDBuildRing::OuterFor(7),LDBuildRing::BandOuter));
    TestTrue(TEXT("A third row pushes the sector's edge past it"),LDBuildRing::OuterFor(8)>=LDBuildRing::RowRadius[2]+LDBuildRing::NodeSize*.5f);
    const FVector2D First=LDBuildRing::SlotPolar(0,0.f);
    TestTrue(TEXT("The first place is the middle of the first row"),FMath::IsNearlyEqual(First.X,LDBuildRing::RowRadius[0])&&FMath::IsNearlyZero(First.Y));
    TestTrue(TEXT("The first row clears the host"),LDBuildRing::RowRadius[0]-LDBuildRing::NodeSize*.5f>LDBuildRing::HostRadius+LDBuildRing::HostSize*.5f);
    TestTrue(TEXT("The host clears the sector's inner rule"),LDBuildRing::HostRadius-LDBuildRing::HostSize*.5f>LDBuildRing::SectorInner);
    TestTrue(TEXT("Two rows sit inside the band's edge"),LDBuildRing::RowRadius[1]+LDBuildRing::NodeSize*.5f<LDBuildRing::BandOuter);
    TSet<FIntPoint> Seen;
    for(int32 Index=0;Index<17;++Index)
    {
        const FVector2D Polar=LDBuildRing::SlotPolar(Index,43.f);
        const FIntPoint Key(FMath::RoundToInt(Polar.X),FMath::RoundToInt(Polar.Y*100.));
        TestFalse(FString::Printf(TEXT("Place %d is its own"),Index),Seen.Contains(Key));Seen.Add(Key);
        // The whole tile stays inside its sector, not only its centre.
        const float Reach=FMath::Abs(float(Polar.Y)-43.f)+FMath::RadiansToDegrees(LDBuildRing::NodeSize*.5f/float(Polar.X));
        TestTrue(FString::Printf(TEXT("Place %d stays inside its sector"),Index),Reach<=LDBuildRing::SectorHalf);
        for(int32 Other=0;Other<Index;++Other)
        {
            const FVector2D A=LDRingPaint::Polar(FVector2D::ZeroVector,float(Polar.X),float(Polar.Y));
            const FVector2D B0=LDBuildRing::SlotPolar(Other,43.f);
            const FVector2D B=LDRingPaint::Polar(FVector2D::ZeroVector,float(B0.X),float(B0.Y));
            TestTrue(FString::Printf(TEXT("Places %d and %d do not overlap"),Index,Other),FVector2D::Distance(A,B)>=LDBuildRing::NodeSize-.5f);
        }
    }
    // Neighbouring sectors never share a degree.
    for(int32 Host=1;Host<LDBuildRing::HostCount;++Host)
    {
        TestTrue(FString::Printf(TEXT("Sectors %d and %d stay apart"),Host-1,Host),LDBuildRing::HostAngle[Host]-LDBuildRing::HostAngle[Host-1]>=2.f*LDBuildRing::SectorHalf);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLDBuildRingLayoutTest,"Lockdown.Rules.BuildRingLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLDBuildRingLayoutTest::RunTest(const FString& Parameters)
{
    // The open ring over a level-16 build at 1920x1080 and 1280x720 with 100% and 125% text, plain and with the most linked
    // card hovered: every part on screen, no side column, bar or row reaching into the band, the table and shop rows apart,
    // the row above the build bar, the hover card clear of the ring's cards, icons at least 36 physical pixels and text at
    // least 12, the table reading the real per-point mapping, and links only between parts sharing a vocabulary key.
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
    FLDProgressionView View=RingFixtureView(Content);
    ULDWorkshop::FInput Input;Input.Mode=ULDWorkshop::EMode::Open;Input.bShown=true;Input.Progression=&View;Input.Match=1;Input.Content=&Content;
    Input.Stage=LDProgression::EMatchStage::Live;
    const FLDAttributeProjectionResult Projected=LDAttributeProjection::Project(FLDAttributeBuildValues(16,{}),*Catalog);
    if(!TestTrue(TEXT("The level-16 build projects"),Projected.IsSuccess())){return false;}
    for(const FLDAttributeContributions& Contribution:Projected.Value->Contributions)
    {
        FLDCombatAttributeRowView& Row=Input.Attributes.AddDefaulted_GetRef();Row.Attribute=uint8(Contribution.Attribute);Row.Final=Contribution.Final;
    }
    struct FCase{FVector2D Pixels;float Text;};
    // The field first, then a rest with a stocked shop (seven items and the skill point) on the right wing.
    for(const bool bRest:{false,true})
    for(const FCase& Case:{FCase{FVector2D(1920,1080),1.f},FCase{FVector2D(1920,1080),1.25f},FCase{FVector2D(1280,720),1.f},FCase{FVector2D(1280,720),1.25f}})
    {
        if(bRest&&!View.Shop.bStocked)
        {
            View.Shop.bStocked=true;View.Shop.Revision=1;View.Shop.SkillPointPrice=3;View.Shop.SkillPointsMax=2;
            for(int32 Index=0;Index<7&&Content.Cards.IsValidIndex(Index);++Index)
            {
                FLDShopSlotView& Slot=View.Shop.Slots.AddDefaulted_GetRef();Slot.Kind=ELDShopItemKind::Card;Slot.DefinitionId=FName(*Content.Cards[Index].Id);Slot.Price=2;
            }
            Input.Stage=LDProgression::EMatchStage::Rest;
        }
        // The HUD lays out at 1080 units tall with the DPI scale the viewport gives.
        const float Scale=float(Case.Pixels.Y)/1080.f;
        const FVector2D Local=Case.Pixels/Scale;
        const FVector2D Origin(-1000000.,-1000000.);
        const FGeometry Geometry=FGeometry::MakeRoot(Local,FSlateLayoutTransform(Scale,Origin));
        // The HUD band (its 1080-unit place) and, in a rest, the two rest actions growing with the text size.
        const auto Absolute=[&](float X,float Y){return Origin+FVector2D(X,Y)*Scale;};
        {
            const FVector2D Min=Absolute(467.f,950.f),Max=Absolute(1453.f,1062.f);
            Input.Band=FSlateRect(Min.X,Min.Y,Max.X,Max.Y);
        }
        Input.Actions=bRest?FVector2D(360.f*Case.Text+40.f,66.f*Case.Text):FVector2D::ZeroVector;
        Ring->ApplyTextScale(Case.Text);
        for(int32 Tick=0;Tick<3;++Tick)
        {
            Ring->Present(Input);
            Slate->SlatePrepass(Scale);
            FHittestGrid HitTest;FSlateWindowElementList Draw{TSharedPtr<SWindow>()};
            Slate->Paint(FPaintArgs(nullptr,HitTest,FVector2D::ZeroVector,1.,1.f/60.f),Geometry,FSlateRect(Origin.X,Origin.Y,Origin.X+Case.Pixels.X,Origin.Y+Case.Pixels.Y),Draw,0,FWidgetStyle(),true);
            Ring->NativeTick(Ring->GetCachedGeometry(),1.f/60.f);
        }
        const FString At=FString::Printf(TEXT("%s %.0fx%.0f text %.2f"),bRest?TEXT("rest"):TEXT("field"),Case.Pixels.X,Case.Pixels.Y,Case.Text);
        const TArray<FString> Problems=Ring->LayoutProblems();
        TestTrue(At+TEXT(": the layout has no faults ")+FString::Join(Problems,TEXT("; ")),Problems.IsEmpty());
        if(bRest){TestEqual(At+TEXT(": the shop lists the shelf, the skill point and the medal"),Ring->View().Shop.Num(),9);}
        const auto& L=Ring->Layout;
        TestTrue(At+TEXT(": the ring has its parts"),Ring->View().bValid&&L.Boxes.Num()>20&&L.Segments.Num()==4);
        TestTrue(At+TEXT(": card icons are at least 36 physical pixels"),(LDBuildRing::NodeSize-4.f)*L.R*L.K>=35.9f);
        TestTrue(At+TEXT(": the stat chips' attribute line is at least 12 physical pixels"),FMath::Max(14.f,12.1f/(L.F*L.K))*L.F*L.K>=11.9f&&18.f*L.F*L.K>=11.9f);
        TestTrue(At+TEXT(": the build bar stands over the HUD band"),L.Bar.bIsValid&&L.Bar.Max.Y<=L.Size.Y-120.f*L.U);
        TestTrue(At+TEXT(": the rest actions' corner stays right of the bar"),Ring->ActionsAnchor().X>L.Bar.Max.X);
        // The hovered card of the most links: its card above the ring, still no faults.
        int32 BestHost=INDEX_NONE,BestIndex=INDEX_NONE,Most=-1;
        for(int32 Host=0;Host<Ring->View().Hosts.Num();++Host)
        {
            for(int32 Index=0;Index<Ring->View().Hosts[Host].Cards.Num();++Index)
            {
                const int32 Links=Ring->LinkCount(ULDWorkshop::EHit::Installed,Host,Index);
                if(Links>Most){Most=Links;BestHost=Host;BestIndex=Index;}
            }
        }
        if(TestTrue(At+TEXT(": the build has an installed card"),BestHost!=INDEX_NONE))
        {
            Ring->PinHover(ULDWorkshop::EHit::Installed,BestHost,BestIndex);
            Ring->NativeTick(Ring->GetCachedGeometry(),1.f/60.f);
            const TArray<FString> Hovered=Ring->LayoutProblems();
            TestTrue(At+TEXT(": the hovered layout has no faults ")+FString::Join(Hovered,TEXT("; ")),Hovered.IsEmpty());
            TestTrue(At+TEXT(": the hover card stands above the ring"),L.Card.bIsValid&&L.Card.Max.Y<=L.Centre.Y);
            Ring->PinHover(ULDWorkshop::EHit::None,INDEX_NONE,INDEX_NONE);
            Ring->NativeTick(Ring->GetCachedGeometry(),1.f/60.f);
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
    // The five-stat table reads the projection's own per-point mapping: each stat's attributes in the order StatEffects lists
    // them for the stat's hover, every one of them present.
    {
        const TArray<const FLDDemoStatDefinition*> Stats=Content.StatsInDesignOrder();
        TestEqual(TEXT("The table lists one row per stat"),Ring->View().Stats.Num(),Stats.Num());
        TestEqual(TEXT("The table lists every per-point attribute once"),Ring->View().Derived.Num(),LDBuildRing::StatAttributes(&Content).Num());
        for(int32 Stat=0;Stat<Stats.Num();++Stat)
        {
            TArray<FString> Expected,Shown;
            for(const LDHUD::FStatEffectView& Effect:LDHUD::StatEffects(*Stats[Stat],Input.Attributes)){Expected.AddUnique(Effect.Attribute);}
            for(const LDBuildRing::FDerived& Row:Ring->View().Derived){if(Row.Source==Stat){Shown.Add(Row.Name);}}
            TestEqual(FString::Printf(TEXT("Stat %d's row reads the attributes its points raise"),Stat),FString::Join(Shown,TEXT(",")),FString::Join(Expected,TEXT(",")));
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
    ULDComponentStorage* Storage=NewObject<ULDComponentStorage>(World);
    TArray<FLDBuildCommand> Sent;
    Storage->SubmitBuild=[&Sent](const FLDBuildCommand& Command,ELDGrowthReason& Reason){Sent.Add(Command);Reason=ELDGrowthReason::None;return uint64(Sent.Num());};
    Storage->PeekRequestId=[&Sent](){return uint64(Sent.Num()+1);};
    TArray<FString> Lines;
    Ring->Notify=[&Lines](const FString& Line){Lines.Add(Line);};
    Ring->Storage=Storage;
    FLDProgressionView View=RingFixtureView(Content);
    // Make room: capacity above what is installed.
    View.CardCapacity=11;
    Storage->Present(&View,1,LDProgression::EMatchStage::Live,ELDLifePhase::Alive,&Content,true);
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
