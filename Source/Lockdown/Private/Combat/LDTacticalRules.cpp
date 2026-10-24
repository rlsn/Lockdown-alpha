#include "Combat/LDTacticalRules.h"
namespace LDTactical
{
    int64 Milliseconds(double Seconds){return FMath::FloorToInt64(Seconds*1000.+.000001);}
    bool FState::Has(EStatus K,int64 At) const{return Statuses.ContainsByPredicate([&](const FStatus& S){return S.Kind==K&&S.Until>At;});}
    uint16 FState::Mask(int64 At) const{uint16 Result=0;for(const auto& S:Statuses){if(S.Until>At){Result|=1<<uint8(S.Kind);}}return Result;}
    double MaxMP(const FLDCharacterBuild& B){return 180.+12*(LDDevelopment::Effective(B)[3]-8)+6*(LDDevelopment::Effective(B)[4]-8)+5*(B.Level-1);}
    bool Allows(const FState& S,EAction A,int64 At,FString* Why)
    {
        const TCHAR* Reason=nullptr;
        if(S.Has(EStatus::Stun,At)){Reason=TEXT("Stunned: external strong dispel or expiry required");}
        else if(S.Has(EStatus::Hex,At)&&A!=EAction::Move){Reason=TEXT("Hexed: external strong dispel or expiry required");}
        else if(A==EAction::Skill&&S.Has(EStatus::Silence,At)){Reason=TEXT("Silenced: skills blocked; active items remain legal");}
        else if(A==EAction::Item&&S.Has(EStatus::Mute,At)){Reason=TEXT("Muted: active items blocked; skills remain legal");}
        else if((A==EAction::Move||A==EAction::Dash)&&S.Has(EStatus::Root,At)){Reason=TEXT("Rooted: voluntary movement blocked");}
        else if(A==EAction::Dash&&S.GroundedUntil>At){Reason=TEXT("Grounded: destroy emitter or walk out");}
        else if(A==EAction::Attack&&S.Has(EStatus::Disarm,At)){Reason=TEXT("Disarmed: basic attacks blocked");}
        if(Why){*Why=Reason?Reason:TEXT("");}return !Reason;
    }
    void Apply(FState& S,EStatus K,int64 Until,FGuid Source)
    {if(auto* Old=S.Statuses.FindByPredicate([&](const FStatus& V){return V.Kind==K&&V.Source==Source;})){Old->Until=FMath::Max(Old->Until,Until);}else{S.Statuses.Add({K,Until,Source});}}
    void Dispel(FState& S,bool Strong,bool Positive)
    {
        if(Positive){S.Barrier=0;S.BarrierUntil=0;S.AnchorUntil=0;return;}
        S.Statuses.RemoveAll([&](const FStatus& V){return Strong||(V.Kind!=EStatus::Stun&&V.Kind!=EStatus::Hex&&V.Kind!=EStatus::Break);});
    }
    void Barrier(FState& S,double Amount,double MaxHP,int64 Until,int64 At)
    {const double Pool=FMath::Clamp(Amount,0.,.4*MaxHP);if(At>=S.BarrierUntil||Pool>S.Barrier){S.Barrier=Pool;S.BarrierUntil=Until;}}
    bool ActivateGuard(FState& S,int64 At)
    {if(At<S.MajorReadyAt){return false;}S.MajorReadyAt=At+8000;S.GuardUntil=At+1500;S.ConcealedUntil=0;return true;}
    bool GuardControl(FState& S,EStatus K,int64 At)
    {if((K!=EStatus::Stun&&K!=EStatus::Hex)||S.GuardUntil<=At){return false;}S.GuardUntil=0;return true;}
    bool PassiveActive(const FState& S,bool Breakable,int64 At){return !Breakable||!S.Has(EStatus::Break,At);}
    float BodySight(const FState& S,int64 At){return S.Has(EStatus::Blind,At)?200.f:720.f;}
    bool Block(FState& S,int64 At){if(S.BlockUntil<=At){return false;}S.BlockUntil=0;return true;}
    double Damage(double& HP,FState& S,double Amount,EDamage Kind,double Armor,double MR,int64 At)
    {
        if(!FMath::IsFinite(Amount)||Amount<=0){return 0;}
        const double A=FMath::Max(0.,Armor);
        const double Reduction=Kind==EDamage::Physical?FMath::Min(.6,A/(100.+A)):Kind==EDamage::Magical?FMath::Clamp(MR,0.,.6):0;
        double D=FMath::FloorToDouble(Amount*(1-Reduction)*100.+.000001)/100.;
        const double Contact=D;
        if(At>=S.BarrierUntil){S.Barrier=0;}
        const double Absorbed=FMath::Min(D,S.Barrier);S.Barrier-=Absorbed;D-=Absorbed;
        const double Lost=FMath::Min(HP,D);HP=FMath::Max(0.,HP-D);return FMath::Min(Contact,Absorbed+Lost);
    }
    void Regenerate(double& HP,FState& S,double MaxHP,double MPMax,int64 At,int32 MPRegen)
    {
        if(S.LastRegen<0){S.LastRegen=At;return;}if(At<=S.LastRegen){return;}
        const int64 Elapsed=At-S.LastRegen;S.LastRegen=At;
        const int64 H=100*Elapsed+S.HPRegenRemainder,P=int64(MPRegen)*Elapsed+S.MPRegenRemainder;S.HPRegenRemainder=H%1000;S.MPRegenRemainder=P%1000;
        if(HP>0){HP=FMath::Min(MaxHP,HP+(H/1000)/100.);S.MP=FMath::Min(MPMax,S.MP+(P/1000)/100.);}
        S.Statuses.RemoveAll([&](const FStatus& V){return V.Until<=At;});if(S.BarrierUntil<=At){S.Barrier=0;}
    }
    FString StatusText(const FState& S,int64 At)
    {
        static const TCHAR* Names[]={TEXT("Stun / strong dispel"),TEXT("Silence / basic dispel"),TEXT("Mute / basic dispel"),TEXT("Root / basic dispel"),TEXT("Disarm / basic dispel"),TEXT("Hex / strong dispel"),TEXT("Break / strong dispel"),TEXT("Blind / basic dispel")};
        FString Result;
        for(uint8 K=0;K<8;++K){int64 Until=At;for(const auto& V:S.Statuses){if(uint8(V.Kind)==K){Until=FMath::Max(Until,V.Until);}}if(Until>At){Result+=FString::Printf(TEXT("%s %.1fs · "),Names[K],(Until-At)/1000.);}}
        if(S.ConcealedUntil>At){Result+=TEXT("Concealed · ");}
        if(S.GroundedUntil>At){Result+=TEXT("Grounded / source-bound · ");}
        if(S.GuardUntil>At){Result+=TEXT("Control guard / one charge · ");}
        return Result;
    }
}
