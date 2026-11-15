#include "UI/LDWorkshopPreview.h"

#include "Combat/LDAttributeProjection.h"
#include "Combat/LDCombatDirector.h"
#include "Combat/LDCombatSourceRules.h"
#include "Combat/LDTacticalRules.h"
#include "Development/LDDemoContentCatalog.h"
#include "UI/LDTerms.h"

namespace
{
using LDWorkshopPreview::EImpact;
using LDWorkshopPreview::FLine;

struct FState
{
    FLDBuildProjectionInput Input;
    FLDAttributeProjection Build;
    FLDAttributeProjection Effective;
    int64 HpP2=0;
    int64 MpP2=0;
};

FString Exact(int64 Numerator,int64 Denominator)
{
    if(Denominator<=0){return TEXT("?");}
    const bool bNegative=Numerator<0;
    const uint64 Magnitude=bNegative?uint64(-(Numerator+1))+1:uint64(Numerator);
    const uint64 Divisor=uint64(Denominator);
    FString Out=(bNegative?TEXT("-"):TEXT(""))+FString::Printf(TEXT("%llu"),Magnitude/Divisor);
    uint64 Remainder=Magnitude%Divisor;
    if(!Remainder){return Out;}
    FString Fraction;
    for(int32 Place=0;Place<6&&Remainder;++Place)
    {
        if(Remainder>MAX_uint64/10){return FString::Printf(TEXT("%lld/%lld"),Numerator,Denominator);}
        Remainder*=10;
        Fraction.AppendChar(TCHAR('0'+Remainder/Divisor));
        Remainder%=Divisor;
    }
    if(Remainder){return FString::Printf(TEXT("%lld/%lld"),Numerator,Denominator);}
    return Out+TEXT(".")+Fraction;
}

FString Exact(const FLDDemoRational& Value,int64 Factor=1)
{
    if(Factor<=0||Value.Numerator>MAX_int64/Factor||Value.Numerator<MIN_int64/Factor)
    {return FString::Printf(TEXT("%lld/%lld × %lld"),Value.Numerator,Value.Denominator,Factor);}
    return Exact(Value.Numerator*Factor,Value.Denominator);
}

FString Seconds(int64 Ms){return Exact(Ms,1000)+TEXT("s");}
FString Points(int64 P2){return Exact(P2,100);}
FString Percent(int64 Bp){return Exact(Bp,100)+TEXT("%");}
FString Cm(const FLDDemoRational& Value){return Exact(Value)+TEXT("cm");}

FString Range(double Value)
{
    for(int32 Precision=0;Precision<=15;++Precision)
    {
        const FString Text=FString::Printf(TEXT("%.*f"),Precision,Value);
        if(FCString::Atod(*Text)==Value){return Text+TEXT("cm");}
    }
    return FString::Printf(TEXT("%.17gcm"),Value);
}

void Add(TArray<FLine>& Lines,const FString& Label,const FString& Before,const FString& After,EImpact Impact)
{
    if(Before!=After){Lines.Add({Label,Before,After,Impact});}
}

EImpact Impact(int64 Before,int64 After,bool bHigherIsBetter)
{
    return (After>Before)==bHigherIsBetter?EImpact::Beneficial:EImpact::Cost;
}

EImpact Impact(double Before,double After,bool bHigherIsBetter)
{
    return (After>Before)==bHigherIsBetter?EImpact::Beneficial:EImpact::Cost;
}

bool Same(const FLDDemoRational& A,const FLDDemoRational& B)
{
    return A.Numerator==B.Numerator&&A.Denominator==B.Denominator;
}

bool Covered(const FLDDemoComponentDefinition& Definition,const FLDComponentTarget& Target)
{
    if((Target.Kind!=ELDComponentTargetKind::Active&&Target.Kind!=ELDComponentTargetKind::Dash)
        ||Definition.Effects.IsEmpty()){return false;}
    for(const FLDDemoEffectDefinition& Effect:Definition.Effects)
    {
        if(Effect.GetOp()!=ELDDemoEffectOp::ControlScale&&Effect.GetOp()!=ELDDemoEffectOp::CooldownCost){return false;}
    }
    return true;
}

bool Partial(const FLDDemoContentSnapshot& Content,const FLDGrowthBuild& Before,const FLDGrowthBuild& After,
    bool& Out,FString& Error)
{
    Out=false;
    for(const FLDComponentInstance& Old:Before.Components)
    {
        const FLDComponentInstance* New=After.FindComponent(Old.Id);
        if(New&&Old.bInstalled==New->bInstalled&&(!Old.bInstalled||Old.Target==New->Target)){continue;}
        const FLDDemoComponentDefinition* Definition=nullptr;
        if(!Content.FindComponent(Old.Id.ToString(),Definition,Error)||!Definition){return false;}
        if((Old.bInstalled&&!Covered(*Definition,Old.Target))||(New&&New->bInstalled&&!Covered(*Definition,New->Target)))
        {Out=true;}
    }
    for(const FLDComponentInstance& New:After.Components)
    {
        if(Before.FindComponent(New.Id)||!New.bInstalled){continue;}
        const FLDDemoComponentDefinition* Definition=nullptr;
        if(!Content.FindComponent(New.Id.ToString(),Definition,Error)||!Definition){return false;}
        if(!Covered(*Definition,New.Target)){Out=true;}
    }
    return true;
}

bool Project(const FLDDemoContentSnapshot& Content,const FLDGrowthBuild& Build,const FLDCombatOwnerView& Combat,
    const FState* Previous,FState& Out,FString& Error)
{
    Out=FState();
    Out.Input=LDProgression::CombatProjectionInput(&Content.Get(),Build);
    Out.Input.DevBonuses=Combat.PreviewDevBonuses;
    const FLDAttributeProjectionResult Projected=LDCombatSourceRules::ProjectBuildInput(Out.Input,Content);
    if(!Projected.IsSuccess())
    {
        Error=Projected.Error.Field+TEXT(": ")+Projected.Error.Message;
        return false;
    }
    Out.Build=Projected.Value.GetValue();
    Out.Effective=Out.Build;
    if(!Combat.bPreviewLiving){return true;}
    if(!Previous)
    {
        if(Combat.MaxHPP2!=Out.Build.Final.MaxHPP2||Combat.MaxMPP2!=Out.Build.Final.MaxMPP2)
        {Error=TEXT("owner resource maxima do not match the installed projection");return false;}
        Out.HpP2=Combat.HPP2;Out.MpP2=Combat.MPP2;
    }
    else
    {
        FLDResourceMaxChangeInput Hp;
        Hp.Pool=ELDResourcePool::HP;Hp.OldCurrentP2=Previous->HpP2;Hp.OldMaxP2=Previous->Build.Final.MaxHPP2;
        Hp.NewMaxP2=Out.Build.Final.MaxHPP2;
        FLDResourceMaxChangeInput Mp;
        Mp.Pool=ELDResourcePool::MP;Mp.OldCurrentP2=Previous->MpP2;Mp.OldMaxP2=Previous->Build.Final.MaxMPP2;
        Mp.NewMaxP2=Out.Build.Final.MaxMPP2;
        const FLDResourceMaxChangeResult Health=LDAttributeProjection::ResourceMaxChange(Hp,Content);
        const FLDResourceMaxChangeResult Mana=LDAttributeProjection::ResourceMaxChange(Mp,Content);
        if(!Health.IsSuccess()||!Mana.IsSuccess())
        {
            const FLDAttributeProjectionFailure& Failure=Health.IsSuccess()?Mana.Error:Health.Error;
            Error=Failure.Field+TEXT(": ")+Failure.Message;
            return false;
        }
        Out.HpP2=Health.Value->NewCurrentP2;Out.MpP2=Mana.Value->NewCurrentP2;
    }
    TArray<FLDCombatTimedBonus> Bonuses;
    for(const FLDCombatPreviewBonus& Row:Combat.PreviewBonuses)
    {
        FLDCombatTimedBonus& Bonus=Bonuses.AddDefaulted_GetRef();Bonus.Stats=Row.Stats;Bonus.ExpiresAtMs=Row.ExpiresAtMs;
    }
    const auto CopySlows=[](const TArray<FLDCombatPreviewSlow>& Rows)
    {
        TArray<FLDCombatSlow> Slows;
        for(const FLDCombatPreviewSlow& Row:Rows)
        {
            FLDCombatSlow& Slow=Slows.AddDefaulted_GetRef();Slow.RatioBp=Row.RatioBp;Slow.ExpiresAtMs=Row.ExpiresAtMs;
        }
        return Slows;
    };
    const TArray<FLDCombatSlow> MoveSlows=CopySlows(Combat.PreviewMoveSlows);
    const TArray<FLDCombatSlow> AttackSlows=CopySlows(Combat.PreviewAttackSlows);
    FLDTemporaryAttributeTerms Terms=LDTactical::TemporaryTerms(Bonuses,MoveSlows,Out.Build.LowHPBonus,
        Out.HpP2,Out.Build.Final.MaxHPP2,Combat.AsOfMs);
    Terms.AttackSlowBp=LDTactical::StrongestSlowBp(AttackSlows,Combat.AsOfMs);
    const FLDAttributeProjectionResult Effective=LDAttributeProjection::ApplyTemporary(Out.Build,Terms,Content);
    if(!Effective.IsSuccess())
    {
        Error=Effective.Error.Field+TEXT(": ")+Effective.Error.Message;
        return false;
    }
    Out.Effective=Effective.Value.GetValue();
    return true;
}

bool MatchesOwner(const FState& State,const FLDCombatOwnerView& Combat)
{
    if(State.Effective.Contributions.Num()!=Combat.Attributes.Num()){return false;}
    for(const FLDAttributeContributions& Row:State.Effective.Contributions)
    {
        const FLDCombatAttributeRowView* Published=Combat.Attributes.FindByPredicate([&Row](const FLDCombatAttributeRowView& Value)
        {return Value.Attribute==uint8(Row.Attribute);});
        if(!Published||!Same(Row.Final,Published->Final)){return false;}
    }
    return true;
}

FString AttributeValue(ELDProjectedAttribute Attribute,const FLDDemoRational& Value)
{
    switch(Attribute)
    {
    case ELDProjectedAttribute::AttackSpeed: return Exact(Value,60)+TEXT("/min");
    case ELDProjectedAttribute::CritChance: return Exact(Value,100)+TEXT("%");
    case ELDProjectedAttribute::AttackRange: return Cm(Value);
    case ELDProjectedAttribute::HPRegen: case ELDProjectedAttribute::MPRegen: return Exact(Value)+TEXT("/s");
    default: return Exact(Value);
    }
}

void Attributes(const FState& Before,const FState& After,TArray<FLine>& Lines)
{
    for(const FLDAttributeContributions& Old:Before.Effective.Contributions)
    {
        const FLDAttributeContributions* New=After.Effective.Contributions.FindByPredicate([&Old](const FLDAttributeContributions& Row)
        {return Row.Attribute==Old.Attribute;});
        if(!New||Same(Old.Final,New->Final)||Old.Final.Denominator<=0||New->Final.Denominator<=0){continue;}
        FLDCombatAttributeRowView Label;Label.Attribute=uint8(Old.Attribute);Label.Final=Old.Final;
        const double A=double(Old.Final.Numerator)/double(Old.Final.Denominator);
        const double B=double(New->Final.Numerator)/double(New->Final.Denominator);
        Add(Lines,LDHUD::AttributeRow(Label).Name,AttributeValue(Old.Attribute,Old.Final),AttributeValue(New->Attribute,New->Final),
            A==B?EImpact::Neutral:Impact(A,B,true));
    }
}

FString EffectName(ELDDemoEffectOp Op)
{
    switch(Op)
    {
    case ELDDemoEffectOp::Root: return LDTerms::Root;
    case ELDDemoEffectOp::Stun: return LDTerms::Stun;
    case ELDDemoEffectOp::Silence: return LDTerms::Silence;
    case ELDDemoEffectOp::Disarm: return LDTerms::Disarm;
    case ELDDemoEffectOp::Taunt: return LDTerms::Taunt;
    case ELDDemoEffectOp::Slow: return LDTerms::Slow;
    case ELDDemoEffectOp::Shield: return LDTerms::Shield;
    case ELDDemoEffectOp::Dash: return LDTerms::Dash;
    case ELDDemoEffectOp::Knockback: return LDTerms::WorkshopKnockback;
    case ELDDemoEffectOp::Zone: case ELDDemoEffectOp::SelfArea: return LDTerms::WorkshopArea;
    case ELDDemoEffectOp::ChainBounce: return LDTerms::WorkshopChain;
    case ELDDemoEffectOp::EmpowerBasic: return LDTerms::BasicAttack;
    default: return LDTerms::WorkshopEffect;
    }
}

bool Compiled(const FLDDemoContentSnapshot& Content,const FState& State,const FLDCombatSourceRef& Source,
    const FLDCombatOwnerView& Combat,FLDCombatActiveTerms& Out,FLDInnateDashTerms& Dash,FString& Error)
{
    Out=FLDCombatActiveTerms();
    const bool bInnate=Source.Kind==ELDCombatSourceKind::Innate;
    if(bInnate)
    {
        const FLDDemoDashDefinition& Definition=Content.Get().Character.Dash;
        Out.Target=Definition.Target;Out.Values=Definition.Values;Out.WindupMs=Definition.WindupMs;
        if(!LDCombatSourceRules::InnateDashTerms(Content,Dash,Error)){return false;}
        if(!LDCombatSourceRules::FinalCooldownCost(&Content,State.Input.FinalMod,Combat.bPreviewFinalField,Source,Dash.CooldownMs,Error)){return false;}
    }
    else
    {
        const FLDDemoAbilityDefinition* Ability=nullptr;
        if(!Content.FindAbility(Source.DefinitionId.ToString(),Ability,Error)||!Ability){return false;}
        const FLDDemoAbilityValues* Values=Content.FindValues(*Ability,State.Input.Level);
        if(!Values){Error=TEXT("the active has no values at this level");return false;}
        Out.Target=Ability->Target;Out.Values=*Values;Out.WindupMs=Ability->WindupMs;
        if(!LDCombatSourceRules::FinalCooldownCost(&Content,State.Input.FinalMod,Combat.bPreviewFinalField,Source,Out.Values.CooldownMs,Error)){return false;}
    }
    if(!LDCombatSourceRules::CompileWorkshopActive(State.Build,Source,&Content,Out,Error)){return false;}
    bool bWidened=false;
    if(!LDCombatSourceRules::WidenWorkshopTerms(State.Effective,Source,&Content,Combat.PreviewWideToken,Combat.AsOfMs,Out,bWidened,Error))
    {return false;}
    if(bInnate){LDCombatSourceRules::ScaleInnateDash(Dash,Out);}
    return true;
}

bool Ability(const FLDDemoContentSnapshot& Content,const FState& Before,const FState& After,
    const FLDCombatSourceRef& Source,const FLDCombatOwnerView& Combat,TArray<FLine>& Lines,FString& Error)
{
    FLDCombatActiveTerms Old,New;
    FLDInnateDashTerms OldDash,NewDash;
    if(!Compiled(Content,Before,Source,Combat,Old,OldDash,Error)
        ||!Compiled(Content,After,Source,Combat,New,NewDash,Error)){return false;}
    if(Old.Values.Effects.Num()!=New.Values.Effects.Num()){Error=TEXT("compiled effect counts differ");return false;}
    const FString Name=Source.Kind==ELDCombatSourceKind::Innate?FString(LDTerms::Dash)
        :LDHUD::ContentName(&Content.Get(),Source.DefinitionId.ToString());
    const auto Label=[&Name](const FString& Value){return Name+TEXT(" · ")+Value;};
    for(int32 Index=0;Index<Old.Values.Effects.Num();++Index)
    {
        const FLDDemoEffectDefinition& A=Old.Values.Effects[Index];
        const FLDDemoEffectDefinition& B=New.Values.Effects[Index];
        if(A.Opcode!=B.Opcode){Error=TEXT("compiled effect identities differ");return false;}
        const FString Effect=EffectName(A.GetOp());
        const bool bControl=A.GetOp()==ELDDemoEffectOp::Root||A.GetOp()==ELDDemoEffectOp::Stun
            ||A.GetOp()==ELDDemoEffectOp::Silence||A.GetOp()==ELDDemoEffectOp::Disarm||A.GetOp()==ELDDemoEffectOp::Taunt;
        if(A.DurationMs!=B.DurationMs)
        {Add(Lines,Label(Effect+LDTerms::WorkshopDuration),Seconds(A.DurationMs),Seconds(B.DurationMs),bControl?Impact(A.DurationMs,B.DurationMs,true):EImpact::Neutral);}
        if(!Same(A.DistanceCm,B.DistanceCm)&&A.DistanceCm.Denominator>0&&B.DistanceCm.Denominator>0)
        {
            const double X=double(A.DistanceCm.Numerator)/A.DistanceCm.Denominator,Y=double(B.DistanceCm.Numerator)/B.DistanceCm.Denominator;
            Add(Lines,Label(Effect+LDTerms::WorkshopDistance),Cm(A.DistanceCm),Cm(B.DistanceCm),X==Y?EImpact::Neutral:Impact(X,Y,true));
        }
        if(!Same(A.RadiusCm,B.RadiusCm)&&A.RadiusCm.Denominator>0&&B.RadiusCm.Denominator>0)
        {
            const double X=double(A.RadiusCm.Numerator)/A.RadiusCm.Denominator,Y=double(B.RadiusCm.Numerator)/B.RadiusCm.Denominator;
            Add(Lines,Label(Effect+LDTerms::WorkshopRadius),Cm(A.RadiusCm),Cm(B.RadiusCm),X==Y?EImpact::Neutral:Impact(X,Y,true));
        }
        if(A.Count!=B.Count){Add(Lines,Label(Effect+LDTerms::WorkshopCount),FString::FromInt(A.Count),FString::FromInt(B.Count),Impact(int64(A.Count),int64(B.Count),true));}
        if(A.BaseP2!=B.BaseP2){Add(Lines,Label(Effect+LDTerms::WorkshopBase),Points(A.BaseP2),Points(B.BaseP2),Impact(A.BaseP2,B.BaseP2,true));}
        if(A.ADRatioBp!=B.ADRatioBp)
        {Add(Lines,Label(Effect+LDTerms::AttackDamage),Percent(A.ADRatioBp),Percent(B.ADRatioBp),Impact(A.ADRatioBp,B.ADRatioBp,true));}
        if(A.APRatioBp!=B.APRatioBp)
        {Add(Lines,Label(Effect+LDTerms::AbilityPower),Percent(A.APRatioBp),Percent(B.APRatioBp),Impact(A.APRatioBp,B.APRatioBp,true));}
        if(A.MaxHPRatioBp!=B.MaxHPRatioBp)
        {Add(Lines,Label(Effect+LDTerms::MaxHealth()),Percent(A.MaxHPRatioBp),Percent(B.MaxHPRatioBp),Impact(A.MaxHPRatioBp,B.MaxHPRatioBp,true));}
        if(A.Stats.Num()!=B.Stats.Num()){Error=TEXT("compiled effect stat counts differ");return false;}
        for(int32 Stat=0;Stat<A.Stats.Num();++Stat)
        {
            const FLDDemoStatBonus& X=A.Stats[Stat];const FLDDemoStatBonus& Y=B.Stats[Stat];
            if(X.Stat!=Y.Stat){Error=TEXT("compiled effect stat identities differ");return false;}
            if(X.Stat==ELDDemoBonusStat::AttackSpeedBonusRatio&&X.Amount!=Y.Amount)
            {Add(Lines,Label(LDTerms::AttackSpeed),Percent(X.Amount),Percent(Y.Amount),Impact(X.Amount,Y.Amount,true));}
        }
    }
    if(Old.PackageScaleBp!=New.PackageScaleBp)
    {Add(Lines,Label(LDTerms::WorkshopDamageScale),Percent(Old.PackageScaleBp),Percent(New.PackageScaleBp),Impact(Old.PackageScaleBp,New.PackageScaleBp,true));}
    if(Old.ContactScaleBp!=New.ContactScaleBp)
    {Add(Lines,Label(LDTerms::WorkshopContactScale),Percent(Old.ContactScaleBp),Percent(New.ContactScaleBp),Impact(Old.ContactScaleBp,New.ContactScaleBp,true));}
    const bool bInnate=Source.Kind==ELDCombatSourceKind::Innate;
    int64 OldCooldown=OldDash.CooldownMs,NewCooldown=NewDash.CooldownMs;
    if(!bInnate&&(!LDTactical::CooldownDurationMs(Old.Values.CooldownMs,Before.Effective.Final.CooldownHasteP2,
            Content.Get().Formulas.HasteScaleP2,OldCooldown,Error)
        ||!LDTactical::CooldownDurationMs(New.Values.CooldownMs,After.Effective.Final.CooldownHasteP2,
            Content.Get().Formulas.HasteScaleP2,NewCooldown,Error))){return false;}
    if(OldCooldown!=NewCooldown)
    {Add(Lines,Label(LDTerms::WorkshopCooldown),Seconds(OldCooldown),Seconds(NewCooldown),Impact(OldCooldown,NewCooldown,false));}
    const int64 OldCost=bInnate?OldDash.ManaCostP2:Old.Values.ManaCostP2;
    const int64 NewCost=bInnate?NewDash.ManaCostP2:New.Values.ManaCostP2;
    if(OldCost!=NewCost){Add(Lines,Label(LDTerms::Mana),Points(OldCost),Points(NewCost),Impact(OldCost,NewCost,false));}
    if(Old.Target.RangeCm.Numerator>0||New.Target.RangeCm.Numerator>0)
    {
        double OldRange=0,NewRange=0;
        if(!LDCombatSourceRules::CastRangeCm(Content,Before.Effective,Old.Target,OldRange)
            ||!LDCombatSourceRules::CastRangeCm(Content,After.Effective,New.Target,NewRange))
        {Error=TEXT("compiled cast range is invalid");return false;}
        if(OldRange!=NewRange)
        {Add(Lines,Label(LDTerms::WorkshopRange),Range(OldRange),Range(NewRange),Impact(OldRange,NewRange,true));}
    }
    const auto TargetSize=[&](const TCHAR* Caption,const FLDDemoRational& A,const FLDDemoRational& B)
    {
        if(Same(A,B)||A.Denominator<=0||B.Denominator<=0){return;}
        const double X=double(A.Numerator)/A.Denominator,Y=double(B.Numerator)/B.Denominator;
        Add(Lines,Label(Caption),Cm(A),Cm(B),X==Y?EImpact::Neutral:Impact(X,Y,true));
    };
    TargetSize(LDTerms::WorkshopProjectileRadius,Old.Target.ProjectileRadiusCm,New.Target.ProjectileRadiusCm);
    TargetSize(LDTerms::WorkshopLineHalfWidth,Old.Target.LineHalfWidthCm,New.Target.LineHalfWidthCm);
    return true;
}
}

LDWorkshopPreview::FResult LDWorkshopPreview::Compare(const FInput& Input)
{
    FResult Result;
    const FLDProgressionView* View=Input.Progression;
    const FLDCombatOwnerView* Combat=Input.Combat;
    if(!Input.Content||!View||!Combat||!Input.bClock||!View->bValid||Input.Match==0||View->Generation!=Input.Match
        ||Combat->Generation!=Input.Match||View->Life!=Combat->Life||View->Life==0||Combat->AsOfMs>Input.NowMs
        ||View->CombatInstalledRevision!=Combat->InstalledBuildRevision||View->CatalogDigest!=Input.Content->Get().Identity.Root
        ||Input.bFinalField!=Combat->bPreviewFinalField||Combat->bPreviewLiving!=(View->LifePhase==ELDLifePhase::Alive))
    {return Result;}
    if(View->LifePhase!=ELDLifePhase::Alive&&View->LifePhase!=ELDLifePhase::Dead)
    {Result.Status=EStatus::Unavailable;return Result;}
    ELDDemoElement Element=Input.Element;
    FName Base=Input.Base;
    if(Input.Drop.Elements.Num()==1){Element=Input.Drop.Elements[0];}
    if(Input.Drop.Bases.Num()==1){Base=Input.Drop.Bases[0];}
    if((!Input.Drop.Elements.IsEmpty()&&!Input.Drop.Elements.Contains(Element))
        ||(!Input.Drop.Bases.IsEmpty()&&!Input.Drop.Bases.Contains(Base)))
    {Result.Status=EStatus::ChoiceRequired;return Result;}

    FLDGrowthBuild Current=LDProgression::BuildFromView(Input.Content->Get(),*View);
    Current.FinalMod=View->FinalMod;
    const FLDComponentInstance* Owned=Current.FindComponent(Input.Component);
    if(!Owned){Result.Status=EStatus::Refused;Result.Reason=ELDGrowthReason::NotOwned;return Result;}
    const bool bMoving=Owned->bInstalled;
    LDProgression::FBuildContext Context;Context.Stage=Input.Stage;Context.LifePhase=View->LifePhase;
    FLDGrowthBuild Available=Current;
    if(bMoving)
    {
        Result.Reason=LDProgression::ApplyUninstallComponent(Input.Content->Get(),Current,Input.Component,Context,Available);
        if(Result.Reason!=ELDGrowthReason::None){Result.Status=EStatus::Refused;return Result;}
    }
    FLDComponentTarget Target;Target.Kind=Input.Drop.Kind;Target.Definition=Input.Drop.TargetId;
    Target.BoundActive=Input.Drop.BindActive;Target.Element=Element;Target.BoundComponent=Base;
    FLDGrowthBuild Installed;
    Result.Reason=LDProgression::ApplyInstallComponent(Input.Content->Get(),Available,Input.Component,Target,Context,Installed,Input.Drop.Replacing);
    if(Result.Reason!=ELDGrowthReason::None){Result.Status=EStatus::Refused;return Result;}

    FState Before,Intermediate,After;
    if(!Project(*Input.Content,Current,*Combat,nullptr,Before,Result.Error))
    {Result.Status=EStatus::Unavailable;return Result;}
    if(!MatchesOwner(Before,*Combat)){return Result;}
    if(bMoving&&!Project(*Input.Content,Available,*Combat,&Before,Intermediate,Result.Error))
    {Result.Status=EStatus::Unavailable;return Result;}
    if(!Project(*Input.Content,Installed,*Combat,bMoving?&Intermediate:&Before,After,Result.Error))
    {Result.Status=EStatus::Unavailable;return Result;}
    if(!Partial(*Input.Content,Current,Installed,Result.bPartial,Result.Error))
    {Result.Status=EStatus::Unavailable;return Result;}
    Result.bPartial|=Before.Input.FinalMod!=After.Input.FinalMod;
    for(const FLDLearnedActiveValue& Active:Before.Input.Actives)
    {
        FLDCombatSourceRef Source;Source.Kind=ELDCombatSourceKind::Learned;Source.DefinitionId=Active.AbilityId;
        if(!Ability(*Input.Content,Before,After,Source,*Combat,Result.Lines,Result.Error))
        {Result.Status=EStatus::Unavailable;Result.Lines.Reset();return Result;}
    }
    FLDCombatSourceRef Dash;Dash.Kind=ELDCombatSourceKind::Innate;Dash.DefinitionId=LDCombatSourceRules::InnateDashId();
    if(!Ability(*Input.Content,Before,After,Dash,*Combat,Result.Lines,Result.Error))
    {Result.Status=EStatus::Unavailable;Result.Lines.Reset();return Result;}
    Attributes(Before,After,Result.Lines);
    Result.Status=EStatus::Ready;
    return Result;
}
