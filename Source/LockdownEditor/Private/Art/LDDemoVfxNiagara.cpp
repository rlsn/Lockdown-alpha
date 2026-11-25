#include "LDDemoVfxNiagara.h"
#include "LDEditorLog.h"
#include "Character/LDHitVfx.h"
#include "UI/LDBroadcastStyle.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NiagaraEmitter.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
    constexpr TCHAR BeamTemplate[] = TEXT("/Niagara/DefaultAssets/Templates/Emitters/DynamicBeam.DynamicBeam");
    constexpr TCHAR SparkTemplate[] = TEXT("/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst");
    constexpr TCHAR JitterModule[] = TEXT("/Niagara/Modules/Update/Position/JitterPosition.JitterPosition");
    constexpr TCHAR DragModule[] = TEXT("/Niagara/Modules/Update/Forces/Drag.Drag");
    constexpr TCHAR VelocityModule[] = TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocityInCone.AddVelocityInCone");
    constexpr TCHAR SubUVModule[] = TEXT("/Niagara/Modules/Update/SubUV/V2/SubUVAnimation.SubUVAnimation");
    const FName EmitterUpdate(TEXT("EmitterUpdateScript")), ParticleSpawn(TEXT("ParticleSpawnScript")), ParticleUpdate(TEXT("ParticleUpdateScript"));
    const FName Bolt(TEXT("Bolt")), Spark(TEXT("Spark"));

    bool NiagaraFail(FString& Error, const FString& Message) { Error = Message; return false; }

    FString Errors(const FNiagaraExternalEditContext& Context)
    {
        FString Out;
        for (const FText& Text : Context.Errors) { Out += Text.ToString() + TEXT(" | "); }
        return Out;
    }

    // Every edit is one logged step; the first one that reports an error stops the build with its text.
    struct FSteps
    {
        UNiagaraSystem* System = nullptr;
        FString Failed;
        bool Run(const FString& What, TFunctionRef<void(FNiagaraExternalEditContext&)> Edit)
        {
            if (!Failed.IsEmpty()) { return false; }
            FNiagaraExternalEditContext Context(System);
            Edit(Context);
            UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_STEP %s %s"), *What, Context.HasErrors() ? *(TEXT("FAILED ") + Errors(Context)) : TEXT("ok"));
            if (Context.HasErrors()) { Failed = What + TEXT(": ") + Errors(Context); }
            return !Context.HasErrors();
        }
    };

    FNiagaraExt_StackItemReference InputOf(UNiagaraSystem* System, FName Emitter, FName Script, FName Module, FName Input)
    {
        FNiagaraExt_StackItemReference Ref(System, Emitter, Script, Module);
        Ref.InputNameStack = {Input};
        return Ref;
    }

    FNiagaraExt_Variable Var(const TCHAR* Name, const FNiagaraTypeDefinition& Type)
    {
        FNiagaraExt_Variable Out;
        Out.Name = Name;
        Out.Type = Type;
        return Out;
    }

    void Linked(FSteps& Steps, FName Emitter, FName Script, FName Module, FName Input, const FNiagaraExt_Variable& Variable)
    {
        Steps.Run(FString::Printf(TEXT("link %s.%s.%s <- %s"), *Emitter.ToString(), *Module.ToString(), *Input.ToString(), *Variable.Name.ToString()),
            [&](FNiagaraExternalEditContext& Context)
            {
                FNiagaraExt_StackInputValue Value;
                Value.InitializeAs<FNiagaraExt_StackInputData_Linked>();
                Value.GetMutable<FNiagaraExt_StackInputData_Linked>().LinkedVariable = Variable;
                UNiagaraExternalEditUtilities::SetStackInputData(InputOf(Steps.System, Emitter, Script, Module, Input), Value, Context);
            });
    }

    void Expression(FSteps& Steps, FName Emitter, FName Script, FName Module, FName Input, const TCHAR* Hlsl)
    {
        Steps.Run(FString::Printf(TEXT("expression %s.%s.%s = %s"), *Emitter.ToString(), *Module.ToString(), *Input.ToString(), Hlsl),
            [&](FNiagaraExternalEditContext& Context)
            {
                FNiagaraExt_StackInputValue Value;
                Value.InitializeAs<FNiagaraExt_StackInputData_HlslExpression>();
                Value.GetMutable<FNiagaraExt_StackInputData_HlslExpression>().HlslExpression = Hlsl;
                UNiagaraExternalEditUtilities::SetStackInputData(InputOf(Steps.System, Emitter, Script, Module, Input), Value, Context);
            });
    }

    template<typename T>
    void Local(FSteps& Steps, FName Emitter, FName Script, FName Module, FName Input, const T& Literal, const FString& Shown)
    {
        Steps.Run(FString::Printf(TEXT("set %s.%s.%s = %s"), *Emitter.ToString(), *Module.ToString(), *Input.ToString(), *Shown),
            [&](FNiagaraExternalEditContext& Context)
            {
                FNiagaraExt_StackInputValue Value;
                Value.InitializeAs<T>(Literal);
                UNiagaraExternalEditUtilities::SetStackInputData(InputOf(Steps.System, Emitter, Script, Module, Input), Value, Context);
            });
    }

    void Float(FSteps& Steps, FName Emitter, FName Script, FName Module, FName Input, float Value)
    {
        FNiagaraFloat Literal;
        Literal.Value = Value;
        Local(Steps, Emitter, Script, Module, Input, Literal, FString::SanitizeFloat(Value));
    }

    void Int(FSteps& Steps, FName Emitter, FName Script, FName Module, FName Input, int32 Value)
    {
        FNiagaraInt32 Literal;
        Literal.Value = Value;
        Local(Steps, Emitter, Script, Module, Input, Literal, FString::FromInt(Value));
    }

    void Bool(FSteps& Steps, FName Emitter, FName Script, FName Module, FName Input, bool bValue)
    {
        FNiagaraBool Literal;
        Literal.SetValue(bValue);
        Local(Steps, Emitter, Script, Module, Input, Literal, bValue ? TEXT("true") : TEXT("false"));
    }

    // An enum input set by the display name of one of its entries ("Infinite", "Direct Set").
    void Enum(FSteps& Steps, FName Emitter, FName Script, FName Module, FName Input, const TCHAR* DisplayName)
    {
        Steps.Run(FString::Printf(TEXT("enum %s.%s.%s = %s"), *Emitter.ToString(), *Module.ToString(), *Input.ToString(), DisplayName),
            [&](FNiagaraExternalEditContext& Context)
            {
                const FNiagaraExt_StackItemReference Ref = InputOf(Steps.System, Emitter, Script, Module, Input);
                FNiagaraExt_StackInputValue Current;
                UNiagaraExternalEditUtilities::GetStackInputData(Ref, Current, Context);
                const FNiagaraExt_StackInputData_Enum* Data = Current.GetPtr<FNiagaraExt_StackInputData_Enum>();
                if (!Data || !Data->Enum)
                {
                    Context.Error(FText::FromString(TEXT("the input does not hold an enum value")));
                    return;
                }
                for (int32 Index = 0; Index < Data->Enum->NumEnums(); ++Index)
                {
                    if (Data->Enum->GetDisplayNameTextByIndex(Index).ToString() == DisplayName)
                    {
                        FNiagaraExt_StackInputValue Value;
                        Value.InitializeAs<FNiagaraExt_StackInputData_Enum>();
                        FNiagaraExt_StackInputData_Enum& Set = Value.GetMutable<FNiagaraExt_StackInputData_Enum>();
                        Set.Enum = Data->Enum;
                        Set.EnumName = Data->Enum->GetNameByIndex(Index);
                        UNiagaraExternalEditUtilities::SetStackInputData(Ref, Value, Context);
                        return;
                    }
                }
                FString Names;
                for (int32 Index = 0; Index < Data->Enum->NumEnums(); ++Index) { Names += Data->Enum->GetDisplayNameTextByIndex(Index).ToString() + TEXT(", "); }
                Context.Error(FText::FromString(FString(TEXT("no entry named ")) + DisplayName + TEXT(" in ") + Names));
            });
    }

    void AddModule(FSteps& Steps, FName Emitter, FName Script, FName After, const TCHAR* Path)
    {
        Steps.Run(FString::Printf(TEXT("add module %s to %s.%s after %s"), Path, *Emitter.ToString(), *Script.ToString(), *After.ToString()),
            [&](FNiagaraExternalEditContext& Context)
            {
                const UNiagaraScript* Module = LoadObject<UNiagaraScript>(nullptr, Path);
                if (!Module) { Context.Error(FText::FromString(TEXT("module asset missing"))); return; }
                FNiagaraExt_ModuleTopology Topology;
                UNiagaraExternalEditUtilities::AddModule(FNiagaraExt_StackItemReference(Steps.System, Emitter, Script, After), Module, Topology, Context);
                for (const FNiagaraExt_StackInputTopology& Input : Topology.Inputs)
                {
                    if (Input.bIsVisible)
                    {
                        UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_TOPOLOGY   %s input %s : %s editable=%d switch=%d"), *Topology.ModuleName.ToString(),
                            *Input.Name.ToString(), *Input.Type.GetName(), Input.bIsEditable, Input.bIsStaticSwitch);
                    }
                }
            });
    }

    // Every package this run saved; the rest of the generated folder is stale and deleted at the end of the build.
    TSet<FString> SavedPackages;

    bool Save(UObject& Asset, FString& Error)
    {
        if (!Asset.GetOutermost()->GetName().StartsWith(FString(LDSliceVfx::GeneratedFolder) + TEXT("/")))
        { return NiagaraFail(Error, TEXT("The Niagara generator only writes under the generated folder: ") + Asset.GetOutermost()->GetName()); }
        const FString File = FPackageName::LongPackageNameToFilename(Asset.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        SavedPackages.Add(Asset.GetOutermost()->GetName());
        return UPackage::SavePackage(Asset.GetOutermost(), &Asset, *File, Args) || NiagaraFail(Error, TEXT("Niagara asset could not be saved: ") + File);
    }

    // An existing asset of this package is moved out of the way, so the build always starts from the same templates.
    void Retire(const FString& PackageName)
    {
        // The generator owns only LDSliceVfx::GeneratedFolder; it never replaces anything outside it.
        if (!ensureMsgf(PackageName.StartsWith(FString(LDSliceVfx::GeneratedFolder) + TEXT("/")), TEXT("%s is outside the generated folder"), *PackageName)) { return; }
        if (!FPackageName::DoesPackageExist(PackageName)) { return; }
        if (UObject* Old = LoadObject<UObject>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName))))
        {
            Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
            Old->ClearFlags(RF_Public | RF_Standalone);
        }
    }

    template<typename T>
    T* AddExpression(UMaterial& Material)
    {
        T* Expression = NewObject<T>(&Material);
        Expression->Material = &Material;
        Material.GetExpressionCollection().AddExpression(Expression);
        return Expression;
    }

    // The particle form of the slice material: the same glow remap (ink 0, Particle Color body at 0.6, white core at 1)
    // with Particle Color's alpha as the fade; Rows > 1 picks one stacked strip variant, changing VariantHz times a second
    // (the lightning atlas's flicker). Sprite flipbooks rely on the renderer's sub-image UVs.
    UMaterial* BuildMaterial(UTexture2D* DefaultMask, FString& Error)
    {
        const FString PackageName = LDSliceVfx::NiagaraMaterial;
        Retire(PackageName);
        UMaterial* Material = NewObject<UMaterial>(CreatePackage(*PackageName), *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
        auto* UV = AddExpression<UMaterialExpressionTextureCoordinate>(*Material);
        auto* Mask = AddExpression<UMaterialExpressionTextureObjectParameter>(*Material);
        Mask->ParameterName = LDSliceVfx::MaskParameter;
        Mask->Texture = DefaultMask;
        Mask->SamplerType = SAMPLERTYPE_LinearColor;
        auto* Particle = AddExpression<UMaterialExpressionParticleColor>(*Material);
        auto* Rows = AddExpression<UMaterialExpressionScalarParameter>(*Material);
        Rows->ParameterName = TEXT("Rows");
        Rows->DefaultValue = 1.f;
        auto* Hz = AddExpression<UMaterialExpressionScalarParameter>(*Material);
        Hz->ParameterName = TEXT("VariantHz");
        Hz->DefaultValue = 0.f;
        auto* Time = AddExpression<UMaterialExpressionTime>(*Material);
        auto* Custom = AddExpression<UMaterialExpressionCustom>(*Material);
        Custom->OutputType = CMOT_Float4;
        Custom->Description = TEXT("LDSliceParticle");
        Custom->Code = TEXT("float R = max(Rows, 1.0);\r\n")
            TEXT("float Row = fmod(floor(Time * VariantHz) * 3.0, R);\r\n")
            TEXT("float4 M = Texture2DSample(Mask, MaskSampler, float2(UV.x, (saturate(UV.y) + Row) / R));\r\n")
            TEXT("float G = M.r;\r\n")
            TEXT("float3 Body = Tint * saturate(G / 0.6);\r\n")
            TEXT("float3 Hot = lerp(Body, max(Tint.r, max(Tint.g, Tint.b)) * 1.4, smoothstep(0.7, 1.0, G));\r\n")
            TEXT("return float4(lerp(float3(0.003, 0.003, 0.004), Hot, saturate(G * 5.0)), M.a * Fade);");
        Custom->Inputs.Reset();
        const auto Bind = [Custom](const TCHAR* Name, UMaterialExpression* Source, int32 Output)
        {
            FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
            Input.InputName = Name;
            Input.Input.Connect(Output, Source);
        };
        Bind(TEXT("UV"), UV, 0);
        Bind(TEXT("Mask"), Mask, 0);
        Bind(TEXT("Tint"), Particle, 0);
        Bind(TEXT("Fade"), Particle, 4);
        Bind(TEXT("Rows"), Rows, 0);
        Bind(TEXT("VariantHz"), Hz, 0);
        Bind(TEXT("Time"), Time, 0);
        auto* Colour = AddExpression<UMaterialExpressionComponentMask>(*Material);
        Colour->Input.Connect(0, Custom);
        Colour->R = Colour->G = Colour->B = 1;
        auto* Coverage = AddExpression<UMaterialExpressionComponentMask>(*Material);
        Coverage->Input.Connect(0, Custom);
        Coverage->A = 1;
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Colour);
        Material->GetEditorOnlyData()->Opacity.Connect(0, Coverage);
        Material->BlendMode = BLEND_Translucent;
        Material->TwoSided = true;
        Material->SetShadingModel(MSM_Unlit);
        bool bNeedsRecompile = false;
        Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_NiagaraSprites);
        Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_NiagaraRibbons);
        Material->PostEditChange();
        Material->MarkPackageDirty();
        return Save(*Material, Error) ? Material : nullptr;
    }

    UMaterialInstanceConstant* BuildInstance(const FString& PackageName, UMaterial& Parent, UTexture2D* Mask, float Rows, float Hz, FString& Error)
    {
        Retire(PackageName);
        auto* Instance = NewObject<UMaterialInstanceConstant>(CreatePackage(*PackageName), *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
        Instance->SetParentEditorOnly(&Parent);
        Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(LDSliceVfx::MaskParameter), Mask);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Rows")), Rows);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("VariantHz")), Hz);
        Instance->PostEditChange();
        Instance->MarkPackageDirty();
        return Save(*Instance, Error) ? Instance : nullptr;
    }

    UTexture2D* SliceTexture(LDSliceVfx::ETex Tex)
    {
        const FString Package = LDSliceVfx::TexturePackage(Tex);
        return LoadObject<UTexture2D>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
    }

    UNiagaraRendererProperties* RendererOf(UNiagaraSystem& System, FName Emitter)
    {
        for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
        {
            if (Handle.GetName() == Emitter)
            {
                FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
                return Data && !Data->GetRenderers().IsEmpty() ? Data->GetRenderers()[0] : nullptr;
            }
        }
        return nullptr;
    }
}

namespace
{
    FString GeneratedPackage(const FString& Name) { return FString(LDSliceVfx::GeneratedFolder) / Name; }

    // ------------------------------------------------------------------ NiagaraV1: the first Niagara chain look

    // The V1 link: a DynamicBeam re-spawned LoopHz times a second (each burst living LifetimeLoops loops, so the loops
    // overlap without gaps) with smooth position jitter, the shock arc atlas stretched once over it, and a spark burst.
    struct FV1Spec
    {
        int32 Points = 16;
        float LoopHz = 15.f, LifetimeLoops = 1.5f;
        float JitterCm = 14.f, JitterHz = 50.f;
        float ArcRows = 8.f, ArcHz = 15.f;
        int32 Sparks = 5;
        float SparkSpeed = 420.f, SparkDrag = 6.f, SparkSize = 2.2f;
    };

    // ------------------------------------------------------------------ candidates A, B and C

    // The trunk of a candidate's link: a piecewise-linear bolt from User.Start to User.End through points spaced about
    // SegCm apart (MinPoints..MaxPoints), each interior point slid along the hop by up to Jitter of a segment and pushed
    // sideways by up to Kink (and vertically UpKink) of a segment, the whole bowed upwards by Bow of its length. Step is
    // the HLSL shape index: the offsets are re-drawn whenever it changes (discrete kinks, never a smooth jitter). Amp
    // scales the offsets (HLSL). Every expression here is plain HLSL over the user parameters and the particle's age and
    // index, so a branch emitter can rebuild any trunk point of the same frame.
    struct FTrunk
    {
        float SegCm = 60.f, MinPoints = 4.f, MaxPoints = 14.f;
        float Jitter = .6f, Kink = .25f, UpKink = .12f, Bow = .08f;
        FString Step = TEXT("floor(Particles.Age * 25.0)");
        FString Amp = TEXT("1.0");

        static FString Length() { return TEXT("length(User.End - User.Start)"); }
        static FString Dir() { return TEXT("((User.End - User.Start) / max(length(User.End - User.Start), 1.0))"); }
        static FString Side() { return FString::Printf(TEXT("normalize(cross(%s, float3(0.0, 0.0, 1.0)) + float3(0.0001, 0.0001, 0.0))"), *Dir()); }
        static FString Up() { return FString::Printf(TEXT("cross(%s, %s)"), *Side(), *Dir()); }
        // A per-link constant so two hops never share a shape.
        static FString Seed() { return TEXT("(frac(dot(User.Start + User.End, float3(0.0131, 0.0173, 0.0057))) * 91.0)"); }
        static FString Hash(const FString& Index, const FString& StepIndex, float Salt)
        {
            return FString::Printf(TEXT("frac(sin((%s) * 12.9898 + (%s) * 78.233 + %s + %.2f) * 43758.5453)"), *Index, *StepIndex, *Seed(), Salt);
        }
        FString Count() const { return FString::Printf(TEXT("clamp(floor(%s / %.1f) + 2.0, %.1f, %.1f)"), *Length(), SegCm, MinPoints, MaxPoints); }
        FString SpawnCount() const { return FString::Printf(TEXT("int(%s)"), *Count()); }
        FString Interior(const FString& Index) const { return FString::Printf(TEXT("(step(0.5, %s) * step(%s, %s - 1.5))"), *Index, *Index, *Count()); }
        FString T(const FString& Index) const
        {
            return FString::Printf(TEXT("saturate(((%s) + (%s - 0.5) * %.2f * %s) / (%s - 1.0))"), *Index, *Hash(Index, Step, 1.7f), Jitter, *Interior(Index), *Count());
        }
        FString Point(const FString& Index, const FString& TOf) const
        {
            return FString::Printf(TEXT("(lerp(User.Start, User.End, %s) + (%s * (%s * 2.0 - 1.0) * %.2f + %s * (%s * 2.0 - 1.0) * %.2f) * (%s / (%s - 1.0)) * %s * (%s)")
                TEXT(" + float3(0.0, 0.0, %.3f) * %s * (1.0 - %s) * %s)"),
                *TOf, *Side(), *Hash(Index, Step, 3.1f), Kink, *Up(), *Hash(Index, Step, 5.3f), UpKink, *Length(), *Count(), *Interior(Index), *Amp,
                4.f * Bow, *TOf, *TOf, *Length());
        }
    };

    const TCHAR* const Own = TEXT("float(Particles.UniqueID)");

    // One emitter of a candidate: a ribbon (the DynamicBeam template), a sprite or a light (the SimpleSpriteBurst template,
    // the light with its sprite renderer swapped for a light renderer). It spawns Count (HLSL int) particles once, each
    // living Lifetime (HLSL seconds), and writes Attributes (HLSL over the user parameters, the particle's age and index,
    // and the attributes set before it) at spawn and every update, after the template's own modules.
    struct FPart
    {
        enum EKind : uint8 { Ribbon, Sprite, Light };
        FName Name;
        EKind Kind = Ribbon;
        FString Count = TEXT("1"), Lifetime = TEXT("0.2");
        TArray<TPair<FString, FString>> Attributes;
        // Material of a ribbon or sprite (an instance of M_LDChain).
        UMaterialInstanceConstant* Paint = nullptr;
        float TileCm = 0.f;                                   // ribbon: UV repeats per world length
        FVector2D SubImages = FVector2D(1., 1.);              // sprite atlas columns and rows
        FVector2D Pivot = FVector2D(.5, .5);                  // sprite pivot in UV space
        ENiagaraSpriteAlignment Align = ENiagaraSpriteAlignment::Unaligned;
        // Ribbon: several ribbons in one emitter, told apart by the int attribute Particles.LDRibbon.
        bool bRibbonIds = false;

        FPart& Set(const TCHAR* Attribute, const FString& Hlsl) { Attributes.Emplace(Attribute, Hlsl); return *this; }
    };

    // A candidate look: its parts plus the colours of its material instances.
    struct FCandidate
    {
        const TCHAR* Look = TEXT("A");
        TArray<FPart> Parts;
    };

    FNiagaraTypeDefinition AttributeType(const FString& Name)
    {
        if (Name == TEXT("Particles.Position")) { return FNiagaraTypeDefinition::GetPositionDef(); }
        if (Name == TEXT("Particles.LDRibbon")) { return FNiagaraTypeDefinition::GetIntDef(); }
        if (Name == TEXT("Particles.Color")) { return FNiagaraTypeDefinition::GetColorDef(); }
        if (Name == TEXT("Particles.SpriteSize")) { return FNiagaraTypeDefinition::GetVec2Def(); }
        if (Name == TEXT("Particles.Velocity") || Name == TEXT("Particles.SpriteAlignment")) { return FNiagaraTypeDefinition::GetVec3Def(); }
        return FNiagaraTypeDefinition::GetFloatDef();
    }

    FString LastModule(UNiagaraSystem* System, FName Emitter, FName Script)
    {
        FNiagaraExternalEditContext Context(System);
        FNiagaraExt_EmitterTopology Topology;
        UNiagaraExternalEditUtilities::GetEmitterTopology(FNiagaraExt_StackItemReference(System, Emitter), Topology, Context);
        const FNiagaraExt_ScriptStackTopology& Stack = Script == ParticleSpawn ? Topology.ParticleSpawnScript
            : Script == ParticleUpdate ? Topology.ParticleUpdateScript : Topology.EmitterUpdateScript;
        return Stack.Modules.IsEmpty() ? FString() : Stack.Modules.Last().ModuleName.ToString();
    }

    // A SetParameters module appended to Script writing each attribute (in order, one module per attribute so a later one
    // reads the earlier ones' new values) from its HLSL expression.
    void SetAttributes(FSteps& Steps, FName Emitter, FName Script, const TArray<TPair<FString, FString>>& Attributes)
    {
        for (const TPair<FString, FString>& Attribute : Attributes)
        {
            FString Module;
            Steps.Run(FString::Printf(TEXT("add set %s to %s.%s"), *Attribute.Key, *Emitter.ToString(), *Script.ToString()), [&](FNiagaraExternalEditContext& Context)
            {
                FNiagaraExt_SetParameterEntry Entry;
                Entry.Variable = Var(*Attribute.Key, AttributeType(Attribute.Key));
                FNiagaraExt_ModuleTopology Topology;
                UNiagaraExternalEditUtilities::AddSetParametersModule(FNiagaraExt_StackItemReference(Steps.System, Emitter, Script, *LastModule(Steps.System, Emitter, Script)),
                    {Entry}, Topology, Context);
                Module = Topology.ModuleName.ToString();
                if (!Context.HasErrors() && Topology.Inputs.Num() != 1) { Context.Error(FText::FromString(FString::Printf(TEXT("expected one input, got %d"), Topology.Inputs.Num()))); }
                if (!Context.HasErrors()) { Module += TEXT("|") + Topology.Inputs[0].Name.ToString(); }
            });
            FString ModuleName, InputName;
            if (!Module.Split(TEXT("|"), &ModuleName, &InputName)) { return; }
            Expression(Steps, Emitter, Script, *ModuleName, *InputName, *Attribute.Value);
        }
    }

    // The candidates' material: one Custom node over the part's texture (glow in R, coverage in A; B's sheets carry their
    // warm-dark edge in B), its noise, the Particle Color (RGB brightness, A fade) and the look's three colours.
    //   Mode 0  glow ramp: edge colour -> body colour (glow 0.6) -> core colour (glow 1)
    //   Mode 1  glow ramp with the noise scrolling along U over the soft layer; the core adds more than it covers
    //   Mode 2  brush stroke: flat body, one highlight band along one edge, the warm-dark edge on the other, dry edges
    //   Mode 3  flat two-tone sheet: body, core where glow > 0.8, warm-dark edge from B
    // bTop: drawn over the bodies (no depth test), for the hit sprites whose centre sits inside the struck body.
    UMaterial* BuildChainMaterial(const TCHAR* Name, bool bTop, UTexture2D* DefaultMask, FString& Error)
    {
        const FString PackageName = GeneratedPackage(Name);
        Retire(PackageName);
        UMaterial* Material = NewObject<UMaterial>(CreatePackage(*PackageName), *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
        auto* UV = AddExpression<UMaterialExpressionTextureCoordinate>(*Material);
        const auto Texture = [&](const TCHAR* Name)
        {
            auto* Object = AddExpression<UMaterialExpressionTextureObjectParameter>(*Material);
            Object->ParameterName = Name;
            Object->Texture = DefaultMask;
            Object->SamplerType = SAMPLERTYPE_LinearColor;
            return Object;
        };
        auto* Mask = Texture(LDSliceVfx::MaskParameter);
        auto* Noise = Texture(TEXT("Noise"));
        auto* Particle = AddExpression<UMaterialExpressionParticleColor>(*Material);
        auto* Time = AddExpression<UMaterialExpressionTime>(*Material);
        const auto Scalar = [&](const TCHAR* Name, float Default)
        {
            auto* Parameter = AddExpression<UMaterialExpressionScalarParameter>(*Material);
            Parameter->ParameterName = Name;
            Parameter->DefaultValue = Default;
            return Parameter;
        };
        const auto Colour = [&](const TCHAR* Name)
        {
            auto* Parameter = AddExpression<UMaterialExpressionVectorParameter>(*Material);
            Parameter->ParameterName = Name;
            Parameter->DefaultValue = FLinearColor::White;
            return Parameter;
        };
        auto* Mode = Scalar(TEXT("Mode"), 0.f);
        auto* CoreGain = Scalar(TEXT("CoreGain"), 1.f);
        auto* BodyGain = Scalar(TEXT("BodyGain"), 1.f);
        auto* NoiseTile = Scalar(TEXT("NoiseTile"), 1.f);
        auto* NoiseSpeed = Scalar(TEXT("NoiseSpeed"), 0.f);
        auto* Core = Colour(TEXT("CoreColor"));
        auto* Body = Colour(TEXT("BodyColor"));
        auto* Edge = Colour(TEXT("EdgeColor"));
        auto* Custom = AddExpression<UMaterialExpressionCustom>(*Material);
        Custom->OutputType = CMOT_Float4;
        Custom->Description = TEXT("LDChain");
        Custom->Code = TEXT("float4 M = Texture2DSample(Mask, MaskSampler, UV);\r\n")
            TEXT("float g = M.r;\r\n")
            TEXT("float a = M.a;\r\n")
            TEXT("float3 C;\r\n")
            TEXT("if (Mode < 1.5)\r\n")
            TEXT("{\r\n")
            TEXT("    float hot = smoothstep(0.62, 0.95, g);\r\n")
            TEXT("    float3 low = lerp(EdgeColor, BodyColor, saturate(g / 0.6)) * BodyGain;\r\n")
            TEXT("    C = lerp(low, CoreColor * CoreGain, hot);\r\n")
            TEXT("    if (Mode > 0.5)\r\n")
            TEXT("    {\r\n")
            TEXT("        float n = Texture2DSample(Noise, NoiseSampler, float2(UV.x * NoiseTile - Time * NoiseSpeed, UV.y * 0.5 + 0.25)).g;\r\n")
            TEXT("        a *= lerp(lerp(1.0, saturate(0.3 + 1.2 * n), 1.0 - hot), 0.5, hot);\r\n")
            TEXT("        C *= lerp(0.7 + 0.7 * n, 1.0, hot);\r\n")
            TEXT("    }\r\n")
            TEXT("}\r\n")
            TEXT("else\r\n")
            TEXT("{\r\n")
            TEXT("    float edge = saturate(M.b);\r\n")
            TEXT("    float3 fill = lerp(BodyColor * BodyGain, CoreColor * CoreGain, step(0.8, g));\r\n")
            TEXT("    if (Mode < 2.5)\r\n")
            TEXT("    {\r\n")
            TEXT("        float n = Texture2DSample(Noise, NoiseSampler, float2(UV.x * NoiseTile, UV.y * 2.5)).r;\r\n")
            TEXT("        float band = step(0.12, UV.y) * step(UV.y, 0.26);\r\n")
            TEXT("        fill = lerp(BodyColor * BodyGain, CoreColor * CoreGain, band);\r\n")
            TEXT("        edge = step(0.8, UV.y) * step(UV.y, 0.92);\r\n")
            TEXT("        float rim = saturate(abs(UV.y - 0.5) * 2.0);\r\n")
            TEXT("        a = step(0.12, UV.y) * step(UV.y, 0.92) * step(rim * rim * rim * 0.7, n) * lerp(1.0, 0.8, edge);\r\n")
            TEXT("    }\r\n")
            TEXT("    C = lerp(fill, EdgeColor, edge);\r\n")
            TEXT("}\r\n")
            TEXT("return float4(C * Tint, saturate(a * Fade));");
        Custom->Inputs.Reset();
        const auto Bind = [Custom](const TCHAR* Name, UMaterialExpression* Source, int32 Output)
        {
            FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
            Input.InputName = Name;
            Input.Input.Connect(Output, Source);
        };
        Bind(TEXT("UV"), UV, 0);
        Bind(TEXT("Mask"), Mask, 0);
        Bind(TEXT("Noise"), Noise, 0);
        Bind(TEXT("Tint"), Particle, 0);
        Bind(TEXT("Fade"), Particle, 4);
        Bind(TEXT("Time"), Time, 0);
        Bind(TEXT("Mode"), Mode, 0);
        Bind(TEXT("CoreGain"), CoreGain, 0);
        Bind(TEXT("BodyGain"), BodyGain, 0);
        Bind(TEXT("NoiseTile"), NoiseTile, 0);
        Bind(TEXT("NoiseSpeed"), NoiseSpeed, 0);
        Bind(TEXT("CoreColor"), Core, 0);
        Bind(TEXT("BodyColor"), Body, 0);
        Bind(TEXT("EdgeColor"), Edge, 0);
        auto* Rgb = AddExpression<UMaterialExpressionComponentMask>(*Material);
        Rgb->Input.Connect(0, Custom);
        Rgb->R = Rgb->G = Rgb->B = 1;
        auto* Coverage = AddExpression<UMaterialExpressionComponentMask>(*Material);
        Coverage->Input.Connect(0, Custom);
        Coverage->A = 1;
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Rgb);
        Material->GetEditorOnlyData()->Opacity.Connect(0, Coverage);
        Material->BlendMode = BLEND_Translucent;
        Material->TwoSided = true;
        Material->bDisableDepthTest = bTop;
        Material->SetShadingModel(MSM_Unlit);
        bool bNeedsRecompile = false;
        Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_NiagaraSprites);
        Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_NiagaraRibbons);
        Material->PostEditChange();
        Material->MarkPackageDirty();
        return Save(*Material, Error) ? Material : nullptr;
    }

    // The three colours of a look (linear) and its glow gains.
    struct FPalette
    {
        FLinearColor Core, Body, Edge;
        float CoreGain = 1.f, BodyGain = 1.f;
    };

    UMaterialInstanceConstant* ChainPaint(const FString& Name, UMaterial& Parent, UTexture2D* Mask, UTexture2D* Noise, float Mode, const FPalette& Palette,
        float NoiseTile, float NoiseSpeed, FString& Error)
    {
        const FString PackageName = GeneratedPackage(Name);
        Retire(PackageName);
        auto* Instance = NewObject<UMaterialInstanceConstant>(CreatePackage(*PackageName), *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
        Instance->SetParentEditorOnly(&Parent);
        Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(LDSliceVfx::MaskParameter), Mask);
        Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Noise")), Noise ? Noise : Mask);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Mode")), Mode);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("CoreGain")), Palette.CoreGain);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BodyGain")), Palette.BodyGain);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("NoiseTile")), NoiseTile);
        Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("NoiseSpeed")), NoiseSpeed);
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("CoreColor")), Palette.Core);
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BodyColor")), Palette.Body);
        Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("EdgeColor")), Palette.Edge);
        Instance->PostEditChange();
        Instance->MarkPackageDirty();
        return Save(*Instance, Error) ? Instance : nullptr;
    }

    // Brightness of a link relative to the local player's own first hop (the runtime's intensity 2.2 at full strength).
    const TCHAR* const Bright = TEXT("(User.Intensity / 2.2)");
    // Width relative to the local player's own first hop (the runtime's 70 cm).
    const TCHAR* const LinkScale = TEXT("(User.Width / 70.0)");

    // Aligned sprites point their V axis along the alignment; the streak, head and shard sheets run along +U, so they turn a
    // quarter (degrees).
    const TCHAR* const Quarter = TEXT("90.0");

    FString Rgba(const FString& Brightness, const FString& Alpha) { return FString::Printf(TEXT("float4(%s, %s, %s, %s)"), *Brightness, *Brightness, *Brightness, *Alpha); }

    // A per-particle random in [0, 1) that stays fixed for the particle's life.
    FString Random(float Salt) { return FTrunk::Hash(Own, TEXT("0.0"), Salt); }

    // A random direction leaving End: along the hop by Forward, upwards by Lift, the rest uniform.
    FString SparkDirection(float Forward, float Lift)
    {
        return FString::Printf(TEXT("normalize(%s * %.2f + float3(%s * 2.0 - 1.0, %s * 2.0 - 1.0, %s * 2.0 - 1.0) + float3(0.0, 0.0, %.2f))"),
            *FTrunk::Dir(), Forward, *Random(11.f), *Random(12.f), *Random(13.f), Lift);
    }

    // A: stylised 3D energy. A bright warm-white core inside a soft energy layer with a darker amber under-layer, the
    // energy noise scrolling along the bolt at a constant density per cm; 30-90 cm segments re-drawn at 25 Hz with a
    // slight upward bow; up to three short branches; a small soft flash, 6-9 velocity-stretched sparks with slight gravity
    // and a short shadowless warm light at the contact. The bolt then narrows to a dim afterimage over 80 ms.
    FCandidate CandidateA(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        const FPalette Palette{FLinearColor(1.f, .91f, .67f), FLinearColor(.89f, .67f, .12f), FLinearColor(.17f, .098f, .016f), 2.6f, 1.3f};
        UTexture2D* Noise = SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise);
        UMaterialInstanceConstant* Trunk = ChainPaint(TEXT("MI_LDChainA_Bolt"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), Noise, 1.f, Palette, 1.f, 1.6f, Error);
        UMaterialInstanceConstant* Flash = ChainPaint(TEXT("MI_LDChainA_Flash"), Top, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Streak = ChainPaint(TEXT("MI_LDChainA_Spark"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        FCandidate A;
        A.Look = TEXT("A");
        if (!Trunk || !Flash || !Streak) { return A; }
        FTrunk Shape;
        Shape.Step = TEXT("floor(min(Particles.Age, User.Duration - 0.04) * 25.0)");
        // The last 80 ms narrow the bolt to 15 % and dim it to a third: the dissipation and its afterimage frame.
        const FString Fade = TEXT("saturate((User.Duration - Particles.Age) / 0.08)");
        FPart& Main = A.Parts.AddDefaulted_GetRef();
        Main.Name = TEXT("Bolt");
        Main.Count = Shape.SpawnCount();
        Main.Lifetime = TEXT("User.Duration + 0.035");
        Main.Paint = Trunk;
        Main.TileCm = 60.f;
        Main.Set(TEXT("Particles.LDT"), Shape.T(Own))
            .Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), Shape.Point(Own, TEXT("Particles.LDT")))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("User.Width * 0.32 * min(1.0, 0.35 + 2.6 * min(Particles.LDT, 1.0 - Particles.LDT)) * lerp(0.15, 1.0, %s)"), *Fade))
            .Set(TEXT("Particles.Color"), Rgba(Bright, FString::Printf(TEXT("lerp(0.35, 1.0, %s)"), *Fade)));
        // Branches leave a trunk point of the same frame at 25-45 degrees, 15-30 % of the hop long, each shown in about
        // half of the 12 Hz windows (2-4 frames at a time).
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const FString Window = TEXT("floor(Particles.Age * 12.0)");
            const auto Pick = [&](float Salt) { return FTrunk::Hash(FString::Printf(TEXT("%d.0"), 40 + Index * 7), Window, Salt); };
            const FString Root = FString::Printf(TEXT("(1.0 + floor(%s * (%s - 2.0)))"), *Pick(.5f), *Shape.Count());
            const FString Angle = FString::Printf(TEXT("radians(25.0 + 20.0 * %s)"), *Pick(1.5f));
            const FString Sign = FString::Printf(TEXT("(step(0.5, %s) * 2.0 - 1.0)"), *Pick(2.5f));
            const FString Reach = FString::Printf(TEXT("(%s * (0.15 + 0.15 * %s))"), *FTrunk::Length(), *Pick(3.5f));
            const FString Along = FString::Printf(TEXT("(%s / 3.0)"), Own);
            FPart& Branch = A.Parts.AddDefaulted_GetRef();
            Branch.Name = *FString::Printf(TEXT("Branch%d"), Index);
            Branch.Count = TEXT("4");
            Branch.Lifetime = TEXT("User.Duration");
            Branch.Paint = Trunk;
            Branch.TileCm = 60.f;
            Branch.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + (%s * cos(%s) + %s * sin(%s) * %s) * %s * %s + %s * (%s * 2.0 - 1.0) * 0.2 * %s / 3.0 * step(0.5, %s)"),
                    *Shape.Point(Root, Shape.T(Root)), *FTrunk::Dir(), *Angle, *FTrunk::Side(), *Angle, *Sign, *Reach, *Along,
                    *FTrunk::Up(), *FTrunk::Hash(Own, Window, 9.1f + Index), *Reach, Own))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("User.Width * 0.15 * (1.0 - 0.8 * %s) * step(%s, 0.55) * %s"), *Along, *Pick(4.5f), *Fade))
                .Set(TEXT("Particles.Color"), Rgba(Bright, TEXT("1.0")));
        }
        FPart& Hit = A.Parts.AddDefaulted_GetRef();
        Hit.Name = TEXT("Flash");
        Hit.Kind = FPart::Sprite;
        Hit.Lifetime = TEXT("0.08");
        Hit.Paint = Flash;
        Hit.Set(TEXT("Particles.Position"), TEXT("User.End"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(70.0, 70.0) * %s"), LinkScale))
            .Set(TEXT("Particles.Color"), Rgba(Bright, TEXT("(1.0 - 0.6 * Particles.NormalizedAge)")));
        FPart& Sparks = A.Parts.AddDefaulted_GetRef();
        Sparks.Name = TEXT("Spark");
        Sparks.Kind = FPart::Sprite;
        Sparks.Count = FString::Printf(TEXT("int(6.0 + 4.0 * frac(%s))"), *FTrunk::Seed());
        Sparks.Lifetime = FString::Printf(TEXT("0.15 + 0.1 * %s"), *Random(14.f));
        Sparks.Paint = Streak;
        Sparks.Align = ENiagaraSpriteAlignment::VelocityAligned;
        const FString Launch = FString::Printf(TEXT("(%s * (350.0 + 300.0 * %s))"), *SparkDirection(.6f, .35f), *Random(15.f));
        Sparks.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s + float3(0.0, 0.0, -600.0) * Particles.Age"), *Launch))
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * Particles.Age + float3(0.0, 0.0, -300.0) * Particles.Age * Particles.Age"), *Launch))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(26.0, 6.5) * %s"), LinkScale))
            .Set(TEXT("Particles.SpriteRotation"), Quarter)
            .Set(TEXT("Particles.Color"), Rgba(Bright, TEXT("(1.0 - Particles.NormalizedAge)")));
        FPart& Light = A.Parts.AddDefaulted_GetRef();
        Light.Name = TEXT("Light");
        Light.Kind = FPart::Light;
        Light.Lifetime = TEXT("0.15");
        Light.Set(TEXT("Particles.Position"), TEXT("User.End + float3(0.0, 0.0, -25.0)"))
            .Set(TEXT("Particles.LightRadius"), FString::Printf(TEXT("170.0 * %s"), LinkScale))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 0.72, 0.34, 1.0) * float4(6.0, 6.0, 6.0, 1.0) * %s * (1.0 - Particles.NormalizedAge)"), Bright));
        return A;
    }

    // B: refined street / comic energy. A flat two-tone gold brush stroke with a cream highlight along one edge and a thin
    // warm-dark edge on the other, dry ragged rims, tapering hard from the source to the target; 3-6 long segments with
    // 30-70 degree kinks held for 2-3 frames then snapped to a new shape (12 Hz, drawn on twos); the first frame is a
    // fainter, wider, straighter smear; one forked tine near the target; at the hit 3-4 angular shards along the hop that
    // break into drifting fragments after two frames (the ground scorch is a slice layer, LDSliceVfx::Impact).
    FCandidate CandidateB(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        const FPalette Palette{FLinearColor(1.f, .86f, .6f), FLinearColor(.5f, .32f, .08f), FLinearColor(.069f, .024f, .007f), 1.f, .9f};
        UMaterialInstanceConstant* Stroke = ChainPaint(TEXT("MI_LDChainB_Stroke"), Material, SliceTexture(LDSliceVfx::ETex::ChainBStrokeProfile),
            SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, Palette, .6f, 0.f, Error);
        UMaterialInstanceConstant* Shards = ChainPaint(TEXT("MI_LDChainB_Shards"), Top, SliceTexture(LDSliceVfx::ETex::ChainBShards), nullptr, 3.f, Palette, 1.f, 0.f, Error);
        FCandidate B;
        B.Look = TEXT("B");
        if (!Stroke || !Shards) { return B; }
        const FString Solid = TEXT("step(0.034, Particles.Age)");
        FTrunk Shape;
        Shape.SegCm = 110.f;
        Shape.MinPoints = 4.f;
        Shape.MaxPoints = 7.f;
        Shape.Jitter = .5f;
        Shape.Kink = .42f;
        Shape.UpKink = .1f;
        Shape.Bow = .05f;
        Shape.Step = TEXT("floor(Particles.Age * 12.0)");
        Shape.Amp = FString::Printf(TEXT("lerp(0.3, 1.0, %s)"), *Solid);
        const FString End = TEXT("saturate((User.Duration - Particles.Age) / 0.05)");
        FPart& Main = B.Parts.AddDefaulted_GetRef();
        Main.Name = TEXT("Bolt");
        Main.Count = Shape.SpawnCount();
        Main.Lifetime = TEXT("User.Duration");
        Main.Paint = Stroke;
        Main.TileCm = 160.f;
        Main.Set(TEXT("Particles.LDT"), Shape.T(Own))
            .Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), Shape.Point(Own, TEXT("Particles.LDT")))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("User.Width * 0.44 * lerp(1.0, 0.1, pow(Particles.LDT, 0.85)) * min(1.0, Particles.LDT * 12.0 + 0.45) * lerp(1.5, 1.0, %s) * (0.7 + 0.6 * %s)"),
                *Solid, *FTrunk::Hash(Own, Shape.Step, 8.3f)))
            .Set(TEXT("Particles.Color"), Rgba(Bright, FString::Printf(TEXT("lerp(0.45, 1.0, %s) * %s"), *Solid, *End)));
        // The tine forks off the last interior kink, 35 +- 10 degrees to a side redrawn with the trunk.
        const FString Root = FString::Printf(TEXT("(%s - 2.0)"), *Shape.Count());
        const auto Pick = [&](float Salt) { return FTrunk::Hash(TEXT("61.0"), Shape.Step, Salt); };
        const FString Angle = FString::Printf(TEXT("radians(25.0 + 20.0 * %s)"), *Pick(.5f));
        const FString Sign = FString::Printf(TEXT("(step(0.5, %s) * 2.0 - 1.0)"), *Pick(1.5f));
        const FString Reach = FString::Printf(TEXT("clamp(0.18 * %s, 40.0, 90.0)"), *FTrunk::Length());
        FPart& Tine = B.Parts.AddDefaulted_GetRef();
        Tine.Name = TEXT("Tine");
        Tine.Count = TEXT("3");
        Tine.Lifetime = TEXT("User.Duration");
        Tine.Paint = Stroke;
        Tine.TileCm = 160.f;
        Tine.Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + (%s * cos(%s) + %s * sin(%s) * %s) * %s * (%s / 2.0)"),
                *Shape.Point(Root, Shape.T(Root)), *FTrunk::Dir(), *Angle, *FTrunk::Side(), *Angle, *Sign, *Reach, Own))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("User.Width * 0.1 * (1.0 - 0.85 * %s / 2.0) * %s"), Own, *Solid))
            .Set(TEXT("Particles.Color"), Rgba(Bright, End));
        // The shards: one atlas cell (3-4 slivers) at the hit point aimed along the hop; row 0 for two frames, then row 1
        // (the same set broken up) drifting on along the hop and fading over 0.15 s.
        const FString Broken = TEXT("step(0.067, Particles.Age)");
        FPart& Hit = B.Parts.AddDefaulted_GetRef();
        Hit.Name = TEXT("Shards");
        Hit.Kind = FPart::Sprite;
        Hit.Lifetime = TEXT("0.217");
        Hit.Paint = Shards;
        Hit.SubImages = FVector2D(4., 2.);
        Hit.Pivot = FVector2D(.3, .5);
        Hit.Align = ENiagaraSpriteAlignment::CustomAlignment;
        Hit.Set(TEXT("Particles.SpriteAlignment"), FTrunk::Dir())
            .Set(TEXT("Particles.SpriteRotation"), Quarter)
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * max(Particles.Age - 0.067, 0.0) * 140.0"), *FTrunk::Dir()))
            .Set(TEXT("Particles.SubImageIndex"), FString::Printf(TEXT("floor(frac(%s) * 4.0) + 4.0 * %s"), *FTrunk::Seed(), *Broken))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(60.0, 60.0) * %s"), LinkScale))
            .Set(TEXT("Particles.Color"), Rgba(Bright, FString::Printf(TEXT("1.0 - %s * saturate((Particles.Age - 0.067) / 0.15)"), *Broken)));
        return B;
    }

    // C: competitive clarity. One thin pale warm-white trunk (8-12 cm, slightly thicker toward the target) of 2-3 long
    // irregular segments, no branches; a spark head runs from the source to the target in 40 ms ahead of it, the trunk holds
    // until 120 ms and cuts, re-drawn once; at the target a pinpoint flash and 4 sparks. Others' links are 60 % wide, at
    // half strength and without sparks. At most 10 particles per hop (the ground tick is a slice layer).
    FCandidate CandidateC(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        const FPalette Palette{FLinearColor(1.f, .91f, .67f), FLinearColor(.79f, .55f, .17f), FLinearColor(.35f, .22f, .06f), 2.2f, 1.2f};
        UMaterialInstanceConstant* Core = ChainPaint(TEXT("MI_LDChainC_Core"), Material, SliceTexture(LDSliceVfx::ETex::ChainCCoreProfile), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Head = ChainPaint(TEXT("MI_LDChainC_Head"), Top, SliceTexture(LDSliceVfx::ETex::ChainCSparkHead), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Pin = ChainPaint(TEXT("MI_LDChainC_Pinpoint"), Top, SliceTexture(LDSliceVfx::ETex::ChainCPinpoint), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        FCandidate C;
        C.Look = TEXT("C");
        if (!Core || !Head || !Pin) { return C; }
        const FString Strength = FString::Printf(TEXT("(%s * lerp(0.714, 1.0, User.Focus))"), Bright);
        const FString Narrow = FString::Printf(TEXT("(%s * lerp(0.75, 1.0, User.Focus))"), LinkScale);
        const FString Arrive = TEXT("saturate(Particles.Age / 0.04)");
        FTrunk Shape;
        Shape.SegCm = 220.f;
        Shape.MinPoints = 3.f;
        Shape.MaxPoints = 4.f;
        Shape.Jitter = .5f;
        Shape.Kink = .2f;
        Shape.UpKink = .06f;
        Shape.Bow = .06f;
        Shape.Step = TEXT("min(floor(Particles.Age / 0.07), 1.0)");
        FPart& Main = C.Parts.AddDefaulted_GetRef();
        Main.Name = TEXT("Bolt");
        Main.Count = Shape.SpawnCount();
        Main.Lifetime = TEXT("0.15");
        Main.Paint = Core;
        Main.TileCm = 60.f;
        Main.Set(TEXT("Particles.LDT"), Shape.T(Own))
            .Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), Shape.Point(Own, TEXT("Particles.LDT")))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("70.0 * 0.15 * %s * lerp(0.7, 1.15, Particles.LDT) * step(Particles.LDT, %s + 0.001) * step(Particles.Age, 0.12)"), *Narrow, *Arrive))
            .Set(TEXT("Particles.Color"), Rgba(Strength, TEXT("1.0")));
        FPart& Runner = C.Parts.AddDefaulted_GetRef();
        Runner.Name = TEXT("Head");
        Runner.Kind = FPart::Sprite;
        Runner.Lifetime = TEXT("0.05");
        Runner.Paint = Head;
        Runner.Align = ENiagaraSpriteAlignment::CustomAlignment;
        Runner.Set(TEXT("Particles.SpriteAlignment"), FTrunk::Dir())
            .Set(TEXT("Particles.SpriteRotation"), Quarter)
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("lerp(User.Start, User.End, %s) + float3(0.0, 0.0, %.3f) * %s * (1.0 - %s) * %s"),
                *Arrive, 4.f * Shape.Bow, *Arrive, *Arrive, *FTrunk::Length()))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(30.0, 7.5) * %s * step(Particles.Age, 0.045)"), *Narrow))
            .Set(TEXT("Particles.Color"), Rgba(Strength, TEXT("1.0")));
        FPart& Flash = C.Parts.AddDefaulted_GetRef();
        Flash.Name = TEXT("Pinpoint");
        Flash.Kind = FPart::Sprite;
        Flash.Lifetime = TEXT("0.15");
        Flash.Paint = Pin;
        Flash.Set(TEXT("Particles.Position"), TEXT("User.End"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(50.0, 50.0) * %s"), *Narrow))
            .Set(TEXT("Particles.Color"), Rgba(Strength, TEXT("step(0.04, Particles.Age) * (1.0 - saturate((Particles.Age - 0.04) / 0.08))")));
        const FString Since = TEXT("max(Particles.Age - 0.04, 0.0)");
        const FString Launch = FString::Printf(TEXT("(%s * (260.0 + 160.0 * %s))"), *SparkDirection(.8f, .3f), *Random(15.f));
        FPart& Sparks = C.Parts.AddDefaulted_GetRef();
        Sparks.Name = TEXT("Spark");
        Sparks.Kind = FPart::Sprite;
        Sparks.Count = TEXT("int(4.0 * User.Focus + 0.01)");
        Sparks.Lifetime = TEXT("0.16");
        Sparks.Paint = Head;
        Sparks.Align = ENiagaraSpriteAlignment::VelocityAligned;
        Sparks.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s + float3(0.0, 0.0, -500.0) * %s"), *Launch, *Since))
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * %s + float3(0.0, 0.0, -250.0) * %s * %s"), *Launch, *Since, *Since, *Since))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(12.0, 3.0) * %s"), *Narrow))
            .Set(TEXT("Particles.SpriteRotation"), Quarter)
            .Set(TEXT("Particles.Color"), Rgba(Strength, FString::Printf(TEXT("step(0.04, Particles.Age) * (1.0 - saturate(%s / 0.12))"), *Since)));
        return C;
    }

    // The shocked-body rim glow (LDSliceVfx::BodyGlowMaterial): an additive unlit shell of the pose pushed 1.2 cm out along
    // its normals so it never fights the body's own depth, bright at the rim (fresnel) and faint across the front.
    // Parameters Glow (colour) and Fade like the afterimage material.
    UMaterial* BuildBodyGlowMaterial(FString& Error)
    {
        const FString PackageName = LDSliceVfx::BodyGlowMaterial;
        Retire(PackageName);
        UMaterial* Material = NewObject<UMaterial>(CreatePackage(*PackageName), *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
        auto* Glow = AddExpression<UMaterialExpressionVectorParameter>(*Material);
        Glow->ParameterName = LDHitVfx::GlowParameter;
        Glow->DefaultValue = FLinearColor(1.f, .74f, .36f);
        auto* Fade = AddExpression<UMaterialExpressionScalarParameter>(*Material);
        Fade->ParameterName = LDHitVfx::FadeParameter;
        Fade->DefaultValue = 0.f;
        auto* Rim = AddExpression<UMaterialExpressionFresnel>(*Material);
        Rim->Exponent = 2.5f;
        Rim->BaseReflectFraction = 0.f;
        auto* Custom = AddExpression<UMaterialExpressionCustom>(*Material);
        Custom->OutputType = CMOT_Float3;
        Custom->Description = TEXT("LDChainBodyGlow");
        Custom->Code = TEXT("return Glow.rgb * Fade * (0.04 + 1.4 * Rim * Rim);");
        Custom->Inputs.Reset();
        const auto Bind = [Custom](const TCHAR* Name, UMaterialExpression* Source)
        {
            FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
            Input.InputName = Name;
            Input.Input.Connect(0, Source);
        };
        Bind(TEXT("Glow"), Glow);
        Bind(TEXT("Fade"), Fade);
        Bind(TEXT("Rim"), Rim);
        auto* Normal = AddExpression<UMaterialExpressionVertexNormalWS>(*Material);
        auto* Push = AddExpression<UMaterialExpressionMultiply>(*Material);
        Push->A.Connect(0, Normal);
        Push->ConstB = 1.2f;
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Custom);
        Material->GetEditorOnlyData()->WorldPositionOffset.Connect(0, Push);
        Material->BlendMode = BLEND_Additive;
        Material->SetShadingModel(MSM_Unlit);
        bool bNeedsRecompile = false;
        Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_SkeletalMesh);
        Material->PostEditChange();
        Material->MarkPackageDirty();
        return Save(*Material, Error) ? Material : nullptr;
    }

    // A+: A's language (warm-white core, soft pale-gold energy layer, no outline, no disc) made richer, with a hit that
    // says "this body got shocked". Per hop, scaled by the link (a bounce's 56 cm link is 80 % of the first hop's 70):
    //   Bolt      the trunk: 55 cm segments, discrete kinks re-drawn at 25 Hz, width breathing with every redraw, bright
    //             surges running source -> target; full for 130 ms, one dim afterimage frame, gone at 165 ms
    //   Strands   two thin filaments twisting round the trunk through its kinks with their own phase (own links only)
    //   Branches  three short kinked forks from the trunk's middle, each shown in about half of the 15 Hz windows
    //   Burst     seven short jagged streaks out of the contact, biased along the hop, 100 ms
    //   Crawl     three small arcs crawling over the struck body for 0.3 s, one at the source
    //   Spark     a spray of velocity-stretched sparks with slight gravity, 0.35-0.55 s (others' links: fewer)
    //   Crackle   tiny sparks shed off the trunk while it lives
    //   Light     a short shadowless warm contact light
    // Others' links (User.Focus 0) are 60 % wide and half as bright. The ground glow and scorch flash are slice layers and
    // the body's rim glow a pooled shell (LDSliceVfx::Impact, ULDHumanoidVisualComponent).
    FCandidate CandidateAPlus(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        const FPalette Palette{FLinearColor(1.f, .93f, .78f), FLinearColor(1.f, .68f, .26f), FLinearColor(.22f, .1f, .03f), 2.8f, 1.2f};
        UTexture2D* Profile = SliceTexture(LDSliceVfx::ETex::ChainAProfile);
        UTexture2D* Noise = SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise);
        UMaterialInstanceConstant* Trunk = ChainPaint(TEXT("MI_LDChainAPlus_Bolt"), Material, Profile, Noise, 1.f, Palette, 1.f, 1.6f, Error);
        UMaterialInstanceConstant* Thread = ChainPaint(TEXT("MI_LDChainAPlus_Thread"), Material, Profile, nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Burst = ChainPaint(TEXT("MI_LDChainAPlus_Burst"), Top, Profile, nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Streak = ChainPaint(TEXT("MI_LDChainAPlus_Spark"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        FCandidate A;
        A.Look = TEXT("APlus");
        if (!Trunk || !Thread || !Burst || !Streak) { return A; }
        FTrunk Shape;
        Shape.SegCm = 55.f;
        Shape.Jitter = .35f;
        Shape.Step = TEXT("floor(min(Particles.Age, 0.13) * 25.0)");
        const FString Focus = TEXT("lerp(0.75, 1.0, User.Focus)");
        const FString Strength = FString::Printf(TEXT("(%s * lerp(0.714, 1.0, User.Focus))"), Bright);
        const FString Scale = FString::Printf(TEXT("(%s * %s)"), LinkScale, *Focus);
        // Full for 130 ms, then one dim, thin afterimage frame.
        const FString After = TEXT("step(0.13, Particles.Age)");
        const FString Breath = FString::Printf(TEXT("(0.75 + 0.5 * %s)"), *FTrunk::Hash(TEXT("0.0"), Shape.Step, 21.f));
        const auto Surge = [](const FString& T) { return FString::Printf(TEXT("exp(-pow((%s - frac(Particles.Age / 0.065)) / 0.14, 2.0))"), *T); };
        const FString Count = Shape.Count();

        FPart& Main = A.Parts.AddDefaulted_GetRef();
        Main.Name = TEXT("Bolt");
        Main.Count = Shape.SpawnCount();
        Main.Lifetime = TEXT("0.165");
        Main.Paint = Trunk;
        Main.TileCm = 60.f;
        Main.Set(TEXT("Particles.LDT"), Shape.T(Own))
            .Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), Shape.Point(Own, TEXT("Particles.LDT")))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("70.0 * 0.32 * %s * min(1.0, 0.35 + 2.6 * min(Particles.LDT, 1.0 - Particles.LDT)) * %s * (1.0 + 0.7 * %s) * lerp(1.0, 0.4, %s)"),
                *Scale, *Breath, *Surge(TEXT("Particles.LDT")), *After))
            .Set(TEXT("Particles.Color"), Rgba(FString::Printf(TEXT("%s * (1.0 + 1.4 * %s)"), *Strength, *Surge(TEXT("Particles.LDT"))), FString::Printf(TEXT("lerp(1.0, 0.35, %s)"), *After)));

        // Two filaments: ribbon r of the emitter, point j of the trunk's points; each passes through the trunk's kinks
        // offset round it by a twist of its own (phase per filament and redraw), meeting the trunk at both ends.
        FPart& Strands = A.Parts.AddDefaulted_GetRef();
        Strands.Name = TEXT("Strands");
        // Others' links (User.Focus 0) go without the filaments: a single thinner, dimmer trunk.
        Strands.Count = FString::Printf(TEXT("int(2.0 * %s * User.Focus + 0.01)"), *Count);
        Strands.Lifetime = TEXT("0.165");
        Strands.Paint = Thread;
        Strands.TileCm = 60.f;
        Strands.bRibbonIds = true;
        const FString J = TEXT("Particles.LDJ");
        const FString Twist = FString::Printf(TEXT("(%s * 1.9 + float(Particles.LDRibbon) * 3.1416 + %s * 2.4)"), *J, *FTrunk::Hash(TEXT("float(Particles.LDRibbon) + 70.0"), Shape.Step, 4.4f));
        Strands.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %s))"), Own, *Count))
            .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %s)"), Own, *Count))
            .Set(TEXT("Particles.LDT"), Shape.T(J))
            .Set(TEXT("Particles.RibbonLinkOrder"), J)
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + (%s * cos(%s) + %s * sin(%s)) * 10.0 * %s * sin(3.1416 * Particles.LDT) * (0.6 + 0.8 * %s)"),
                *Shape.Point(J, TEXT("Particles.LDT")), *FTrunk::Side(), *Twist, *FTrunk::Up(), *Twist, *Scale, *FTrunk::Hash(J, Shape.Step, 6.6f)))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("70.0 * 0.09 * %s * lerp(1.0, 0.4, %s)"), *Scale, *After))
            .Set(TEXT("Particles.Color"), Rgba(FString::Printf(TEXT("%s * 0.9"), *Strength), FString::Printf(TEXT("lerp(0.9, 0.3, %s)"), *After)));

        // Three kinked forks from the trunk's middle third, 12-22 % of the hop (at most 110 cm), at 25-45 degrees, each
        // shown in about half of the 15 Hz windows (two frames at a time).
        FPart& Branches = A.Parts.AddDefaulted_GetRef();
        Branches.Name = TEXT("Branches");
        Branches.Count = TEXT("12");
        Branches.Lifetime = TEXT("0.13");
        Branches.Paint = Thread;
        Branches.TileCm = 60.f;
        Branches.bRibbonIds = true;
        {
            const FString Window = TEXT("floor(Particles.Age * 15.0)");
            const FString Fork = TEXT("float(Particles.LDRibbon) * 7.0 + 40.0");
            const auto Pick = [&](float Salt) { return FTrunk::Hash(Fork, Window, Salt); };
            const FString Root = FString::Printf(TEXT("floor(%s * (0.35 + 0.3 * %s))"), *Count, *Pick(.5f));
            const FString Angle = FString::Printf(TEXT("radians(25.0 + 20.0 * %s)"), *Pick(1.5f));
            const FString Sign = FString::Printf(TEXT("(step(0.5, %s) * 2.0 - 1.0)"), *Pick(2.5f));
            const FString Reach = FString::Printf(TEXT("min(%s * (0.12 + 0.1 * %s), 110.0)"), *FTrunk::Length(), *Pick(3.5f));
            const FString Along = TEXT("(Particles.LDJ / 3.0)");
            Branches.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / 4.0))"), Own))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * 4.0)"), Own))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + (%s * cos(%s) + %s * sin(%s) * %s) * %s * %s + %s * (%s * 2.0 - 1.0) * 0.3 * %s / 3.0 * step(0.5, Particles.LDJ)"),
                    *Shape.Point(Root, Shape.T(Root)), *FTrunk::Dir(), *Angle, *FTrunk::Side(), *Angle, *Sign, *Reach, *Along,
                    *FTrunk::Up(), *FTrunk::Hash(FString::Printf(TEXT("Particles.LDJ + %s"), *Fork), Window, 9.1f), *Reach))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("70.0 * 0.12 * %s * (1.0 - 0.8 * %s) * step(%s, 0.55)"), *Scale, *Along, *Pick(4.5f)))
                .Set(TEXT("Particles.Color"), Rgba(Strength, TEXT("1.0")));
        }

        // The contact burst: seven jagged streaks out of the hit point, along the hop more than back, each with one kink
        // re-drawn at 30 Hz, growing over 25 ms and thinning out over 100 ms.
        FPart& Jag = A.Parts.AddDefaulted_GetRef();
        Jag.Name = TEXT("Burst");
        Jag.Count = TEXT("21");
        Jag.Lifetime = TEXT("0.1");
        Jag.Paint = Burst;
        Jag.TileCm = 60.f;
        Jag.bRibbonIds = true;
        {
            const FString Ray = TEXT("float(Particles.LDRibbon) + 90.0");
            const auto Pick = [&](float Salt) { return FTrunk::Hash(Ray, TEXT("0.0"), Salt); };
            const FString Out = FString::Printf(TEXT("normalize(%s * 1.1 + float3(%s * 2.0 - 1.0, %s * 2.0 - 1.0, (%s * 2.0 - 1.0) * 0.8))"),
                *FTrunk::Dir(), *Pick(1.f), *Pick(2.f), *Pick(3.f));
            const FString Length = FString::Printf(TEXT("((40.0 + 35.0 * %s) * %s * saturate(Particles.Age / 0.025 + 0.4))"), *Pick(4.f), *Scale);
            const FString Across = FString::Printf(TEXT("normalize(cross(%s, float3(0.0, 0.0, 1.0)) + float3(0.0001, 0.0001, 0.0))"), *Out);
            Jag.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / 3.0))"), Own))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * 3.0)"), Own))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * %s * (Particles.LDJ / 2.0) + %s * (%s * 2.0 - 1.0) * 0.35 * %s * 0.5 * step(0.5, Particles.LDJ) * step(Particles.LDJ, 1.5)"),
                    *Out, *Length, *Across, *FTrunk::Hash(FString::Printf(TEXT("%s * 3.0"), *Ray), TEXT("floor(Particles.Age * 30.0)"), 5.f), *Length))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("11.0 * %s * (1.0 - 0.85 * Particles.LDJ / 2.0) * (1.0 - 0.6 * Particles.NormalizedAge)"), *Scale))
                .Set(TEXT("Particles.Color"), Rgba(FString::Printf(TEXT("%s * 2.4"), *Strength), TEXT("1.0")));
        }

        // Small arcs crawling over the struck body for 0.3 s (ribbons 0-2 round User.End at different heights, ribbon 3
        // small round User.Start), redrawn at 20 Hz while their start angle keeps turning.
        FPart& Crawl = A.Parts.AddDefaulted_GetRef();
        Crawl.Name = TEXT("Crawl");
        Crawl.Count = TEXT("20");
        Crawl.Lifetime = TEXT("0.3");
        Crawl.Paint = Thread;
        Crawl.TileCm = 60.f;
        Crawl.bRibbonIds = true;
        {
            const FString Arc = TEXT("float(Particles.LDRibbon) + 120.0");
            const FString Window = TEXT("floor(Particles.Age * 20.0)");
            const FString AtSource = TEXT("step(2.5, float(Particles.LDRibbon))");
            const FString Centre = FString::Printf(TEXT("lerp(User.End + float3(0.0, 0.0, (%s - 0.6) * 70.0), User.Start, %s)"), *FTrunk::Hash(Arc, TEXT("0.0"), 1.f), *AtSource);
            const FString Turn = FString::Printf(TEXT("(%s * 6.2832 + Particles.Age * 9.0 * (%s * 2.0 - 1.0) + Particles.LDJ * 0.5)"),
                *FTrunk::Hash(Arc, TEXT("0.0"), 2.f), *FTrunk::Hash(Arc, TEXT("0.0"), 3.f));
            const FString Radius = FString::Printf(TEXT("(lerp(34.0, 16.0, %s) * %s)"), *AtSource, *Scale);
            Crawl.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / 5.0))"), Own))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * 5.0)"), Own))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + float3(cos(%s), sin(%s), 0.0) * %s * (0.85 + 0.3 * %s) + float3(0.0, 0.0, (%s - 0.5) * 18.0 * %s)"),
                    *Centre, *Turn, *Turn, *Radius, *FTrunk::Hash(FString::Printf(TEXT("Particles.LDJ + %s"), *Arc), Window, 4.f),
                    *FTrunk::Hash(FString::Printf(TEXT("Particles.LDJ + %s"), *Arc), Window, 5.f), *Scale))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("8.0 * %s * sin(3.1416 * Particles.LDJ / 4.0) * step(%s, 0.8)"), *Scale, *FTrunk::Hash(Arc, Window, 6.f)))
                .Set(TEXT("Particles.Color"), Rgba(FString::Printf(TEXT("%s * 2.2"), *Strength), TEXT("(1.0 - Particles.NormalizedAge * Particles.NormalizedAge)")));
        }

        const FString Launch = FString::Printf(TEXT("(%s * (250.0 + 230.0 * %s))"), *SparkDirection(.7f, .5f), *Random(15.f));
        FPart& Sparks = A.Parts.AddDefaulted_GetRef();
        Sparks.Name = TEXT("Spark");
        Sparks.Kind = FPart::Sprite;
        Sparks.Count = FString::Printf(TEXT("int(lerp(5.0, 14.0, User.Focus) * %s + 0.5)"), LinkScale);
        Sparks.Lifetime = FString::Printf(TEXT("0.35 + 0.2 * %s"), *Random(14.f));
        Sparks.Paint = Streak;
        Sparks.Align = ENiagaraSpriteAlignment::VelocityAligned;
        Sparks.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s + float3(0.0, 0.0, -500.0) * Particles.Age"), *Launch))
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * Particles.Age + float3(0.0, 0.0, -250.0) * Particles.Age * Particles.Age"), *Launch))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(40.0, 9.0) * %s"), *Scale))
            .Set(TEXT("Particles.SpriteRotation"), Quarter)
            .Set(TEXT("Particles.Color"), Rgba(FString::Printf(TEXT("%s * 2.0"), *Strength), TEXT("(1.0 - pow(Particles.NormalizedAge, 1.5))")));

        // Crackle: eight tiny sparks shed off trunk points while the trunk lives, each for 50 ms at its own moment.
        FPart& Crackle = A.Parts.AddDefaulted_GetRef();
        Crackle.Name = TEXT("Crackle");
        Crackle.Kind = FPart::Sprite;
        Crackle.Count = TEXT("8");
        Crackle.Lifetime = TEXT("0.165");
        Crackle.Paint = Streak;
        Crackle.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString Root = FString::Printf(TEXT("(1.0 + floor(%s * (%s - 2.0)))"), *Random(21.f), *Count);
            const FString From = FString::Printf(TEXT("(%s * 0.11)"), *Random(22.f));
            const FString Since = FString::Printf(TEXT("max(Particles.Age - %s, 0.0)"), *From);
            const FString Kick = FString::Printf(TEXT("(normalize(float3(%s * 2.0 - 1.0, %s * 2.0 - 1.0, 0.6)) * 180.0)"), *Random(23.f), *Random(24.f));
            Crackle.Set(TEXT("Particles.Velocity"), Kick)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + %s * %s"), *Shape.Point(Root, Shape.T(Root)), *Kick, *Since))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(16.0, 4.0) * %s * step(%s, Particles.Age) * step(Particles.Age, %s + 0.05)"), *Scale, *From, *From))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Rgba(FString::Printf(TEXT("%s * 1.6"), *Strength), TEXT("1.0")));
        }

        FPart& Light = A.Parts.AddDefaulted_GetRef();
        Light.Name = TEXT("Light");
        Light.Kind = FPart::Light;
        Light.Lifetime = TEXT("0.2");
        Light.Set(TEXT("Particles.Position"), TEXT("User.End + float3(0.0, 0.0, -20.0)"))
            .Set(TEXT("Particles.LightRadius"), FString::Printf(TEXT("220.0 * %s"), *Scale))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 0.74, 0.38, 1.0) * float4(9.0, 9.0, 9.0, 1.0) * %s * (1.0 - Particles.NormalizedAge)"), *Strength));
        return A;
    }

    // A compact, order-stable description of a built system: user parameters, emitters, modules with their input count and
    // renderer properties. Two builds of the same code give the same text.
    FString SummaryOf(UNiagaraSystem& System)
    {
        FString Out;
        FNiagaraExternalEditContext Context(&System);
        FNiagaraExt_SystemSummary Summary;
        UNiagaraExternalEditUtilities::GetSystemSummary(&System, Summary, Context);
        for (const FNiagaraExt_UserVariable& Variable : Summary.UserVariables) { Out += FString::Printf(TEXT("user %s:%s;"), *Variable.Name.ToString(), *Variable.Type.GetName()); }
        for (const FNiagaraExt_EmitterSummary& Emitter : Summary.Emitters)
        {
            FNiagaraExt_EmitterTopology Topology;
            UNiagaraExternalEditUtilities::GetEmitterTopology(FNiagaraExt_StackItemReference(&System, Emitter.EmitterName), Topology, Context);
            Out += FString::Printf(TEXT("|emitter %s"), *Emitter.EmitterName.ToString());
            for (const FNiagaraExt_ScriptStackTopology* Stack : {&Topology.EmitterSpawnScript, &Topology.EmitterUpdateScript, &Topology.ParticleSpawnScript, &Topology.ParticleUpdateScript})
            {
                for (const FNiagaraExt_ModuleTopology& Module : Stack->Modules)
                {
                    // A SetParameters module is named after a fresh GUID on every build; its input names it instead.
                    const FString Name = Module.ModuleName.ToString();
                    const bool bSet = Name.StartsWith(TEXT("SetVariables_")) && Module.Inputs.Num() == 1;
                    Out += FString::Printf(TEXT(" %s(%d)"), bSet ? *(TEXT("Set:") + Module.Inputs[0].Name.ToString()) : *Name, Module.Inputs.Num());
                }
            }
            for (const FNiagaraExt_RendererRef& Renderer : Topology.Renderers)
            {
                FNiagaraExt_StackItemReference Ref(&System, Emitter.EmitterName);
                Ref.RendererIndex = Renderer.RendererIndex;
                FNiagaraExt_RendererData Data;
                UNiagaraExternalEditUtilities::GetRendererData(Ref, Data, Context);
                Out += FString::Printf(TEXT(" renderer %s %08x"), Renderer.RendererClass ? *Renderer.RendererClass->GetName() : TEXT("?"), FCrc::StrCrc32(*Data.PropertyValues));
            }
        }
        return Out;
    }

    // A fresh system with the chain's user parameters (LDSliceVfx::Chain*), every one with a typed default.
    UNiagaraSystem* CreateChainSystem(const FString& PackageName, FSteps& Steps, FString& Error)
    {
        Retire(PackageName);
        FNiagaraExternalEditContext Create;
        UNiagaraSystem* System = UNiagaraExternalEditUtilities::CreateNiagaraSystem(FPackageName::GetShortName(PackageName),
            FPackageName::GetLongPackagePath(PackageName), nullptr, Create);
        UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_STEP create %s %s"), *PackageName, System ? TEXT("ok") : *(TEXT("FAILED ") + Errors(Create)));
        if (!System) { NiagaraFail(Error, TEXT("CreateNiagaraSystem failed: ") + Errors(Create)); return nullptr; }
        Steps.System = System;
        const FNiagaraTypeDefinition Vec3 = FNiagaraTypeDefinition::GetVec3Def(), FloatType = FNiagaraTypeDefinition::GetFloatDef(), Colour = FNiagaraTypeDefinition::GetColorDef();
        const auto User = [&Steps](const TCHAR* Name, const FNiagaraTypeDefinition& Type, const void* Default, int32 Size, const TCHAR* Description)
        {
            Steps.Run(FString(TEXT("add user parameter ")) + Name, [&](FNiagaraExternalEditContext& Context)
            {
                FNiagaraExt_UserVariable Variable;
                Variable.Name = Name;
                Variable.Type = Type;
                Variable.Description = FText::FromString(Description);
                FNiagaraVariant Value;
                Value.SetBytes(static_cast<const uint8*>(Default), Size);
                Variable.DefaultValue.Set(Type, Value);
                UNiagaraExternalEditUtilities::AddUserVariable(Steps.System, Variable, Context);
            });
        };
        const FVector3f Origin(0.f, 0.f, 0.f), Ahead(300.f, 0.f, 0.f);
        const FLinearColor Shock = LDBroadcast::Shock;
        const float Width = 70.f, Intensity = 2.2f, Duration = .26f, Focus = 1.f;
        User(LDSliceVfx::ChainStart, Vec3, &Origin, sizeof(Origin), TEXT("World start of the link (the caster's hand or the last body struck)."));
        User(LDSliceVfx::ChainEnd, Vec3, &Ahead, sizeof(Ahead), TEXT("World end of the link (the body struck)."));
        User(LDSliceVfx::ChainTint, Colour, &Shock, sizeof(Shock), TEXT("Element ink of the body (LDBroadcast element colour); the candidate looks keep their own palette."));
        User(LDSliceVfx::ChainWidth, FloatType, &Width, sizeof(Width), TEXT("Link width in centimetres (70 for the local player's own first hop); the looks scale their parts by it."));
        User(LDSliceVfx::ChainIntensity, FloatType, &Intensity, sizeof(Intensity), TEXT("Brightness (2.2 at full strength)."));
        User(LDSliceVfx::ChainDuration, FloatType, &Duration, sizeof(Duration), TEXT("Seconds the link lasts."));
        User(LDSliceVfx::ChainFocus, FloatType, &Focus, sizeof(Focus), TEXT("1 for the local player's own or focused link, 0 for others'."));
        return Steps.Failed.IsEmpty() ? System : nullptr;
    }

    bool CompileAndSave(UNiagaraSystem& System, const TCHAR* Look, const FString& PackageName, FString& Error)
    {
        System.RequestCompile(false);
        System.WaitForCompilationComplete(true, false);
        FNiagaraExternalEditContext Diagnose(&System);
        FNiagaraExt_SystemCompileState State;
        UNiagaraExternalEditUtilities::GetSystemCompileState(&System, State, Diagnose);
        for (const FNiagaraExt_ScriptCompileInfo& Script : State.Scripts)
        {
            for (const FNiagaraExt_CompileEvent& Event : Script.CompileEvents)
            {
                UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_COMPILE %s %s/%s %d %s"), Look, *Script.EmitterName.ToString(), *Script.ScriptName.ToString(),
                    int32(Event.Severity), *Event.Message);
            }
        }
        UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_STEP compile %s errors=%d warnings=%d compiling=%d"), Look, State.bHasErrors, State.bHasWarnings, State.bIsCompiling);
        if (State.bHasErrors) { return NiagaraFail(Error, FString::Printf(TEXT("The Chain %s system does not compile; see LD_NIAGARA_COMPILE lines."), Look)); }
        const FString Summary = SummaryOf(System);
        UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_SUMMARY %s %08x %s"), Look, FCrc::StrCrc32(*Summary), *Summary);
        System.MarkPackageDirty();
        if (!Save(System, Error)) { return false; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_STEP saved %s"), *PackageName);
        return true;
    }

    FVersionedNiagaraEmitterData* EmitterDataOf(UNiagaraSystem& System, FName Emitter)
    {
        for (FNiagaraEmitterHandle& Handle : System.GetEmitterHandles()) { if (Handle.GetName() == Emitter) { return Handle.GetEmitterData(); } }
        return nullptr;
    }

    bool BuildV1(const FV1Spec& Spec, UMaterial& Material, UTexture2D& Arcs, UTexture2D& Sparks, UNiagaraEmitter& BeamEmitter,
        UNiagaraEmitter& SparkEmitter, FString& Error)
    {
        UMaterialInstanceConstant* ArcPaint = BuildInstance(GeneratedPackage(TEXT("MI_LDChainArc_V1")), Material, &Arcs, Spec.ArcRows, Spec.ArcHz, Error);
        UMaterialInstanceConstant* SparkPaint = BuildInstance(GeneratedPackage(TEXT("MI_LDChainSpark_V1")), Material, &Sparks, 1.f, 0.f, Error);
        if (!ArcPaint || !SparkPaint) { return false; }
        const FString PackageName = GeneratedPackage(TEXT("NS_LDChain_V1"));
        FSteps Steps;
        UNiagaraSystem* System = CreateChainSystem(PackageName, Steps, Error);
        if (!System) { return Error.IsEmpty() ? NiagaraFail(Error, TEXT("Chain V1 user parameters: ") + Steps.Failed) : false; }
        Steps.Run(TEXT("add emitter Bolt (DynamicBeam)"), [&](FNiagaraExternalEditContext& Context)
        { FNiagaraExt_EmitterTopology Topology; UNiagaraExternalEditUtilities::AddEmitter(&BeamEmitter, Bolt, Topology, Context); });
        Steps.Run(TEXT("add emitter Spark (SimpleSpriteBurst)"), [&](FNiagaraExternalEditContext& Context)
        { FNiagaraExt_EmitterTopology Topology; UNiagaraExternalEditUtilities::AddEmitter(&SparkEmitter, Spark, Topology, Context); });
        const FNiagaraExt_Variable StartVar = Var(LDSliceVfx::ChainStart, FNiagaraTypeDefinition::GetVec3Def()), EndVar = Var(LDSliceVfx::ChainEnd, FNiagaraTypeDefinition::GetVec3Def());
        const FNiagaraExt_Variable DurationVar = Var(LDSliceVfx::ChainDuration, FNiagaraTypeDefinition::GetFloatDef());
        const TCHAR* TintTimesIntensity = TEXT("User.Tint * float4(User.Intensity, User.Intensity, User.Intensity, 1.0)");
        Linked(Steps, Bolt, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Beam Start"), StartVar);
        Linked(Steps, Bolt, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Beam End"), EndVar);
        Bool(Steps, Bolt, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Absolute Beam Start"), true);
        Bool(Steps, Bolt, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Absolute Beam End"), true);
        Enum(Steps, Bolt, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Behavior"), TEXT("Infinite"));
        Float(Steps, Bolt, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Duration"), 1.f / Spec.LoopHz);
        Int(Steps, Bolt, EmitterUpdate, TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count"), Spec.Points);
        Float(Steps, Bolt, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Lifetime"), Spec.LifetimeLoops / Spec.LoopHz);
        Expression(Steps, Bolt, ParticleSpawn, TEXT("BeamWidth"), TEXT("Beam Width"), TEXT("User.Width * 1.00"));
        Expression(Steps, Bolt, ParticleUpdate, TEXT("Color"), TEXT("Color"), TintTimesIntensity);
        AddModule(Steps, Bolt, ParticleUpdate, TEXT("UpdateBeam"), JitterModule);
        Float(Steps, Bolt, ParticleUpdate, TEXT("JitterPosition"), TEXT("Jitter Amount"), Spec.JitterCm);
        Float(Steps, Bolt, ParticleUpdate, TEXT("JitterPosition"), TEXT("Jitter Delay"), 1.f / Spec.JitterHz);
        // The end sparks: a burst at End flying out with drag, each playing the contact spark flipbook, fading over Duration.
        Enum(Steps, Spark, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Position Mode"), TEXT("Direct Set"));
        Expression(Steps, Spark, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Position"), TEXT("User.End"));
        Linked(Steps, Spark, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Lifetime"), DurationVar);
        Expression(Steps, Spark, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Color"), TintTimesIntensity);
        Expression(Steps, Spark, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Uniform Sprite Size"), *FString::Printf(TEXT("User.Width * %.2f"), Spec.SparkSize));
        Int(Steps, Spark, EmitterUpdate, TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count"), Spec.Sparks);
        AddModule(Steps, Spark, ParticleSpawn, TEXT("InitializeParticle"), VelocityModule);
        Float(Steps, Spark, ParticleSpawn, TEXT("AddVelocityInCone"), TEXT("Velocity Strength"), Spec.SparkSpeed);
        AddModule(Steps, Spark, ParticleUpdate, TEXT("ScaleColor"), DragModule);
        Float(Steps, Spark, ParticleUpdate, TEXT("Drag"), TEXT("Drag"), Spec.SparkDrag);
        AddModule(Steps, Spark, ParticleUpdate, TEXT("ScaleColor"), SubUVModule);
        if (!Steps.Failed.IsEmpty()) { return NiagaraFail(Error, TEXT("Niagara Chain V1 build step failed: ") + Steps.Failed); }

        auto* Ribbon = Cast<UNiagaraRibbonRendererProperties>(RendererOf(*System, Bolt));
        auto* Sprite = Cast<UNiagaraSpriteRendererProperties>(RendererOf(*System, Spark));
        if (!Ribbon || !Sprite) { return NiagaraFail(Error, TEXT("Chain V1 lost a template renderer.")); }
        Ribbon->Material = ArcPaint;
        Ribbon->bCastShadows = false;
        Ribbon->PostEditChange();
        Sprite->Material = SparkPaint;
        Sprite->SubImageSize = FVector2D(4., 4.);
        Sprite->bSubImageBlend = false;
        Sprite->bCastShadows = false;
        Sprite->PostEditChange();
        return CompileAndSave(*System, TEXT("V1"), PackageName, Error);
    }

    bool BuildCandidate(const FCandidate& Look, UNiagaraEmitter& BeamEmitter, UNiagaraEmitter& SparkEmitter, FString& Error)
    {
        if (!Error.IsEmpty()) { return false; }
        const FString PackageName = GeneratedPackage(FString(TEXT("NS_LDChain_")) + Look.Look);
        FSteps Steps;
        UNiagaraSystem* System = CreateChainSystem(PackageName, Steps, Error);
        if (!System) { return Error.IsEmpty() ? NiagaraFail(Error, FString::Printf(TEXT("Chain %s user parameters: %s"), Look.Look, *Steps.Failed)) : false; }
        for (const FPart& Part : Look.Parts)
        {
            const bool bRibbon = Part.Kind == FPart::Ribbon;
            Steps.Run(FString::Printf(TEXT("add emitter %s"), *Part.Name.ToString()), [&](FNiagaraExternalEditContext& Context)
            { FNiagaraExt_EmitterTopology Topology; UNiagaraExternalEditUtilities::AddEmitter(bRibbon ? &BeamEmitter : &SparkEmitter, Part.Name, Topology, Context); });
            if (bRibbon)
            {
                // One burst per activation (the runtime resets the component for each hop), so Particles.UniqueID is the
                // point's index along the bolt.
                Enum(Steps, Part.Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Behavior"), TEXT("Infinite"));
                Float(Steps, Part.Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Duration"), 10.f);
            }
            Expression(Steps, Part.Name, EmitterUpdate, TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count"), *Part.Count);
            Expression(Steps, Part.Name, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Lifetime"), *Part.Lifetime);
            SetAttributes(Steps, Part.Name, ParticleSpawn, Part.Attributes);
            SetAttributes(Steps, Part.Name, ParticleUpdate, Part.Attributes);
            if (Part.Kind == FPart::Light)
            {
                Steps.Run(FString::Printf(TEXT("swap %s sprite renderer for a light"), *Part.Name.ToString()), [&](FNiagaraExternalEditContext& Context)
                {
                    FNiagaraExt_StackItemReference Sprite(System, Part.Name);
                    Sprite.RendererIndex = 0;
                    UNiagaraExternalEditUtilities::RemoveRenderer(Sprite, Context);
                    FNiagaraExt_RendererRef Added;
                    UNiagaraExternalEditUtilities::AddRenderer(FNiagaraExt_StackItemReference(System, Part.Name), UNiagaraLightRendererProperties::StaticClass(), Added, Context);
                });
            }
        }
        if (!Steps.Failed.IsEmpty()) { return NiagaraFail(Error, FString::Printf(TEXT("Niagara Chain %s build step failed: %s"), Look.Look, *Steps.Failed)); }

        // Emitter and renderer properties are plain UPROPERTY edits.
        for (const FPart& Part : Look.Parts)
        {
            FVersionedNiagaraEmitterData* Data = EmitterDataOf(*System, Part.Name);
            if (!Data || Data->GetRenderers().Num() != 1) { return NiagaraFail(Error, FString::Printf(TEXT("Chain %s emitter %s should have one renderer."), Look.Look, *Part.Name.ToString())); }
            // Every part writes world positions.
            Data->bLocalSpace = false;
            UNiagaraRendererProperties* Renderer = Data->GetRenderers()[0];
            if (auto* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer))
            {
                Ribbon->Material = Part.Paint;
                Ribbon->bCastShadows = false;
                Ribbon->TessellationMode = ENiagaraRibbonTessellationMode::Disabled;
                Ribbon->UV0Settings.DistributionMode = ENiagaraRibbonUVDistributionMode::TiledOverRibbonLength;
                Ribbon->UV0Settings.TilingLength = Part.TileCm;
                if (Part.bRibbonIds)
                {
                    const FNiagaraVariable Id(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particles.LDRibbon"));
                    Ribbon->RibbonIdBinding.Setup(Id, Id);
                }
            }
            else if (auto* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
            {
                Sprite->Material = Part.Paint;
                Sprite->bCastShadows = false;
                Sprite->SubImageSize = Part.SubImages;
                Sprite->bSubImageBlend = false;
                Sprite->PivotInUVSpace = Part.Pivot;
                Sprite->Alignment = Part.Align;
            }
            else if (auto* Light = Cast<UNiagaraLightRendererProperties>(Renderer))
            {
                Light->bUseInverseSquaredFalloff = false;
                Light->DefaultExponent = 2.f;
                Light->bAffectsTranslucency = false;
            }
            else { return NiagaraFail(Error, FString::Printf(TEXT("Chain %s emitter %s has an unexpected renderer."), Look.Look, *Part.Name.ToString())); }
            Renderer->PostEditChange();
        }
        return CompileAndSave(*System, Look.Look, PackageName, Error);
    }
}

bool LDDemoVfxNiagara::Build(FString& Error)
{
    SavedPackages.Reset();
    UNiagaraEmitter* BeamEmitter = LoadObject<UNiagaraEmitter>(nullptr, BeamTemplate);
    UNiagaraEmitter* SparkEmitter = LoadObject<UNiagaraEmitter>(nullptr, SparkTemplate);
    UTexture2D* Arcs = SliceTexture(LDSliceVfx::ETex::ShockArcs);
    UTexture2D* Sparks = SliceTexture(LDSliceVfx::ETex::ShockSpark);
    if (!BeamEmitter || !SparkEmitter) { return NiagaraFail(Error, TEXT("The engine Niagara emitter templates DynamicBeam / SimpleSpriteBurst are missing.")); }
    for (int32 Tex = 0; Tex < int32(LDSliceVfx::ETex::Count); ++Tex)
    {
        if (!SliceTexture(LDSliceVfx::ETex(Tex))) { return NiagaraFail(Error, FString::Printf(TEXT("The slice texture %s is missing; the slice import must run first."), LDSliceVfx::TexId(LDSliceVfx::ETex(Tex)))); }
    }
    UMaterial* Particle = BuildMaterial(Arcs, Error);
    UMaterial* Chain = Particle ? BuildChainMaterial(TEXT("M_LDChain"), false, Arcs, Error) : nullptr;
    UMaterial* Top = Chain ? BuildChainMaterial(TEXT("M_LDChainTop"), true, Arcs, Error) : nullptr;
    if (!Particle || !Chain || !Top || !BuildBodyGlowMaterial(Error)) { return false; }
    if (!BuildV1(FV1Spec(), *Particle, *Arcs, *Sparks, *BeamEmitter, *SparkEmitter, Error)) { return false; }
    for (FCandidate (*Make)(UMaterial&, UMaterial&, FString&) : {&CandidateA, &CandidateB, &CandidateC, &CandidateAPlus})
    {
        const FCandidate Look = Make(*Chain, *Top, Error);
        if (!BuildCandidate(Look, *BeamEmitter, *SparkEmitter, Error)) { return false; }
    }
    // The folder is the generator's: a package it no longer builds is deleted.
    TArray<FString> Files;
    const FString Folder = FPackageName::LongPackageNameToFilename(LDSliceVfx::GeneratedFolder);
    IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*") + FPackageName::GetAssetPackageExtension()), true, false);
    for (const FString& File : Files)
    {
        const FString Package = FString(LDSliceVfx::GeneratedFolder) / FPaths::GetBaseFilename(File);
        if (!SavedPackages.Contains(Package))
        {
            UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_STEP delete stale %s"), *Package);
            IFileManager::Get().Delete(*(Folder / File));
        }
    }
    Error.Reset();
    return true;
}

bool LDDemoVfxNiagara::Validate(FString& Error)
{
    for (const TCHAR* Look : {TEXT("V1"), TEXT("A"), TEXT("B"), TEXT("C"), TEXT("APlus")})
    {
        const FString PackageName = GeneratedPackage(FString(TEXT("NS_LDChain_")) + Look);
        UNiagaraSystem* System = FPackageName::DoesPackageExist(PackageName)
            ? LoadObject<UNiagaraSystem>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName))) : nullptr;
        if (!System) { return NiagaraFail(Error, PackageName + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        TArray<FNiagaraVariable> Parameters;
        System->GetExposedParameters().GetUserParameters(Parameters);
        for (const TCHAR* Name : {LDSliceVfx::ChainStart, LDSliceVfx::ChainEnd, LDSliceVfx::ChainTint, LDSliceVfx::ChainWidth, LDSliceVfx::ChainIntensity,
            LDSliceVfx::ChainDuration, LDSliceVfx::ChainFocus})
        {
            const FString Short = FString(Name).RightChop(5);
            if (!Parameters.ContainsByPredicate([&Short](const FNiagaraVariable& Variable) { return Variable.GetName().ToString().EndsWith(Short); }))
            { return NiagaraFail(Error, PackageName + TEXT(" lacks its user parameter ") + Name + TEXT("; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        }
    }
    Error.Reset();
    return true;
}
