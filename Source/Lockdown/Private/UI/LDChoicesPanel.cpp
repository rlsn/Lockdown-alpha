#include "UI/LDChoicesPanel.h"
#include "Core/LDLog.h"
#include "UI/LDHUDModel.h"
#include "UI/LDHUDStyle.h"
#include "UI/LDTerms.h"
#include "Development/LDDemoContentDefinitions.h"
#include "Development/LDProgressionRules.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
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
constexpr float ChoicesCardWidth=308.f,ChoicesCardHeight=64.f,ChoicesIconSize=44.f,ChoicesDetailWidth=300.f;
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
        auto* Top=WidgetTree->ConstructWidget<UHorizontalBox>();Root->AddChildToVerticalBox(Top);
        Header=Text(TEXT(""),17);Top->AddChildToHorizontalBox(Header)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        // No close button: the HUD's blind-box button toggles the panel, and Back closes it like every main panel.
        // The three compact cards stack in a right-hand column, so the panel never covers the own body and its surroundings
        // while the match keeps running. A click on a card is the choice; there is no separate confirm button.
        auto* Row=WidgetTree->ConstructWidget<UVerticalBox>();
        Root->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,8));
        for(int32 Index=0;Index<3;++Index)
        {
            FCardWidgets& Card=Cards.AddDefaulted_GetRef();
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
            Card.Button=LDWidgets::Button(WidgetTree,TEXT(""),[this,Index](){ChooseCard(Index);},false,FName(*FString::Printf(TEXT("ChoiceCard%d"),Index)));
            Card.Button->SetContent(Face);
            auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(ChoicesCardWidth);Size->SetMinDesiredHeight(ChoicesCardHeight);Size->SetContent(Card.Button);
            Row->AddChildToVerticalBox(Size)->SetPadding(FMargin(0,0,0,Index<2?8:0));
            CardWidgets.Add(Card.Button);CardWidgets.Add(Card.IconSize);CardWidgets.Add(Card.IconShape);CardWidgets.Add(Card.Glyph);CardWidgets.Add(Card.Name);CardWidgets.Add(Card.Summary);
        }
        CardViews.SetNum(Cards.Num());
        auto* Replace=WidgetTree->ConstructWidget<UVerticalBox>();
        ReplaceText=Text(TEXT(""),16);Replace->AddChildToVerticalBox(ReplaceText);
        Replace->AddChildToVerticalBox(Text(TEXT("被替换的技能会被遗忘，旧投入不返还；新技能从旧技能的等级减一开始（至少 1 级）；冷却照常计时。"),14))->SetPadding(FMargin(0,6,0,0));
        // The slot buttons carry only their key letter (the skills they hold are named in the prompt above, which wraps);
        // clicking one is the replacement's confirmation and submits it.
        auto* ReplaceActions=WidgetTree->ConstructWidget<UHorizontalBox>();Replace->AddChildToVerticalBox(ReplaceActions)->SetPadding(FMargin(0,10,0,0));
        for(uint8 SkillSlot=0;SkillSlot<2;++SkillSlot)
        {
            ReplaceSlotButtons[SkillSlot]=LDWidgets::Button(WidgetTree,SkillSlot?TEXT("W"):TEXT("Q"),[this,SkillSlot](){ChooseReplacement(SkillSlot);},false,SkillSlot?FName(TEXT("ReplaceSlotW")):FName(TEXT("ReplaceSlotQ")));
            auto* SlotSize=WidgetTree->ConstructWidget<USizeBox>();SlotSize->SetMinDesiredWidth(56);SlotSize->SetContent(ReplaceSlotButtons[SkillSlot]);
            CastChecked<UTextBlock>(ReplaceSlotButtons[SkillSlot]->GetContent())->SetJustification(ETextJustify::Center);
            ReplaceActions->AddChildToHorizontalBox(SlotSize)->SetPadding(FMargin(0,0,8,0));
        }
        auto* BackSlot=ReplaceActions->AddChildToHorizontalBox(LDWidgets::Button(WidgetTree,TEXT("返回三卡"),[this](){CloseChildConfirmation();},false,TEXT("ReplaceBack")));
        BackSlot->SetPadding(FMargin(8,0,0,0));
        ReplacePanel=LDWidgets::Panel(WidgetTree,Replace,FMargin(16));ReplacePanel->SetBrushColor(FLinearColor(.06f,.05f,.03f,.9f));ReplacePanel->SetVisibility(ESlateVisibility::Collapsed);
        Root->AddChildToVerticalBox(ReplacePanel)->SetPadding(FMargin(0,2,0,8));
        auto* Bottom=WidgetTree->ConstructWidget<UVerticalBox>();Root->AddChildToVerticalBox(Bottom);
        Feedback=Text(TEXT(""),14);Bottom->AddChildToVerticalBox(Feedback);
        RerollButton=LDWidgets::Button(WidgetTree,TEXT("免费重抽"),[this](){Reroll();},false,TEXT("ChoicesReroll"));
        Bottom->AddChildToVerticalBox(RerollButton)->SetPadding(FMargin(0,6,0,0));
        // A see-through backing in the bottom-right corner, right above the HUD's shop entry (SetBottomDock): clear of the
        // centre (the own body and about 5 m around it at either resolution) and of the bottom band.
        MainPanel=LDWidgets::Panel(WidgetTree,Root,FMargin(14));MainPanel->SetBrushColor(FLinearColor(.025f,.045f,.07f,.72f));
        auto* Width=WidgetTree->ConstructWidget<USizeBox>();Width->SetWidthOverride(340);Width->SetContent(MainPanel);
        auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor::Transparent);Frame->SetHorizontalAlignment(HAlign_Right);Frame->SetVerticalAlignment(VAlign_Bottom);
        Frame->SetPadding(FMargin(0,0,16,BottomDock));Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Frame->SetContent(Width);
        DockFrame=Frame;
        // The full description of the hovered or focused card pops up left of the column, level with that card. It sits on its
        // own canvas layer over the whole panel, so its size never moves or squeezes the cards (which would drop the hover and
        // flicker), and it wraps at a fixed width so it measures the same from its first frame. It takes no pointer input, so
        // the world beside the panel stays clickable, and it only shows while a card is being read.
        auto* DetailLines=WidgetTree->ConstructWidget<UVerticalBox>();
        auto FixedWrap=[](UTextBlock* Line){Line->SetAutoWrapText(false);Line->SetWrapTextAt(ChoicesDetailWidth-28.f);return Line;};
        DetailName=FixedWrap(Text(TEXT(""),17));DetailLines->AddChildToVerticalBox(DetailName);
        DetailSummary=FixedWrap(Text(TEXT(""),13));DetailSummary->SetColorAndOpacity(LDHUDStyle::TextAccent);DetailLines->AddChildToVerticalBox(DetailSummary)->SetPadding(FMargin(0,2,0,0));
        DetailBody=FixedWrap(Text(TEXT(""),14));DetailLines->AddChildToVerticalBox(DetailBody)->SetPadding(FMargin(0,8,0,0));
        Detail=LDWidgets::Panel(WidgetTree,DetailLines,FMargin(14,12));Detail->SetBrushColor(FLinearColor(.02f,.035f,.055f,.94f));
        auto* DetailWidth=WidgetTree->ConstructWidget<USizeBox>();DetailWidth->SetWidthOverride(ChoicesDetailWidth);DetailWidth->SetContent(Detail);
        DetailWidth->SetVisibility(ESlateVisibility::Collapsed);
        DetailLayer=WidgetTree->ConstructWidget<UCanvasPanel>();DetailLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        DetailSlot=DetailLayer->AddChildToCanvas(DetailWidth);DetailSlot->SetAutoSize(true);DetailSlot->SetAlignment(FVector2D(1,0));
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
            ChoicesText(DetailName,View.Name);ChoicesText(DetailSummary,View.Summary);
            FString Body=View.Description;for(const FString& Line:View.Lines){Body+=(Body.IsEmpty()?TEXT(""):TEXT("\n"))+Line;}
            if(View.bNeedsReplacement){Body+=(Body.IsEmpty()?TEXT(""):TEXT("\n\n"))+FString(TEXT("Q/W 已满：点选后需选择要遗忘的技能。"));}
            ChoicesText(DetailBody,Body);
        }
    }
    if(DetailCard!=INDEX_NONE&&DetailLayer&&Popup)
    {
        // Right edge 8 units left of the card, top level with it, read from last frame's layout. The card's place does not
        // depend on the popup, so this never feeds back; the popup is only pulled up to stay on screen.
        const FGeometry& Layer=DetailLayer->GetCachedGeometry();
        const FVector2D CardTop=Layer.AbsoluteToLocal(Cards[DetailCard].Button->GetCachedGeometry().GetAbsolutePosition());
        const float Bottom=float(Layer.GetLocalSize().Y)-float(Popup->GetDesiredSize().Y)-8.f;
        const FVector2D Place(FMath::RoundToFloat(float(CardTop.X)-8.f),FMath::RoundToFloat(FMath::Max(0.f,FMath::Min(float(CardTop.Y),Bottom))));
        if(!DetailSlot->GetPosition().Equals(Place,.5f)){DetailSlot->SetPosition(Place);}
    }
}
void ULDChoicesPanel::SetBottomDock(float Bottom)
{
    BottomDock=Bottom;
    if(DockFrame){DockFrame->SetPadding(FMargin(0,0,16,Bottom));}
}
UWidget* ULDChoicesPanel::FocusScope() const{return bReplacing?static_cast<UWidget*>(ReplacePanel.Get()):static_cast<UWidget*>(MainPanel.Get());}
UWidget* ULDChoicesPanel::HitArea() const{return MainPanel;}
void ULDChoicesPanel::OpenPanel()
{
    bOpen=true;ResultNotice.Reset();SetVisibility(ESlateVisibility::SelfHitTestInvisible);Fold.Opened(false,UiNow());
    if(!PendingRequest){ChoicesText(Feedback,TEXT("点卡片即选定；悬停或聚焦查看详情。对局不会暂停。"));}
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
    ResetReplacement();ChoicesText(Feedback,TEXT("已返回三卡，没有消耗本次选择。"));return true;
}
void ULDChoicesPanel::ResetReplacement()
{
    bReplacing=false;ReplaceSlot=INDEX_NONE;ReplaceSkill=NAME_None;ReplaceRank=0;ReplaceBuildRevision=0;
    if(ReplacePanel){ReplacePanel->SetVisibility(ESlateVisibility::Collapsed);}
}
void ULDChoicesPanel::Present(const FLDProgressionView& View,uint32 Match,uint32 Life,ELDLifePhase Phase,const FLDDemoContentDefinitions* Content)
{
    const FGuid KeepOffer=ShownOffer;
    const FGuid KeepOption=Selected!=INDEX_NONE&&Latest.Offer.Options.IsValidIndex(Selected)?Latest.Offer.Options[Selected].OptionId:FGuid();
    Latest=View;LatestMatch=Match;LatestLife=Life;LatestPhase=Phase;LatestContent=Content;
    if(!MainPanel){return;}
    const bool bUsable=View.bValid&&View.Generation==Match&&View.Life==Life;
    const bool bOffer=bUsable&&View.bHasOffer&&View.Offer.Options.Num()==3;
    const FString DeadNote=Phase==ELDLifePhase::Dead?FString(TEXT(" · "))+LDTerms::Dead+TEXT("也能开，只更新构筑"):FString();
    ChoicesText(Header,!bUsable?FString(LDTerms::Choice)+TEXT(" · 成长数据同步中"):FString::Printf(TEXT("%s · 待选 %d 次%s"),LDTerms::Choice,View.PendingChoiceCount,*DeadNote));
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
    const bool bReady=Selected!=INDEX_NONE&&!PendingRequest&&ShownOffer.IsValid()&&LDGrowthWire::IsChoicePhase(LatestPhase);
    const uint8 Tokens=Latest.RerollTokens;
    const bool bCanReroll=Latest.bHasOffer&&!Latest.Offer.bRerolled&&Tokens>0&&!PendingRequest&&ShownOffer.IsValid()
        &&LDGrowthWire::IsChoicePhase(LatestPhase)&&!bReplacing;
    RerollButton->SetIsEnabled(bCanReroll);
    ChoicesText(CastChecked<UTextBlock>(RerollButton->GetContent()),Latest.bHasOffer&&Latest.Offer.bRerolled
        ?FString::Printf(TEXT("本组已重抽 · 剩 %d 次"),Tokens):FString::Printf(TEXT("免费重抽 · 剩 %d 次"),Tokens));
    for(uint8 SkillSlot=0;SkillSlot<2;++SkillSlot)
    {
        ReplaceSlotButtons[SkillSlot]->SetIsEnabled(bReady&&bReplacing);
        ChoicesTint(ReplaceSlotButtons[SkillSlot],int32(SkillSlot)==ReplaceSlot?ChoicesSelected:ChoicesIdle);
    }
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
    if(!Skill){ChoicesText(Feedback,TEXT("该技能槽为空，无需替换；请返回三卡重新确认。"));return false;}
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
            // Child confirmation: both current actives, no default slot.
            bReplacing=true;ReplaceBuildRevision=Latest.BuildRevision;ReplacePanel->SetVisibility(ESlateVisibility::Visible);
            // The buttons say only Q/W; the wrapping prompt names what each holds and the rank the new skill starts at.
            FString Prompt=TEXT("学习「")+Shown.Name+TEXT("」需要遗忘一个主动技能。点要遗忘的技能槽即确认：");
            for(uint8 SkillSlot=0;SkillSlot<2;++SkillSlot)
            {
                const FLDLearnedSkillView* Skill=Latest.Skills.FindByPredicate([SkillSlot](const FLDLearnedSkillView& S){return S.bActive&&S.Slot==SkillSlot;});
                Prompt+=FString::Printf(TEXT("\n%s：%s"),SkillSlot?TEXT("W"):TEXT("Q"),Skill?*FString::Printf(TEXT("%s %d 级 → 新技能 %d 级"),
                    *LDHUD::ContentName(LatestContent,Skill->DefinitionId.ToString()),Skill->Rank,LDProgression::ReplacementRank(Skill->Rank)):TEXT("空"));
            }
            ChoicesText(ReplaceText,Prompt);
            ChoicesText(Feedback,TEXT("点 Q 或 W 选择要遗忘的技能，或返回三卡。"));RefreshSelection();
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
    if(Answers==Before){PendingRequest=Request;ChoicesText(Feedback,TEXT("已提交，等待服务器确认…"));}
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
    case ELDAdmissionStatus::AcceptedPending: ChoicesText(Feedback,TEXT("已受理，等待结算…"));return;
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
    case ELDReceiptLookup::Pending: ChoicesText(Feedback,TEXT("已受理，等待结算…"));return;
    case ELDReceiptLookup::Final:
        if(Result.bHasReceipt){ShowFinal(Result.Receipt);}else{ChoicesText(Feedback,TEXT("回执缺少结果，请以当前状态为准。"));}
        break;
    case ELDReceiptLookup::ReceiptExpired: ChoicesText(Feedback,TEXT("回执已过期，请以当前状态为准。"));break;
    case ELDReceiptLookup::NotFound: ChoicesText(Feedback,TEXT("服务器没有找到这次请求，请以当前状态为准。"));return;
    default: ChoicesText(Feedback,TEXT("回执查询无效，请以当前状态为准。"));return;
    }
    FinishRequest();
}
