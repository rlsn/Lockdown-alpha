#include "UI/LDChoicesPanel.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"

namespace
{
const FLinearColor ChoicesIdle(.045f,.12f,.15f),ChoicesSelected(.26f,.2f,.05f);
// Compact card geometry (1080p reference units): every card has the same one-name, one-summary layout, so the three share
// one height and never scroll; larger text preferences grow all three alike.
constexpr float ChoicesPanelScale=.8f;
constexpr float ChoicesCardWidth=250.f,ChoicesCardHeight=64.f,ChoicesIconSize=44.f,ChoicesDetailWidth=300.f;
void ChoicesTint(ULDCommandButton* Button,const FLinearColor& Color)
{
    auto Style=Button->GetStyle();Style.Normal.TintColor=FSlateColor(Color);Style.Hovered.TintColor=FSlateColor(Color*1.6f);Button->SetStyle(Style);
}
void ChoicesText(UTextBlock* Text,const FString& Value)
{
    if(Text&&!Text->GetText().ToString().Equals(Value,ESearchCase::CaseSensitive)){Text->SetText(FText::FromString(Value));}
}
// Placeholder icons until real art lands (#93): actives are rounded squares, passives circles, attribute cards diamonds,
// each in its own colour with the name's first character; the summary line names the kind as well.
void ChoicesIcon(USizeBox* Size,UBorder* Shape,LDHUD::EChoiceIcon Icon)
{
    using LDHUD::EChoiceIcon;
    const bool bDiamond=Icon==EChoiceIcon::Attribute;
    const float Side=bDiamond?ChoicesIconSize*.7f:ChoicesIconSize;
    const float Radius=Icon==EChoiceIcon::Passive?Side*.5f:(bDiamond?3.f:7.f);
    const FLinearColor Fill=Icon==EChoiceIcon::Active?FLinearColor(.62f,.22f,.04f):Icon==EChoiceIcon::Passive?FLinearColor(.26f,.10f,.58f)
        :Icon==EChoiceIcon::Attribute?FLinearColor(.06f,.40f,.14f):FLinearColor(.12f,.13f,.15f);
    Size->SetWidthOverride(Side);Size->SetHeightOverride(Side);
    Shape->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,Radius,FLinearColor(1,1,1,.55f),1.5f));Shape->SetBrushColor(Fill);
    Shape->SetRenderTransformPivot(FVector2D(.5f,.5f));Shape->SetRenderTransformAngle(bDiamond?45.f:0.f);
}
}

TSharedRef<SWidget> ULDChoicesPanel::RebuildWidget()
{
    if(!MainPanel)
    {
        auto Text=[this](const FString& Value,int32 Size){return LDWidgets::Text(WidgetTree,Value,Size);};
        auto OneLine=[&Text](int32 Size){UTextBlock* Line=Text(TEXT(""),Size);Line->SetAutoWrapText(false);Line->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);return Line;};
        auto* Root=WidgetTree->ConstructWidget<UVerticalBox>();
        // No header and no close button: the HUD's blind-box button shows the pending count and toggles the panel, and Back
        // closes it like every main panel. #97: the three compact cards sit side by side in one low row next to that
        // button, with the free reroll as a fourth card; a click on a card is the choice, with no separate confirm button.
        auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
        Root->AddChildToVerticalBox(Row);
        for(int32 Index=0;Index<4;++Index)
        {
            const bool bReroll=Index==3;
            FCardWidgets& Card=bReroll?RerollCard:Cards.AddDefaulted_GetRef();
            auto* Face=WidgetTree->ConstructWidget<UHorizontalBox>();
            auto* IconBox=WidgetTree->ConstructWidget<UOverlay>();
            Card.IconShape=WidgetTree->ConstructWidget<UBorder>();Card.IconShape->SetVisibility(ESlateVisibility::HitTestInvisible);
            Card.IconSize=WidgetTree->ConstructWidget<USizeBox>();Card.IconSize->SetContent(Card.IconShape);
            auto* ShapeSlot=IconBox->AddChildToOverlay(Card.IconSize);ShapeSlot->SetHorizontalAlignment(HAlign_Center);ShapeSlot->SetVerticalAlignment(VAlign_Center);
            Card.Glyph=Text(TEXT(""),18);Card.Glyph->SetAutoWrapText(false);Card.Glyph->SetJustification(ETextJustify::Center);
            auto* GlyphSlot=IconBox->AddChildToOverlay(Card.Glyph);GlyphSlot->SetHorizontalAlignment(HAlign_Center);GlyphSlot->SetVerticalAlignment(VAlign_Center);
            auto* IconFrame=WidgetTree->ConstructWidget<USizeBox>();IconFrame->SetWidthOverride(ChoicesIconSize);IconFrame->SetHeightOverride(ChoicesIconSize);IconFrame->SetContent(IconBox);
            auto* IconSlot=Face->AddChildToHorizontalBox(IconFrame);IconSlot->SetVerticalAlignment(VAlign_Center);IconSlot->SetPadding(FMargin(0,0,10,0));
            auto* Lines=WidgetTree->ConstructWidget<UVerticalBox>();
            Card.Name=OneLine(17);Lines->AddChildToVerticalBox(Card.Name);
            Card.Summary=OneLine(13);Card.Summary->SetColorAndOpacity(LDHUDStyle::TextAccent);Lines->AddChildToVerticalBox(Card.Summary)->SetPadding(FMargin(0,2,0,0));
            auto* LinesSlot=Face->AddChildToHorizontalBox(Lines);LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));LinesSlot->SetVerticalAlignment(VAlign_Center);
            Card.Button=bReroll?LDWidgets::Button(WidgetTree,TEXT(""),[this](){Reroll();},false,TEXT("ChoicesReroll"))
                :LDWidgets::Button(WidgetTree,TEXT(""),[this,Index](){ChooseCard(Index);},false,FName(*FString::Printf(TEXT("ChoiceCard%d"),Index)));
            Card.Button->SetContent(Face);
            // After SetContent: replacing the caption makes a new, centred slot, which would shrink the icon and lines to
            // their own width in the middle of the card; they start at the card's left edge instead.
            if(UButtonSlot* FaceSlot=Cast<UButtonSlot>(Card.Button->GetContentSlot())){FaceSlot->SetHorizontalAlignment(HAlign_Fill);FaceSlot->SetVerticalAlignment(VAlign_Center);}
            auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(ChoicesCardWidth);Size->SetMinDesiredHeight(ChoicesCardHeight);Size->SetContent(Card.Button);
            Row->AddChildToHorizontalBox(Size)->SetPadding(FMargin(Index?8.f:0.f,0,0,0));
            CardWidgets.Add(Card.Button);CardWidgets.Add(Card.IconSize);CardWidgets.Add(Card.IconShape);CardWidgets.Add(Card.Glyph);CardWidgets.Add(Card.Name);CardWidgets.Add(Card.Summary);
        }
        CardViews.SetNum(Cards.Num());
        // The reroll card: a slate circle with a turn arrow; its lines say what it does and how many rerolls are left.
        RerollButton=RerollCard.Button;
        ChoicesIcon(RerollCard.IconSize,RerollCard.IconShape,LDHUD::EChoiceIcon::Passive);RerollCard.IconShape->SetBrushColor(FLinearColor(.20f,.26f,.32f));
        ChoicesText(RerollCard.Glyph,TEXT("↻"));ChoicesText(RerollCard.Name,TEXT("免费重抽"));
        // Only a result or a refusal shows under the cards (no standing instructions); it collapses while empty and wraps
        // at the row's width, so a long notice never widens the panel.
        Feedback=Text(TEXT(""),14);Feedback->SetAutoWrapText(false);Feedback->SetWrapTextAt(4*ChoicesCardWidth+24.f);
        Feedback->SetVisibility(ESlateVisibility::Collapsed);Root->AddChildToVerticalBox(Feedback)->SetPadding(FMargin(0,8,0,0));
        // A see-through backing right above the HUD's skill slots (SetBottomDock), clear of the bottom band.
        MainPanel=LDWidgets::Panel(WidgetTree,Root,FMargin(14));MainPanel->SetBrushColor(FLinearColor(.025f,.045f,.07f,.72f));
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetHorizontalAlignment(HAlign_Left);Frame->SetVerticalAlignment(VAlign_Bottom);
        // #97: drawn at 80% (user: "shrink the choice list and shop panel size to 80%").
        auto* Scale=WidgetTree->ConstructWidget<UScaleBox>();Scale->SetStretch(EStretch::UserSpecified);Scale->SetUserSpecifiedScale(ChoicesPanelScale);
        Scale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Scale->SetContent(MainPanel);
        Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Frame->SetContent(Scale);
        DockFrame=Frame;SetBottomDock(BottomDock,DockLeft);
        // The full description of the hovered or focused card pops up above that card. It sits on its own canvas layer over
        // the whole panel, so its size never moves or squeezes the cards (which would drop the hover and flicker), and it
        // wraps at a fixed width so it measures the same from its first frame. It takes no pointer input, so the world
        // beside the panel stays clickable, and it only shows while a card is being read.
        auto* DetailLines=WidgetTree->ConstructWidget<UVerticalBox>();
        auto FixedWrap=[](UTextBlock* Line){Line->SetAutoWrapText(false);Line->SetWrapTextAt(ChoicesDetailWidth-28.f);return Line;};
        DetailName=FixedWrap(Text(TEXT(""),17));DetailLines->AddChildToVerticalBox(DetailName);
        DetailSummary=FixedWrap(Text(TEXT(""),13));DetailSummary->SetColorAndOpacity(LDHUDStyle::TextAccent);DetailLines->AddChildToVerticalBox(DetailSummary)->SetPadding(FMargin(0,2,0,0));
        DetailBody=FixedWrap(Text(TEXT(""),14));DetailLines->AddChildToVerticalBox(DetailBody)->SetPadding(FMargin(0,8,0,0));
        Detail=LDWidgets::Panel(WidgetTree,DetailLines,FMargin(14,12));Detail->SetBrushColor(FLinearColor(.02f,.035f,.055f,.94f));
        auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>();DetailWidth->SetWidthOverride(ChoicesDetailWidth);DetailWidth->SetContent(Detail);
        DetailWidth->SetVisibility(ESlateVisibility::Collapsed);
        DetailLayer=WidgetTree->ConstructWidget<UCanvasPanel>();DetailLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        DetailSlot=DetailLayer->AddChildToCanvas(DetailWidth);DetailSlot->SetAutoSize(true);DetailSlot->SetAlignment(FVector2D(.5f,1));
        auto* Layers=WidgetTree->ConstructWidget<UOverlay>();Layers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* FrameLayer=Layers->AddChildToOverlay(Frame);FrameLayer->SetHorizontalAlignment(HAlign_Fill);FrameLayer->SetVerticalAlignment(VAlign_Fill);
        auto* PopupLayer=Layers->AddChildToOverlay(DetailLayer);PopupLayer->SetHorizontalAlignment(HAlign_Fill);PopupLayer->SetVerticalAlignment(VAlign_Fill);
        WidgetTree->RootWidget=Layers;
        SetVisibility(ESlateVisibility::Collapsed);
    }
    return Super::RebuildWidget();
}
void ULDChoicesPanel::NativeTick(const FGeometry& Geometry,float Delta)
{
    Super::NativeTick(Geometry,Delta);
    if(Feedback)
    {
        const ESlateVisibility Shown=Feedback->GetText().IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible;
        if(Feedback->GetVisibility()!=Shown){Feedback->SetVisibility(Shown);}
    }
    UpdateDetail();
}
void ULDChoicesPanel::UpdateDetail()
{
    if(!Detail||!DetailSlot){return;}
    // The pointer wins over keyboard focus, which wins over a pinned preview; during the replacement child the card being
    // learned stays explained.
    auto Shown=[this](int32 Index){return Cards.IsValidIndex(Index)&&Cards[Index].Button->IsVisible();};
    int32 Next=INDEX_NONE;
    if(bOpen&&ShownOffer.IsValid())
    {
        for(int32 Index=0;Index<Cards.Num()&&Next==INDEX_NONE;++Index){if(Shown(Index)&&Cards[Index].Button->IsHovered()){Next=Index;}}
        for(int32 Index=0;Index<Cards.Num()&&Next==INDEX_NONE;++Index){if(Shown(Index)&&Cards[Index].Button->HasKeyboardFocus()){Next=Index;}}
        if(Next==INDEX_NONE&&Shown(PinnedPreview)){Next=PinnedPreview;}
        if(Next==INDEX_NONE&&bReplacing&&Shown(Selected)){Next=Selected;}
    }
    UWidget* Popup=DetailSlot->Content;
    if(Next!=DetailCard||bDetailStale)
    {
        DetailCard=Next;bDetailStale=false;
        if(Popup){Popup->SetVisibility(Next==INDEX_NONE?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
        if(Next!=INDEX_NONE&&CardViews.IsValidIndex(Next))
        {
            const LDHUD::FChoiceCardView& View=CardViews[Next];
            ChoicesText(DetailName,View.Name);ChoicesText(DetailSummary,View.Detail);
            FString Body=View.Description;for(const FString& Line:View.Lines){Body+=(Body.IsEmpty()?TEXT(""):TEXT("\n"))+Line;}
            if(View.bNeedsReplacement){Body+=(Body.IsEmpty()?TEXT(""):TEXT("\n\n"))+FString(TEXT("Q/W 已满：点选后再点 HUD 上要遗忘的 Q 或 W 技能槽。被替换的技能会被遗忘，旧投入不返还；新技能从旧技能的等级减一开始（至少 1 级）。"));}
            ChoicesText(DetailBody,Body);
        }
    }
    if(DetailCard!=INDEX_NONE&&DetailLayer&&Popup)
    {
        // Centred 8 units above the card, read from last frame's layout. The card's place does not depend on the popup, so
        // this never feeds back; the popup is only pushed sideways to stay on screen.
        const FGeometry& Layer=DetailLayer->GetCachedGeometry();const FGeometry& Card=Cards[DetailCard].Button->GetCachedGeometry();
        const FVector2D CardTop=Layer.AbsoluteToLocal(Card.GetAbsolutePosition());
        const FVector2D CardEnd=Layer.AbsoluteToLocal(Card.GetAbsolutePosition()+Card.GetAbsoluteSize());
        const float Half=ChoicesDetailWidth*.5f+8.f;const float Wide=float(Layer.GetLocalSize().X);
        const float Centre=float(CardTop.X+CardEnd.X)*.5f;
        const FVector2D Place(FMath::RoundToFloat(Wide>2*Half?FMath::Clamp(Centre,Half,Wide-Half):Centre),
            FMath::RoundToFloat(FMath::Max(float(Popup->GetDesiredSize().Y),float(CardTop.Y)-8.f)));
        if(!DetailSlot->GetPosition().Equals(Place,.5f)){DetailSlot->SetPosition(Place);}
    }
}
void ULDChoicesPanel::SetBottomDock(float Bottom,float Left)
{
    BottomDock=Bottom;DockLeft=Left;
    if(DockFrame){DockFrame->SetPadding(FMargin(FMath::Max(0.f,Left),0,0,Bottom));}
}
float ULDChoicesPanel::PanelTop() const
{
    return BottomDock+(MainPanel?float(MainPanel->GetDesiredSize().Y)*ChoicesPanelScale:0.f);
}
UWidget* ULDChoicesPanel::FocusScope() const{return MainPanel;}
UWidget* ULDChoicesPanel::HitArea() const{return MainPanel;}
void ULDChoicesPanel::OpenPanel()
{
    bOpen=true;ResultNotice.Reset();SetVisibility(ESlateVisibility::SelfHitTestInvisible);Fold.Opened(false,UiNow());
    if(!PendingRequest){ChoicesText(Feedback,TEXT(""));}
}
bool ULDChoicesPanel::ClosePanel()
{
    // Closing only hides the panel: the opportunity, the offer and any submitted request keep their real state.
    ResetReplacement();PinnedPreview=INDEX_NONE;ResultNotice.Reset();bOpen=false;SetVisibility(ESlateVisibility::Collapsed);Fold.Closed();UpdateDetail();return true;
}
double ULDChoicesPanel::UiNow() const{const UWorld* World=GetWorld();return World?World->GetRealTimeSeconds():0.;}
void ULDChoicesPanel::MarkOpenedAutomatically()
{
    if(!bOpen){return;}
    Fold.Opened(true,UiNow());
    UE_LOG(LogLDUI,Log,TEXT("LD_HUD_OPENING_CHOICE_SHOWN The opening three-choice panel opened automatically."));
}
void ULDChoicesPanel::UpdateFold(bool bOwnHit)
{
    // Folding back to the pending pill hides the panel only; a selected card stays selected for the next opening.
    if(bOpen&&Fold.ShouldFold(UiNow(),IsEngaged(),bOwnHit))
    {
        UE_LOG(LogLDUI,Log,TEXT("LD_HUD_CHOICES_FOLDED %s"),bOwnHit?TEXT("own hit"):TEXT("automatic open timed out"));
        ClosePanel();
    }
}
bool ULDChoicesPanel::CloseChildConfirmation()
{
    if(!bReplacing){return false;}
    ResetReplacement();ChoicesText(Feedback,TEXT(""));return true;
}
void ULDChoicesPanel::ResetReplacement()
{
    bReplacing=false;ReplaceSlot=INDEX_NONE;ReplaceSkill=NAME_None;ReplaceRank=0;ReplaceBuildRevision=0;
}
void ULDChoicesPanel::Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase Phase,const FLDDemoContentDefinitions* Content)
{
    const FGuid KeepOffer=ShownOffer;
    const FGuid KeepOption=Selected!=INDEX_NONE&&Latest.Offer.Options.IsValidIndex(Selected)?Latest.Offer.Options[Selected].OptionId:FGuid();
    Latest=View;LatestMatch=Match;LatestLife=Life;LatestPhase=Phase;LatestContent=Content;
    if(!MainPanel){return;}
    const bool bUsable=View.bValid&&View.Generation==Match&&View.Life==Life;
    const bool bOffer=bUsable&&View.bHasOffer&&View.Offer.Options.Num()==3;
    if(!bOffer||View.Offer.OfferId!=ShownOffer||View.Offer.OfferRevision!=ShownOfferRevision)
    {
        // A new offer replaces the cards. A revised offer of the same opportunity refreshes its guarded versions and keeps
        // the same option marked, so after a stale rejection the player clicks the same card again (UI 3.2).
        ShownOffer=bOffer?View.Offer.OfferId:FGuid();ShownOfferRevision=bOffer?View.Offer.OfferRevision:0;Selected=INDEX_NONE;ResetReplacement();
        if(!bOffer||View.Offer.OfferId!=KeepOffer){PinnedPreview=INDEX_NONE;}
        bDetailStale=true;
        if(bOffer&&View.Offer.OfferId==KeepOffer&&KeepOption.IsValid())
        {Selected=View.Offer.Options.IndexOfByPredicate([&KeepOption](const FLDChoiceOption& Option){return Option.OptionId==KeepOption;});}
        for(int32 Index=0;Index<Cards.Num();++Index)
        {
            FCardWidgets& Card=Cards[Index];
            Card.Button->SetVisibility(bOffer?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
            if(!bOffer){continue;}
            const LDHUD::FChoiceCardView Shown=LDHUD::ChoiceCard(View.Offer.Options[Index],View,Content);
            CardViews[Index]=Shown;
            ChoicesText(Card.Name,Shown.Name);ChoicesText(Card.Summary,Shown.Summary);ChoicesText(Card.Glyph,Shown.Glyph);
            ChoicesIcon(Card.IconSize,Card.IconShape,Shown.Icon);
        }
        if(!bOffer&&!PendingRequest){ChoicesText(Feedback,bUsable?FString::Printf(TEXT("暂无可以%s的机会。升级后会获得新的选择机会。"),LDTerms::Choice):FString(TEXT("等待成长数据同步。")));}
    }
    // Nothing left to pick: an open panel would only say so, so it closes itself. The last result still reaches the
    // player as a toast. A pending count whose offer has not arrived yet keeps it open.
    if(bOpen&&bUsable&&!bOffer&&!PendingRequest&&View.PendingChoiceCount==0)
    {
        const FString Result=ResultNotice;ClosePanel();
        if(!Result.IsEmpty()&&Notify){Notify(FString(LDTerms::Choice)+TEXT("：")+Result);}
        return;
    }
    else if(bReplacing&&ReplaceBuildRevision!=0&&View.BuildRevision!=ReplaceBuildRevision)
    {
        // The skills shown for replacement changed: the confirmation is withdrawn, never redirected to another slot.
        ResetReplacement();ChoicesText(Feedback,TEXT("技能已变化，替换确认已撤销，请重新选择。"));
    }
    RefreshSelection();
}
void ULDChoicesPanel::RefreshSelection()
{
    for(int32 Index=0;Index<Cards.Num();++Index){ChoicesTint(Cards[Index].Button,Index==Selected?ChoicesSelected:ChoicesIdle);}
    const uint8 Tokens=Latest.RerollTokens;
    const bool bCanReroll=Latest.bHasOffer&&!Latest.Offer.bRerolled&&Tokens>0&&!PendingRequest&&ShownOffer.IsValid()
        &&LDGrowthWire::IsChoicePhase(LatestPhase)&&!bReplacing;
    RerollButton->SetIsEnabled(bCanReroll);
    RerollButton->SetVisibility(ShownOffer.IsValid()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    ChoicesText(RerollCard.Summary,Latest.bHasOffer&&Latest.Offer.bRerolled?FString::Printf(TEXT("本组已重抽 · 剩 %d"),Tokens):FString::Printf(TEXT("剩 %d 次"),Tokens));
}
bool ULDChoicesPanel::IsReplacementTarget(uint8 SkillSlot) const
{
    return bReplacing&&!PendingRequest&&SkillSlot<2&&Latest.Skills.ContainsByPredicate([SkillSlot](const FLDLearnedSkillView& S){return S.bActive&&S.Slot==SkillSlot;});
}
bool ULDChoicesPanel::ChooseCard(int32 Index)
{
    if(!Latest.bHasOffer||!Latest.Offer.Options.IsValidIndex(Index)||Latest.Offer.OfferId!=ShownOffer){return false;}
    // One click is one choice: a click while a request is in flight never queues a second one.
    if(PendingRequest){ChoicesText(Feedback,TEXT("上一次选择仍在处理中。"));return false;}
    // A click (even on the card being replaced) restarts from that card, so a replacement slot is always picked afresh.
    Selected=Index;PinnedPreview=INDEX_NONE;ResetReplacement();RefreshSelection();
    ConfirmChoice();
    return true;
}
bool ULDChoicesPanel::PreviewChoice(int32 Index)
{
    if(!Latest.bHasOffer||!Latest.Offer.Options.IsValidIndex(Index)||Latest.Offer.OfferId!=ShownOffer){return false;}
    PinnedPreview=Index;UpdateDetail();return true;
}
bool ULDChoicesPanel::ChooseReplacement(uint8 SkillSlot)
{
    if(!bReplacing||SkillSlot>1){return false;}
    if(PendingRequest){ChoicesText(Feedback,TEXT("上一次选择仍在处理中。"));return false;}
    const FLDLearnedSkillView* Skill=Latest.Skills.FindByPredicate([SkillSlot](const FLDLearnedSkillView& S){return S.bActive&&S.Slot==SkillSlot;});
    if(!Skill){ChoicesText(Feedback,TEXT("该技能槽为空，无需替换；请重新点卡。"));return false;}
    // The slot click is the explicit choice of what to forget, so it submits at once with that skill and rank as expected.
    ReplaceSlot=SkillSlot;ReplaceSkill=Skill->DefinitionId;ReplaceRank=Skill->Rank;RefreshSelection();
    ConfirmChoice();
    return true;
}
void ULDChoicesPanel::ConfirmChoice()
{
    if(PendingRequest){ChoicesText(Feedback,TEXT("上一次选择仍在处理中。"));return;}
    if(Selected==INDEX_NONE||!Latest.bHasOffer||!Latest.Offer.Options.IsValidIndex(Selected)||Latest.Offer.OfferId!=ShownOffer){ChoicesText(Feedback,TEXT("请先点选一张卡。"));return;}
    if(!LDGrowthWire::IsChoicePhase(LatestPhase)){ChoicesText(Feedback,TEXT("当前状态不能确认选择。"));return;}
    const FLDChoiceOption& Option=Latest.Offer.Options[Selected];
    FLDChoiceCommand Command;
    Command.Generation=LatestMatch;Command.Life=LatestLife;Command.ExpectedLifePhase=LatestPhase;
    Command.OfferId=Latest.Offer.OfferId;Command.OfferRevision=Latest.Offer.OfferRevision;Command.OptionId=Option.OptionId;
    Command.ExpectedBuildRevision=Latest.BuildRevision;Command.CatalogDigest=Latest.CatalogDigest;
    const LDHUD::FChoiceCardView Shown=LDHUD::ChoiceCard(Option,Latest,LatestContent);
    if(Shown.bNeedsReplacement)
    {
        if(!bReplacing)
        {
            // #97 short path: the HUD's Q/W slots glow red (IsReplacementTarget) and a click on one is the choice of what to
            // forget, with no further confirmation and no default slot; another card restarts from that card, Back cancels,
            // and any other input (a key, a world or HUD click, zoom) cancels it and acts as usual (ChildConfirmationYields).
            bReplacing=true;ReplaceBuildRevision=Latest.BuildRevision;
            // The toast asks; the red slot rings show where; the selected card's popup keeps the rank rule.
            ChoicesText(Feedback,TEXT(""));
            if(Notify){Notify(TEXT("选择要替换的技能"));}
            RefreshSelection();
            return;
        }
        if(ReplaceSlot==INDEX_NONE||!FLDDefinitionWire::Encode(ReplaceSkill,Command.ExpectedOldSkillId))
        {ChoicesText(Feedback,TEXT("请选择要替换的技能槽。"));return;}
        Command.bReplace=true;Command.ReplaceSlot=uint8(ReplaceSlot);Command.ExpectedOldRank=ReplaceRank;Command.bReplaceConfirmed=true;
    }
    PendingName=Shown.Name;bPendingReroll=false;
    Send(Command);
}
bool ULDChoicesPanel::Reroll()
{
    if(PendingRequest){ChoicesText(Feedback,TEXT("上一次请求仍在处理中。"));return false;}
    if(!Latest.bHasOffer||Latest.Offer.OfferId!=ShownOffer||!LDGrowthWire::IsChoicePhase(LatestPhase)){ChoicesText(Feedback,TEXT("当前没有可以重抽的选择。"));return false;}
    if(Latest.Offer.bRerolled){ChoicesText(Feedback,TEXT("这一组已经重抽过一次。"));return false;}
    if(Latest.RerollTokens==0){ChoicesText(Feedback,TEXT("没有免费重抽次数；升到指定等级会获得。"));return false;}
    FLDChoiceCommand Command;
    Command.Generation=LatestMatch;Command.Life=LatestLife;Command.ExpectedLifePhase=LatestPhase;
    Command.OfferId=Latest.Offer.OfferId;Command.OfferRevision=Latest.Offer.OfferRevision;
    Command.ExpectedBuildRevision=Latest.BuildRevision;Command.CatalogDigest=Latest.CatalogDigest;Command.bReroll=true;
    Selected=INDEX_NONE;ResetReplacement();PendingName.Reset();bPendingReroll=true;
    Send(Command);
    return true;
}
void ULDChoicesPanel::Send(const FLDChoiceCommand& Command)
{
    if(!SubmitChoice)
    {
        UE_LOG(LogLDUI,Error,TEXT("LD_CHOICES_SUBMIT_UNAVAILABLE The growth choice entry is not connected to this panel."));
        ChoicesText(Feedback,TEXT("选择入口不可用，本次没有提交。"));return;
    }
    // A Listen Host answers inside the submit call, so the pending entry is keyed to the id it will receive first.
    const uint64 Expected=PeekRequestId?PeekRequestId():0;
    const uint32 Before=Answers;PendingRequest=Expected;
    ELDGrowthReason Reason=ELDGrowthReason::None;
    const uint64 Request=SubmitChoice(Command,Reason);
    if(!Request)
    {
        if(PendingRequest==Expected){PendingRequest=0;}
        const FString Why=LDHUD::GrowthReasonText(Reason);
        ChoicesText(Feedback,TEXT("没有提交：")+(Why.IsEmpty()?FString(TEXT("原因未知")):Why));RefreshSelection();return;
    }
    if(Answers==Before){PendingRequest=Request;ChoicesText(Feedback,TEXT(""));}
    RefreshSelection();
}
void ULDChoicesPanel::FinishRequest()
{
    PendingRequest=0;RefreshSelection();
    // A result that arrives after the panel was closed still reaches the player, without reopening the panel; while it
    // is open the result is kept for the toast in case the panel closes itself with nothing left to pick.
    if(!bOpen&&Notify&&Feedback){Notify(FString(LDTerms::Choice)+TEXT("：")+Feedback->GetText().ToString());}
    else if(Feedback){ResultNotice=Feedback->GetText().ToString();}
}
void ULDChoicesPanel::ShowFinal(const FLDChoiceReceipt& Receipt)
{
    if(Receipt.Header.Outcome==ELDReceiptOutcome::Applied&&Receipt.bRerolled)
    {
        ChoicesText(Feedback,FString::Printf(TEXT("已重抽，这一组换成了新的三张卡（剩余重抽 %d 次）"),Receipt.RerollTokensLeft));
        return;
    }
    if(Receipt.Header.Outcome==ELDReceiptOutcome::Applied)
    {
        ChoicesText(Feedback,TEXT("已确认：")+PendingName+(Receipt.bReplaced
            ?FString::Printf(TEXT("（已遗忘被替换的技能，新技能 %d 级）"),Receipt.NewRank):FString()));
        Selected=INDEX_NONE;ResetReplacement();
        return;
    }
    // A rejected choice keeps the chosen card marked: after the view refreshes the player clicks it (or another) again.
    ChoicesText(Feedback,TEXT("没有生效：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Receipt.Header.Reason)));
}
// The pending entry clears exactly when the controller releases its in-flight request: a final submit answer, an
// unreserved rejection, or a final/expired receipt. Other answers only explain the wait.
void ULDChoicesPanel::HandleSubmit(const FLDSubmitResult& Result)
{
    if(Result.Family!=ELDGrowthFamily::Choice||!PendingRequest||Result.RequestId!=PendingRequest||Result.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Status)
    {
    case ELDAdmissionStatus::AcceptedPending: return;
    case ELDAdmissionStatus::AdmissionRejected:
        ChoicesText(Feedback,TEXT("没有受理：")+LDHUD::GrowthReasonText(LDGrowthWire::Reason(Result.Reason)));
        if(Result.bSequenceReserved){return;}
        break;
    case ELDAdmissionStatus::Final:
        if(Result.bHasChoiceReceipt){ShowFinal(Result.ChoiceReceipt);}else{ChoicesText(Feedback,TEXT("请求已结束，请以当前状态为准。"));}
        break;
    default: ChoicesText(Feedback,TEXT("无法识别服务器的答复，请以当前状态为准。"));return;
    }
    FinishRequest();
}
void ULDChoicesPanel::HandleReceipt(const FLDChoiceReceiptQueryResult& Result)
{
    if(Result.Query.Family!=ELDGrowthFamily::Choice||!PendingRequest||Result.Query.RequestId!=PendingRequest||Result.Query.Generation!=LatestMatch){return;}
    ++Answers;
    switch(Result.Lookup)
    {
    case ELDReceiptLookup::Pending: return;
    case ELDReceiptLookup::Final:
        if(Result.bHasReceipt){ShowFinal(Result.Receipt);}else{ChoicesText(Feedback,TEXT("回执缺少结果，请以当前状态为准。"));}
        break;
    case ELDReceiptLookup::ReceiptExpired: ChoicesText(Feedback,TEXT("回执已过期，请以当前状态为准。"));break;
    case ELDReceiptLookup::NotFound: ChoicesText(Feedback,TEXT("服务器没有找到这次请求，请以当前状态为准。"));return;
    default: ChoicesText(Feedback,TEXT("回执查询无效，请以当前状态为准。"));return;
    }
    FinishRequest();
}
