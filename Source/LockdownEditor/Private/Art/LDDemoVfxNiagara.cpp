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
#include "Materials/MaterialExpressionParticleSize.h"
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
    const FName EmitterUpdate(TEXT("EmitterUpdateScript")), ParticleSpawn(TEXT("ParticleSpawnScript")), ParticleUpdate(TEXT("ParticleUpdateScript"));

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

    // ------------------------------------------------------------------ the generated systems

    // The trunk of a link: a piecewise-linear bolt from User.Start to User.End through points spaced about
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

    // One emitter of a generated system: a ribbon (the DynamicBeam template), a sprite or a light (the SimpleSpriteBurst template,
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
        // Sprite facing (CustomFacingVector lays a sprite on the ground with Particles.SpriteFacing up).
        ENiagaraSpriteFacingMode Facing = ENiagaraSpriteFacingMode::Automatic;

        FPart& Set(const TCHAR* Attribute, const FString& Hlsl) { Attributes.Emplace(Attribute, Hlsl); return *this; }
    };

    // A generated system: its parts.
    struct FSystemParts
    {
        LDSliceVfx::ESystem System = LDSliceVfx::ESystem::Chain;
        TArray<FPart> Parts;
    };

    FNiagaraTypeDefinition AttributeType(const FString& Name)
    {
        if (Name == TEXT("Particles.Position")) { return FNiagaraTypeDefinition::GetPositionDef(); }
        if (Name == TEXT("Particles.LDRibbon")) { return FNiagaraTypeDefinition::GetIntDef(); }
        if (Name == TEXT("Particles.Color")) { return FNiagaraTypeDefinition::GetColorDef(); }
        if (Name == TEXT("Particles.SpriteSize")) { return FNiagaraTypeDefinition::GetVec2Def(); }
        if (Name == TEXT("Particles.Velocity") || Name == TEXT("Particles.SpriteAlignment") || Name == TEXT("Particles.SpriteFacing")) { return FNiagaraTypeDefinition::GetVec3Def(); }
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

    // The generated systems' material: one Custom node over the part's texture (glow in R, coverage in A), its noise, the
    // Particle Color (RGB tint of the outer layers times brightness, A fade) and three colours.
    //   Mode 0  glow ramp: edge colour -> body colour (glow 0.6) -> core colour (glow 1)
    //   Mode 1  glow ramp with the noise scrolling along U over the soft layer; the core adds more than it covers
    UMaterial* BuildChainMaterial(const TCHAR* Name, bool bTop, UTexture2D* DefaultMask, FString& Error, bool bAdditive = false)
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
        auto* Additive = Scalar(TEXT("Additive"), bAdditive ? 1.f : 0.f);
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
            TEXT("float hot = smoothstep(0.62, 0.95, g);\r\n")
            TEXT("float3 low = lerp(EdgeColor, BodyColor, saturate(g / 0.6)) * BodyGain;\r\n")
            TEXT("float3 C = lerp(low, CoreColor * CoreGain, hot);\r\n")
            TEXT("if (Mode > 0.5)\r\n")
            TEXT("{\r\n")
            TEXT("    float n = Texture2DSample(Noise, NoiseSampler, float2(UV.x * NoiseTile - Time * NoiseSpeed, UV.y * 0.5 + 0.25)).g;\r\n")
            TEXT("    a *= lerp(lerp(1.0, saturate(0.3 + 1.2 * n), 1.0 - hot), 0.5, hot);\r\n")
            TEXT("    C *= lerp(0.7 + 0.7 * n, 1.0, hot);\r\n")
            TEXT("}\r\n")
            TEXT("// Particle Color tints the outer layers; the core keeps its own warm white, only its brightness follows.\r\n")
            TEXT("float Level = max(Tint.r, max(Tint.g, Tint.b));\r\n")
            TEXT("// Additive: the coverage scales the light itself (a warm glow, never a patch).\r\n")
            TEXT("float Cover = saturate(a * Fade);\r\n")
            TEXT("return float4(lerp(C * Tint, C * Level, hot) * lerp(1.0, Cover, Additive), Cover);");
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
        Bind(TEXT("Additive"), Additive, 0);
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
        Material->BlendMode = bAdditive ? BLEND_Additive : BLEND_Translucent;
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

    // The chain (active_zap): warm-white core, soft pale-gold energy layer, no outline, no disc, with a hit that
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
    FSystemParts ChainParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        const FPalette Palette{FLinearColor(1.f, .93f, .78f), FLinearColor(1.f, .68f, .26f), FLinearColor(.22f, .1f, .03f), 2.8f, 1.2f};
        UTexture2D* Profile = SliceTexture(LDSliceVfx::ETex::ChainAProfile);
        UTexture2D* Noise = SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise);
        UMaterialInstanceConstant* Trunk = ChainPaint(TEXT("MI_LDChain_Bolt"), Material, Profile, Noise, 1.f, Palette, 1.f, 1.6f, Error);
        UMaterialInstanceConstant* Thread = ChainPaint(TEXT("MI_LDChain_Thread"), Material, Profile, nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Burst = ChainPaint(TEXT("MI_LDChain_Burst"), Top, Profile, nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Streak = ChainPaint(TEXT("MI_LDChain_Spark"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        FSystemParts A;
        A.System = LDSliceVfx::ESystem::Chain;
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

    // The neutral palette of the physical effects: a warm-white core over a white body the Particle Color tints (the
    // runtime passes the strike's ink: the local player's own in a soft orange, others' a pale ivory).
    const FPalette Neutral{FLinearColor(1.f, .95f, .86f), FLinearColor(.92f, .92f, .92f), FLinearColor(.22f, .2f, .18f), 2.4f, 1.2f};
    // The strike's tint times its strength (others' half as bright, the runtime's intensity included).
    FString Tinted(float Gain, const FString& Alpha)
    {
        return FString::Printf(TEXT("float4(User.Tint.r, User.Tint.g, User.Tint.b, 1.0) * float4(%s, %s, %s, 0.0) * %.2f + float4(0.0, 0.0, 0.0, %s)"),
            Bright, Bright, Bright, Gain, *Alpha);
    }

    // A directional physical hit at User.End struck from User.Start (the strike direction): straight tapered impact streaks
    // fanned along the strike, a spray of sparks with slight gravity and, for the finisher, an expanding impact arc in front
    // of the body and a short warm contact light. Basic < finisher by silhouette and richness, not only size: the basic hit
    // is five short streaks; the finisher nine long ones, the arc and the light, and its sparks live longer.
    FSystemParts HitParts(bool bFinisher, UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UTexture2D* Profile = SliceTexture(LDSliceVfx::ETex::ChainAProfile);
        UMaterialInstanceConstant* Streaks = ChainPaint(bFinisher ? TEXT("MI_LDHitFinisher_Streak") : TEXT("MI_LDHitBasic_Streak"), Top, Profile, nullptr, 0.f, Neutral, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Spark = ChainPaint(bFinisher ? TEXT("MI_LDHitFinisher_Spark") : TEXT("MI_LDHitBasic_Spark"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, Neutral, 1.f, 0.f, Error);
        FSystemParts Hit;
        Hit.System = bFinisher ? LDSliceVfx::ESystem::HitFinisher : LDSliceVfx::ESystem::HitBasic;
        if (!Streaks || !Spark) { return Hit; }
        const FString Scale = FString::Printf(TEXT("(%s * lerp(0.75, 1.0, User.Focus))"), LinkScale);
        const int32 Rays = bFinisher ? 11 : 7;
        const float Life = bFinisher ? .16f : .12f;
        FPart& Jag = Hit.Parts.AddDefaulted_GetRef();
        Jag.Name = TEXT("Streaks");
        // Others' hits: fewer, shorter streaks held tighter along the strike.
        Jag.Count = FString::Printf(TEXT("int(lerp(%d.0, %d.0, User.Focus) + 0.01) * 3"), Rays - 3, Rays);
        Jag.Lifetime = FString::Printf(TEXT("%.3f"), Life);
        Jag.Paint = Streaks;
        Jag.TileCm = 60.f;
        Jag.bRibbonIds = true;
        {
            const FString Ray = TEXT("float(Particles.LDRibbon) + 90.0");
            const auto Pick = [&](float Salt) { return FTrunk::Hash(Ray, TEXT("0.0"), Salt); };
            // Fanned along the strike: the cone narrows with the forward weight.
            const FString Out = FString::Printf(TEXT("normalize(%s * lerp(2.8, %.2f, User.Focus) + float3(%s * 2.0 - 1.0, %s * 2.0 - 1.0, (%s * 2.0 - 1.0) * 0.7))"),
                *FTrunk::Dir(), bFinisher ? 1.3f : 1.6f, *Pick(1.f), *Pick(2.f), *Pick(3.f));
            const FString Length = FString::Printf(TEXT("((%.1f + %.1f * %s) * %s * lerp(0.6, 1.0, User.Focus) * saturate(Particles.Age / 0.02 + 0.35))"),
                bFinisher ? 70.f : 45.f, bFinisher ? 50.f : 35.f, *Pick(4.f), *Scale);
            const FString Across = FString::Printf(TEXT("normalize(cross(%s, float3(0.0, 0.0, 1.0)) + float3(0.0001, 0.0001, 0.0))"), *Out);
            // Streaks start a little out from the contact so the centre stays open (no disc).
            Jag.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / 3.0))"), Own))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * 3.0)"), Own))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * (10.0 * %s + %s * (Particles.LDJ / 2.0)) + %s * (%s * 2.0 - 1.0) * 0.1 * %s * step(0.5, Particles.LDJ) * step(Particles.LDJ, 1.5)"),
                    *Out, *Scale, *Length, *Across, *Pick(5.f), *Length))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("%.1f * %s * (1.0 - 0.9 * Particles.LDJ / 2.0) * lerp(1.0, 0.4, step(0.75, Particles.NormalizedAge))"), bFinisher ? 18.f : 14.f, *Scale))
                // Full, then one thin dim afterimage frame, then cut (no fade-out).
                .Set(TEXT("Particles.Color"), Tinted(bFinisher ? 3.2f : 2.8f, TEXT("lerp(1.0, 0.35, step(0.75, Particles.NormalizedAge))")));
        }
        if (bFinisher)
        {
            // The impact arc: half a ring in front of the body, facing the strike, opening from 20 to 75 cm over 0.12 s.
            FPart& Ring = Hit.Parts.AddDefaulted_GetRef();
            Ring.Name = TEXT("Arc");
            Ring.Count = TEXT("9");
            Ring.Lifetime = TEXT("0.18");
            Ring.Paint = Streaks;
            Ring.TileCm = 60.f;
            const FString Theta = FString::Printf(TEXT("((%s / 8.0 - 0.5) * 3.1416)"), Own);
            const FString Across = FString::Printf(TEXT("normalize(cross(%s, float3(0.0, 0.0, 1.0)) + float3(0.0001, 0.0001, 0.0))"), *FTrunk::Dir());
            const FString Radius = FString::Printf(TEXT("(lerp(30.0, 110.0, 1.0 - pow(1.0 - Particles.NormalizedAge, 2.0)) * %s)"), *Scale);
            Ring.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * 18.0 * %s + (%s * sin(%s) + float3(0.0, 0.0, 1.0) * cos(%s) * 0.8) * %s - %s * (1.0 - cos(%s)) * 10.0"),
                    *FTrunk::Dir(), *Scale, *Across, *Theta, *Theta, *Radius, *FTrunk::Dir(), *Theta))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("14.0 * %s * cos(%s) * lerp(1.0, 0.4, step(0.8, Particles.NormalizedAge))"), *Scale, *Theta))
                .Set(TEXT("Particles.Color"), Tinted(2.4f, TEXT("lerp(1.0, 0.35, step(0.8, Particles.NormalizedAge))")));
        }
        const FString Launch = FString::Printf(TEXT("(%s * (260.0 + 220.0 * %s))"), *SparkDirection(1.1f, .4f), *Random(15.f));
        FPart& Sparks = Hit.Parts.AddDefaulted_GetRef();
        Sparks.Name = TEXT("Spark");
        Sparks.Kind = FPart::Sprite;
        Sparks.Count = FString::Printf(TEXT("int(lerp(%.1f, %.1f, User.Focus) + 0.5)"), bFinisher ? 5.f : 3.f, bFinisher ? 14.f : 8.f);
        Sparks.Lifetime = FString::Printf(TEXT("%.2f + 0.15 * %s"), bFinisher ? .3f : .2f, *Random(14.f));
        Sparks.Paint = Spark;
        Sparks.Align = ENiagaraSpriteAlignment::VelocityAligned;
        Sparks.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s + float3(0.0, 0.0, -500.0) * Particles.Age"), *Launch))
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * Particles.Age + float3(0.0, 0.0, -250.0) * Particles.Age * Particles.Age"), *Launch))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(44.0, 10.0) * %s"), *Scale))
            .Set(TEXT("Particles.SpriteRotation"), Quarter)
            .Set(TEXT("Particles.Color"), Tinted(2.4f, TEXT("(1.0 - pow(Particles.NormalizedAge, 1.5))")));
        if (bFinisher)
        {
            FPart& Light = Hit.Parts.AddDefaulted_GetRef();
            Light.Name = TEXT("Light");
            Light.Kind = FPart::Light;
            Light.Lifetime = TEXT("0.14");
            Light.Set(TEXT("Particles.Position"), TEXT("User.End"))
                .Set(TEXT("Particles.LightRadius"), FString::Printf(TEXT("160.0 * %s"), *Scale))
                .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(User.Tint.r, User.Tint.g, User.Tint.b, 1.0) * float4(6.0, 6.0, 6.0, 1.0) * %s * (1.0 - Particles.NormalizedAge)"), Bright));
        }
        return Hit;
    }

    // The basic swing of combo step Step as a 3D arc of motion: a ribbon whose head sweeps an ellipse round the chest
    // (User.Start, the swing centre) in a tilted plane facing User.End - User.Start, its tail following, so the trail is
    // the path just swept, thick at the head and thin at the tail. Two ribbons: a soft tinted body and a thin warm-white
    // edge; the finisher adds an echo arc a little inside and behind, and sweeps wider and slower. Steps 1 and 2 sweep
    // opposite ways (1 right to left, 2 left to right), each tilted the other way.
    FSystemParts SwingParts(uint8 Step, UMaterial& Material, FString& Error)
    {
        UTexture2D* Profile = SliceTexture(LDSliceVfx::ETex::ChainAProfile);
        UMaterialInstanceConstant* Trail = ChainPaint(*FString::Printf(TEXT("MI_LDSwing%d_Trail"), Step), Material, Profile,
            SliceTexture(LDSliceVfx::ETex::DissolveNoise), 1.f, Neutral, .6f, 2.f, Error);
        FSystemParts Swing;
        Swing.System = Step == 1 ? LDSliceVfx::ESystem::Swing1 : Step == 2 ? LDSliceVfx::ESystem::Swing2 : LDSliceVfx::ESystem::Swing3;
        if (!Trail) { return Swing; }
        const bool bFinisher = Step >= 3;
        // Ellipse radii along the facing and across (cm), the sweep (degrees, + = towards the left), the plane's tilt
        // (rise per unit across) and the time the head takes to sweep.
        const float Ahead = bFinisher ? 120.f : Step == 1 ? 92.f : 100.f, Side = bFinisher ? 100.f : Step == 1 ? 62.f : 68.f;
        const float From = bFinisher ? -95.f : Step == 1 ? -58.f : 60.f, To = bFinisher ? 95.f : Step == 1 ? 58.f : -60.f;
        const float Tilt = bFinisher ? .12f : Step == 1 ? -.45f : .4f, Sweep = bFinisher ? .1f : .065f, Hold = bFinisher ? .16f : .1f;
        const int32 Points = bFinisher ? 16 : 12;
        const FString Scale = FString::Printf(TEXT("(%s * lerp(0.8, 1.0, User.Focus))"), LinkScale);
        const FString Forward = TEXT("normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0))");
        const FString Left = FString::Printf(TEXT("cross(%s, float3(0.0, 0.0, 1.0))"), *Forward);
        // The head leaves at the swing's start and eases out to the end over Sweep; the tail follows, catching up over Hold.
        const FString Head = FString::Printf(TEXT("radians(lerp(%.1f, %.1f, 1.0 - pow(1.0 - saturate(Particles.Age / %.3f), 2.0)))"), From, To, Sweep);
        const FString Tail = FString::Printf(TEXT("radians(lerp(%.1f, %.1f, saturate((Particles.Age - %.3f) / %.3f)))"), From, To, Sweep * .5f, Hold + Sweep * .5f);
        const FString Life = FString::Printf(TEXT("%.3f"), Sweep * .5f + Hold + Sweep * .5f + .034f);
        const auto Arc = [&](FPart& Part, const FString& Along, float Radius, float Lag, float Width, float Gain, float Alpha)
        {
            const FString Theta = FString::Printf(TEXT("lerp(%s, %s, %s)"),
                *Tail.Replace(TEXT("Particles.Age"), *FString::Printf(TEXT("(Particles.Age - %.3f)"), Lag)),
                *Head.Replace(TEXT("Particles.Age"), *FString::Printf(TEXT("(Particles.Age - %.3f)"), Lag)), *Along);
            Part.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + (%s * cos(%s) * %.1f + %s * sin(%s) * %.1f) * %.2f * %s + float3(0.0, 0.0, 1.0) * sin(%s) * %.1f * %.2f * %s"),
                    *Forward, *Theta, Ahead, *Left, *Theta, Side, Radius, *Scale, *Theta, Side, Tilt * Radius, *Scale))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("%.1f * %s * pow(%s, 0.7) * step(%.3f, Particles.Age) * lerp(1.0, 0.35, step(%.3f, Particles.Age))"),
                    Width, *Scale, *Along, Lag, Sweep * .5f + Hold + Sweep * .5f + Lag))
                .Set(TEXT("Particles.Color"), Tinted(Gain, FString::Printf(TEXT("%.2f * lerp(1.0, 0.35, step(%.3f, Particles.Age))"), Alpha, Sweep * .5f + Hold + Sweep * .5f + Lag)));
        };
        const int32 Ribbons = bFinisher ? 3 : 2;
        FPart& Main = Swing.Parts.AddDefaulted_GetRef();
        Main.Name = TEXT("Trail");
        Main.Count = FString::Printf(TEXT("%d"), Points * Ribbons);
        Main.Lifetime = Life;
        Main.Paint = Trail;
        Main.TileCm = 80.f;
        Main.bRibbonIds = true;
        Main.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
            .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
            .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"));
        // One emitter, each ribbon its own arc: 0 the soft body, 1 the thin bright edge just outside it, 2 (finisher) the
        // echo inside and 25 ms behind. The attributes switch on the ribbon index.
        FPart Body, Edge, Echo;
        const FString Along = FString::Printf(TEXT("(Particles.LDJ / %d.0)"), Points - 1);
        Arc(Body, Along, 1.f, 0.f, bFinisher ? 48.f : 34.f, 1.f, .85f);
        Arc(Edge, Along, 1.06f, 0.f, bFinisher ? 13.f : 10.f, 2.2f, 1.f);
        Arc(Echo, Along, .8f, .025f, bFinisher ? 28.f : 0.f, .9f, .6f);
        const FString R = TEXT("float(Particles.LDRibbon)");
        for (int32 Index = 0; Index < Body.Attributes.Num(); ++Index)
        {
            Main.Set(*Body.Attributes[Index].Key, FString::Printf(TEXT("lerp(lerp(%s, %s, step(0.5, %s)), %s, step(1.5, %s))"),
                *Body.Attributes[Index].Value, *Edge.Attributes[Index].Value, *R, *Echo.Attributes[Index].Value, *R));
        }
        return Swing;
    }

    // The anticipation of combo step Step: the same ellipse as its swing (SwingParts) drawn thin and warm on the floor
    // round User.Start (the feet), revealing from its tail to its head over the windup (User.Duration) with a brighter
    // front, gone at the release. Never an inked crescent.
    FSystemParts TellParts(uint8 Step, UMaterial& Material, FString& Error)
    {
        UMaterialInstanceConstant* Line = ChainPaint(*FString::Printf(TEXT("MI_LDTell%d_Line"), Step), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile),
            nullptr, 0.f, Neutral, 1.f, 0.f, Error);
        FSystemParts Tell;
        Tell.System = Step == 1 ? LDSliceVfx::ESystem::Tell1 : Step == 2 ? LDSliceVfx::ESystem::Tell2 : LDSliceVfx::ESystem::Tell3;
        if (!Line) { return Tell; }
        const bool bFinisher = Step >= 3;
        const float Ahead = bFinisher ? 120.f : Step == 1 ? 92.f : 100.f, Side = bFinisher ? 100.f : Step == 1 ? 62.f : 68.f;
        const float From = bFinisher ? -95.f : Step == 1 ? -58.f : 60.f, To = bFinisher ? 95.f : Step == 1 ? 58.f : -60.f;
        const int32 Points = 20;
        const FString Scale = FString::Printf(TEXT("(%s * lerp(0.8, 1.0, User.Focus))"), LinkScale);
        const FString Forward = TEXT("normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0))");
        const FString Left = FString::Printf(TEXT("cross(%s, float3(0.0, 0.0, 1.0))"), *Forward);
        const FString Along = FString::Printf(TEXT("(%s / %d.0)"), Own, Points - 1);
        const FString Shown = TEXT("saturate(Particles.Age / max(User.Duration, 0.05))");
        const FString Theta = FString::Printf(TEXT("radians(lerp(%.1f, %.1f, %s))"), From, To, *Along);
        const FString Front = FString::Printf(TEXT("exp(-pow((%s - %s) / 0.08, 2.0))"), *Along, *Shown);
        FPart& Arc = Tell.Parts.AddDefaulted_GetRef();
        Arc.Name = TEXT("Arc");
        Arc.Count = FString::Printf(TEXT("%d"), Points);
        Arc.Lifetime = TEXT("User.Duration + 0.03");
        Arc.Paint = Line;
        Arc.TileCm = 60.f;
        Arc.Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + (%s * cos(%s) * %.1f + %s * sin(%s) * %.1f) * %s + float3(0.0, 0.0, 3.0)"),
                *Forward, *Theta, Ahead, *Left, *Theta, Side, *Scale))
            // The tail end shows first (the swing's start), the head end last; the front of the reveal is brighter.
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(4.0 + 5.0 * %s) * %s * step(%s, %s + 0.001)"), *Front, *Scale, *Along, *Shown))
            .Set(TEXT("Particles.Color"), Tinted(1.2f, FString::Printf(TEXT("(0.45 + 0.55 * %s) * saturate(Particles.Age / 0.05)"), *Front)));
        return Tell;
    }

    // A bolt between two HLSL points A and B in the chain's construction (ChainParts' trunk): Points points, interior
    // points slid along and pushed off the line by hashed amounts re-drawn whenever Step changes, bowed up by Bow of its
    // length. Index is the point's index along it, Salt sets the bolt apart from its neighbours.
    FString BoltPoint(const FString& A, const FString& B, const FString& Index, const FString& Salt, const FString& Step, int32 Points,
        float Kink, float UpKink, float Bow)
    {
        const FString Length = FString::Printf(TEXT("length((%s) - (%s))"), *B, *A);
        const FString Dir = FString::Printf(TEXT("(((%s) - (%s)) / max(%s, 1.0))"), *B, *A, *Length);
        const FString Side = FString::Printf(TEXT("normalize(cross(%s, float3(0.0, 0.0, 1.0)) + float3(0.0001, 0.0001, 0.0))"), *Dir);
        const FString Up = FString::Printf(TEXT("cross(%s, %s)"), *Side, *Dir);
        const FString Key = FString::Printf(TEXT("(%s + (%s) * 17.0)"), *Index, *Salt);
        const FString Interior = FString::Printf(TEXT("(step(0.5, %s) * step(%s, %.1f))"), *Index, *Index, Points - 1.5f);
        const FString T = FString::Printf(TEXT("saturate((%s + (%s - 0.5) * 0.35 * %s) / %.1f)"), *Index, *FTrunk::Hash(Key, Step, 1.7f), *Interior, Points - 1.f);
        return FString::Printf(TEXT("(lerp(%s, %s, %s) + (%s * (%s * 2.0 - 1.0) * %.2f + %s * (%s * 2.0 - 1.0) * %.2f) * (%s / %.1f) * %s + float3(0.0, 0.0, %.3f) * %s * (1.0 - %s) * %s)"),
            *A, *B, *T, *Side, *FTrunk::Hash(Key, Step, 3.1f), Kink, *Up, *FTrunk::Hash(Key, Step, 5.3f), UpKink, *Length, Points - 1.f, *Interior,
            4.f * Bow, *T, *T, *Length);
    }

    // The electric field ground state and the conduct reaction's splash, at its authoritative radius User.Width round
    // User.Start on the floor, built from the chain's parts in the chain's material:
    //   Edge    fourteen chain trunks joined end to end round the radius, discrete kinks re-drawn at 25 Hz, width breathing,
    //           a segment or two dark at a time (12 Hz), two bright surges running round it
    //   Arcs    five short chain arcs jumping between floor points inside, re-picked four times a second
    //   Floor   an additive warm ground light (never a patch)
    // (the arcs that reach the people standing inside are the runtime's reduced chain hops, ULDImpactFxSubsystem::Bite).
    // It opens from 90 % of the radius over 0.25 s and fades with User.Intensity (the ground state's remaining share) and
    // over the last 0.3 s of User.Duration.
    FSystemParts FieldParts(UMaterial& Material, UMaterial& Additive, FString& Error)
    {
        const FPalette Palette{FLinearColor(1.f, .93f, .78f), FLinearColor(1.f, .68f, .26f), FLinearColor(.22f, .1f, .03f), 2.8f, 1.2f};
        UTexture2D* Profile = SliceTexture(LDSliceVfx::ETex::ChainAProfile);
        UMaterialInstanceConstant* Bolt = ChainPaint(TEXT("MI_LDField_Bolt"), Material, Profile, SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise), 1.f, Palette, 1.f, 1.6f, Error);
        UMaterialInstanceConstant* Floor = ChainPaint(TEXT("MI_LDField_Floor"), Additive, SliceTexture(LDSliceVfx::ETex::ChainAGroundGlow), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        FSystemParts Field;
        Field.System = LDSliceVfx::ESystem::Field;
        if (!Bolt || !Floor) { return Field; }
        const FString Open = TEXT("saturate(Particles.Age / 0.25)");
        const FString Fade = FString::Printf(TEXT("(%s * saturate((User.Duration - Particles.Age) / 0.3) * saturate(User.Intensity / 2.2))"), *Open);
        const FString Radius = FString::Printf(TEXT("(User.Width * lerp(0.9, 1.0, %s))"), *Open);
        const FString Step = TEXT("floor(Particles.Age * 25.0)");
        const FString J = TEXT("Particles.LDJ"), R = TEXT("float(Particles.LDRibbon)");
        const auto Ribbons = [&](FPart& Part, int32 Points)
        {
            Part.bRibbonIds = true;
            Part.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), J);
        };
        FPart& Glow = Field.Parts.AddDefaulted_GetRef();
        Glow.Name = TEXT("Floor");
        Glow.Kind = FPart::Sprite;
        Glow.Lifetime = TEXT("User.Duration");
        Glow.Paint = Floor;
        Glow.Facing = ENiagaraSpriteFacingMode::CustomFacingVector;
        Glow.Set(TEXT("Particles.SpriteFacing"), TEXT("float3(0.0, 0.0, 1.0)"))
            .Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 2.0)"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(2.3, 2.3) * %s"), *Radius))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(0.9, 0.9, 0.9, %s)"), *Fade));
        const int32 Segments = 14, EdgePoints = 5;
        FPart& Edge = Field.Parts.AddDefaulted_GetRef();
        Edge.Name = TEXT("Edge");
        Edge.Count = FString::Printf(TEXT("%d"), Segments * EdgePoints);
        Edge.Lifetime = TEXT("User.Duration");
        Edge.Paint = Bolt;
        Edge.TileCm = 60.f;
        Ribbons(Edge, EdgePoints);
        {
            const auto Corner = [&](const FString& K) { return FString::Printf(TEXT("(User.Start + float3(cos(%s * %.5f), sin(%s * %.5f), 0.0) * %s + float3(0.0, 0.0, 8.0))"),
                *K, 6.2832f / Segments, *K, 6.2832f / Segments, *Radius); };
            const FString Along = FString::Printf(TEXT("((%s + %s / %d.0) / %d.0)"), *R, *J, EdgePoints - 1, Segments);
            const auto Surge = [&](float Phase) { return FString::Printf(TEXT("exp(-pow((abs(frac(%s - Particles.Age * 0.45 - %.2f + 0.5) - 0.5)) / 0.035, 2.0))"), *Along, Phase); };
            const FString Surges = FString::Printf(TEXT("(%s + %s)"), *Surge(0.f), *Surge(.5f));
            const FString Window = TEXT("floor(Particles.Age * 12.0)");
            const FString Breath = FString::Printf(TEXT("(0.75 + 0.5 * %s)"), *FTrunk::Hash(R, Step, 21.f));
            const FString Shown = FString::Printf(TEXT("max(step(0.2, %s), step(0.3, %s))"), *FTrunk::Hash(FString::Printf(TEXT("%s + 300.0"), *R), Window, 2.f), *Surges);
            Edge.Set(TEXT("Particles.Position"), BoltPoint(Corner(R), Corner(FString::Printf(TEXT("(%s + 1.0)"), *R)), J, R, Step, EdgePoints, .28f, .15f, .06f))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("16.0 * min(1.0, 0.45 + 2.0 * min(%s / %d.0, 1.0 - %s / %d.0) + step(0.5, %s) * step(%s, %d.5)) * %s * (1.0 + 0.8 * %s) * %s"),
                    *J, EdgePoints - 1, *J, EdgePoints - 1, *J, *J, EdgePoints - 2, *Breath, *Surges, *Shown))
                .Set(TEXT("Particles.Color"), Rgba(FString::Printf(TEXT("%s * (0.85 + 1.4 * %s)"), Bright, *Surges), *Fade));
        }
        const int32 Arcs = 5, ArcPoints = 6;
        FPart& Inside = Field.Parts.AddDefaulted_GetRef();
        Inside.Name = TEXT("Arcs");
        Inside.Count = FString::Printf(TEXT("%d"), Arcs * ArcPoints);
        Inside.Lifetime = TEXT("User.Duration");
        Inside.Paint = Bolt;
        Inside.TileCm = 60.f;
        Ribbons(Inside, ArcPoints);
        {
            const FString Pick = TEXT("floor(Particles.Age * 4.0)");
            const auto Spot = [&](float Salt)
            {
                const FString Angle = FTrunk::Hash(FString::Printf(TEXT("%s + 400.0"), *R), Pick, Salt);
                const FString Reach = FTrunk::Hash(FString::Printf(TEXT("%s + 400.0"), *R), Pick, Salt + .5f);
                return FString::Printf(TEXT("(User.Start + float3(cos(%s * 6.2832), sin(%s * 6.2832), 0.0) * sqrt(%s) * 0.8 * %s + float3(0.0, 0.0, 6.0))"),
                    *Angle, *Angle, *Reach, *Radius);
            };
            const FString From = Spot(1.f);
            // The far end lands 60-140 cm away from the first, still inside the radius.
            const FString Toward = FTrunk::Hash(FString::Printf(TEXT("%s + 400.0"), *R), Pick, 3.f);
            const FString To = FString::Printf(TEXT("(%s + float3(cos(%s * 6.2832), sin(%s * 6.2832), 0.0) * (60.0 + 80.0 * %s))"),
                *From, *Toward, *Toward, *FTrunk::Hash(FString::Printf(TEXT("%s + 400.0"), *R), Pick, 4.f));
            const FString Clamped = FString::Printf(TEXT("(User.Start + float3(1.0, 1.0, 0.0) * ((%s) - User.Start) * min(1.0, 0.85 * %s / max(length(((%s) - User.Start) * float3(1.0, 1.0, 0.0)), 1.0)) + float3(0.0, 0.0, 6.0))"),
                *To, *Radius, *To);
            const FString Shown = FString::Printf(TEXT("step(%s, 0.6)"), *FTrunk::Hash(FString::Printf(TEXT("%s + 500.0"), *R), TEXT("floor(Particles.Age * 12.0)"), 6.f));
            Inside.Set(TEXT("Particles.Position"), BoltPoint(From, Clamped, J, FString::Printf(TEXT("(%s + 50.0)"), *R), Step, ArcPoints, .3f, .2f, .3f))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("12.0 * min(1.0, 0.35 + 2.4 * min(%s / %d.0, 1.0 - %s / %d.0)) * %s"), *J, ArcPoints - 1, *J, ArcPoints - 1, *Shown))
                .Set(TEXT("Particles.Color"), Rgba(FString::Printf(TEXT("%s * 0.9"), Bright), *Fade));
        }
        return Field;
    }

    // The field candidates' materials: one Custom node (Code) over the first UV, the Particle Color's four channels
    // (PR, PG, PB, PA: each candidate says what they carry), a sprite's size (bSize; ribbons have none), the time, the
    // vein noise and, when given, the charged floor's Web and Energy textures, returning the emissive colour and the
    // coverage. Translucent, unlit, two-sided; the palette is the chain's (warm-white core,
    // pale-gold body, a dark amber fringe that keeps the edge readable on the bright floor).
    UMaterial* BuildFieldMaterial(const TCHAR* Name, const TCHAR* Code, UTexture2D* Noise, bool bSize, FString& Error,
        UTexture2D* WebTexture = nullptr, UTexture2D* EnergyTexture = nullptr)
    {
        const FString PackageName = GeneratedPackage(Name);
        Retire(PackageName);
        UMaterial* Material = NewObject<UMaterial>(CreatePackage(*PackageName), *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
        auto* UV = AddExpression<UMaterialExpressionTextureCoordinate>(*Material);
        auto* Vein = AddExpression<UMaterialExpressionTextureObjectParameter>(*Material);
        Vein->ParameterName = LDSliceVfx::NoiseParameter;
        Vein->Texture = Noise;
        Vein->SamplerType = SAMPLERTYPE_LinearColor;
        const auto Extra = [&](const TCHAR* Parameter, UTexture2D* Texture) -> UMaterialExpression*
        {
            if (!Texture) { return nullptr; }
            auto* Object = AddExpression<UMaterialExpressionTextureObjectParameter>(*Material);
            Object->ParameterName = Parameter;
            Object->Texture = Texture;
            Object->SamplerType = SAMPLERTYPE_LinearColor;
            return Object;
        };
        UMaterialExpression* Web = Extra(TEXT("Web"), WebTexture);
        UMaterialExpression* Energy = Extra(TEXT("Energy"), EnergyTexture);
        auto* Particle = AddExpression<UMaterialExpressionParticleColor>(*Material);
        UMaterialExpression* Size = bSize ? static_cast<UMaterialExpression*>(AddExpression<UMaterialExpressionParticleSize>(*Material)) : nullptr;
        auto* Time = AddExpression<UMaterialExpressionTime>(*Material);
        auto* Custom = AddExpression<UMaterialExpressionCustom>(*Material);
        Custom->OutputType = CMOT_Float4;
        Custom->Description = Name;
        Custom->Code = FString(TEXT("float3 CoreC = float3(1.0, 0.93, 0.78) * 2.6;\r\n"))
            + TEXT("float3 BodyC = float3(1.0, 0.68, 0.26) * 1.2;\r\n")
            + TEXT("float3 EdgeC = float3(0.30, 0.13, 0.03);\r\n")
            + Code;
        Custom->Inputs.Reset();
        const auto Bind = [Custom](const TCHAR* Input, UMaterialExpression* Source, int32 Output)
        {
            FCustomInput& Added = Custom->Inputs.AddDefaulted_GetRef();
            Added.InputName = Input;
            Added.Input.Connect(Output, Source);
        };
        Bind(TEXT("UV"), UV, 0);
        Bind(TEXT("Noise"), Vein, 0);
        Bind(TEXT("PR"), Particle, 1);
        Bind(TEXT("PG"), Particle, 2);
        Bind(TEXT("PB"), Particle, 3);
        Bind(TEXT("PA"), Particle, 4);
        if (Size) { Bind(TEXT("Size"), Size, 0); }
        if (Web) { Bind(TEXT("Web"), Web, 0); }
        if (Energy) { Bind(TEXT("Energy"), Energy, 0); }
        Bind(TEXT("Time"), Time, 0);
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
        Material->SetShadingModel(MSM_Unlit);
        bool bNeedsRecompile = false;
        Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_NiagaraSprites);
        Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_NiagaraRibbons);
        Material->PostEditChange();
        Material->MarkPackageDirty();
        return Save(*Material, Error) ? Material : nullptr;
    }

    // A plain instance of a generated material (the renderers take instances).
    UMaterialInstanceConstant* ChainPaintOf(const TCHAR* Name, UMaterial& Parent, FString& Error)
    {
        const FString PackageName = GeneratedPackage(Name);
        Retire(PackageName);
        auto* Instance = NewObject<UMaterialInstanceConstant>(CreatePackage(*PackageName), *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
        Instance->SetParentEditorOnly(&Parent);
        Instance->PostEditChange();
        Instance->MarkPackageDirty();
        return Save(*Instance, Error) ? Instance : nullptr;
    }

    // The share of a field still shown: the ground state's remaining share (User.Intensity / 2.2, the runtime's fade over
    // the last third of its life) and the last 0.3 s of User.Duration (the conduct splash's own end).
    const TCHAR* const FieldShare = TEXT("(saturate(User.Intensity / 2.2) * saturate((User.Duration - Particles.Age) / 0.3))");

    // Field candidate F1, the energy fence: the boundary carries the identity, the inside stays empty.
    //   Fence   one disc on the floor 1.1 x the radius drawing only a 30 cm electric band on the radius: a hot centre
    //           line with crackle re-drawn at 12 Hz along it; living: the band trembles across the radius at 12 Hz, 45 cm
    //           stretches go dark and come back ten times a second, three bright surges run round it. Particle Color:
    //           R brightness, G the opening (the band runs round the circle from one point with a bright front), B the
    //           band's width share (it widens as it opens and narrows at the end), A the coverage.
    //   Arches  arcs leaping 35-60 cm high between two points of the fence 50-90 cm apart, the fence's height
    //           (decoration: seven for the own or focused field, three for others').
    // The boundary is the same for everyone's field; only the arches thin out.
    FSystemParts FieldFenceParts(UMaterial& Bolts, FString& Error)
    {
        UMaterial* Material = BuildFieldMaterial(TEXT("M_LDFieldFence"),
            TEXT("float K = 1.1;\r\n")
            TEXT("float2 q = (UV * 2.0 - 1.0) * K;\r\n")
            TEXT("float r = length(q);\r\n")
            TEXT("float R = max(Size.x * 0.5 / K, 1.0);\r\n")
            TEXT("float ang = frac(atan2(q.y, q.x) / 6.2832 + 1.0);\r\n")
            TEXT("float along = ang * 6.2832 * R / 60.0;\r\n")
            TEXT("float t = floor(Time * 12.0);\r\n")
            TEXT("float wob = (Texture2DSample(Noise, NoiseSampler, float2(along * 0.35, frac(t * 0.071))).g - 0.5) * 14.0;\r\n")
            TEXT("float d = ((r - 1.0) * R + wob) / max(PB, 0.05);\r\n")
            TEXT("float core = exp(-pow(d / 6.0, 2.0));\r\n")
            TEXT("float n = Texture2DSample(Noise, NoiseSampler, float2(along * 0.5 + frac(t * 0.137), d / 40.0 + frac(t * 0.071))).g;\r\n")
            TEXT("float crackle = smoothstep(0.4, 0.9, n) * exp(-pow(d / 16.0, 2.0));\r\n")
            TEXT("float fringe = exp(-pow(d / 16.0, 2.0)) * 0.3;\r\n")
            TEXT("float seg = floor(along / 0.75);\r\n")
            TEXT("float h = frac(sin(seg * 12.9898 + floor(Time * 10.0) * 78.233) * 43758.5453);\r\n")
            TEXT("float gap = PG < 0.999 ? 1.0 : step(0.17, h);\r\n")
            TEXT("float shown = step(ang, PG * 1.02);\r\n")
            TEXT("float front = PG < 0.999 ? exp(-pow((ang - PG) / 0.04, 2.0)) : 0.0;\r\n")
            TEXT("float s0 = abs(frac(ang - Time * 0.3) - 0.5), s1 = abs(frac(ang - Time * 0.3 + 0.3333) - 0.5), s2 = abs(frac(ang - Time * 0.3 + 0.6667) - 0.5);\r\n")
            TEXT("float surge = exp(-pow((0.5 - s0) / 0.025, 2.0)) + exp(-pow((0.5 - s1) / 0.025, 2.0)) + exp(-pow((0.5 - s2) / 0.025, 2.0));\r\n")
            TEXT("float g = saturate(core * 0.95 + crackle * 0.75);\r\n")
            TEXT("float3 low = lerp(EdgeC, BodyC, saturate(g / 0.6));\r\n")
            TEXT("float3 C = lerp(low, CoreC, smoothstep(0.62, 0.95, g)) * PR * (1.0 + 1.6 * surge + 2.0 * front);\r\n")
            TEXT("float a = saturate(core * 0.9 + crackle * 0.65 + fringe) * gap * shown * PA * step(r, K);\r\n")
            TEXT("return float4(C, a);"),
            SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise), true, Error);
        const FPalette Palette{FLinearColor(1.f, .93f, .78f), FLinearColor(1.f, .68f, .26f), FLinearColor(.22f, .1f, .03f), 2.8f, 1.2f};
        UMaterialInstanceConstant* Arc = ChainPaint(TEXT("MI_LDFieldF1_Arc"), Bolts, SliceTexture(LDSliceVfx::ETex::ChainAProfile),
            SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise), 1.f, Palette, 1.f, 1.6f, Error);
        UMaterialInstanceConstant* Band = Material ? ChainPaintOf(TEXT("MI_LDFieldF1_Fence"), *Material, Error) : nullptr;
        FSystemParts Field;
        Field.System = LDSliceVfx::ESystem::FieldF1;
        if (!Band || !Arc) { return Field; }
        const FString Share = FieldShare;
        const FString Radius = TEXT("User.Width");
        const FString Hold = FString::Printf(TEXT("saturate(%s * 2.5)"), *Share);
        const FString Rise = FString::Printf(TEXT("lerp(0.15, 1.0, saturate(Particles.Age / 0.25)) * lerp(0.25, 1.0, %s)"), *Hold);
        FPart& Fence = Field.Parts.AddDefaulted_GetRef();
        Fence.Name = TEXT("Fence");
        Fence.Kind = FPart::Sprite;
        Fence.Lifetime = TEXT("User.Duration");
        Fence.Paint = Band;
        Fence.Facing = ENiagaraSpriteFacingMode::CustomFacingVector;
        Fence.Set(TEXT("Particles.SpriteFacing"), TEXT("float3(0.0, 0.0, 1.0)"))
            .Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 2.5)"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(2.2, 2.2) * %s"), *Radius))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, saturate(Particles.Age / 0.3), %s, saturate(Particles.Age / 0.04) * %s * %s)"),
                *Rise, *Hold, *Share));
        // Arches: arc k of the emitter's ribbons spans the fence from angle A to A + 40-70 cm, re-picked every 0.18 s and
        // shown in about six windows of ten; its five points rise to 25-45 cm with kinks re-drawn at 20 Hz.
        const int32 ArchPoints = 5;
        FPart& Arches = Field.Parts.AddDefaulted_GetRef();
        Arches.Name = TEXT("Arches");
        Arches.Count = FString::Printf(TEXT("int(lerp(3.0, 7.0, User.Focus) + 0.01) * %d"), ArchPoints);
        Arches.Lifetime = TEXT("User.Duration");
        Arches.Paint = Arc;
        Arches.TileCm = 60.f;
        Arches.bRibbonIds = true;
        {
            const FString R = TEXT("float(Particles.LDRibbon)");
            const FString K = TEXT("Particles.LDJ");
            const FString Pick = TEXT("floor(Particles.Age / 0.18)");
            const FString Seed = FString::Printf(TEXT("(%s + 600.0)"), *R);
            const FString From = FString::Printf(TEXT("(%s * 6.2832)"), *FTrunk::Hash(Seed, Pick, 1.f));
            const FString Span = FString::Printf(TEXT("((50.0 + 40.0 * %s) / max(%s, 1.0))"), *FTrunk::Hash(Seed, Pick, 2.f), *Radius);
            const FString S = FString::Printf(TEXT("(%s / %d.0)"), *K, ArchPoints - 1);
            const FString A = FString::Printf(TEXT("(%s + %s * %s)"), *From, *Span, *S);
            const FString Lift = FString::Printf(TEXT("((35.0 + 25.0 * %s) * sin(3.1416 * %s))"), *FTrunk::Hash(Seed, Pick, 3.f), *S);
            const FString Kink = FString::Printf(TEXT("((%s - 0.5) * 12.0 * step(0.5, %s) * step(%s, %d.5))"),
                *FTrunk::Hash(FString::Printf(TEXT("%s * 7.0 + %s"), *Seed, *K), TEXT("floor(Particles.Age * 20.0)"), 4.f), *K, *K, ArchPoints - 2);
            const FString On = FString::Printf(TEXT("step(%s, 0.6) * step(0.3, Particles.Age)"), *FTrunk::Hash(Seed, Pick, 5.f));
            Arches.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, ArchPoints))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, ArchPoints))
                .Set(TEXT("Particles.RibbonLinkOrder"), K)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * (%s + %s) + float3(0.0, 0.0, 2.0 + %s + %s * 0.5)"),
                    *A, *A, *Radius, *Kink, *Lift, *Kink))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("9.0 * min(1.0, 0.4 + 2.4 * min(%s, 1.0 - %s)) * %s"), *S, *S, *On))
                .Set(TEXT("Particles.Color"), Rgba(TEXT("1.7"), FString::Printf(TEXT("%s * %s"), *Hold, *Share)));
        }
        return Field;
    }

    // Field candidate F2, the charged surface: the floor itself is the state.
    //   Surface  one disc on the floor 1.12 x the radius: a soft but clear rim (a thin warm-white line with a dark-amber
    //            fringe and a 40 cm glow falling inwards) whose edge trembles at 12 Hz with two slow surges running round
    //            it, and inside a faint electric vein network crawling over the floor (re-drawn ten times a second) with a
    //            slow ripple travelling outwards. Particle Color: R brightness, G the inside's share, B the rim's opening
    //            flash, A the coverage. Opens by expanding from 75 % of the radius with a bright rim, the inside filling in
    //            after it; at the end the inside goes first, then the rim tightens and fades.
    // Others' fields keep the same rim; their veins are fainter.
    FSystemParts FieldSurfaceParts(FString& Error)
    {
        UMaterial* Material = BuildFieldMaterial(TEXT("M_LDFieldSurface"),
            TEXT("float K = 1.12;\r\n")
            TEXT("float2 q = (UV * 2.0 - 1.0) * K;\r\n")
            TEXT("float r = length(q);\r\n")
            TEXT("float R = max(Size.x * 0.5 / K, 1.0);\r\n")
            TEXT("float2 P = q * R;\r\n")
            TEXT("float t = floor(Time * 12.0);\r\n")
            TEXT("float ang = atan2(q.y, q.x) / 6.2832;\r\n")
            TEXT("float along = ang * R / 60.0;\r\n")
            // The edge trembles and its brightness breaks up along it, both re-drawn twelve times a second.
            TEXT("float wob = Texture2DSample(Noise, NoiseSampler, float2(along * 0.35, frac(t * 0.071))).g - 0.5;\r\n")
            TEXT("float crack = 0.45 + 1.1 * Texture2DSample(Noise, NoiseSampler, float2(frac(t * 0.113), along * 0.5)).g;\r\n")
            TEXT("float d = (r - 1.0) * R + wob * 10.0;\r\n")
            TEXT("float s1 = abs(frac(ang - Time * 0.18) - 0.5);\r\n")
            TEXT("float s2 = abs(frac(ang - Time * 0.18 + 0.5) - 0.5);\r\n")
            TEXT("float surge = exp(-pow((0.5 - s1) / 0.05, 2.0)) + exp(-pow((0.5 - s2) / 0.05, 2.0));\r\n")
            // Layers, each a colour and a coverage: the rim rimv, a thin dark-amber shade just outside it (contrast on the
            // sunlit floor), the warm glow falling inwards, the veins and a faint warm fill.
            TEXT("float rimv = exp(-pow(d / 5.5, 2.0)) * saturate(crack + surge) * (1.0 + PB);\r\n")
            TEXT("float shade = d > 0.0 ? exp(-pow((d - 8.0) / 7.0, 2.0)) * 0.45 : 0.0;\r\n")
            TEXT("float glow = d < 0.0 ? exp(d / 45.0) * 0.24 : 0.0;\r\n")
            TEXT("float tv = floor(Time * 10.0) * 0.1;\r\n")
            TEXT("float va = Texture2DSample(Noise, NoiseSampler, P / 420.0 + float2(tv * 0.37, tv * 0.21)).g;\r\n")
            TEXT("float vb = Texture2DSample(Noise, NoiseSampler, float2(P.y, -P.x) / 340.0 + float2(-tv * 0.29, tv * 0.43)).g;\r\n")
            // Veins flicker in patches: a slow blob noise thresholded anew ten times a second.
            TEXT("float blot = Texture2DSample(Noise, NoiseSampler, P / 900.0 + float2(0.5, frac(tv * 0.17))).g;\r\n")
            TEXT("float veins = smoothstep(0.82, 0.98, max(va, vb)) * smoothstep(0.3, 0.55, blot + 0.25 * sin(r * 8.0 - Time * 4.0));\r\n")
            TEXT("float inside = d < 0.0 ? PG : 0.0;\r\n")
            TEXT("float vein = veins * inside * saturate(-d / 25.0) * 0.6;\r\n")
            TEXT("float fill = inside * 0.05;\r\n")
            TEXT("float3 LineC = lerp(BodyC * 1.4, CoreC, 0.25) * (1.0 + 0.6 * surge);\r\n")
            TEXT("float a1 = saturate(rimv), a2 = shade * PG, a3 = glow, a4 = vein, a5 = fill;\r\n")
            TEXT("float a = 1.0 - (1.0 - a1) * (1.0 - a2) * (1.0 - a3) * (1.0 - a4) * (1.0 - a5);\r\n")
            TEXT("float3 C = (LineC * a1 + EdgeC * a2 + BodyC * a3 + lerp(BodyC * 1.6, CoreC, 0.3) * a4 + BodyC * 0.8 * a5) / max(a1 + a2 + a3 + a4 + a5, 0.001);\r\n")
            TEXT("return float4(C * PR, saturate(a) * PA * step(r, K));"),
            SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise), true, Error);
        UMaterialInstanceConstant* Paint = Material ? ChainPaintOf(TEXT("MI_LDFieldF2_Surface"), *Material, Error) : nullptr;
        FSystemParts Field;
        Field.System = LDSliceVfx::ESystem::FieldF2;
        if (!Paint) { return Field; }
        const FString Share = FieldShare;
        const FString Hold = FString::Printf(TEXT("saturate(%s * 2.5)"), *Share);
        const FString Open = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.3), 3.0))");
        const FString Inside = FString::Printf(TEXT("(saturate((Particles.Age - 0.12) / 0.3) * saturate(%s * 1.6 - 0.4) * lerp(0.55, 1.0, User.Focus))"), *Share);
        FPart& Surface = Field.Parts.AddDefaulted_GetRef();
        Surface.Name = TEXT("Surface");
        Surface.Kind = FPart::Sprite;
        Surface.Lifetime = TEXT("User.Duration");
        Surface.Paint = Paint;
        Surface.Facing = ENiagaraSpriteFacingMode::CustomFacingVector;
        Surface.Set(TEXT("Particles.SpriteFacing"), TEXT("float3(0.0, 0.0, 1.0)"))
            .Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 2.0)"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(2.24, 2.24) * User.Width * lerp(0.75, 1.0, %s) * lerp(0.92, 1.0, %s)"), *Open, *Hold))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, %s, 1.5 * exp(-Particles.Age / 0.12), saturate(Particles.Age / 0.06) * %s * %s)"),
                *Inside, *Hold, *Share));
        return Field;
    }

    // Field candidate F3, F1's edge over a charged floor (the user's pick of F1's boundary and F2's floor):
    //   Field   one disc on the floor 1.12 x the radius:
    //           edge   F1's living electric band on the radius (hot core, crackle re-drawn at 12 Hz, 12 Hz tremble, three
    //                  travelling surges), warm gold rather than white, over a narrow dark-amber under-edge just outside it
    //                  (contrast on sunlit floors); its dark stretches dim to a third instead of vanishing, so the danger
    //                  edge never breaks
    //           floor  the field_web crack web in world space (about 320 cm per tile, two samples at different scales),
    //                  re-randomised three times a second with a slow drift, shown in soft patches (field_energy_noise
    //                  R), with some web cells flaring briefly (B); low contrast, fading out near the edge
    //           Particle Color: R brightness (the opening flash), G the floor's share, B the edge's width share, A coverage.
    //           Opens by expanding from 75 % of the radius with a bright edge, the floor filling in after it; at the end
    //           the floor goes first, then the edge narrows, tightens and fades.
    //   Arches  F1's arcs leaping over the edge, thicker (decoration: seven for the own or focused field, three for
    //           others'). Others' fields keep the same edge; their floor web is fainter.
    FSystemParts FieldChargedParts(UMaterial& Bolts, FString& Error)
    {
        UMaterial* Material = BuildFieldMaterial(TEXT("M_LDFieldCharged"),
            TEXT("float K = 1.12;\r\n")
            TEXT("float2 q = (UV * 2.0 - 1.0) * K;\r\n")
            TEXT("float r = length(q);\r\n")
            TEXT("float R = max(Size.x * 0.5 / K, 1.0);\r\n")
            TEXT("float2 P = q * R;\r\n")
            TEXT("float ang = frac(atan2(q.y, q.x) / 6.2832 + 1.0);\r\n")
            TEXT("float along = ang * 6.2832 * R / 60.0;\r\n")
            TEXT("float t = floor(Time * 12.0);\r\n")
            // The edge: F1's band.
            TEXT("float wob = (Texture2DSample(Noise, NoiseSampler, float2(along * 0.35, frac(t * 0.071))).g - 0.5) * 14.0;\r\n")
            TEXT("float d0 = (r - 1.0) * R + wob;\r\n")
            TEXT("float d = d0 / max(PB, 0.05);\r\n")
            TEXT("float core = exp(-pow(d / 6.0, 2.0));\r\n")
            TEXT("float n = Texture2DSample(Noise, NoiseSampler, float2(along * 0.5 + frac(t * 0.137), d / 40.0 + frac(t * 0.071))).g;\r\n")
            TEXT("float crack = smoothstep(0.4, 0.9, n) * exp(-pow(d / 16.0, 2.0));\r\n")
            TEXT("float seg = floor(along / 0.75);\r\n")
            TEXT("float h = frac(sin(seg * 12.9898 + floor(Time * 10.0) * 78.233) * 43758.5453);\r\n")
            TEXT("float dim = PG < 0.999 ? 1.0 : lerp(0.35, 1.0, step(0.17, h));\r\n")
            TEXT("float s0 = abs(frac(ang - Time * 0.3) - 0.5), s1 = abs(frac(ang - Time * 0.3 + 0.3333) - 0.5), s2 = abs(frac(ang - Time * 0.3 + 0.6667) - 0.5);\r\n")
            TEXT("float surge = exp(-pow((0.5 - s0) / 0.025, 2.0)) + exp(-pow((0.5 - s1) / 0.025, 2.0)) + exp(-pow((0.5 - s2) / 0.025, 2.0));\r\n")
            TEXT("float gE = saturate(core * 0.95 + crack * 0.75);\r\n")
            TEXT("float aE = saturate(core * 0.9 + crack * 0.65 + exp(-pow(d / 16.0, 2.0)) * 0.3) * dim;\r\n")
            TEXT("float3 lowE = lerp(EdgeC, BodyC * 1.3, saturate(gE / 0.6));\r\n")
            TEXT("float3 CE = lerp(lowE, lerp(BodyC * 1.6, CoreC, 0.5), smoothstep(0.62, 0.95, gE)) * (1.0 + 1.4 * surge) * sqrt(dim);\r\n")
            // The dark-amber under-edge just outside the band, and the edge's glow falling inwards.
            TEXT("float aS = d0 > 0.0 ? exp(-pow((d0 - 14.0 * PB) / 5.0, 2.0)) * 0.36 * PB : 0.0;\r\n")
            TEXT("float aG = d0 < 0.0 ? exp(d0 / 45.0) * 0.1 : 0.0;\r\n")
            // The floor: the crack web, re-randomised three times a second, in patches, with flaring cells.
            TEXT("float tq = floor(Time * 3.0);\r\n")
            TEXT("float2 o1 = frac(float2(tq * 0.37, tq * 0.61)) + float2(Time * 0.012, -Time * 0.009);\r\n")
            TEXT("float2 o2 = frac(float2(tq * 0.53 + 0.5, tq * 0.29 + 0.25)) + float2(-Time * 0.01, Time * 0.013);\r\n")
            TEXT("float2 uA = P / 320.0 + o1;\r\n")
            TEXT("float2 uB = float2(P.y, -P.x) / 420.0 + o2;\r\n")
            TEXT("float4 wA = Texture2DSample(Web, WebSampler, uA);\r\n")
            TEXT("float4 wB = Texture2DSample(Web, WebSampler, uB);\r\n")
            TEXT("float web = max(wA.a * (0.5 + 0.5 * wA.r), 0.6 * wB.a * (0.5 + 0.5 * wB.r));\r\n")
            TEXT("float cell = Texture2DSample(Energy, EnergySampler, uA).b;\r\n")
            TEXT("float flare = step(0.92, frac(cell * 13.7 + floor(Time * 6.0) * 0.618)) * wA.a;\r\n")
            TEXT("float blob = smoothstep(0.3, 0.75, Texture2DSample(Energy, EnergySampler, P / 700.0 + float2(Time * 0.02, -Time * 0.015)).r);\r\n")
            TEXT("float inside = d0 < 0.0 ? PG : 0.0;\r\n")
            TEXT("float aW = saturate(web * (0.08 + 0.3 * blob) + flare * 0.6) * inside * saturate(-d0 / 25.0);\r\n")
            TEXT("float3 CW = lerp(BodyC * 1.2, CoreC, saturate(0.2 + 0.7 * flare));\r\n")
            TEXT("float aF = inside * 0.025;\r\n")
            TEXT("float a = 1.0 - (1.0 - aE) * (1.0 - aS) * (1.0 - aG) * (1.0 - aW) * (1.0 - aF);\r\n")
            TEXT("float3 C = (CE * aE + EdgeC * aS + BodyC * aG + CW * aW + BodyC * 0.8 * aF) / max(aE + aS + aG + aW + aF, 0.001);\r\n")
            TEXT("return float4(C * PR, saturate(a) * PA * step(r, K));"),
            SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise), true, Error,
            SliceTexture(LDSliceVfx::ETex::FieldWeb), SliceTexture(LDSliceVfx::ETex::FieldEnergyNoise));
        const FPalette Palette{FLinearColor(1.f, .93f, .78f), FLinearColor(1.f, .68f, .26f), FLinearColor(.22f, .1f, .03f), 2.8f, 1.2f};
        UMaterialInstanceConstant* Arc = ChainPaint(TEXT("MI_LDFieldF3_Arc"), Bolts, SliceTexture(LDSliceVfx::ETex::ChainAProfile),
            SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise), 1.f, Palette, 1.f, 1.6f, Error);
        UMaterialInstanceConstant* Paint = Material ? ChainPaintOf(TEXT("MI_LDFieldF3_Field"), *Material, Error) : nullptr;
        FSystemParts Field;
        Field.System = LDSliceVfx::ESystem::FieldF3;
        if (!Paint || !Arc) { return Field; }
        const FString Share = FieldShare;
        const FString Radius = TEXT("User.Width");
        const FString Hold = FString::Printf(TEXT("saturate(%s * 2.5)"), *Share);
        const FString Open = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.3), 3.0))");
        const FString Inside = FString::Printf(TEXT("(saturate((Particles.Age - 0.12) / 0.3) * saturate(%s * 1.6 - 0.4) * lerp(0.6, 1.0, User.Focus))"), *Share);
        const FString Rise = FString::Printf(TEXT("lerp(0.35, 1.0, %s) * lerp(0.25, 1.0, %s)"), *Open, *Hold);
        FPart& Disc = Field.Parts.AddDefaulted_GetRef();
        Disc.Name = TEXT("Field");
        Disc.Kind = FPart::Sprite;
        Disc.Lifetime = TEXT("User.Duration");
        Disc.Paint = Paint;
        Disc.Facing = ENiagaraSpriteFacingMode::CustomFacingVector;
        Disc.Set(TEXT("Particles.SpriteFacing"), TEXT("float3(0.0, 0.0, 1.0)"))
            .Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 2.5)"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(2.24, 2.24) * %s * lerp(0.75, 1.0, %s) * lerp(0.92, 1.0, %s)"), *Radius, *Open, *Hold))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0 + 1.5 * exp(-Particles.Age / 0.12), %s, %s, saturate(Particles.Age / 0.04) * %s * %s)"),
                *Inside, *Rise, *Hold, *Share));
        // Arches over the edge, as F1's, a little thicker.
        const int32 ArchPoints = 5;
        FPart& Arches = Field.Parts.AddDefaulted_GetRef();
        Arches.Name = TEXT("Arches");
        Arches.Count = FString::Printf(TEXT("int(lerp(3.0, 7.0, User.Focus) + 0.01) * %d"), ArchPoints);
        Arches.Lifetime = TEXT("User.Duration");
        Arches.Paint = Arc;
        Arches.TileCm = 60.f;
        Arches.bRibbonIds = true;
        {
            const FString R = TEXT("float(Particles.LDRibbon)");
            const FString K = TEXT("Particles.LDJ");
            const FString Pick = TEXT("floor(Particles.Age / 0.18)");
            const FString Seed = FString::Printf(TEXT("(%s + 600.0)"), *R);
            const FString From = FString::Printf(TEXT("(%s * 6.2832)"), *FTrunk::Hash(Seed, Pick, 1.f));
            const FString Span = FString::Printf(TEXT("((50.0 + 40.0 * %s) / max(%s, 1.0))"), *FTrunk::Hash(Seed, Pick, 2.f), *Radius);
            const FString S = FString::Printf(TEXT("(%s / %d.0)"), *K, ArchPoints - 1);
            const FString A = FString::Printf(TEXT("(%s + %s * %s)"), *From, *Span, *S);
            const FString Lift = FString::Printf(TEXT("((35.0 + 25.0 * %s) * sin(3.1416 * %s))"), *FTrunk::Hash(Seed, Pick, 3.f), *S);
            const FString Kink = FString::Printf(TEXT("((%s - 0.5) * 12.0 * step(0.5, %s) * step(%s, %d.5))"),
                *FTrunk::Hash(FString::Printf(TEXT("%s * 7.0 + %s"), *Seed, *K), TEXT("floor(Particles.Age * 20.0)"), 4.f), *K, *K, ArchPoints - 2);
            const FString On = FString::Printf(TEXT("step(%s, 0.6) * step(0.3, Particles.Age)"), *FTrunk::Hash(Seed, Pick, 5.f));
            Arches.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, ArchPoints))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, ArchPoints))
                .Set(TEXT("Particles.RibbonLinkOrder"), K)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * (%s + %s) + float3(0.0, 0.0, 2.0 + %s + %s * 0.5)"),
                    *A, *A, *Radius, *Kink, *Lift, *Kink))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("12.0 * min(1.0, 0.4 + 2.4 * min(%s, 1.0 - %s)) * %s"), *S, *S, *On))
                .Set(TEXT("Particles.Color"), Rgba(TEXT("1.7"), FString::Printf(TEXT("%s * %s"), *Hold, *Share)));
        }
        return Field;
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

    bool BuildSystem(const FSystemParts& Look, UNiagaraEmitter& BeamEmitter, UNiagaraEmitter& SparkEmitter, FString& Error)
    {
        if (!Error.IsEmpty()) { return false; }
        const FString PackageName = LDSliceVfx::SystemPackage(Look.System);
        FSteps Steps;
        UNiagaraSystem* System = CreateChainSystem(PackageName, Steps, Error);
        if (!System) { return Error.IsEmpty() ? NiagaraFail(Error, FString::Printf(TEXT("Chain %s user parameters: %s"), LDSliceVfx::SystemName(Look.System), *Steps.Failed)) : false; }
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
        if (!Steps.Failed.IsEmpty()) { return NiagaraFail(Error, FString::Printf(TEXT("Niagara Chain %s build step failed: %s"), LDSliceVfx::SystemName(Look.System), *Steps.Failed)); }

        // Emitter and renderer properties are plain UPROPERTY edits.
        for (const FPart& Part : Look.Parts)
        {
            FVersionedNiagaraEmitterData* Data = EmitterDataOf(*System, Part.Name);
            if (!Data || Data->GetRenderers().Num() != 1) { return NiagaraFail(Error, FString::Printf(TEXT("Chain %s emitter %s should have one renderer."), LDSliceVfx::SystemName(Look.System), *Part.Name.ToString())); }
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
                Sprite->FacingMode = Part.Facing;
            }
            else if (auto* Light = Cast<UNiagaraLightRendererProperties>(Renderer))
            {
                Light->bUseInverseSquaredFalloff = false;
                Light->DefaultExponent = 2.f;
                Light->bAffectsTranslucency = false;
            }
            else { return NiagaraFail(Error, FString::Printf(TEXT("Chain %s emitter %s has an unexpected renderer."), LDSliceVfx::SystemName(Look.System), *Part.Name.ToString())); }
            Renderer->PostEditChange();
        }
        return CompileAndSave(*System, LDSliceVfx::SystemName(Look.System), PackageName, Error);
    }
}

bool LDDemoVfxNiagara::Build(FString& Error)
{
    SavedPackages.Reset();
    UNiagaraEmitter* BeamEmitter = LoadObject<UNiagaraEmitter>(nullptr, BeamTemplate);
    UNiagaraEmitter* SparkEmitter = LoadObject<UNiagaraEmitter>(nullptr, SparkTemplate);
    if (!BeamEmitter || !SparkEmitter) { return NiagaraFail(Error, TEXT("The engine Niagara emitter templates DynamicBeam / SimpleSpriteBurst are missing.")); }
    for (int32 Tex = 0; Tex < int32(LDSliceVfx::ETex::Count); ++Tex)
    {
        if (!SliceTexture(LDSliceVfx::ETex(Tex))) { return NiagaraFail(Error, FString::Printf(TEXT("The slice texture %s is missing; the slice import must run first."), LDSliceVfx::TexId(LDSliceVfx::ETex(Tex)))); }
    }
    UTexture2D* Default = SliceTexture(LDSliceVfx::ETex::ChainAProfile);
    UMaterial* Chain = BuildChainMaterial(TEXT("M_LDChain"), false, Default, Error);
    UMaterial* Top = Chain ? BuildChainMaterial(TEXT("M_LDChainTop"), true, Default, Error) : nullptr;
    UMaterial* Light = Top ? BuildChainMaterial(TEXT("M_LDChainAdd"), false, Default, Error, true) : nullptr;
    if (!Chain || !Top || !Light || !BuildBodyGlowMaterial(Error)) { return false; }
    const TArray<TFunction<FSystemParts()>> Systems = {
        [&]() { return ChainParts(*Chain, *Top, Error); },
        [&]() { return HitParts(false, *Chain, *Top, Error); },
        [&]() { return HitParts(true, *Chain, *Top, Error); },
        [&]() { return SwingParts(1, *Chain, Error); },
        [&]() { return SwingParts(2, *Chain, Error); },
        [&]() { return SwingParts(3, *Chain, Error); },
        [&]() { return TellParts(1, *Chain, Error); },
        [&]() { return TellParts(2, *Chain, Error); },
        [&]() { return TellParts(3, *Chain, Error); },
        [&]() { return FieldParts(*Chain, *Light, Error); },
        [&]() { return FieldFenceParts(*Chain, Error); },
        [&]() { return FieldSurfaceParts(Error); },
        [&]() { return FieldChargedParts(*Chain, Error); }};
    for (const TFunction<FSystemParts()>& Make : Systems)
    {
        const FSystemParts Parts = Make();
        if (!BuildSystem(Parts, *BeamEmitter, *SparkEmitter, Error)) { return false; }
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
    for (int32 Index = 0; Index < int32(LDSliceVfx::ESystem::Count); ++Index)
    {
        const FString PackageName = LDSliceVfx::SystemPackage(LDSliceVfx::ESystem(Index));
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
