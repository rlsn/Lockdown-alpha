#pragma once
#include "CoreMinimal.h"
#include "Character/LDCharacter.h"
#include "GameFramework/Actor.h"
#include "Development/LDCharacterBuild.h"
#include "Combat/LDTacticalRules.h"
#include "Combat/LDEnvironmentRules.h"
#include "Combat/LDTacticalRequest.h"
#include "Inventory/LDInventoryTypes.h"
#include "LDCombatDirector.generated.h"
class ALDCharacter;
class ALDWorldObject;
class ALDPlayerController;
enum class ELDSkillTaskPhase : uint8 { Validate, Accept, Commit };

struct FLDBodyCombatState
{
    FGuid Identity=FGuid::NewGuid();
    int64 WetFrostReadyAt=0;
    double Hp=100;
    double CommittedDamage=20;
    double LastDamage=-1000,LastDealt=-1000,QuietSince=0,ExposedUntil=0;
    double BasicReady=0,AssassinReady=0,DashReady=0,ProtectionUntil=0,HitUntil=0,WarningUntil=0;
    uint8 Action=0;
    double ImpactAt=0,RecoveryUntil=0,DashUntil=0;
    FVector CommittedAim=FVector::ForwardVector,Home=FVector::ZeroVector;
    TWeakObjectPtr<AActor> Target;
    uint64 Order=0;
    uint8 IncomingSector=0;
    // Server-only action snapshot. Skill IDs never double as public pose codes.
    ELDSkill Skill=ELDSkill::None;
    FLDCharacterBuild Snapshot,NPCBuild;
    bool bHasNPCBuild=false,bHoldMoving=false;
    uint32 SkillToken=0,HoldSequence=0;
    double LastSkillHold=-1000;
    FVector SkillAnchor=FVector::ZeroVector;
    LDTactical::FState Tactical;
    uint32 LastCoreToken=0;
    uint32 CancelledCoreThrough=0;
    int64 CoreEndsAt=0;
    FGuid CoreTargetLife;
    FGuid CoreItem;
    FVector CorePoint=FVector::ZeroVector;
    FVector CoreDirection=FVector::ForwardVector;
    uint32 CoreBuildRevision=0;
    double CoreRecoveryUntil=0;
    double NullBellReadyAt=0;
    TWeakObjectPtr<AActor> CoreSecondTarget;
    FGuid CoreSecondLife;
    FVector CoreSecondPoint=FVector::ZeroVector;
    FLDTacticalRequest ContentRequest;
    FGuid DaggerItem;
    int64 DaggerUntil=0,SalveNext=0,SalveEnd=0,SalveDamageAt=0,GateLock=0,LastFootstep=0;
    int32 SalveTicks=0;
    int64 CoverSince=0,WatchAt=0;
    FVector CoverAnchor=FVector::ZeroVector;
    bool CoverConcealed=false;
    FGuid WatchVictim;
    TMap<FName,int64> ProcReady;
    FString ContentChannel;
    FVector ChannelDestination=FVector::ZeroVector;
    bool HadPositiveChannelDamage=false;
    TMap<FGuid,int32> WatchHits;
    bool bAttackOrder=false;
    TWeakObjectPtr<AActor> AttackTarget;
    FGuid AttackLife;
    uint32 AttackToken=0;
};

UCLASS()
class LOCKDOWN_API ALDArcProjectile:public AActor
{
    GENERATED_BODY()
public:
    ALDArcProjectile(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
    FGuid Identity=FGuid::NewGuid();
    TWeakObjectPtr<ALDCharacter> Source;
    TWeakObjectPtr<ALDCharacter> Victim;
    FGuid VictimLife;
    FGuid CasterIdentity;
    TWeakObjectPtr<AActor> ActorVictim;
    FVector Endpoint;
    double Amount=0;
    int64 AdvancedAt=0;
    uint32 Token=0;
    bool Basic=false,Reflected=false,Fire=false;
    FGuid EnvironmentLease;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};

enum class ELDTacticalField : uint8 {Smoke, Ward, Trap, Detector, Grounding, Marker, Decoy, Ember, Prison, Telegraph, Dome, Gate, Trail, Footstep, Scramble, GuardianPulse, WetSurface, FrozenSurface, BurningSurface, ElectricSurface, SmokeSurface, GrowthSurface, OilSurface, Conduit};
UCLASS()
class LOCKDOWN_API ALDTacticalField:public AActor
{
    GENERATED_BODY()
public:
    ALDTacticalField(){bReplicates=false;RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));}
    FGuid Identity=FGuid::NewGuid();
    TWeakObjectPtr<ALDCharacter> Source;
    FGuid SourceLife;
    TWeakObjectPtr<ALDPlayerController> PlayerSource;
    bool bPlayerSource=false;
    ELDTacticalField Kind=ELDTacticalField::Smoke;
    int64 ExpiresAt=0,ArmedAt=0;
    double Hp=1,Radius=0;
    bool Active(int64 At) const;
    TWeakObjectPtr<ALDTacticalField> Partner;
    TWeakObjectPtr<ALDCharacter> Audience;
    FVector Direction=FVector::ForwardVector;
    int64 NextTick=0;
    int32 TickIndex=0;
    double Amount=0;
    FGuid ChargeIdentity;
    ELDItemKind ChargeKind=ELDItemKind::WardTotem;
    bool ItemSource=false;
    ELDSkill Skill=ELDSkill::None;
    double SkillCooldown=0;
    bool EnemyDestroyed=false,EnvironmentPool=false;
    uint8 Conditions=0;
    int64 SuppressedUntil=0;
    bool PersistentZone=false;
    FGuid CreditOwner;
    bool Enabled(int64 At) const {return Active(At)&&At>=SuppressedUntil;}
    TArray<FVector> TravelPath;
    int32 PathIndex=1;
    int64 LastUpdate=0;
};

struct FLDCombatHit
{
    ALDCharacter* Source;
    AActor* Target;
    double Damage;
    LDTactical::EDamage Kind=LDTactical::EDamage::Physical;
    bool Spell=false;
    int32 StunMS=0;
    uint32 Token=0;
    FGuid SourceIdentity;
    bool Interrupt=false;
    int32 SilenceMS=0;
    int32 BreakMS=0;
    int32 SlowMS=0,MuteMS=0;
    bool Basic=false,Periodic=false;
    bool DirectFrost=false,DirectWater=false;
    FGuid TargetLife;
    FGuid CreditOwner,SurfaceLease;
    bool DirectFire=false;
    int32 RootMS=0;
    bool WetLightningBonus=false;
    double ExtraMagical=0;
    int32 BarrierBonus=0,DisarmMS=0,SlowMagnitude=40;
    uint8 EnvironmentKind=0;
};

UCLASS()
class LOCKDOWN_API ALDCorpse : public AActor
{
    GENERATED_BODY()
public:
    ALDCorpse();
    uint8 Presentation=0;
};

UCLASS()
class LOCKDOWN_API ALDCombatDirector : public AActor
{
    GENERATED_BODY()
public:
    ALDCombatDirector();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    void Register(ALDCharacter* Body);
    void Queue(ALDPlayerController* Controller,uint32 Match,uint32 Life,uint32 Request,uint8 Action,const FVector& Direction);
    bool Request(ALDPlayerController* Controller,uint8 Action,const FVector& Direction,FString& Result);
    bool RequestBody(ALDCharacter* Body,uint8 Action,const FVector& Direction,FString& Result);
    void QueueSkill(ALDPlayerController* PC,uint32 Match,uint32 Life,uint32 Request,uint32 BuildRevision,uint8 Slot,const FVector& Direction,FGuid Observed=FGuid(),FVector Point=FVector::ZeroVector);
    bool RequestEquipped(ALDCharacter* Body,uint32 BuildRevision,uint8 Slot,uint32 Token,const FVector& Direction,FString& Result,AActor* ExplicitTarget=nullptr,FVector Point=FVector::ZeroVector);
    void RefreshSkillHold(ALDCharacter* Body,uint32 Token,uint32 Sequence,bool Moving);
    void ReleaseSkillHold(ALDPlayerController* PC,uint32 Token);
    bool RegisterNPCBuild(ALDCharacter* Body,const FLDCharacterBuild& Build);
    // Validate is read-only. Accept may reserve a retry window for this token;
    // Commit runs once after damage and final validation, using the source snapshot.
    using FSkillTask = TFunction<bool(ALDCharacter*,ELDSkill,const FLDCharacterBuild&,uint32,ELDSkillTaskPhase,FString&)>;
    void RegisterSkillTask(AActor* Target,const TSet<ELDSkill>& Skills,FSkillTask Task);
    bool HasSkillTask(AActor* Target) const {return SkillTasks.Contains(Target);}
    FLDBodyCombatState* StateFor(ALDCharacter* Body) { return Bodies.Find(Body); }
    const FLDBodyCombatState* StateFor(const ALDCharacter* Body) const { return Bodies.Find(const_cast<ALDCharacter*>(Body)); }
    uint8 CueFor(const ALDCharacter* Body) const;
    double ExposedFor(const ALDCharacter* Body) const;
    bool Allows(ALDCharacter* Body,LDTactical::EAction Action,FString* Why=nullptr) const;
    void ApplyStatus(ALDCharacter* Body,LDTactical::EStatus Status,int64 Until,FGuid Source);
    bool RequestTacticalItem(ALDPlayerController* PC,FGuid Item,uint32 Token,FString& Why);
    void CancelTactical(ALDCharacter* Body,const FString& Why);
    void Sample(double Now);
    void FlushPastCommand();
    void AdvanceAuthority(double Through);
    bool QuietForPreparation(ALDCharacter* Body) const;
    bool Concealed(const ALDCharacter* Body,bool IncludeGrass=true) const;
    void BreakConcealment(ALDCharacter* Body,bool Offense=false);
    void ExplicitInterrupt(ALDCharacter* Body,double Now);
    bool Push(ALDCharacter* Body,const FVector& Direction,double Distance,double Now);
    void SampleFields(int64 At);
    void QueueContent(ALDPlayerController* PC,const FLDTacticalRequest& Request);
    void ContentHold(ALDPlayerController* PC,uint32 Life,uint32 Token,uint32 Sequence,bool Moving,bool Release=false);
    bool RequestContent(ALDCharacter* Body,const FLDTacticalRequest& Request,AActor* First,AActor* Second,FString& Why);
    bool IsB84Skill(ELDSkill Skill) const;
    bool ReleaseCatalog(ALDCharacter* Body,int64 At,TArray<FLDCombatHit>& Hits,FString& Why);
    bool HasModifier(const ALDCharacter* Body,const TCHAR* Id,int64 At) const;
    bool ClaimProc(ALDCharacter* Body,const TCHAR* Family,int64 At,int64 Cooldown);
    void AugmentBasic(FLDCombatHit& Hit,int64 At);
    double AuraArmor(ALDCharacter* Body,int64 At) const;
    void SampleCatalog(int64 At);
    bool IsContentSkill(ELDSkill Skill) const;
    void SampleContent(int64 At,TArray<FLDCombatHit>& Hits);
    void CompleteContentChannels(int64 At);
    bool ReleaseContent(ALDCharacter* Body,int64 At,TArray<FLDCombatHit>& Hits,FString& Why);
    void DestroyField(ALDTacticalField* Field,const TCHAR* Cause=TEXT("TacticalSourceExpired"));
    ALDTacticalField* CreateField(ALDCharacter* Body,ELDTacticalField Kind,FVector Point,int64 At,int64 Duration,double Hp,double Radius,bool Item=false);
    bool MobilityEndpoint(ALDCharacter* Body,FVector Desired,FVector& Endpoint,const AActor* Ignore=nullptr) const;
    bool AtomicSwap(AActor* First,AActor* Second,ALDCharacter* Caster,int64 At,FString& Why,bool Execute);
    bool HasObjectiveCargo(ALDPlayerController* PC) const;
    bool ProjectileIntercept(ALDArcProjectile* Projectile,FVector From,FVector To,int64 At);
    void WeaponContact(const FLDCombatHit& Hit,ALDCharacter* Victim,double Contact,int64 At);
    void RefreshItemFamilies(ALDPlayerController* PC);
    void EndContentLife(ALDCharacter* Body);
    bool Explored(ALDPlayerController* PC,FVector Point) const;
    // Server-only river contacts and bounded commands; no canonical grid replication.
    void RefreshRiverContact(ALDCharacter* Body,int64 At,bool Boundary=false);
    void SampleRiver(int64 At);
    void CommitRiver(int64 At);
    bool StageRiver(ALDCharacter* Body,FVector Aim,bool Disk,bool Frozen,double Length,double Width,int64 At,int64 Duration);
    bool StageEnvironment(ALDCharacter* Body,FVector Aim,LDEnvironment::EInput Input,bool Disk,double Length,double Width,int64 At,int64 Duration,int32 Limit=16,bool Cone=false);
    FGuid ReserveEnvironment(ALDCharacter* Body,int32 Cells,int64 At,int64 Duration);
    void ReleaseEnvironment(FGuid Lease);
    FGuid ReserveIgnition(ALDCharacter* Body,FVector Point,ALDWorldObject* Socket,int64 At);
    bool CommitIgnition(FGuid Lease,ALDCharacter* Body,ALDWorldObject* Socket,int64 At);
    bool ImpactEnvironment(FGuid Lease,FVector Point,ALDCharacter* Source,int64 At);
    bool EnvironmentOccludes(FVector From,FVector To,bool SurfaceCue=false) const;
    void EnvironmentDamage(int64 At,TArray<FLDCombatHit>& Hits);
    void PublishEnvironmentCues(int64 At);
    TArray<FIntPoint> EnvironmentMask(ALDCharacter* Body,FVector Aim,bool Disk,double Length,double Width,int32 Limit=16,bool Cone=false) const;
    void ElementContact(const FLDCombatHit& Hit,ALDCharacter* Victim,int64 At);
    double LastSampleMs=0;
    uint64 SampleSerial=0;
private:
    friend class ALDB82Probe;
    friend class ALDB83Probe;
    friend class ALDB84Probe;
    friend class ALDB86Probe;
    friend class ALDB91FieldsProbe;
    friend class ALDB91IgnitionProbe;
    friend class ALDB91GrassProbe;
    using FRiverState=LDEnvironment::FRecord;
    using FRiverCell=LDEnvironment::FCell;
    using FRiverCommand=LDEnvironment::FCommand;
    struct FEnvironmentLease {FRiverCommand Command;int32 Count=0;int64 Expires=0;TWeakObjectPtr<ALDWorldObject> Socket;bool Ignition=false;};
    TMap<FGuid,FEnvironmentLease> EnvironmentLeases;
    TArray<TWeakObjectPtr<ALDTacticalField>> EnvironmentPool;
    TSet<FIntPoint> ObscuredCells;
    TMap<TWeakObjectPtr<ALDCharacter>,FRiverState> BurningUnits;
    TArray<FLDCombatHit> SurfaceBursts;
    uint32 EnvironmentRevision=0;
    int64 EnvironmentDamageAt=-1;
    bool EnsureEnvironmentPool();
    bool FullMapEnvironment() const;
    int32 EnvironmentCapacity() const;
    int32 EnvironmentTransitionCapacity() const;
    double EnvironmentSurfaceHint(ALDCharacter* Body,FVector Aim,FIntPoint Cell,bool Disk,double Length) const;
    void PublishFullMapSurfaces(int64 At);
    uint64 SurfaceViewRevision=0;
    FRiverCommand EnvironmentCommand(ALDCharacter* Body,LDEnvironment::EInput Input,int64 At,int64 Duration);
    bool QueueEnvironment(FRiverCommand Command,int64 At,bool Reserved=false);
    void RetireEnvironment(int64 At);
    int32 HeldEnvironment(FGuid Owner=FGuid()) const;
    TMap<FIntPoint,FRiverCell> RiverCells;
    TArray<FRiverCommand> RiverCommands;
    TMap<FIntPoint,FRiverCell> ProjectedEnvironment;
    struct FRiverContact {bool Wet=false;int64 SampledAt=-1;};
    TMap<TWeakObjectPtr<ALDCharacter>,FRiverContact> RiverContacts;
    TMap<FString,int64> FrostReceipts;
    int64 RiverBoundary=-1,RiverTransitionAt=-1;
    int32 RiverEvaluations=0;
    friend class ALDContentProbe;
    friend class ALDContentNetworkProbe;
    struct FResidentReturn { double At; FVector Home; FGuid Wallet;bool HasBuild=false;FLDCharacterBuild Build; };
    TMap<TWeakObjectPtr<ALDCharacter>,FLDBodyCombatState> Bodies;
    TArray<FResidentReturn> Residents;
    struct FMark {TWeakObjectPtr<ALDCharacter> Source,Target;FGuid SourceLife,TargetLife;int64 Until=0,Next=0;};
    TArray<FMark> Marks;
    bool bScrambleLinks=false;
    struct FIntent { TWeakObjectPtr<ALDPlayerController> Controller;uint32 Match,Life,Request;uint8 Action;FVector Direction;uint32 BuildRevision=0;FGuid Observed;FVector Point=FVector::ZeroVector; FLDTacticalRequest Content;bool IsContent=false; };
    TArray<FIntent> Intents;
    double NextSample=0;
    bool bSampling=false,bAdvancing=false;
    double LastAuthorityAt=-1;
    uint64 NextOrder=0;
    struct FTask { FGuid Identity=FGuid::NewGuid(); TSet<ELDSkill> Skills; FSkillTask Execute; };
    TMap<TWeakObjectPtr<AActor>,FTask> SkillTasks;
    TMap<TWeakObjectPtr<ALDPlayerController>,uint32> ReleasedSkills;
    FLDCharacterBuild* BuildFor(ALDCharacter* Body);
    bool SkillTargetLegal(ALDCharacter* Source,AActor* Target,ELDSkill Skill) const;
    void CompleteSkillChannels(double Now);
    void CancelSkill(ALDCharacter* Body,FLDBodyCombatState& State);
    bool LegalTarget(ALDCharacter* Source,AActor* Target,double Reach,const FVector* Aim,double Cone) const;
    AActor* Nearest(ALDCharacter* Source,double Reach,const FVector& Aim,double Cone,bool bAssassin,bool bBodiesOnly=false) const;
    void Die(ALDCharacter* Body,double Now);
    void Sync(ALDCharacter* Body,FLDBodyCombatState& State);
    bool RequestTactical(ALDCharacter* Body,ELDSkill Skill,uint32 Token,AActor* Target,FString& Why,FVector Point,FVector Direction);
    void PrepareTactical(double Now,TArray<FLDCombatHit>& Hits);
};
