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
#include "Materials/MaterialExpressionTime.h"
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

    bool Save(UObject& Asset, FString& Error)
    {
        if (!Asset.GetOutermost()->GetName().StartsWith(FString(LDSliceVfx::GeneratedFolder) + TEXT("/")))
        { return NiagaraFail(Error, TEXT("The Niagara generator only writes under the generated folder: ") + Asset.GetOutermost()->GetName()); }
        const FString File = FPackageName::LongPackageNameToFilename(Asset.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
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
    // What one chain look is made of. Every candidate is a set of these generator options, not new user parameters.
    struct FChainSpec
    {
        const TCHAR* Look = TEXT("V1");     // system NS_LDChain_<Look>, materials MI_LDChain{Arc,Spark}_<Look>
        // Bolt: points per beam, re-spawned LoopHz times a second, each living LifetimeLoops loops (overlap = no gaps).
        int32 Points = 16;
        float LoopHz = 15.f, LifetimeLoops = 1.5f;
        // Ribbon UV: 0 stretches the arc texture once over the beam; > 0 repeats it every TileCm of world length.
        float TileCm = 0.f;
        // Kinks: off = smooth jitter on a tessellated ribbon; on = untessellated straight segments between points whose
        // offsets (KinkCm) are re-randomised KinkHz times a second.
        bool bKinks = false;
        float JitterCm = 14.f, JitterHz = 50.f, KinkCm = 24.f, KinkHz = 15.f;
        // Short side branches leaving the beam part way (0..2), as a share of the bolt width.
        int32 Branches = 0;
        float BranchCm = 70.f;
        // Width taper towards both ends (0 none, 1 to nothing), along the beam's link order.
        float Taper = 0.f;
        // A small dynamic light on each end spark (its radius as a share of the sprite size); 0 none.
        float LightRadius = 0.f;
        // Material: arc atlas rows and flicker rate.
        float ArcRows = 8.f, ArcHz = 15.f;
        // End sparks: count, speed, drag and size as a share of the width.
        int32 Sparks = 5;
        float SparkSpeed = 420.f, SparkDrag = 6.f, SparkSize = 2.2f;
    };

    // NiagaraV1 and, until their designs land, candidates A/B/C share one spec; Capabilities switches every option on so
    // each import proves they still build and compile (it is never shown).
    TArray<FChainSpec> ChainSpecs()
    {
        FChainSpec V1;
        FChainSpec A = V1, B = V1, C = V1;
        A.Look = TEXT("A");
        B.Look = TEXT("B");
        C.Look = TEXT("C");
        FChainSpec Capabilities = V1;
        Capabilities.Look = TEXT("Capabilities");
        Capabilities.Points = 8;
        Capabilities.TileCm = 120.f;
        Capabilities.bKinks = true;
        Capabilities.Branches = 2;
        Capabilities.Taper = .6f;
        Capabilities.LightRadius = 1.5f;
        return {V1, A, B, C, Capabilities};
    }

    FString GeneratedPackage(const FString& Name) { return FString(LDSliceVfx::GeneratedFolder) / Name; }

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
                for (const FNiagaraExt_ModuleTopology& Module : Stack->Modules) { Out += FString::Printf(TEXT(" %s(%d)"), *Module.ModuleName.ToString(), Module.Inputs.Num()); }
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

    bool BuildChain(const FChainSpec& Spec, UMaterial& Material, UTexture2D& Arcs, UTexture2D& Sparks, UNiagaraEmitter& BeamEmitter,
        UNiagaraEmitter& SparkEmitter, FString& Error)
    {
        UMaterialInstanceConstant* ArcPaint = BuildInstance(GeneratedPackage(FString(TEXT("MI_LDChainArc_")) + Spec.Look), Material, &Arcs, Spec.ArcRows, Spec.ArcHz, Error);
        UMaterialInstanceConstant* SparkPaint = BuildInstance(GeneratedPackage(FString(TEXT("MI_LDChainSpark_")) + Spec.Look), Material, &Sparks, 1.f, 0.f, Error);
        if (!ArcPaint || !SparkPaint) { return false; }
        const FString PackageName = GeneratedPackage(FString(TEXT("NS_LDChain_")) + Spec.Look);
        Retire(PackageName);
        FNiagaraExternalEditContext Create;
        UNiagaraSystem* System = UNiagaraExternalEditUtilities::CreateNiagaraSystem(FPackageName::GetShortName(PackageName),
            FPackageName::GetLongPackagePath(PackageName), nullptr, Create);
        UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_STEP create %s %s"), *PackageName, System ? TEXT("ok") : *(TEXT("FAILED ") + Errors(Create)));
        if (!System) { return NiagaraFail(Error, TEXT("CreateNiagaraSystem failed: ") + Errors(Create)); }
        FSteps Steps;
        Steps.System = System;

        // User parameters (LDSliceVfx::Chain*): every one has a typed default, never an empty value.
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
        const float Width = 40.f, Intensity = 2.2f, Duration = .26f;
        User(LDSliceVfx::ChainStart, Vec3, &Origin, sizeof(Origin), TEXT("World start of the link (the caster's hand or the last body struck)."));
        User(LDSliceVfx::ChainEnd, Vec3, &Ahead, sizeof(Ahead), TEXT("World end of the link (the body struck)."));
        User(LDSliceVfx::ChainTint, Colour, &Shock, sizeof(Shock), TEXT("Element ink of the body (LDBroadcast element colour)."));
        User(LDSliceVfx::ChainWidth, FloatType, &Width, sizeof(Width), TEXT("Bolt width in centimetres; the end sparks scale with it."));
        User(LDSliceVfx::ChainIntensity, FloatType, &Intensity, sizeof(Intensity), TEXT("Brightness multiplier of the tint."));
        User(LDSliceVfx::ChainDuration, FloatType, &Duration, sizeof(Duration), TEXT("Seconds the link crackles; the end sparks live this long."));

        TArray<FName> Bolts = {Bolt};
        for (int32 Index = 0; Index < Spec.Branches; ++Index) { Bolts.Add(*FString::Printf(TEXT("Branch%d"), Index)); }
        for (const FName& Name : Bolts)
        {
            Steps.Run(FString::Printf(TEXT("add emitter %s (DynamicBeam)"), *Name.ToString()), [&](FNiagaraExternalEditContext& Context)
            { FNiagaraExt_EmitterTopology Topology; UNiagaraExternalEditUtilities::AddEmitter(&BeamEmitter, Name, Topology, Context); });
        }
        Steps.Run(TEXT("add emitter Spark (SimpleSpriteBurst)"), [&](FNiagaraExternalEditContext& Context)
        { FNiagaraExt_EmitterTopology Topology; UNiagaraExternalEditUtilities::AddEmitter(&SparkEmitter, Spark, Topology, Context); });

        const FNiagaraExt_Variable StartVar = Var(LDSliceVfx::ChainStart, Vec3), EndVar = Var(LDSliceVfx::ChainEnd, Vec3);
        const FNiagaraExt_Variable DurationVar = Var(LDSliceVfx::ChainDuration, FloatType);
        const TCHAR* TintTimesIntensity = TEXT("User.Tint * float4(User.Intensity, User.Intensity, User.Intensity, 1.0)");
        for (int32 Index = 0; Index < Bolts.Num(); ++Index)
        {
            const FName Name = Bolts[Index];
            const bool bBranch = Index > 0;
            // The main bolt spans Start to End; a branch leaves it part way and runs off to one side.
            if (!bBranch)
            {
                Linked(Steps, Name, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Beam Start"), StartVar);
                Linked(Steps, Name, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Beam End"), EndVar);
            }
            else
            {
                const float At = Index == 1 ? .4f : .65f, Side = Index == 1 ? 1.f : -1.f;
                Expression(Steps, Name, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Beam Start"),
                    *FString::Printf(TEXT("lerp(User.Start, User.End, %.2f)"), At));
                Expression(Steps, Name, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Beam End"), *FString::Printf(
                    TEXT("lerp(User.Start, User.End, %.2f) + normalize(cross(User.End - User.Start, float3(0.0, 0.0, 1.0)) + float3(0.0, 0.0, 0.001)) * %.1f"),
                    At + .15f, Side * Spec.BranchCm));
            }
            Bool(Steps, Name, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Absolute Beam Start"), true);
            Bool(Steps, Name, EmitterUpdate, TEXT("BeamEmitterSetup"), TEXT("Absolute Beam End"), true);
            Enum(Steps, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Behavior"), TEXT("Infinite"));
            Float(Steps, Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Duration"), 1.f / Spec.LoopHz);
            Int(Steps, Name, EmitterUpdate, TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count"), bBranch ? FMath::Max(Spec.Points / 2, 3) : Spec.Points);
            Float(Steps, Name, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Lifetime"), Spec.LifetimeLoops / Spec.LoopHz);
            const FString WidthExpression = Spec.Taper > 0.f
                ? FString::Printf(TEXT("User.Width * %.2f * (1.0 - %.2f * abs(Particles.RibbonLinkOrder * 2.0 - 1.0))"), bBranch ? .5f : 1.f, Spec.Taper)
                : FString::Printf(TEXT("User.Width * %.2f"), bBranch ? .5f : 1.f);
            Expression(Steps, Name, ParticleSpawn, TEXT("BeamWidth"), TEXT("Beam Width"), *WidthExpression);
            Expression(Steps, Name, ParticleUpdate, TEXT("Color"), TEXT("Color"), TintTimesIntensity);
            AddModule(Steps, Name, ParticleUpdate, TEXT("UpdateBeam"), JitterModule);
            Float(Steps, Name, ParticleUpdate, TEXT("JitterPosition"), TEXT("Jitter Amount"), Spec.bKinks ? Spec.KinkCm : Spec.JitterCm);
            Float(Steps, Name, ParticleUpdate, TEXT("JitterPosition"), TEXT("Jitter Delay"), 1.f / (Spec.bKinks ? Spec.KinkHz : Spec.JitterHz));
        }

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
        if (Spec.LightRadius > 0.f)
        {
            Steps.Run(TEXT("add light renderer to Spark"), [&](FNiagaraExternalEditContext& Context)
            {
                FNiagaraExt_RendererRef Added;
                UNiagaraExternalEditUtilities::AddRenderer(FNiagaraExt_StackItemReference(System, Spark), UNiagaraLightRendererProperties::StaticClass(), Added, Context);
            });
        }
        if (!Steps.Failed.IsEmpty()) { return NiagaraFail(Error, FString::Printf(TEXT("Niagara Chain %s build step failed: %s"), Spec.Look, *Steps.Failed)); }

        // Renderer properties are plain UPROPERTY edits.
        for (const FName& Name : Bolts)
        {
            auto* Ribbon = Cast<UNiagaraRibbonRendererProperties>(RendererOf(*System, Name));
            if (!Ribbon) { return NiagaraFail(Error, FString::Printf(TEXT("Chain %s emitter %s lost its ribbon renderer."), Spec.Look, *Name.ToString())); }
            Ribbon->Material = ArcPaint;
            Ribbon->bCastShadows = false;
            if (Spec.TileCm > 0.f)
            {
                Ribbon->UV0Settings.DistributionMode = ENiagaraRibbonUVDistributionMode::TiledOverRibbonLength;
                Ribbon->UV0Settings.TilingLength = Spec.TileCm;
            }
            if (Spec.bKinks) { Ribbon->TessellationMode = ENiagaraRibbonTessellationMode::Disabled; }
            Ribbon->PostEditChange();
        }
        auto* Sprite = Cast<UNiagaraSpriteRendererProperties>(RendererOf(*System, Spark));
        if (!Sprite) { return NiagaraFail(Error, FString::Printf(TEXT("Chain %s lost its spark sprite renderer."), Spec.Look)); }
        Sprite->Material = SparkPaint;
        Sprite->SubImageSize = FVector2D(4., 4.);
        Sprite->bSubImageBlend = false;
        Sprite->bCastShadows = false;
        Sprite->PostEditChange();
        if (Spec.LightRadius > 0.f)
        {
            for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
            {
                if (Handle.GetName() != Spark || !Handle.GetEmitterData()) { continue; }
                for (UNiagaraRendererProperties* Renderer : Handle.GetEmitterData()->GetRenderers())
                {
                    if (auto* Light = Cast<UNiagaraLightRendererProperties>(Renderer))
                    {
                        Light->RadiusScale = Spec.LightRadius;
                        Light->bAffectsTranslucency = false;
                        Light->PostEditChange();
                    }
                }
            }
        }

        System->RequestCompile(false);
        System->WaitForCompilationComplete(true, false);
        FNiagaraExternalEditContext Diagnose(System);
        FNiagaraExt_SystemCompileState State;
        UNiagaraExternalEditUtilities::GetSystemCompileState(System, State, Diagnose);
        for (const FNiagaraExt_ScriptCompileInfo& Script : State.Scripts)
        {
            for (const FNiagaraExt_CompileEvent& Event : Script.CompileEvents)
            {
                UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_COMPILE %s %s/%s %d %s"), Spec.Look, *Script.EmitterName.ToString(), *Script.ScriptName.ToString(),
                    int32(Event.Severity), *Event.Message);
            }
        }
        UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_STEP compile %s errors=%d warnings=%d compiling=%d"), Spec.Look, State.bHasErrors, State.bHasWarnings, State.bIsCompiling);
        if (State.bHasErrors) { return NiagaraFail(Error, FString::Printf(TEXT("The Chain %s system does not compile; see LD_NIAGARA_COMPILE lines."), Spec.Look)); }
        const FString Summary = SummaryOf(*System);
        UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_SUMMARY %s %08x %s"), Spec.Look, FCrc::StrCrc32(*Summary), *Summary);
        System->MarkPackageDirty();
        if (!Save(*System, Error)) { return false; }
        UE_LOG(LogLDEditor, Display, TEXT("LD_NIAGARA_STEP saved %s"), *PackageName);
        return true;
    }
}

bool LDDemoVfxNiagara::Build(FString& Error)
{
    UNiagaraEmitter* BeamEmitter = LoadObject<UNiagaraEmitter>(nullptr, BeamTemplate);
    UNiagaraEmitter* SparkEmitter = LoadObject<UNiagaraEmitter>(nullptr, SparkTemplate);
    UTexture2D* Arcs = SliceTexture(LDSliceVfx::ETex::ShockArcs);
    UTexture2D* Sparks = SliceTexture(LDSliceVfx::ETex::ShockSpark);
    if (!BeamEmitter || !SparkEmitter) { return NiagaraFail(Error, TEXT("The engine Niagara emitter templates DynamicBeam / SimpleSpriteBurst are missing.")); }
    if (!Arcs || !Sparks) { return NiagaraFail(Error, TEXT("The slice shock textures are missing; the slice import must run first.")); }
    UMaterial* Material = BuildMaterial(Arcs, Error);
    if (!Material) { return false; }
    for (const FChainSpec& Spec : ChainSpecs())
    {
        if (!BuildChain(Spec, *Material, *Arcs, *Sparks, *BeamEmitter, *SparkEmitter, Error)) { return false; }
    }
    Error.Reset();
    return true;
}

bool LDDemoVfxNiagara::Validate(FString& Error)
{
    for (const FChainSpec& Spec : ChainSpecs())
    {
        const FString PackageName = GeneratedPackage(FString(TEXT("NS_LDChain_")) + Spec.Look);
        UNiagaraSystem* System = FPackageName::DoesPackageExist(PackageName)
            ? LoadObject<UNiagaraSystem>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName))) : nullptr;
        if (!System) { return NiagaraFail(Error, PackageName + TEXT(" is missing; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        TArray<FNiagaraVariable> Parameters;
        System->GetExposedParameters().GetUserParameters(Parameters);
        for (const TCHAR* Name : {LDSliceVfx::ChainStart, LDSliceVfx::ChainEnd, LDSliceVfx::ChainTint, LDSliceVfx::ChainWidth, LDSliceVfx::ChainIntensity, LDSliceVfx::ChainDuration})
        {
            const FString Short = FString(Name).RightChop(5);
            if (!Parameters.ContainsByPredicate([&Short](const FNiagaraVariable& Variable) { return Variable.GetName().ToString().EndsWith(Short); }))
            { return NiagaraFail(Error, PackageName + TEXT(" lacks its user parameter ") + Name + TEXT("; run ImportDemoArt.ps1 -Mode ImportVFX.")); }
        }
    }
    Error.Reset();
    return true;
}