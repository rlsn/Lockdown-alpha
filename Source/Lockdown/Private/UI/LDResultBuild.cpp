#include "UI/LDResultBuild.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDBuildBlueprint.h"
#include "Engine/Texture2D.h"
#include "Framework/LDCaptureTypes.h"
#include "UI/LDHUDModel.h"
#include "UI/LDBroadcastStyle.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "UI/LDUIIcons.h"
#include "UI/LDUIShapes.h"
#include "UI/LDWidgets.h"
#include "UI/LDWorkshopModel.h"

namespace
{
ELDCardTargetKind GrowthTarget(ELDResultBuildTarget Kind)
{
    switch (Kind)
    {
    case ELDResultBuildTarget::Active: return ELDCardTargetKind::Skill;
    case ELDResultBuildTarget::Dash: return ELDCardTargetKind::Dash;
    default: return ELDCardTargetKind::Body;
    }
}

UTextBlock* BoardText(UWidgetTree* Tree, const FString& Text, int32 Size)
{
    UTextBlock* Label = LDWidgets::Text(Tree, Text, Size, NAME_None, LDUIFonts::ERole::Strong);
    Label->SetAutoWrapText(false);
    Label->SetJustification(ETextJustify::Center);
    Label->SetColorAndOpacity(LDHUDStyle::TextSecondary);
    return Label;
}

UWidget* BoardIcon(UWidgetTree* Tree, const FLDDemoContentDefinitions* Content, FName Id, float Size, bool bComponent,
    const FString& OverrideIcon = FString())
{
    auto* Box = Tree->ConstructWidget<USizeBox>();
    Box->SetWidthOverride(Size);
    Box->SetHeightOverride(Size);
    auto* Overlay = Tree->ConstructWidget<UOverlay>();
    Box->SetContent(Overlay);
    const FName Definition = bComponent ? LDHUD::CardDefinition(Id) : Id;
    const FString Name = Id.IsNone() ? FString() : LDHUD::ContentName(Content, Definition.ToString());
    const FString IconId = OverrideIcon.IsEmpty() && !Id.IsNone() ? LDHUD::ContentIconId(Content, Definition.ToString()) : OverrideIcon;
    auto* Image = Tree->ConstructWidget<UImage>();
    const bool bHasIcon = !IconId.IsEmpty() && LDUIIcons::Apply(Image, IconId, 0.f);
    // Keep the brush's texture alive without painting a square behind the shaped tile.
    Image->SetVisibility(ESlateVisibility::Collapsed);
    Overlay->AddChildToOverlay(Image);
    const LDWorkshop::FCardFacts Facts = bComponent ? LDWorkshop::CardFacts(Id, Content) : LDWorkshop::FCardFacts();
    auto* Face = Tree->ConstructWidget<ULDPaintLayer>();
    const TWeakObjectPtr<UImage> Icon(Image);
    Face->Painter = [Icon, bComponent, Kind = uint8(Facts.Type), bHasIcon](const LDUIPaint::FContext& Paint)
    {
        UTexture2D* Texture = Icon.IsValid() ? Cast<UTexture2D>(Icon->GetBrush().GetResourceObject()) : nullptr;
        const FBox2D Rect(FVector2D::ZeroVector, FVector2D(Paint.Geometry.GetLocalSize()));
        if (bComponent) { Paint.Tile(Paint.Layer, Rect, Kind, LDUIPaint::ETileState::Normal, Texture); }
        else { Paint.SkillArt(Paint.Layer, Rect, false, !bHasIcon, Texture); }
    };
    auto* FaceSlot = Overlay->AddChildToOverlay(Face);
    FaceSlot->SetHorizontalAlignment(HAlign_Fill);
    FaceSlot->SetVerticalAlignment(VAlign_Fill);
    if (!bHasIcon && !Name.IsEmpty())
    {
        UTextBlock* Fallback = BoardText(Tree, LDHUD::IconGlyph(Name), FMath::Max(8, FMath::RoundToInt(Size * .5f)));
        auto* Slot = Overlay->AddChildToOverlay(Fallback);
        Slot->SetHorizontalAlignment(HAlign_Center);
        Slot->SetVerticalAlignment(VAlign_Center);
    }
    if (!Name.IsEmpty()) { Box->SetToolTipText(FText::FromString(Name)); }
    return Box;
}
}

UWidget* LDResultBuild::Make(UWidgetTree* Tree, const FLDResultBuild& Build, const FLDDemoContentDefinitions* Content,
    float Width, float Height, float TextScale)
{
    auto* Bounds = Tree->ConstructWidget<USizeBox>();
    Bounds->SetWidthOverride(Width);
    Bounds->SetHeightOverride(Height);
    FString Error;
    if (!Build.bAvailable) { Error = TEXT("构筑摘要不可用"); }
    else if (!Content) { Error = TEXT("本地构筑目录不可用"); }
    else if (!Build.CatalogRoot.Equals(Content->Identity.Root.ToHex(), ESearchCase::IgnoreCase)) { Error = TEXT("构筑目录不匹配"); }
    else if (!LDCapture::ValidateResultBuild(Build, Error)) { Error = TEXT("构筑摘要无效"); }
    if (!Error.IsEmpty())
    {
        Bounds->SetContent(BoardText(Tree, Error, FMath::RoundToInt(12 * TextScale)));
        return Bounds;
    }

    FLDProgressionView Progression;
    Progression.bValid = true;
    Progression.Level = 1;
    for (const FLDResultBuildActive& Active : Build.Actives)
    {
        FLDActiveSlotView& Row = Progression.Actives.AddDefaulted_GetRef();
        Row.DefinitionId = Active.DefinitionId;
        Row.Slot = Active.Slot;
        Row.Branch = Active.Branch;
    }
    for (const FLDResultBuildCard& Card : Build.Cards)
    {
        FLDCardInstanceView& Row = Progression.Cards.AddDefaulted_GetRef();
        Row.Serial = uint16(Progression.Cards.Num());
        Row.DefinitionId = Card.DefinitionId;
        Row.Target.Kind = GrowthTarget(Card.Target);
        Row.Target.Active = Card.TargetId;
        Row.Target.Element = Card.Element;
    }
    LDHUD::FStorageView Board = LDHUD::CardStorage(Progression, Content);
    // The result board lists a column's element medal first, with its cards.
    for (LDHUD::FStorageColumnView& Column : Board.Columns) { if (Column.bHasMedal) { Column.Entries.Insert(Column.Medal, 0); } }
    int32 Most = 0;
    for (const LDHUD::FStorageColumnView& Column : Board.Columns) { Most = FMath::Max(Most, Column.Entries.Num()); }
    const float ColumnWidth = Width / float(LDHUD::StorageColumnCount);
    const bool bExpanded = Height >= 180.f;
    const bool bShowStats = bExpanded && Height >= 205.f && !Build.StatPoints.IsEmpty();
    const float StatStripHeight = bShowStats ? 26.f : 0.f;
    const float BoardHeight = Height - StatStripHeight;
    const float Header = bExpanded ? 64.f : FMath::Min(BoardHeight * .34f, ColumnWidth * .36f);
    const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(Most, 2));
    const float TileSize = bExpanded ? 54.f
        : FMath::Clamp(FMath::Min((ColumnWidth - 4.f) / 2.f, (BoardHeight - Header - 4.f) / Rows - 1.f), 4.f, Header * .72f);

    // A final mod binds no source: it sits on the body column.
    const int32 FinalColumn = Build.FinalMod.IsNone() ? INDEX_NONE : int32(LDHUD::EStorageColumn::Body);
    auto* Outer = Tree->ConstructWidget<UVerticalBox>();
    auto* Columns = Tree->ConstructWidget<UHorizontalBox>();
    Outer->AddChildToVerticalBox(Columns);
    Bounds->SetContent(Outer);
    // 普攻, Q, W, E, D, 本体 left to right, like the workshop board.
    for (int32 Position = 0; Position < LDHUD::StorageColumnCount; ++Position)
    {
        const LDHUD::EStorageColumn ColumnId = LDHUD::BoardColumn(Position);
        const int32 Index = int32(ColumnId);
        if (!Board.Columns.IsValidIndex(Index)) { continue; }
        const LDHUD::FStorageColumnView& Column = Board.Columns[Index];
        auto* ColumnBox = Tree->ConstructWidget<USizeBox>();
        ColumnBox->SetWidthOverride(ColumnWidth);
        auto* Stack = Tree->ConstructWidget<UVerticalBox>();
        ColumnBox->SetContent(Stack);
        Columns->AddChildToHorizontalBox(ColumnBox);
        auto* Head = Tree->ConstructWidget<UHorizontalBox>();
        Stack->AddChildToVerticalBox(Head);
        auto* Label = BoardText(Tree, LDHUD::ColumnLabel(ColumnId), FMath::Max(8, FMath::RoundToInt(Header * .37f * TextScale)));
        Head->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);
        if (ColumnId != LDHUD::EStorageColumn::Body)
        {
            UWidget* Icon = BoardIcon(Tree, Content, Column.Active, Header, false,
                ColumnId == LDHUD::EStorageColumn::Dash ? FString(LDUIIcons::Dash) : FString());
            Head->AddChildToHorizontalBox(Icon)->SetPadding(FMargin(2, 0, 0, 0));
            if (!Column.Active.IsNone())
            {
                const FLDResultBuildActive* FoundActive = Build.Actives.FindByPredicate([&Column](const FLDResultBuildActive& A)
                { return A.DefinitionId == Column.Active; });
                if (FoundActive)
                {
                    const uint8 Rank = LDSkillBranch::RankOf(FoundActive->Branch);
                    FString Tip = LDHUD::ContentName(Content, Column.Active.ToString()) + FString::Printf(TEXT(" · %s %u"), LDTerms::SkillRank, uint32(Rank));
                    if (FoundActive->Branch != ELDSkillBranch::None)
                    {
                        const LDHUD::FBranchCardView BranchView = LDHUD::BranchCard(Content, Column.Active, FoundActive->Branch, LDHUD::FCardStats());
                        if (!BranchView.Name.IsEmpty()) { Tip += TEXT("（") + BranchView.Name + TEXT("）"); }
                    }
                    Icon->SetToolTipText(FText::FromString(Tip));
                    if (bExpanded)
                    {
                        UTextBlock* Dots = BoardText(Tree, LDHUD::RankDots(Rank), FMath::Max(8, FMath::RoundToInt(9 * TextScale)));
                        Dots->SetColorAndOpacity(Rank >= 3 ? LDBroadcast::Gold : LDBroadcast::White);
                        auto* DotsSlot = Head->AddChildToHorizontalBox(Dots);
                        DotsSlot->SetVerticalAlignment(VAlign_Bottom);
                        DotsSlot->SetPadding(FMargin(2, 0, 0, 0));
                    }
                }
            }
        }
        if (FinalColumn == Index)
        {
            UWidget* Final = BoardIcon(Tree, Content, Build.FinalMod, bExpanded ? 54.f : Header * .65f, false);
            Head->AddChildToHorizontalBox(Final)->SetPadding(FMargin(2, 0, 0, 0));
        }
        auto* Grid = Tree->ConstructWidget<UWrapBox>();
        Grid->SetExplicitWrapSize(true);
        Grid->SetWrapSize(ColumnWidth - 2.f);
        Grid->SetInnerSlotPadding(FVector2D(1.f, 1.f));
        Stack->AddChildToVerticalBox(Grid)->SetPadding(FMargin(0, 2, 0, 0));
        const int32 Visible = bExpanded && Column.Entries.Num() > 4 ? 3 : Column.Entries.Num();
        for (int32 EntryIndex = 0; EntryIndex < Visible; ++EntryIndex)
        {
            const LDHUD::FEquippedView& Entry = Column.Entries[EntryIndex];
            UWidget* Tile = BoardIcon(Tree, Content, Entry.Card, TileSize, true);
            if (Entry.Copies > 1) { Tile->SetToolTipText(FText::FromString(LDHUD::ContentName(Content, LDHUD::CardDefinition(Entry.Card).ToString())
                + TEXT(" ") + LDTerms::CopiesBadge(Entry.Copies))); }
            Grid->AddChildToWrapBox(Tile);
        }
        if (Visible < Column.Entries.Num())
        {
            auto* More = Tree->ConstructWidget<USizeBox>();
            More->SetWidthOverride(TileSize);More->SetHeightOverride(TileSize);
            More->SetContent(BoardText(Tree,FString::Printf(TEXT("+%d"),Column.Entries.Num()-Visible),FMath::RoundToInt(20*TextScale)));
            TArray<FString> Names;
            for(int32 EntryIndex=Visible;EntryIndex<Column.Entries.Num();++EntryIndex)
            {Names.Add(LDHUD::ContentName(Content,LDHUD::CardDefinition(Column.Entries[EntryIndex].Card).ToString()));}
            More->SetToolTipText(FText::FromString(FString::Join(Names,TEXT("\n"))));
            Grid->AddChildToWrapBox(More);
        }
    }
    if (bShowStats)
    {
        auto* StatRow = Tree->ConstructWidget<UHorizontalBox>();
        const TArray<const FLDDemoStatDefinition*> StatOrder = Content->StatsInDesignOrder();
        const int32 Threshold = int32(FMath::Max<int64>(1, Content->Parameters.KeystoneThresholdPoints));
        for (int32 StatIdx = 0; StatIdx < StatOrder.Num() && Build.StatPoints.IsValidIndex(StatIdx); ++StatIdx)
        {
            const int32 Pts = int32(Build.StatPoints[StatIdx]);
            const FString StatName = LDHUD::ContentName(Content, StatOrder[StatIdx]->Id);
            const FString EntryText = FString::Printf(TEXT("%s %d%s"), *StatName, Pts, Pts >= Threshold ? TEXT("★") : TEXT(""));
            UTextBlock* Chip = BoardText(Tree, EntryText, FMath::Max(10, FMath::RoundToInt(13 * TextScale)));
            Chip->SetColorAndOpacity(Pts >= Threshold ? LDBroadcast::Gold : LDBroadcast::White);
            StatRow->AddChildToHorizontalBox(Chip)->SetPadding(FMargin(StatIdx ? 14.f : 0.f, 0, 0, 0));
        }
        Outer->AddChildToVerticalBox(StatRow)->SetPadding(FMargin(0, 6, 0, 0));
    }
    return Bounds;
}

namespace
{
FString ReportP2(int64 Value)
{
    return FText::AsNumber(double(Value)/100.).ToString();
}

UTextBlock* ReportLine(UWidgetTree* Tree,UVerticalBox* Body,const FString& Value,float TextScale,bool bHeading=false)
{
    auto* Line=LDWidgets::Text(Tree,Value,FMath::RoundToInt((bHeading?17.f:14.f)*TextScale),NAME_None,
        bHeading?LDUIFonts::ERole::Strong:LDUIFonts::ERole::Body);
    Line->SetAutoWrapText(true);
    Line->SetColorAndOpacity(bHeading?LDBroadcast::White:LDBroadcast::Grey);
    Body->AddChildToVerticalBox(Line)->SetPadding(FMargin(0,bHeading?9.f:3.f,8.f,2.f));
    return Line;
}

void AddComparison(UWidgetTree* Tree,UVerticalBox* Body,const FLDResultView& Result,const FGuid& Selected,float TextScale)
{
    ReportLine(Tree,Body,TEXT("对玩家伤害　物理 / 法术 / 元素反应"),TextScale,true);
    for(const FLDResultRow& Row:Result.Rows)
    {
        const FLDResultCombat& Combat=Row.Combat;
        ReportLine(Tree,Body,Row.ContenderId==Selected?Row.Name+TEXT(" ◆"):Row.Name,TextScale,true);
        if(!Combat.bAvailable)
        {
            ReportLine(Tree,Body,TEXT("战斗数据不可用"),TextScale);
            continue;
        }
        const int64 Physical=Combat.PhysicalToPlayersP2, Magical=Combat.MagicalToPlayersP2, Reaction=Combat.ReactionToPlayersP2;
        ReportLine(Tree,Body,FString::Printf(TEXT("%s　（%s / %s / %s）"),*ReportP2(Physical+Magical+Reaction),
            *ReportP2(Physical),*ReportP2(Magical),*ReportP2(Reaction)),TextScale);
        if(Physical+Magical+Reaction>0)
        {
            auto* Bar=Tree->ConstructWidget<ULDPaintLayer>();
            Bar->Painter=[Physical,Magical,Reaction](const LDUIPaint::FContext& Paint)
            {
                const double Total=double(Physical)+double(Magical)+double(Reaction);
                const FVector2D Size(Paint.Geometry.GetLocalSize());
                const double PhysicalEnd=Size.X*Physical/Total;
                const double MagicalEnd=Size.X*(Physical+Magical)/Total;
                const auto Segment=[&](double Left,double Right,const FLinearColor& Color)
                {
                    if(Right<=Left){return;}
                    Paint.Shape(Paint.Layer,LDUIShapes::EShape::Rounded,FBox2D(FVector2D(Left,0),FVector2D(Right,Size.Y)),
                        2.f,LDUIPaint::FFill::Solid(Color));
                };
                Segment(0,PhysicalEnd,LDHUDStyle::RewardActiveRim);
                Segment(PhysicalEnd,MagicalEnd,LDHUDStyle::RewardCardRim);
                Segment(MagicalEnd,Size.X,LDHUDStyle::RewardStoneRim);
            };
            auto* Height=Tree->ConstructWidget<USizeBox>();
            Height->SetHeightOverride(7.f);
            Height->SetContent(Bar);
            Body->AddChildToVerticalBox(Height)->SetPadding(FMargin(0,2,16,4));
        }
        ReportLine(Tree,Body,FString::Printf(TEXT("承伤 %s　护盾抵挡 %s　治疗 %s（恢复球 %s）"),
            *ReportP2(Combat.DamageTakenP2),*ReportP2(Combat.ShieldAbsorbedP2),*ReportP2(Combat.HealingP2),*ReportP2(Combat.RecoveryPackP2)),TextScale);
        ReportLine(Tree,Body,FString::Printf(TEXT("反应 %lld 次　最高爆发 %s　涂鸦守点 %s 秒"),
            Combat.Reactions,*ReportP2(Combat.PeakReactionP2),*FText::AsNumber(double(Row.BankedGuardMs)/1000.).ToString()),TextScale);
    }
}

void AddTimeline(UWidgetTree* Tree,UVerticalBox* Body,const FLDResultRow& Row,const FLDDemoContentDefinitions* Content,float TextScale)
{
    if(Row.Growth.IsEmpty()){ReportLine(Tree,Body,TEXT("成长记录不可用"),TextScale);}
    for(int32 Index=0;Index<Row.Growth.Num();++Index)
    {
        const FLDResultGrowthPhase& Phase=Row.Growth[Index];
        const FString Heading=Index==0?FString(TEXT("开局")):Index>FLDTimelineParameters::TimedSegments?FString(TEXT("终局"))
            :FString::Printf(TEXT("R%d"),Index);
        ReportLine(Tree,Body,Heading,TextScale,true);
        ReportLine(Tree,Body,FString::Printf(TEXT("加点 %lld　退回 %lld　买牌 %lld　买技能 %lld　刷新 %lld"),
            Phase.Allocated,Phase.Refunded,Phase.BoughtCards,Phase.BoughtSkills,Phase.RefreshedShop),TextScale);
        TArray<FString> Skills;
        for(const FLDResultBuildActive& Skill:Phase.Skills)
        {
            const FString Branch=LDBuildBlueprint::BranchText(Skill.Branch);
            Skills.Add(FString::Printf(TEXT("%s %s %s"),*LDHUD::FSlotKeys().Of(Skill.Slot),
                *LDHUD::ContentName(Content,Skill.DefinitionId.ToString()),*Branch).TrimEnd());
        }
        if(!Skills.IsEmpty()){ReportLine(Tree,Body,FString::Join(Skills,TEXT(" → ")),TextScale);}
        if(!Phase.AcquiredCards.IsEmpty())
        {
            ReportLine(Tree,Body,FString::JoinBy(Phase.AcquiredCards,TEXT("、"),[Content](FName Id){return LDHUD::ContentName(Content,Id.ToString());}),TextScale);
        }
        if(!Phase.FinalMod.IsNone()){ReportLine(Tree,Body,LDHUD::ContentName(Content,Phase.FinalMod.ToString()),TextScale);}
    }
    if(!Row.Combat.bAvailable){ReportLine(Tree,Body,TEXT("卡牌贡献不可用"),TextScale);return;}
    TArray<FName> Cards;
    for(const FLDResultBuildCard& Card:Row.Build.Cards){Cards.AddUnique(Card.DefinitionId);}
    for(const FLDResultCardContribution& Card:Row.Combat.Cards){Cards.AddUnique(Card.Card);}
    if(Cards.IsEmpty()){return;}
    ReportLine(Tree,Body,TEXT("卡牌贡献"),TextScale,true);
    const auto Contribution=[&Row](FName Id)
    {
        const FLDResultCardContribution* Entry=Row.Combat.Cards.FindByPredicate([Id](const FLDResultCardContribution& Card){return Card.Card==Id;});
        return Entry?*Entry:FLDResultCardContribution();
    };
    Cards.StableSort([&](FName A,FName B)
    {
        const auto Left=Contribution(A),Right=Contribution(B);
        if(Left.DamageP2!=Right.DamageP2){return Left.DamageP2>Right.DamageP2;}
        if(Left.ShieldP2!=Right.ShieldP2){return Left.ShieldP2>Right.ShieldP2;}
        return Left.Triggers>Right.Triggers;
    });
    for(FName Card:Cards)
    {
        const FLDResultCardContribution Entry=Contribution(Card);
        ReportLine(Tree,Body,FString::Printf(TEXT("%s　伤害 %s　触发 %lld 次　护盾 %s"),
            *LDHUD::ContentName(Content,Card.ToString()),*ReportP2(Entry.DamageP2),Entry.Triggers,*ReportP2(Entry.ShieldP2)),TextScale);
    }
}
}

UWidget* LDResultBuild::MakeReport(UWidgetTree* Tree,const FLDResultView& Result,const FLDResultRow& Row,
    const FLDDemoContentDefinitions* Content,EReportTab Tab,float Width,float TextScale)
{
    auto* Size=Tree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(Width);
    Size->SetHeightOverride(282.f);
    // No scroll in game UI: the report scales down as one piece when a tab holds more than the board's height.
    auto* Fit=Tree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);
    Size->SetContent(Fit);
    auto* Body=Tree->ConstructWidget<UVerticalBox>();
    Fit->SetContent(Body);
    if(auto* FitSlot=Cast<UScaleBoxSlot>(Fit->GetContentSlot())){FitSlot->SetHorizontalAlignment(HAlign_Left);FitSlot->SetVerticalAlignment(VAlign_Top);}
    if(Tab==EReportTab::Comparison){AddComparison(Tree,Body,Result,Row.ContenderId,TextScale);return Size;}
    if(Tab==EReportTab::Timeline){AddTimeline(Tree,Body,Row,Content,TextScale);return Size;}

    Body->AddChildToVerticalBox(Make(Tree,Row.Build,Content,Width-18.f,198.f,TextScale));
    if(Content&&Row.Build.bAvailable&&Row.Combat.bAvailable)
    {
        // The five stats side by side: name and points over a meter of the keystone threshold.
        const auto Stats=Content->StatsInDesignOrder();
        auto* StatRow=Tree->ConstructWidget<UHorizontalBox>();
        Body->AddChildToVerticalBox(StatRow)->SetPadding(FMargin(0,6,0,0));
        for(int32 Index=0;Index<Stats.Num()&&Row.Build.StatPoints.IsValidIndex(Index);++Index)
        {
            const FName StatId(*Stats[Index]->Id);
            FString Text=FString::Printf(TEXT("%s %d"),*LDHUD::ContentName(Content,Stats[Index]->Id),Row.Build.StatPoints[Index]);
            const bool bKeystone=(Row.Combat.KeystoneMask&(1<<Index))!=0;
            if(bKeystone)
            {
                if(const TCHAR* Keystone=LDTerms::KeystoneName(StatId)){Text+=FString::Printf(TEXT(" ★%s"),Keystone);}
            }
            auto* Column=Tree->ConstructWidget<UVerticalBox>();
            UHorizontalBoxSlot* ColumnSlot=StatRow->AddChildToHorizontalBox(Column);ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            ColumnSlot->SetPadding(FMargin(0,0,10,0));
            ReportLine(Tree,Column,Text,TextScale,true);
            const int32 Cells=int32(Content->Parameters.KeystoneThresholdPoints);
            const int32 Lit=FMath::Clamp(Row.Build.StatPoints[Index],0,Cells);
            if(Cells<=0){continue;}
            auto* Meter=Tree->ConstructWidget<ULDPaintLayer>();
            Meter->Painter=[Cells,Lit,bKeystone](const LDUIPaint::FContext& Paint)
            {
                const FVector2D Size(Paint.Geometry.GetLocalSize());
                const double Step=Size.X/Cells;
                for(int32 Cell=0;Cell<Cells;++Cell)
                {
                    const FLinearColor Tint=Cell<Lit?(bKeystone?LDBroadcast::Gold:LDBroadcast::White):LDBroadcast::Raised;
                    Paint.Fill(Paint.Layer,FBox2D(FVector2D(Step*Cell,0),FVector2D(Step*(Cell+1)-1.,Size.Y)),Tint,0.f);
                }
            };
            auto* Strip=Tree->ConstructWidget<USizeBox>();
            Strip->SetHeightOverride(7.f);Strip->SetContent(Meter);
            Column->AddChildToVerticalBox(Strip)->SetPadding(FMargin(0,2,0,3));
        }
    }
    if(!Row.Combat.bAvailable){ReportLine(Tree,Body,TEXT("战斗属性不可用"),TextScale);return Size;}
    TArray<FString> Attributes;
    for(const FLDResultAttribute& Attribute:Row.Combat.Attributes)
    {
        FLDCombatAttributeRowView Value;
        Value.Attribute=Attribute.Attribute;
        Value.Final=Attribute.Value;
        const auto Text=LDHUD::AttributeRow(Value);
        Attributes.Add(Text.Name+TEXT(" ")+Text.Value);
    }
    if(!Attributes.IsEmpty()){ReportLine(Tree,Body,FString::Join(Attributes,TEXT("　")),TextScale);}
    return Size;
}
