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

        // Attributes written only when a particle spawns, before Attributes (an origin a trail particle keeps where it was
        // left, a per-particle random draw).
        TArray<TPair<FString, FString>> SpawnOnly;
        // A sprite or light part that keeps spawning: its burst fires again every LoopSeconds (0 spawns once).
        float LoopSeconds = 0.f;

        FPart& Set(const TCHAR* Attribute, const FString& Hlsl) { Attributes.Emplace(Attribute, Hlsl); return *this; }
        FPart& AtSpawn(const TCHAR* Attribute, const FString& Hlsl) { SpawnOnly.Emplace(Attribute, Hlsl); return *this; }
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
        if (Name == TEXT("Particles.Velocity") || Name == TEXT("Particles.SpriteAlignment") || Name == TEXT("Particles.SpriteFacing")
            || Name == TEXT("Particles.LDOrigin") || Name == TEXT("Particles.LDHeading")) { return FNiagaraTypeDefinition::GetVec3Def(); }
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
            TEXT("float hot = smoothstep(0.62, 0.95, g);\r\n")
            TEXT("float3 low = lerp(EdgeColor, BodyColor, saturate(g / 0.6)) * BodyGain;\r\n")
            TEXT("float3 C = lerp(low, CoreColor * CoreGain, hot);\r\n")
            TEXT("if (Mode > 0.5 && Mode < 1.5)\r\n")
            TEXT("{\r\n")
            TEXT("    float n = Texture2DSample(Noise, NoiseSampler, float2(UV.x * NoiseTile - Time * NoiseSpeed, UV.y * 0.5 + 0.25)).g;\r\n")
            TEXT("    a *= lerp(lerp(1.0, saturate(0.3 + 1.2 * n), 1.0 - hot), 0.5, hot);\r\n")
            TEXT("    C *= lerp(0.7 + 0.7 * n, 1.0, hot);\r\n")
            TEXT("}\r\n")
            TEXT("// Mode 2: a floor patch whose outline follows its area: a soft round edge pushed in and out by the noise.\r\n")
            TEXT("if (Mode > 1.5)\r\n")
            TEXT("{\r\n")
            TEXT("    float e = Texture2DSample(Noise, NoiseSampler, UV * 1.7).g;\r\n")
            TEXT("    float r = length(UV - 0.5) * 2.0 + (e - 0.5) * 0.35;\r\n")
            TEXT("    a *= smoothstep(1.0, 0.55, r);\r\n")
            TEXT("}\r\n")
            TEXT("// Particle Color tints the outer layers; the core keeps its own warm white, only its brightness follows.\r\n")
            TEXT("float Level = max(Tint.r, max(Tint.g, Tint.b));\r\n")
            TEXT("return float4(lerp(C * Tint, C * Level, hot), saturate(a * Fade));");
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
    const FPalette NeutralPalette{FLinearColor(1.f, .95f, .86f), FLinearColor(.92f, .92f, .92f), FLinearColor(.22f, .2f, .18f), 2.4f, 1.2f};
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
        UMaterialInstanceConstant* Streaks = ChainPaint(bFinisher ? TEXT("MI_LDHitFinisher_Streak") : TEXT("MI_LDHitBasic_Streak"), Top, Profile, nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Spark = ChainPaint(bFinisher ? TEXT("MI_LDHitFinisher_Spark") : TEXT("MI_LDHitBasic_Spark"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
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
            SliceTexture(LDSliceVfx::ETex::DissolveNoise), 1.f, NeutralPalette, .6f, 2.f, Error);
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
            nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
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
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(9.0 + 9.0 * %s) * %s * step(%s, %s + 0.001)"), *Front, *Scale, *Along, *Shown))
            .Set(TEXT("Particles.Color"), Tinted(2.f, FString::Printf(TEXT("(0.6 + 0.4 * %s) * saturate(Particles.Age / 0.03)"), *Front)));
        // The swept zone inside the arc: a wide faint band at two thirds of its radius, revealed with it, so the reach
        // reads as an area at gameplay zoom without an inked crescent.
        FPart& Zone = Tell.Parts.AddDefaulted_GetRef();
        Zone.Name = TEXT("Zone");
        Zone.Count = FString::Printf(TEXT("%d"), Points);
        Zone.Lifetime = TEXT("User.Duration + 0.03");
        Zone.Paint = Line;
        Zone.TileCm = 60.f;
        Zone.Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + (%s * cos(%s) * %.1f + %s * sin(%s) * %.1f) * 0.62 * %s + float3(0.0, 0.0, 2.0)"),
                *Forward, *Theta, Ahead, *Left, *Theta, Side, *Scale))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("%.1f * %s * step(%s, %s + 0.001)"), .55f * Side, *Scale, *Along, *Shown))
            .Set(TEXT("Particles.Color"), Tinted(1.2f, TEXT("0.22 * saturate(Particles.Age / 0.03)")));
        return Tell;
    }

    // ------------------------------------------------------------------ water (active_hose_down) and the zap's charge
    // Water: a cohesive see-through stream that breaks into beads with gravity, a cool white glint for its core, mist where
    // it strikes, a dark soaked floor where it lands. Never a flat blue fill, never an outline.

    // Floor-referenced geometry of a band from User.Start (the floor under the caster) to User.End (the floor at its reach).
    const TCHAR* const BandDir = TEXT("normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0))");
    const TCHAR* const BandSide = TEXT("normalize(cross(normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0)), float3(0.0, 0.0, 1.0)))");
    const TCHAR* const BandLength = TEXT("length(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0))");

    // The water palettes: the stream's body is white so User.Tint (the water ink) colours it, its glints a cool white; the
    // soaked floor is dark (it darkens what is under it) with rare glints; mist is a pale cool grey.
    const FPalette WaterPalette{FLinearColor(.9f, .98f, 1.f), FLinearColor(1.f, 1.f, 1.f), FLinearColor(.08f, .2f, .3f), 1.8f, 1.f};
    const FPalette WetPalette{FLinearColor(.85f, .95f, 1.f), FLinearColor(.18f, .22f, .25f), FLinearColor(0.f, 0.f, 0.f), 1.2f, 1.f};
    const FPalette MistPalette{FLinearColor(1.f, 1.f, 1.f), FLinearColor(.92f, .96f, 1.f), FLinearColor(.72f, .82f, .9f), 1.f, 1.f};

    // A floor-laid sprite (facing up) whose U runs along Along.
    void Floor(FPart& Part, const FString& Along)
    {
        Part.Kind = FPart::Sprite;
        Part.Facing = ENiagaraSpriteFacingMode::CustomFacingVector;
        Part.Align = ENiagaraSpriteAlignment::CustomAlignment;
        Part.Set(TEXT("Particles.SpriteFacing"), TEXT("float3(0.0, 0.0, 1.0)"))
            .Set(TEXT("Particles.SpriteAlignment"), Along);
    }

    // active_hose_down's windup on the floor (User.Duration, 0.2 s): the band it will soak at its approved reach and half
    // width (User.Width). Two thin cool rails and the far edge draw out within the first third, so the danger area is
    // whole early; inside them a faint flowing water sheet and a bright front fill from the caster to the far edge as the
    // windup runs out, so the front says when it fires. Others' bands keep the same rails and front (danger information
    // never degrades); only the sheet is fainter.
    FSystemParts HoseTellParts(UMaterial& Material, FString& Error)
    {
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDHoseTell_Line"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f,
            WaterPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Sheet = ChainPaint(TEXT("MI_LDHoseTell_Sheet"), Material, SliceTexture(LDSliceVfx::ETex::WaterFlow),
            SliceTexture(LDSliceVfx::ETex::WaterFlow), 1.f, WaterPalette, 1.f, 2.5f, Error);
        FSystemParts Tell;
        Tell.System = LDSliceVfx::ESystem::HoseTell;
        if (!Line || !Sheet) { return Tell; }
        const FString Dur = TEXT("max(User.Duration, 0.05)");
        const FString Drawn = FString::Printf(TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / (0.33 * %s)), 2.0))"), *Dur);
        const FString Fill = FString::Printf(TEXT("saturate(Particles.Age / %s)"), *Dur);
        const FString In = TEXT("saturate(Particles.Age / 0.03)");
        const int32 Points = 12;
        // Ribbons 0 and 1 the side rails, 2 the far edge, 3 the front.
        FPart& Lines = Tell.Parts.AddDefaulted_GetRef();
        Lines.Name = TEXT("Lines");
        Lines.Count = FString::Printf(TEXT("%d"), Points * 4);
        Lines.Lifetime = TEXT("User.Duration + 0.03");
        Lines.Paint = Line;
        Lines.TileCm = 60.f;
        Lines.bRibbonIds = true;
        {
            const FString R = TEXT("float(Particles.LDRibbon)");
            const FString T = FString::Printf(TEXT("(Particles.LDJ / %d.0)"), Points - 1);
            const FString Rail = FString::Printf(TEXT("(User.Start + %s * %s * %s * 0.999 + %s * User.Width * (%s * 2.0 - 1.0) * 0.985)"),
                BandDir, BandLength, *(T + TEXT(" * ") + Drawn), BandSide, *R);
            const auto Across = [&](const FString& Along)
            { return FString::Printf(TEXT("(User.Start + %s * %s * %s + %s * User.Width * (%s * 2.0 - 1.0) * 0.985)"), BandDir, BandLength, *Along, BandSide, *T); };
            const FString Far = Across(TEXT("1.0"));
            const FString Front = Across(Fill);
            Lines.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("lerp(lerp(%s, %s, step(1.5, %s)), %s, step(2.5, %s)) + float3(0.0, 0.0, 2.5)"),
                    *Rail, *Far, *R, *Front, *R))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("lerp(lerp(5.0, 5.0 * step(0.98, %s), step(1.5, %s)), 9.0 * step(0.02, %s), step(2.5, %s))"),
                    *Drawn, *R, *Fill, *R))
                .Set(TEXT("Particles.Color"), Tinted(1.6f, FString::Printf(TEXT("%s * lerp(0.75, 1.0, step(2.5, %s))"), *In, *R)));
        }
        // The water sheet behind the front: a floor sprite from the caster to the front.
        FPart& Water = Tell.Parts.AddDefaulted_GetRef();
        Water.Name = TEXT("Sheet");
        Water.Lifetime = TEXT("User.Duration + 0.03");
        Water.Paint = Sheet;
        Floor(Water, BandDir);
        Water.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * %s * %s * 0.5 + float3(0.0, 0.0, 2.0)"), BandDir, BandLength, *Fill))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(max(%s * %s, 1.0), User.Width * 1.9)"), BandLength, *Fill))
            .Set(TEXT("Particles.SpriteRotation"), Quarter)
            .Set(TEXT("Particles.Color"), Tinted(1.2f, FString::Printf(TEXT("%s * lerp(0.22, 0.34, User.Focus)"), *In)));
        return Tell;
    }

    // active_hose_down's release over its band: the stream leaves the hand (about 95 cm over the floor, 30 cm ahead) and arcs
    // down to 25 cm at the far edge, its head reaching the edge in 90 ms; it holds to 0.24 s and its tail runs out to the
    // edge by 0.42 s. Its surface flows (water_flow scrolling) round a cool white pressure core; beads spray off its length
    // and fall, a crown of drops bursts where its head lands, mist hangs over it, and the floor under it darkens as soaked
    // and dries over a second. Others' streams: fewer beads and drops, no mist.
    FSystemParts HoseJetParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Flow = ChainPaint(TEXT("MI_LDHoseJet_Flow"), Material, SliceTexture(LDSliceVfx::ETex::WaterFlow),
            SliceTexture(LDSliceVfx::ETex::WaterFlow), 1.f, WaterPalette, 1.f, 4.f, Error);
        UMaterialInstanceConstant* Core = ChainPaint(TEXT("MI_LDHoseJet_Core"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f,
            WaterPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Drop = ChainPaint(TEXT("MI_LDHoseJet_Drop"), Top, SliceTexture(LDSliceVfx::ETex::WaterDrop), nullptr, 0.f, WaterPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Mist = ChainPaint(TEXT("MI_LDHoseJet_Mist"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, MistPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Wet = ChainPaint(TEXT("MI_LDHoseJet_Wet"), Material, SliceTexture(LDSliceVfx::ETex::WaterWet), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, WetPalette, 1.f, 0.f, Error);
        FSystemParts Jet;
        Jet.System = LDSliceVfx::ESystem::HoseJet;
        if (!Flow || !Core || !Drop || !Mist || !Wet) { return Jet; }
        const FString Len = BandLength;
        const FString Head = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.09), 2.0))");
        const FString Tail = TEXT("pow(saturate((Particles.Age - 0.24) / 0.18), 1.5)");
        // The stream's centre line at s (0 the hand, 1 the far edge).
        const auto Path = [&](const FString& S, const FString& Wobble)
        {
            return FString::Printf(TEXT("(User.Start + %s * (30.0 + (%s - 30.0) * (%s)) + float3(0.0, 0.0, 1.0) * lerp(95.0, 25.0, pow(saturate(%s), 1.6)) + %s * %s)"),
                BandDir, *Len, *S, *S, BandSide, *Wobble);
        };
        const int32 Points = 28;
        const FString S = FString::Printf(TEXT("lerp(%s, %s, %s / %d.0)"), *Tail, *Head, Own, Points - 1);
        const FString Wave = FString::Printf(TEXT("(sin((%s) * 9.0 + Particles.Age * 34.0) * 4.0)"), *S);
        const FString Live = TEXT("step(Particles.Age, 0.43)");
        const auto Stream = [&](FPart& Part, const TCHAR* Name, UMaterialInstanceConstant* Paint, float Narrow, float Gain, float Alpha)
        {
            Part.Name = Name;
            Part.Count = FString::Printf(TEXT("%d"), Points);
            Part.Lifetime = TEXT("0.45");
            Part.Paint = Paint;
            Part.TileCm = 140.f;
            Part.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), Path(S, Wave))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(16.0 + 44.0 * (%s)) * %.2f * %s * (1.0 - 0.6 * %s)"), *S, Narrow, *Live, *Tail))
                .Set(TEXT("Particles.Color"), Tinted(Gain, FString::Printf(TEXT("%.2f * %s * lerp(0.75, 1.0, User.Focus)"), Alpha, *Live)));
        };
        Stream(Jet.Parts.AddDefaulted_GetRef(), TEXT("Stream"), Flow, 1.f, 1.3f, .9f);
        Stream(Jet.Parts.AddDefaulted_GetRef(), TEXT("Core"), Core, .3f, 2.2f, .55f);
        // Beads off the stream's length: each leaves its point when the head has passed and the stream still stands there.
        const FString Gravity = TEXT("float3(0.0, 0.0, -980.0)");
        const auto Ballistic = [&](FPart& Part, const FString& From, const FString& Velocity, const FString& Born, const FString& Size, float Life)
        {
            const FString Tau = FString::Printf(TEXT("max(Particles.Age - (%s), 0.0)"), *Born);
            const FString Shown = FString::Printf(TEXT("step(%s, Particles.Age)"), *Born);
            Part.Kind = FPart::Sprite;
            Part.Lifetime = FString::Printf(TEXT("%.2f"), Life);
            Part.Paint = Drop;
            Part.Align = ENiagaraSpriteAlignment::VelocityAligned;
            Part.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("(%s) + %s * %s"), *Velocity, *Gravity, *Tau))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("max((%s) + (%s) * %s + 0.5 * %s * %s * %s, float3(-1.0e7, -1.0e7, User.Start.z + 2.0))"),
                    *From, *Velocity, *Tau, *Gravity, *Tau, *Tau))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("(%s) * float2(1.0 + 0.0015 * length(Particles.Velocity), 1.0) * %s"), *Size, *Shown))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Tinted(1.5f, FString::Printf(TEXT("%s * (1.0 - saturate((%s - %.2f) / 0.25))"), *Shown, *Tau, Life - .45f)));
        };
        FPart& Beads = Jet.Parts.AddDefaulted_GetRef();
        Beads.Name = TEXT("Beads");
        Beads.Count = TEXT("int(lerp(18.0, 44.0, User.Focus) + 0.5)");
        {
            const FString At = Random(31.f);
            Ballistic(Beads, Path(At, TEXT("0.0")),
                FString::Printf(TEXT("%s * (120.0 + 260.0 * %s) + %s * (%s * 2.0 - 1.0) * 170.0 + float3(0.0, 0.0, 60.0 + 170.0 * %s)"),
                    BandDir, *Random(32.f), BandSide, *Random(33.f), *Random(34.f)),
                FString::Printf(TEXT("%s * 0.09 + %s * 0.28"), *At, *Random(35.f)),
                FString::Printf(TEXT("float2(1.0, 1.0) * (5.0 + 8.0 * %s)"), *Random(36.f)), 1.f);
        }
        FPart& Crown = Jet.Parts.AddDefaulted_GetRef();
        Crown.Name = TEXT("Crown");
        Crown.Count = TEXT("int(lerp(10.0, 24.0, User.Focus) + 0.5)");
        {
            const FString Out = FString::Printf(TEXT("normalize(%s * 0.7 + %s * (%s * 2.0 - 1.0) * 1.2)"), BandDir, BandSide, *Random(41.f));
            Ballistic(Crown, Path(TEXT("1.0"), TEXT("0.0")),
                FString::Printf(TEXT("%s * (140.0 + 240.0 * %s) + float3(0.0, 0.0, 220.0 + 220.0 * %s)"), *Out, *Random(42.f), *Random(43.f)),
                FString::Printf(TEXT("0.08 + 0.17 * %s"), *Random(44.f)),
                FString::Printf(TEXT("float2(1.0, 1.0) * (6.0 + 9.0 * %s)"), *Random(45.f)), 1.1f);
        }
        // Mist over the stream's far half (own streams only).
        FPart& Haze = Jet.Parts.AddDefaulted_GetRef();
        Haze.Name = TEXT("Mist");
        Haze.Kind = FPart::Sprite;
        Haze.Count = TEXT("int(8.0 * User.Focus + 0.01)");
        Haze.Lifetime = TEXT("0.9");
        Haze.Paint = Mist;
        {
            const FString Born = FString::Printf(TEXT("(0.06 + 0.2 * %s)"), *Random(51.f));
            const FString Tau = FString::Printf(TEXT("max(Particles.Age - %s, 0.0)"), *Born);
            Haze.Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + float3(0.0, 0.0, 22.0) * %s + %s * 40.0 * %s"),
                    *Path(FString::Printf(TEXT("(0.35 + 0.65 * %s)"), *Random(52.f)), TEXT("0.0")), *Tau, BandDir, *Tau))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (30.0 + 90.0 * saturate(%s / 0.6)) * step(%s, Particles.Age)"), *Tau, *Born))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(53.f)))
                .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(0.85, 0.93, 1.0, 0.0) * %s + float4(0.0, 0.0, 0.0, 0.24 * saturate(%s / 0.08) * (1.0 - saturate(%s / 0.65)))"),
                    Bright, *Tau, *Tau));
        }
        // The soaked floor: six patches along the band, each darkening as the head passes and drying from 0.6 s.
        FPart& Soak = Jet.Parts.AddDefaulted_GetRef();
        Soak.Name = TEXT("Soak");
        Soak.Count = TEXT("6");
        Soak.Lifetime = TEXT("User.Duration");
        Soak.Paint = Wet;
        Floor(Soak, BandDir);
        {
            const FString K = FString::Printf(TEXT("((%s + 0.5) / 6.0)"), Own);
            const FString Born = FString::Printf(TEXT("(%s * 0.09 + 0.04)"), *K);
            Soak.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * (30.0 + (%s - 30.0) * %s) + %s * (%s * 2.0 - 1.0) * 12.0 + float3(0.0, 0.0, 1.5)"),
                    BandDir, *Len, *K, BandSide, *Random(61.f)))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(%s / 6.0 * 1.7, User.Width * (1.2 + 0.4 * %s)) * step(%s, Particles.Age)"), *Len, *Random(62.f), *Born))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("90.0 + (%s * 2.0 - 1.0) * 8.0"), *Random(63.f)))
                .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 1.0, 1.0, 0.0) * %s + float4(0.0, 0.0, 0.0, 0.6 * saturate((Particles.Age - %s) / 0.1) * (1.0 - saturate((Particles.Age - 0.6) / max(User.Duration - 0.65, 0.1))))"),
                    Bright, *Born));
        }
        return Jet;
    }

    // A water hit at User.End struck from User.Start's side: drops burst out of the far side of the body and fall, a puff of
    // mist hangs at the contact, a few drips run off it, and the floor under it shows a soaked patch that dries over a
    // second. Others' hits: fewer drops, no drips.
    FSystemParts WaterHitParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Drop = ChainPaint(TEXT("MI_LDWaterHit_Drop"), Top, SliceTexture(LDSliceVfx::ETex::WaterDrop), nullptr, 0.f, WaterPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Mist = ChainPaint(TEXT("MI_LDWaterHit_Mist"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, MistPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Wet = ChainPaint(TEXT("MI_LDWaterHit_Wet"), Material, SliceTexture(LDSliceVfx::ETex::WaterWet), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, WetPalette, 1.f, 0.f, Error);
        FSystemParts Hit;
        Hit.System = LDSliceVfx::ESystem::WaterHit;
        if (!Drop || !Mist || !Wet) { return Hit; }
        const FString Scale = FString::Printf(TEXT("(%s * lerp(0.8, 1.0, User.Focus))"), LinkScale);
        const FString Floor0 = TEXT("(User.End.z - 98.0)");
        const FString Gravity = TEXT("float3(0.0, 0.0, -980.0)");
        const auto Drops = [&](FPart& Part, const TCHAR* Name, const FString& Count, const FString& From, const FString& Velocity, const FString& Born,
            const FString& Size, float Life)
        {
            const FString Tau = FString::Printf(TEXT("max(Particles.Age - (%s), 0.0)"), *Born);
            const FString Shown = FString::Printf(TEXT("step(%s, Particles.Age)"), *Born);
            Part.Name = Name;
            Part.Kind = FPart::Sprite;
            Part.Count = Count;
            Part.Lifetime = FString::Printf(TEXT("%.2f"), Life);
            Part.Paint = Drop;
            Part.Align = ENiagaraSpriteAlignment::VelocityAligned;
            Part.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("(%s) + %s * %s"), *Velocity, *Gravity, *Tau))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("max((%s) + (%s) * %s + 0.5 * %s * %s * %s, float3(-1.0e7, -1.0e7, %s + 2.0))"),
                    *From, *Velocity, *Tau, *Gravity, *Tau, *Tau, *Floor0))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("(%s) * float2(1.0 + 0.0015 * length(Particles.Velocity), 1.0) * %s * %s"), *Size, *Shown, *Scale))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Tinted(1.5f, FString::Printf(TEXT("%s * (1.0 - saturate((%s - %.2f) / 0.25))"), *Shown, *Tau, Life - .4f)));
        };
        const FString Dir = FTrunk::Dir();
        const FString Side = FTrunk::Side();
        Drops(Hit.Parts.AddDefaulted_GetRef(), TEXT("Splash"), TEXT("int(lerp(10.0, 26.0, User.Focus) + 0.5)"),
            FString::Printf(TEXT("User.End + %s * 12.0"), *Dir),
            FString::Printf(TEXT("normalize(%s * 0.9 + %s * (%s * 2.0 - 1.0) * 0.9 + float3(0.0, 0.0, 0.35 + 0.8 * %s)) * (200.0 + 260.0 * %s) * %s"),
                *Dir, *Side, *Random(71.f), *Random(72.f), *Random(73.f), *Scale),
            FString::Printf(TEXT("0.04 * %s"), *Random(74.f)), FString::Printf(TEXT("float2(1.0, 1.0) * (6.0 + 8.0 * %s)"), *Random(75.f)), .9f);
        Drops(Hit.Parts.AddDefaulted_GetRef(), TEXT("Drips"), TEXT("int(4.0 * User.Focus + 0.01)"),
            FString::Printf(TEXT("User.End + float3((%s * 2.0 - 1.0) * 18.0, (%s * 2.0 - 1.0) * 18.0, -10.0 - 30.0 * %s)"), *Random(76.f), *Random(77.f), *Random(78.f)),
            TEXT("float3(0.0, 0.0, -40.0)"), FString::Printf(TEXT("0.12 + 0.4 * %s"), *Random(79.f)), TEXT("float2(1.0, 1.0) * 5.0"), 1.1f);
        FPart& Haze = Hit.Parts.AddDefaulted_GetRef();
        Haze.Name = TEXT("Mist");
        Haze.Kind = FPart::Sprite;
        Haze.Count = TEXT("int(lerp(1.0, 3.0, User.Focus) + 0.01)");
        Haze.Lifetime = TEXT("0.6");
        Haze.Paint = Mist;
        Haze.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * (15.0 + 30.0 * Particles.NormalizedAge) + float3((%s * 2.0 - 1.0) * 15.0, 0.0, (%s * 2.0 - 1.0) * 15.0)"),
                *Dir, *Random(81.f), *Random(82.f)))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (25.0 + 75.0 * pow(Particles.NormalizedAge, 0.5)) * %s"), *Scale))
            .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(83.f)))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(0.85, 0.93, 1.0, 0.0) * %s + float4(0.0, 0.0, 0.0, 0.3 * (1.0 - Particles.NormalizedAge))"), Bright));
        FPart& Soak = Hit.Parts.AddDefaulted_GetRef();
        Soak.Name = TEXT("Soak");
        Soak.Lifetime = TEXT("User.Duration");
        Soak.Paint = Wet;
        Floor(Soak, Dir);
        Soak.Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.End.x, User.End.y, %s + 1.5) + %s * 20.0"), *Floor0, *Dir))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 0.8) * 150.0 * %s * lerp(0.6, 1.0, saturate((Particles.Age - 0.06) / 0.2))"), *Scale))
            .Set(TEXT("Particles.SpriteRotation"), Quarter)
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 1.0, 1.0, 0.0) * %s + float4(0.0, 0.0, 0.0, 0.55 * saturate((Particles.Age - 0.06) / 0.1) * (1.0 - saturate((Particles.Age - 0.5) / max(User.Duration - 0.55, 0.1))))"),
                Bright));
        return Hit;
    }

    // active_zap's windup at the casting hand (User.Start, User.Duration 0.25 s): the same warm-white and pale-gold energy as
    // the approved chain (§2.1), gathering: three tiny kinked arcs crawl over a sphere that grows from 8 to 22 cm, redrawn at
    // 25 Hz, motes stream into the hand, and a soft warm glow builds. No star, no disc. The release glow (ZapRelease) and
    // the chain take over at the release.
    FSystemParts ZapChargeParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        const FPalette Palette{FLinearColor(1.f, .93f, .78f), FLinearColor(1.f, .68f, .26f), FLinearColor(.22f, .1f, .03f), 2.8f, 1.2f};
        UMaterialInstanceConstant* Arc = ChainPaint(TEXT("MI_LDZapCharge_Arc"), Top, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Glow = ChainPaint(TEXT("MI_LDZapCharge_Glow"), Material, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Mote = ChainPaint(TEXT("MI_LDZapCharge_Mote"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        FSystemParts Charge;
        Charge.System = LDSliceVfx::ESystem::ZapCharge;
        if (!Arc || !Glow || !Mote) { return Charge; }
        const FString Scale = TEXT("(User.Width / 40.0 * lerp(0.8, 1.0, User.Focus))");
        const FString P = TEXT("saturate(Particles.Age / max(User.Duration, 0.05))");
        const FString Step = TEXT("floor(Particles.Age * 25.0)");
        const int32 Points = 4;
        FPart& Arcs = Charge.Parts.AddDefaulted_GetRef();
        Arcs.Name = TEXT("Arcs");
        Arcs.Count = FString::Printf(TEXT("int(lerp(2.0, 3.0, User.Focus) + 0.01) * %d"), Points);
        Arcs.Lifetime = TEXT("User.Duration + 0.02");
        Arcs.Paint = Arc;
        Arcs.TileCm = 30.f;
        Arcs.bRibbonIds = true;
        {
            const FString R = TEXT("(float(Particles.LDRibbon) + 300.0)");
            const auto H = [&](const FString& Index, float Salt) { return FTrunk::Hash(Index, Step, Salt); };
            // Each arc runs between two random points of the sphere through two kinked interior points.
            const FString A = FString::Printf(TEXT("normalize(float3(%s, %s, %s) * 2.0 - 1.0 + float3(0.001, 0.0, 0.0))"), *H(R, 1.f), *H(R, 2.f), *H(R, 3.f));
            const FString B = FString::Printf(TEXT("normalize(%s + float3(%s, %s, %s) * 1.6 - 0.8)"), *A, *H(R, 4.f), *H(R, 5.f), *H(R, 6.f));
            const FString T = FString::Printf(TEXT("(Particles.LDJ / %d.0)"), Points - 1);
            const FString K = FString::Printf(TEXT("(Particles.LDJ + %s * 7.0)"), *R);
            const FString Radius = FString::Printf(TEXT("((8.0 + 14.0 * %s) * %s)"), *P, *Scale);
            Arcs.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + normalize(lerp(%s, %s, %s) + (float3(%s, %s, %s) * 2.0 - 1.0) * 0.35 * step(0.5, Particles.LDJ) * step(Particles.LDJ, 2.5)) * %s"),
                    *A, *B, *T, *H(K, 7.f), *H(K, 8.f), *H(K, 9.f), *Radius))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(2.0 + 2.5 * %s) * %s * step(0.3, %s)"), *P, *Scale, *H(R, 10.f)))
                .Set(TEXT("Particles.Color"), Tinted(2.6f, FString::Printf(TEXT("(0.5 + 0.5 * %s) * saturate(Particles.Age / 0.03)"), *P)));
        }
        FPart& Soft = Charge.Parts.AddDefaulted_GetRef();
        Soft.Name = TEXT("Glow");
        Soft.Kind = FPart::Sprite;
        Soft.Lifetime = TEXT("User.Duration + 0.02");
        Soft.Paint = Glow;
        Soft.Set(TEXT("Particles.Position"), TEXT("User.Start"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (18.0 + 30.0 * %s) * %s"), *P, *Scale))
            .Set(TEXT("Particles.Color"), Tinted(1.6f, FString::Printf(TEXT("0.3 + 0.5 * %s"), *P)));
        FPart& Motes = Charge.Parts.AddDefaulted_GetRef();
        Motes.Name = TEXT("Motes");
        Motes.Kind = FPart::Sprite;
        Motes.Count = TEXT("int(lerp(3.0, 7.0, User.Focus) + 0.5)");
        Motes.Lifetime = TEXT("User.Duration + 0.02");
        Motes.Paint = Mote;
        Motes.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString Born = FString::Printf(TEXT("(%s * max(User.Duration - 0.12, 0.0))"), *Random(91.f));
            const FString T = FString::Printf(TEXT("saturate((Particles.Age - %s) / 0.12)"), *Born);
            const FString From = FString::Printf(TEXT("normalize(float3(%s, %s, %s) * 2.0 - 1.0 + float3(0.0, 0.0, 0.001))"), *Random(92.f), *Random(93.f), *Random(94.f));
            Motes.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * 45.0 * %s * (1.0 - %s)"), *From, *Scale, *T))
                .Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("-%s * 300.0"), *From))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(16.0, 4.0) * %s * step(%s, Particles.Age) * step(%s, 0.99)"), *Scale, *Born, *T))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Tinted(2.2f, TEXT("1.0")));
        }
        FPart& Light = Charge.Parts.AddDefaulted_GetRef();
        Light.Name = TEXT("Light");
        Light.Kind = FPart::Light;
        Light.Lifetime = TEXT("User.Duration + 0.02");
        Light.Set(TEXT("Particles.Position"), TEXT("User.Start"))
            .Set(TEXT("Particles.LightRadius"), FString::Printf(TEXT("(30.0 + 50.0 * %s) * %s"), *P, *Scale))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 0.78, 0.4, 1.0) * float4(3.0, 3.0, 3.0, 1.0) * %s * %s"), Bright, *P));
        return Charge;
    }

    // ------------------------------------------------------------------ fire (active_seethe; fire hits)
    // Fire: soft volumetric tongues that rise, lean and thin into wisps (fire_tongue), embers drifting up, dark smoke, a
    // scorched floor with glowing cracks (fire_char). Its hue lives in its palette (deep red edge, orange body, pale gold
    // core), so the parts pass a neutral brightness instead of User.Tint. Never a flat orange disc or a cartoon flame.
    const FPalette FirePalette{FLinearColor(1.f, .9f, .62f), FLinearColor(1.f, .5f, .14f), FLinearColor(.45f, .06f, .015f), 2.6f, 1.5f};
    const FPalette SmokePalette{FLinearColor(.3f, .28f, .26f), FLinearColor(.16f, .15f, .14f), FLinearColor(.06f, .055f, .05f), 1.f, 1.f};
    const FPalette CharPalette{FLinearColor(1.f, .78f, .4f), FLinearColor(1.f, .38f, .08f), FLinearColor(.035f, .025f, .02f), 2.2f, 1.6f};

    // A neutral brightness (the palette carries the hue) times the runtime's intensity.
    FString Glowing(float Gain, const FString& Alpha)
    {
        return FString::Printf(TEXT("float4(%s, %s, %s, 0.0) * %.2f + float4(0.0, 0.0, 0.0, %s)"), Bright, Bright, Bright, Gain, *Alpha);
    }

    // Flame tongue sprites: base pivot, one random cell of the 2 x 2 atlas each.
    void Tongues(FPart& Part, UMaterialInstanceConstant* Paint)
    {
        Part.Kind = FPart::Sprite;
        Part.Paint = Paint;
        Part.SubImages = FVector2D(2., 2.);
        Part.Pivot = FVector2D(.5, 1.);
        Part.Set(TEXT("Particles.SubImageIndex"), FString::Printf(TEXT("floor(%s * 3.99)"), *Random(101.f)));
    }

    // A ribbon circle of Points round User.Start at Radius (HLSL), lifted Lift cm.
    FString Circle(const FString& Radius, int32 Points, float Lift)
    {
        return FString::Printf(TEXT("User.Start + float3(cos(%s / %d.0 * 6.2832), sin(%s / %d.0 * 6.2832), 0.0) * (%s) + float3(0.0, 0.0, %.1f)"),
            Own, Points - 1, Own, Points - 1, *Radius, Lift);
    }

    // active_seethe's windup (User.Duration, 0.25 s) round the floor under the caster (User.Start) at its radius (User.Width):
    // a thin ember-orange boundary drawn round within the first third (danger information, same for others' casts), a front
    // ring that grows from the body to the boundary as the windup runs out, and heat gathering at the feet: small tongues
    // licking up round the ankles, taller as the release nears.
    FSystemParts SeetheTellParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDSeetheTell_Line"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Flame = ChainPaint(TEXT("MI_LDSeetheTell_Flame"), Material, SliceTexture(LDSliceVfx::ETex::FireTongue), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        FSystemParts Tell;
        Tell.System = LDSliceVfx::ESystem::SeetheTell;
        if (!Line || !Flame) { return Tell; }
        const FString P = TEXT("saturate(Particles.Age / max(User.Duration, 0.05))");
        const FString Drawn = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / (0.33 * max(User.Duration, 0.05))), 2.0))");
        const int32 Points = 40;
        FPart& Edge = Tell.Parts.AddDefaulted_GetRef();
        Edge.Name = TEXT("Edge");
        Edge.Count = FString::Printf(TEXT("%d"), Points);
        Edge.Lifetime = TEXT("User.Duration + 0.03");
        Edge.Paint = Line;
        Edge.TileCm = 60.f;
        Edge.Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), Circle(TEXT("User.Width * 0.99"), Points, 3.f))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("6.0 * step(%s / %d.0, %s + 0.001)"), Own, Points - 1, *Drawn))
            .Set(TEXT("Particles.Color"), Glowing(1.3f, TEXT("0.85 * saturate(Particles.Age / 0.03)")));
        FPart& Front = Tell.Parts.AddDefaulted_GetRef();
        Front.Name = TEXT("Front");
        Front.Count = FString::Printf(TEXT("%d"), Points);
        Front.Lifetime = TEXT("User.Duration + 0.03");
        Front.Paint = Line;
        Front.TileCm = 60.f;
        Front.Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), Circle(FString::Printf(TEXT("User.Width * max(%s, 0.05)"), *P), Points, 3.f))
            .Set(TEXT("Particles.RibbonWidth"), TEXT("9.0"))
            .Set(TEXT("Particles.Color"), Glowing(1.8f, FString::Printf(TEXT("(0.35 + 0.55 * %s) * saturate(Particles.Age / 0.03)"), *P)));
        FPart& Heat = Tell.Parts.AddDefaulted_GetRef();
        Heat.Name = TEXT("Heat");
        Heat.Count = TEXT("int(lerp(5.0, 11.0, User.Focus) + 0.5)");
        Heat.Lifetime = TEXT("User.Duration + 0.03");
        Tongues(Heat, Flame);
        {
            const FString Angle = FString::Printf(TEXT("(%s * 6.2832)"), *Random(102.f));
            Heat.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * (25.0 + 30.0 * %s) + float3(0.0, 0.0, 2.0)"),
                    *Angle, *Angle, *Random(103.f)))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(0.5, 1.0) * (20.0 + 60.0 * %s * (0.6 + 0.4 * %s)) * (0.9 + 0.2 * sin(Particles.Age * 40.0 + %s * 9.0))"),
                    *P, *Random(104.f), *Random(105.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.2f, FString::Printf(TEXT("(0.3 + 0.6 * %s) * saturate(Particles.Age / 0.05)"), *P)));
        }
        return Tell;
    }

    // active_seethe's release round the floor under the caster (User.Start) at its radius (User.Width): a wall of flame
    // tongues races out from the body to the edge in 0.2 s and collapses, a column of tall tongues bursts up round the body,
    // embers and a little dark smoke climb, a short warm light flashes, and the floor inside the radius is left scorched
    // with glowing cracks that cool over a second and a half. Others' bursts: half the tongues, embers and smoke.
    FSystemParts SeetheBurstParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Flame = ChainPaint(TEXT("MI_LDSeethe_Flame"), Material, SliceTexture(LDSliceVfx::ETex::FireTongue), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Ember = ChainPaint(TEXT("MI_LDSeethe_Ember"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Smoke = ChainPaint(TEXT("MI_LDSeethe_Smoke"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, SmokePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Char = ChainPaint(TEXT("MI_LDSeethe_Char"), Material, SliceTexture(LDSliceVfx::ETex::FireChar), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, CharPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDSeethe_Line"), Top, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        FSystemParts Burst;
        Burst.System = LDSliceVfx::ESystem::SeetheBurst;
        if (!Flame || !Ember || !Smoke || !Char || !Line) { return Burst; }
        const FString Out = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.2), 2.0))");
        FPart& Wall = Burst.Parts.AddDefaulted_GetRef();
        Wall.Name = TEXT("Wall");
        Wall.Count = TEXT("int(lerp(18.0, 40.0, User.Focus) + 0.5)");
        Wall.Lifetime = TEXT("0.6");
        Tongues(Wall, Flame);
        {
            const FString Angle = FString::Printf(TEXT("((%s + %s * 0.6) / max(float(int(lerp(18.0, 40.0, User.Focus) + 0.5)), 1.0) * 6.2832)"), Own, *Random(111.f));
            const FString Life = TEXT("saturate((Particles.Age - 0.12) / 0.42)");
            Wall.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * User.Width * %s * (0.9 + 0.1 * %s) + float3(0.0, 0.0, 2.0)"),
                    *Angle, *Angle, *Out, *Random(112.f)))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(0.55, 1.0) * (55.0 + 45.0 * %s) * (0.5 + 0.5 * %s) * (1.0 - 0.7 * %s)"), *Random(113.f), *Out, *Life))
                .Set(TEXT("Particles.Color"), Glowing(1.4f, FString::Printf(TEXT("(1.0 - %s) * saturate(Particles.Age / 0.02)"), *Life)));
        }
        FPart& Ring = Burst.Parts.AddDefaulted_GetRef();
        Ring.Name = TEXT("HeatRing");
        Ring.Count = TEXT("40");
        Ring.Lifetime = TEXT("0.3");
        Ring.Paint = Line;
        Ring.TileCm = 60.f;
        Ring.Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), Circle(FString::Printf(TEXT("User.Width * max(%s, 0.05)"), *Out), 40, 4.f))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("14.0 * (1.0 - saturate((Particles.Age - 0.18) / 0.1))")))
            .Set(TEXT("Particles.Color"), Glowing(2.2f, TEXT("0.9")));
        FPart& Column = Burst.Parts.AddDefaulted_GetRef();
        Column.Name = TEXT("Column");
        Column.Count = TEXT("int(lerp(5.0, 11.0, User.Focus) + 0.5)");
        Column.Lifetime = TEXT("0.6");
        Tongues(Column, Flame);
        {
            const FString Angle = FString::Printf(TEXT("(%s * 6.2832)"), *Random(121.f));
            const FString Up = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.12), 2.0))");
            Column.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * (30.0 + 25.0 * %s) + float3(0.0, 0.0, 2.0 + 40.0 * Particles.NormalizedAge)"),
                    *Angle, *Angle, *Random(122.f)))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(0.5, 1.0) * (110.0 + 60.0 * %s) * %s * (1.0 - 0.6 * Particles.NormalizedAge)"), *Random(123.f), *Up))
                .Set(TEXT("Particles.Color"), Glowing(1.5f, TEXT("(1.0 - pow(Particles.NormalizedAge, 1.5))")));
        }
        FPart& Embers = Burst.Parts.AddDefaulted_GetRef();
        Embers.Name = TEXT("Embers");
        Embers.Kind = FPart::Sprite;
        Embers.Count = TEXT("int(lerp(12.0, 30.0, User.Focus) + 0.5)");
        Embers.Lifetime = FString::Printf(TEXT("0.7 + 0.5 * %s"), *Random(131.f));
        Embers.Paint = Ember;
        Embers.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString Angle = FString::Printf(TEXT("(%s * 6.2832)"), *Random(132.f));
            const FString V = FString::Printf(TEXT("(float3(cos(%s), sin(%s), 0.0) * (120.0 + 260.0 * %s) + float3(0.0, 0.0, 180.0 + 260.0 * %s))"), *Angle, *Angle, *Random(133.f), *Random(134.f));
            Embers.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s * (1.0 - 0.7 * Particles.NormalizedAge) + float3(sin(Particles.Age * 7.0 + %s * 6.0), cos(Particles.Age * 5.0 + %s * 6.0), 0.0) * 40.0"),
                    *V, *Random(135.f), *Random(136.f)))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(0.0, 0.0, 30.0 + 60.0 * %s) + %s * (Particles.Age - 0.35 * Particles.Age * Particles.NormalizedAge)"),
                    *Random(137.f), *V))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(18.0, 4.0) * (1.0 - 0.5 * Particles.NormalizedAge)"))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(2.4f, FString::Printf(TEXT("(1.0 - Particles.NormalizedAge) * step(0.25, frac(Particles.Age * 6.0 + %s))"), *Random(138.f))));
        }
        FPart& Haze = Burst.Parts.AddDefaulted_GetRef();
        Haze.Name = TEXT("Smoke");
        Haze.Kind = FPart::Sprite;
        Haze.Count = TEXT("int(lerp(2.0, 6.0, User.Focus) + 0.5)");
        Haze.Lifetime = TEXT("1.2");
        Haze.Paint = Smoke;
        {
            const FString Angle = FString::Printf(TEXT("(%s * 6.2832)"), *Random(141.f));
            Haze.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * (40.0 + 120.0 * %s) + float3(0.0, 0.0, 60.0 + 120.0 * Particles.NormalizedAge)"),
                    *Angle, *Angle, *Random(142.f)))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * (60.0 + 110.0 * Particles.NormalizedAge)"))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0 + Particles.Age * 20.0"), *Random(143.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.f, TEXT("0.32 * saturate((Particles.Age - 0.12) / 0.15) * (1.0 - Particles.NormalizedAge)")));
        }
        FPart& Scorch = Burst.Parts.AddDefaulted_GetRef();
        Scorch.Name = TEXT("Scorch");
        Scorch.Lifetime = TEXT("User.Duration");
        Scorch.Paint = Char;
        Floor(Scorch, TEXT("float3(1.0, 0.0, 0.0)"));
        // The cracks cool (brightness) faster than the soot fades (alpha).
        Scorch.Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 1.5)"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * User.Width * 1.8 * max(%s, 0.1)"), *Out))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 1.0, 1.0, 0.0) * %s * (0.3 + 1.2 * (1.0 - saturate((Particles.Age - 0.2) / 0.9))) + float4(0.0, 0.0, 0.0, 0.55 * (1.0 - saturate((Particles.Age - 0.5) / max(User.Duration - 0.55, 0.1))))"),
                Bright));
        FPart& Light = Burst.Parts.AddDefaulted_GetRef();
        Light.Name = TEXT("Light");
        Light.Kind = FPart::Light;
        Light.Lifetime = TEXT("0.3");
        Light.Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 80.0)"))
            .Set(TEXT("Particles.LightRadius"), TEXT("User.Width * 1.3"))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 0.55, 0.2, 1.0) * float4(5.0, 5.0, 5.0, 1.0) * %s * (1.0 - Particles.NormalizedAge)"), Bright));
        return Burst;
    }

    // A fire hit at User.End struck from User.Start's side: a few tongues lick up off the struck side, embers spit away from
    // the strike, and a puff of soot rises. Small and quick: burns tick often, so a fire hit never floods the body.
    FSystemParts FireHitParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Flame = ChainPaint(TEXT("MI_LDFireHit_Flame"), Top, SliceTexture(LDSliceVfx::ETex::FireTongue), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Ember = ChainPaint(TEXT("MI_LDFireHit_Ember"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Smoke = ChainPaint(TEXT("MI_LDFireHit_Smoke"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, SmokePalette, 1.f, 0.f, Error);
        FSystemParts Hit;
        Hit.System = LDSliceVfx::ESystem::FireHit;
        if (!Flame || !Ember || !Smoke) { return Hit; }
        const FString Scale = FString::Printf(TEXT("(%s * lerp(0.8, 1.0, User.Focus))"), LinkScale);
        const FString Dir = FTrunk::Dir();
        FPart& Licks = Hit.Parts.AddDefaulted_GetRef();
        Licks.Name = TEXT("Licks");
        Licks.Count = TEXT("int(lerp(3.0, 6.0, User.Focus) + 0.5)");
        Licks.Lifetime = FString::Printf(TEXT("0.28 + 0.12 * %s"), *Random(151.f));
        Tongues(Licks, Flame);
        Licks.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End - %s * 12.0 + float3((%s * 2.0 - 1.0) * 22.0, (%s * 2.0 - 1.0) * 22.0, -25.0 + 30.0 * %s + 30.0 * Particles.NormalizedAge) * %s"),
                *Dir, *Random(152.f), *Random(153.f), *Random(154.f), *Scale))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(0.55, 1.0) * (35.0 + 30.0 * %s) * %s * (1.0 - 0.5 * Particles.NormalizedAge) * saturate(Particles.Age / 0.04)"),
                *Random(155.f), *Scale))
            .Set(TEXT("Particles.Color"), Glowing(1.5f, TEXT("(1.0 - pow(Particles.NormalizedAge, 2.0))")));
        FPart& Embers = Hit.Parts.AddDefaulted_GetRef();
        Embers.Name = TEXT("Embers");
        Embers.Kind = FPart::Sprite;
        Embers.Count = TEXT("int(lerp(4.0, 10.0, User.Focus) + 0.5)");
        Embers.Lifetime = FString::Printf(TEXT("0.4 + 0.3 * %s"), *Random(161.f));
        Embers.Paint = Ember;
        Embers.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString V = FString::Printf(TEXT("(%s * (200.0 + 200.0 * %s) * %s)"), *SparkDirection(1.2f, .9f), *Random(162.f), *Scale);
            Embers.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s * (1.0 - 0.6 * Particles.NormalizedAge) + float3(0.0, 0.0, 120.0) * Particles.NormalizedAge"), *V))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * Particles.Age * (1.0 - 0.3 * Particles.NormalizedAge) + float3(0.0, 0.0, 60.0) * Particles.Age * Particles.NormalizedAge"), *V))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(16.0, 3.5) * %s"), *Scale))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(2.4f, TEXT("(1.0 - Particles.NormalizedAge)")));
        }
        FPart& Soot = Hit.Parts.AddDefaulted_GetRef();
        Soot.Name = TEXT("Soot");
        Soot.Kind = FPart::Sprite;
        Soot.Count = TEXT("int(lerp(1.0, 2.0, User.Focus) + 0.5)");
        Soot.Lifetime = TEXT("0.8");
        Soot.Paint = Smoke;
        Soot.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * 10.0 + float3(0.0, 0.0, 10.0 + 50.0 * Particles.NormalizedAge)"), *Dir))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (30.0 + 60.0 * Particles.NormalizedAge) * %s"), *Scale))
            .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(171.f)))
            .Set(TEXT("Particles.Color"), Glowing(1.f, TEXT("0.3 * saturate(Particles.Age / 0.08) * (1.0 - Particles.NormalizedAge)")));
        return Hit;
    }

    // ------------------------------------------------------------------ chill (active_cola_shake)
    // Chill: a shaken-can blast of icy fizz. Fast white-cyan fizz beads that slow in the air, faceted shards tumbling and
    // falling, cold mist that billows and sinks, a frosted floor of branching needles. Hard facets and sinking cold, where
    // water is soft and fire rises.
    const FPalette IcePalette{FLinearColor(.95f, 1.f, 1.f), FLinearColor(.62f, .86f, 1.f), FLinearColor(.12f, .26f, .4f), 2.f, 1.2f};
    const FPalette FrostPalette{FLinearColor(.95f, 1.f, 1.f), FLinearColor(.7f, .9f, 1.f), FLinearColor(.3f, .45f, .55f), 1.6f, 1.f};
    const FPalette ColdMistPalette{FLinearColor(1.f, 1.f, 1.f), FLinearColor(.88f, .96f, 1.f), FLinearColor(.66f, .8f, .9f), 1.f, 1.f};

    // A cone on the floor from User.Start towards User.End (its length the reach) with half angle User.Width (degrees):
    // the direction at fraction A of the way from its left edge (0) to its right edge (1).
    FString ConeRay(const FString& A)
    {
        return FString::Printf(TEXT("(%s * cos(radians(User.Width) * ((%s) * 2.0 - 1.0)) + %s * sin(radians(User.Width) * ((%s) * 2.0 - 1.0)))"),
            BandDir, *A, BandSide, *A);
    }

    // active_cola_shake's windup (User.Duration, 0.15 s): the cone it will blast at its approved reach and angle, both edges
    // and the far arc drawn at once in a cold white (danger information, the same for others' casts), and a front arc that
    // runs out to the far arc as the windup ends; a few frost needles creep from the caster's feet.
    FSystemParts ColaTellParts(UMaterial& Material, FString& Error)
    {
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDColaTell_Line"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, IcePalette, 1.f, 0.f, Error);
        FSystemParts Tell;
        Tell.System = LDSliceVfx::ESystem::ColaTell;
        if (!Line) { return Tell; }
        const FString Fill = TEXT("saturate(Particles.Age / max(User.Duration, 0.05))");
        const FString Drawn = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.06), 2.0))");
        const int32 Points = 16;
        // Ribbons 0 and 1 the edges, 2 the far arc, 3 the front arc.
        FPart& Lines = Tell.Parts.AddDefaulted_GetRef();
        Lines.Name = TEXT("Lines");
        Lines.Count = FString::Printf(TEXT("%d"), Points * 4);
        Lines.Lifetime = TEXT("User.Duration + 0.03");
        Lines.Paint = Line;
        Lines.TileCm = 60.f;
        Lines.bRibbonIds = true;
        {
            const FString R = TEXT("float(Particles.LDRibbon)");
            const FString T = FString::Printf(TEXT("(Particles.LDJ / %d.0)"), Points - 1);
            const FString Edge = FString::Printf(TEXT("(User.Start + %s * %s * %s * %s)"), *ConeRay(R), BandLength, *T, *Drawn);
            const FString Arc = FString::Printf(TEXT("(User.Start + %s * %s)"), *ConeRay(T), BandLength);
            const FString Front = FString::Printf(TEXT("(User.Start + %s * %s * max(%s, 0.04))"), *ConeRay(T), BandLength, *Fill);
            Lines.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("lerp(lerp(%s, %s, step(1.5, %s)), %s, step(2.5, %s)) + float3(0.0, 0.0, 3.0)"), *Edge, *Arc, *R, *Front, *R))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("lerp(lerp(5.0, 5.0 * step(0.98, %s), step(1.5, %s)), 9.0, step(2.5, %s))"), *Drawn, *R, *R))
                .Set(TEXT("Particles.Color"), Glowing(1.4f, FString::Printf(TEXT("saturate(Particles.Age / 0.03) * lerp(0.8, 1.0, step(2.5, %s))"), *R)));
        }
        return Tell;
    }

    // active_cola_shake's release from the hand along the cone (User.Start the floor under the caster, User.End the floor at
    // its reach, User.Width its half angle): a cold flash at the nozzle, a spray of fizz beads that leave fast and slow in
    // the air (drag), faceted shards tumbling out and falling, cold mist billowing through the cone and sinking, and the
    // floor of the cone frosted with needles that melt away over a second. Others' blasts: half the beads, no mist.
    FSystemParts ColaBlastParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Fizz = ChainPaint(TEXT("MI_LDCola_Fizz"), Top, SliceTexture(LDSliceVfx::ETex::WaterDrop), nullptr, 0.f, IcePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Shard = ChainPaint(TEXT("MI_LDCola_Shard"), Top, SliceTexture(LDSliceVfx::ETex::IceShard), nullptr, 0.f, IcePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Mist = ChainPaint(TEXT("MI_LDCola_Mist"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, ColdMistPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Frost = ChainPaint(TEXT("MI_LDCola_Frost"), Material, SliceTexture(LDSliceVfx::ETex::IceFrost), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, FrostPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Flash = ChainPaint(TEXT("MI_LDCola_Flash"), Top, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, IcePalette, 1.f, 0.f, Error);
        FSystemParts Blast;
        Blast.System = LDSliceVfx::ESystem::ColaBlast;
        if (!Fizz || !Shard || !Mist || !Frost || !Flash) { return Blast; }
        const FString Nozzle = FString::Printf(TEXT("(User.Start + %s * 35.0 + float3(0.0, 0.0, 105.0))"), BandDir);
        const FString Len = BandLength;
        // Travel with drag k: distance = v (1 - e^(-k t)) / k.
        const auto Drag = [](const FString& V, float K) { return FString::Printf(TEXT("((%s) * (1.0 - exp(-%.2f * Particles.Age)) / %.2f)"), *V, K, K); };
        FPart& Beads = Blast.Parts.AddDefaulted_GetRef();
        Beads.Name = TEXT("Fizz");
        Beads.Kind = FPart::Sprite;
        Beads.Count = TEXT("int(lerp(26.0, 60.0, User.Focus) + 0.5)");
        Beads.Lifetime = FString::Printf(TEXT("0.45 + 0.35 * %s"), *Random(201.f));
        Beads.Paint = Fizz;
        Beads.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString Ray = ConeRay(Random(202.f));
            const FString V = FString::Printf(TEXT("(%s * %s * (2.6 + 1.6 * %s) + float3(0.0, 0.0, (%s * 2.0 - 1.0) * 120.0))"), *Ray, *Len, *Random(203.f), *Random(204.f));
            Beads.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("(%s) * exp(-3.0 * Particles.Age) + float3(0.0, 0.0, -300.0) * Particles.Age"), *V))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + %s + float3(0.0, 0.0, -150.0) * Particles.Age * Particles.Age"), *Nozzle, *Drag(V, 3.f)))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0 + 0.003 * length(Particles.Velocity), 1.0) * (4.0 + 6.0 * %s)"), *Random(205.f)))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(1.6f, TEXT("(1.0 - pow(Particles.NormalizedAge, 2.0))")));
        }
        FPart& Shards = Blast.Parts.AddDefaulted_GetRef();
        Shards.Name = TEXT("Shards");
        Shards.Kind = FPart::Sprite;
        Shards.Count = TEXT("int(lerp(6.0, 14.0, User.Focus) + 0.5)");
        Shards.Lifetime = FString::Printf(TEXT("0.6 + 0.3 * %s"), *Random(211.f));
        Shards.Paint = Shard;
        Shards.SubImages = FVector2D(2., 2.);
        {
            const FString Ray = ConeRay(Random(212.f));
            const FString V = FString::Printf(TEXT("(%s * (500.0 + 500.0 * %s) + float3(0.0, 0.0, 80.0 + 160.0 * %s))"), *Ray, *Random(213.f), *Random(214.f));
            Shards.Set(TEXT("Particles.SubImageIndex"), FString::Printf(TEXT("floor(%s * 3.99)"), *Random(215.f)))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("max(%s + %s + float3(0.0, 0.0, -490.0) * Particles.Age * Particles.Age, float3(-1.0e7, -1.0e7, User.Start.z + 4.0))"),
                    *Nozzle, *Drag(V, 1.5f)))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (12.0 + 12.0 * %s) * saturate(Particles.Age / 0.04)"), *Random(216.f)))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0 + Particles.Age * (%s * 2.0 - 1.0) * 720.0"), *Random(217.f), *Random(218.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.3f, TEXT("(1.0 - pow(Particles.NormalizedAge, 3.0))")));
        }
        FPart& Haze = Blast.Parts.AddDefaulted_GetRef();
        Haze.Name = TEXT("Mist");
        Haze.Kind = FPart::Sprite;
        Haze.Count = TEXT("int(9.0 * User.Focus + 0.01)");
        Haze.Lifetime = TEXT("1.1");
        Haze.Paint = Mist;
        {
            const FString Ray = ConeRay(FString::Printf(TEXT("(0.2 + 0.6 * %s)"), *Random(221.f)));
            const FString V = FString::Printf(TEXT("(%s * %s * (1.2 + 1.6 * %s))"), *Ray, *Len, *Random(222.f));
            // Cold air sinks: it leaves at hand height and settles towards the floor.
            Haze.Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + %s + float3(0.0, 0.0, -70.0) * saturate(Particles.Age / 0.8)"), *Nozzle, *Drag(V, 2.2f)))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (40.0 + 120.0 * pow(Particles.NormalizedAge, 0.6)) * (0.8 + 0.4 * %s)"), *Random(223.f)))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0 + Particles.Age * 25.0"), *Random(224.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.1f, TEXT("0.3 * saturate(Particles.Age / 0.06) * (1.0 - Particles.NormalizedAge)")));
        }
        // The frosted floor: five patches along the cone's axis widening with the cone.
        FPart& Floored = Blast.Parts.AddDefaulted_GetRef();
        Floored.Name = TEXT("Frost");
        Floored.Count = TEXT("5");
        Floored.Lifetime = TEXT("User.Duration");
        Floored.Paint = Frost;
        Floor(Floored, BandDir);
        {
            const FString K = FString::Printf(TEXT("((%s + 0.6) / 5.0)"), Own);
            const FString Born = FString::Printf(TEXT("(0.03 + 0.12 * %s)"), *K);
            Floored.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * %s * %s + float3(0.0, 0.0, 1.5)"), BandDir, *Len, *K))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(%s * 0.3, 2.0 * %s * %s * tan(radians(User.Width)) + 40.0) * saturate((Particles.Age - %s) / 0.12)"),
                    *Len, *Len, *K, *Born))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(1.2f, FString::Printf(TEXT("0.7 * step(%s, Particles.Age) * (1.0 - saturate((Particles.Age - 0.5) / max(User.Duration - 0.55, 0.1)))"), *Born)));
        }
        FPart& Pop = Blast.Parts.AddDefaulted_GetRef();
        Pop.Name = TEXT("Flash");
        Pop.Kind = FPart::Sprite;
        Pop.Lifetime = TEXT("0.12");
        Pop.Paint = Flash;
        Pop.Set(TEXT("Particles.Position"), *Nozzle)
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * (40.0 + 50.0 * Particles.NormalizedAge)"))
            .Set(TEXT("Particles.Color"), Glowing(2.f, TEXT("(1.0 - Particles.NormalizedAge)")));
        return Blast;
    }

    // A chill hit at User.End struck from User.Start's side: a burst of faceted shards out of the far side that tumble and
    // fall, a cold puff that sinks, and a few crystals that cling to the struck side for a moment. No magic circle, no star.
    FSystemParts ChillHitParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Shard = ChainPaint(TEXT("MI_LDChillHit_Shard"), Top, SliceTexture(LDSliceVfx::ETex::IceShard), nullptr, 0.f, IcePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Mist = ChainPaint(TEXT("MI_LDChillHit_Mist"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, ColdMistPalette, 1.f, 0.f, Error);
        FSystemParts Hit;
        Hit.System = LDSliceVfx::ESystem::ChillHit;
        if (!Shard || !Mist) { return Hit; }
        const FString Scale = FString::Printf(TEXT("(%s * lerp(0.8, 1.0, User.Focus))"), LinkScale);
        const FString Dir = FTrunk::Dir();
        FPart& Burst = Hit.Parts.AddDefaulted_GetRef();
        Burst.Name = TEXT("Shards");
        Burst.Kind = FPart::Sprite;
        Burst.Count = TEXT("int(lerp(5.0, 12.0, User.Focus) + 0.5)");
        Burst.Lifetime = FString::Printf(TEXT("0.45 + 0.3 * %s"), *Random(231.f));
        Burst.Paint = Shard;
        Burst.SubImages = FVector2D(2., 2.);
        {
            const FString V = FString::Printf(TEXT("(%s * (180.0 + 260.0 * %s) * %s)"), *SparkDirection(1.3f, .7f), *Random(232.f), *Scale);
            Burst.Set(TEXT("Particles.SubImageIndex"), FString::Printf(TEXT("floor(%s * 3.99)"), *Random(233.f)))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("max(User.End + %s * Particles.Age + float3(0.0, 0.0, -490.0) * Particles.Age * Particles.Age, float3(-1.0e7, -1.0e7, User.End.z - 96.0))"), *V))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (9.0 + 9.0 * %s) * %s * saturate(Particles.Age / 0.03)"), *Random(234.f), *Scale))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0 + Particles.Age * (%s * 2.0 - 1.0) * 900.0"), *Random(235.f), *Random(236.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.4f, TEXT("(1.0 - pow(Particles.NormalizedAge, 3.0))")));
        }
        FPart& Cling = Hit.Parts.AddDefaulted_GetRef();
        Cling.Name = TEXT("Cling");
        Cling.Kind = FPart::Sprite;
        Cling.Count = TEXT("int(lerp(2.0, 5.0, User.Focus) + 0.5)");
        Cling.Lifetime = TEXT("0.7");
        Cling.Paint = Shard;
        Cling.SubImages = FVector2D(2., 2.);
        Cling.Set(TEXT("Particles.SubImageIndex"), FString::Printf(TEXT("floor(%s * 3.99)"), *Random(241.f)))
            .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End - %s * 14.0 + float3((%s * 2.0 - 1.0) * 20.0, (%s * 2.0 - 1.0) * 20.0, (%s * 2.0 - 1.0) * 30.0) * %s"),
                *Dir, *Random(242.f), *Random(243.f), *Random(244.f), *Scale))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (10.0 + 8.0 * %s) * %s * saturate(Particles.Age / 0.05)"), *Random(245.f), *Scale))
            .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("(%s * 2.0 - 1.0) * 40.0"), *Random(246.f)))
            .Set(TEXT("Particles.Color"), Glowing(1.3f, TEXT("(1.0 - saturate((Particles.NormalizedAge - 0.6) / 0.4))")));
        FPart& Haze = Hit.Parts.AddDefaulted_GetRef();
        Haze.Name = TEXT("Mist");
        Haze.Kind = FPart::Sprite;
        Haze.Count = TEXT("int(lerp(1.0, 2.0, User.Focus) + 0.5)");
        Haze.Lifetime = TEXT("0.8");
        Haze.Paint = Mist;
        Haze.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * 12.0 + float3(0.0, 0.0, -45.0) * Particles.NormalizedAge"), *Dir))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (35.0 + 70.0 * pow(Particles.NormalizedAge, 0.6)) * %s"), *Scale))
            .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(251.f)))
            .Set(TEXT("Particles.Color"), Glowing(1.1f, TEXT("0.3 * (1.0 - Particles.NormalizedAge)")));
        return Hit;
    }

    // ------------------------------------------------------------------ element states on a body
    // A looping system that follows a body: User.Start its feet, User.End the top of its head, User.Width its radius,
    // User.Intensity its strength (stacks; others' bodies dimmer), User.Focus 1 for the local player's own body or fight.
    // Every particle lives the whole time and cycles: its phase frac(Age * rate + seed) restarts it, so the effect needs no
    // spawning while it lasts and follows the body as the runtime moves Start and End. Each element its own behaviour:
    // fire licks up and sheds embers, water drips and pools, shock crawls in short arcs, chill frosts the legs and sinks
    // cold mist, rot bubbles and gives off a sour murk.
    const TCHAR* const BodyUp = TEXT("(User.End - User.Start)");
    // A point on the body: height H (0 feet .. 1 head), round its axis at angle A (radians), at R of its radius.
    FString OnBody(const FString& H, const FString& A, const FString& R)
    {
        return FString::Printf(TEXT("(User.Start + %s * (%s) + float3(cos(%s), sin(%s), 0.0) * User.Width * (%s))"), BodyUp, *H, *A, *A, *R);
    }
    // A cycling phase in [0, 1) for a particle, Rate cycles per second, offset by its own random.
    FString Cycle(float Rate, float Salt) { return FString::Printf(TEXT("frac(Particles.Age * %.2f + %s)"), Rate, *Random(Salt)); }
    // Strength of a state: User.Intensity / 2.2 (one stack own), shown faster at its start.
    const TCHAR* const StateOn = TEXT("(saturate(Particles.Age / 0.25) * saturate(User.Intensity / 2.2))");


    // A low soft glow of the state's element on the floor under the body: readable from the gameplay camera, kept well
    // under a hit's or a telegraph's brightness.
    void StateAura(FSystemParts& State, const TCHAR* Name, UMaterialInstanceConstant* Paint, float Gain, float Alpha)
    {
        FPart& Aura = State.Parts.AddDefaulted_GetRef();
        Aura.Name = Name;
        Aura.Lifetime = TEXT("User.Duration");
        Aura.Paint = Paint;
        Floor(Aura, TEXT("float3(1.0, 0.0, 0.0)"));
        Aura.Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 2.0)"))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * User.Width * 4.0"))
            .Set(TEXT("Particles.Color"), Glowing(Gain, FString::Printf(TEXT("%.2f * %s * (0.85 + 0.15 * sin(Particles.Age * 4.0))"), Alpha, StateOn)));
    }

    FSystemParts BurnStateParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Flame = ChainPaint(TEXT("MI_LDBurn_Flame"), Top, SliceTexture(LDSliceVfx::ETex::FireTongue), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Ember = ChainPaint(TEXT("MI_LDBurn_Ember"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Glow = ChainPaint(TEXT("MI_LDBurn_Aura"), Material, SliceTexture(LDSliceVfx::ETex::ChainAGroundGlow), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        FSystemParts State;
        State.System = LDSliceVfx::ESystem::BurnState;
        if (!Flame || !Ember || !Glow) { return State; }
        StateAura(State, TEXT("Aura"), Glow, 1.4f, .45f);
        // Tongues licking up the body's sides (more on the own body), each rising and shrinking over its 0.45 s cycle.
        FPart& Licks = State.Parts.AddDefaulted_GetRef();
        Licks.Name = TEXT("Licks");
        Licks.Count = TEXT("int(lerp(6.0, 11.0, User.Focus) + 0.5)");
        Licks.Lifetime = TEXT("User.Duration");
        Tongues(Licks, Flame);
        {
            const FString T = Cycle(2.2f, 301.f);
            const FString A = FString::Printf(TEXT("(%s * 6.2832 + floor(Particles.Age * 2.2 + %s) * 2.4)"), *Random(302.f), *Random(301.f));
            const FString H = FString::Printf(TEXT("(0.15 + 0.55 * frac(%s + floor(Particles.Age * 2.2 + %s) * 0.37) + 0.12 * %s)"), *Random(303.f), *Random(301.f), *T);
            Licks.Set(TEXT("Particles.Position"), OnBody(H, A, TEXT("0.8")))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(0.55, 1.0) * (38.0 + 30.0 * %s) * sin(%s * 3.1416) * lerp(0.7, 1.0, %s)"), *Random(304.f), *T, StateOn))
                .Set(TEXT("Particles.Color"), Glowing(1.3f, FString::Printf(TEXT("%s"), StateOn)));
        }
        FPart& Embers = State.Parts.AddDefaulted_GetRef();
        Embers.Name = TEXT("Embers");
        Embers.Kind = FPart::Sprite;
        Embers.Count = TEXT("int(lerp(3.0, 7.0, User.Focus) + 0.5)");
        Embers.Lifetime = TEXT("User.Duration");
        Embers.Paint = Ember;
        Embers.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString T = Cycle(1.1f, 311.f);
            const FString A = FString::Printf(TEXT("(%s * 6.2832)"), *Random(312.f));
            Embers.Set(TEXT("Particles.Velocity"), TEXT("float3(0.0, 0.0, 100.0)"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + float3(sin(%s * 9.0 + %s * 5.0), cos(%s * 7.0), 0.0) * 12.0 + float3(0.0, 0.0, 90.0) * %s"),
                    *OnBody(TEXT("0.55"), A, TEXT("0.6")), *T, *Random(313.f), *T, *T))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(12.0, 3.0)"))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(2.2f, FString::Printf(TEXT("%s * (1.0 - %s) * step(0.3, frac(Particles.Age * 5.0 + %s))"), StateOn, *T, *Random(314.f))));
        }
        return State;
    }

    FSystemParts WetStateParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Drop = ChainPaint(TEXT("MI_LDWet_Drop"), Top, SliceTexture(LDSliceVfx::ETex::WaterDrop), nullptr, 0.f, WaterPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Wet = ChainPaint(TEXT("MI_LDWet_Pool"), Material, SliceTexture(LDSliceVfx::ETex::WaterWet), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, WetPalette, 1.f, 0.f, Error);
        FSystemParts State;
        State.System = LDSliceVfx::ESystem::WetState;
        if (!Drop || !Wet) { return State; }
        // Drips that gather at the hem, hands and hair and fall: each hangs for the first half of its cycle, then drops.
        FPart& Drips = State.Parts.AddDefaulted_GetRef();
        Drips.Name = TEXT("Drips");
        Drips.Kind = FPart::Sprite;
        Drips.Count = TEXT("int(lerp(7.0, 14.0, User.Focus) + 0.5)");
        Drips.Lifetime = TEXT("User.Duration");
        Drips.Paint = Drop;
        Drips.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString T = Cycle(1.3f, 321.f);
            const FString Fall = FString::Printf(TEXT("max(%s - 0.45, 0.0) / 1.3"), *T);
            const FString H = FString::Printf(TEXT("(0.35 + 0.6 * %s)"), *Random(322.f));
            Drips.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("float3(0.0, 0.0, -1.0) * (20.0 + 980.0 * %s)"), *Fall))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("max(%s - float3(0.0, 0.0, 490.0) * %s * %s, User.Start + float3(0.0, 0.0, 2.0) + %s * 0.0)"),
                    *OnBody(H, FString::Printf(TEXT("(%s * 6.2832)"), *Random(323.f)), TEXT("0.85")), *Fall, *Fall, BodyUp))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0 + 1.5 * step(0.45, %s), 1.0) * 9.0 * saturate(%s / 0.3)"), *T, *T))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Tinted(2.2f, StateOn));
        }
        FPart& Pool = State.Parts.AddDefaulted_GetRef();
        Pool.Name = TEXT("Pool");
        Pool.Lifetime = TEXT("User.Duration");
        Pool.Paint = Wet;
        Floor(Pool, TEXT("float3(1.0, 0.0, 0.0)"));
        Pool.Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 1.5)"))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * User.Width * 2.6"))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 1.0, 1.0, 0.0) * %s + float4(0.0, 0.0, 0.0, 0.6 * %s)"), Bright, StateOn));
        return State;
    }

    FSystemParts ShockStateParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        const FPalette Palette{FLinearColor(1.f, .93f, .78f), FLinearColor(1.f, .68f, .26f), FLinearColor(.22f, .1f, .03f), 2.8f, 1.2f};
        UMaterialInstanceConstant* Arc = ChainPaint(TEXT("MI_LDShockState_Arc"), Top, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Flicker = ChainPaint(TEXT("MI_LDShockState_Glow"), Material, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        FSystemParts State;
        State.System = LDSliceVfx::ESystem::ShockState;
        UMaterialInstanceConstant* AuraPaint = ChainPaint(TEXT("MI_LDShockState_Aura"), Material, SliceTexture(LDSliceVfx::ETex::ChainAGroundGlow), nullptr, 0.f, Palette, 1.f, 0.f, Error);
        if (!Arc || !Flicker || !AuraPaint) { return State; }
        StateAura(State, TEXT("Aura"), AuraPaint, 1.4f, .4f);
        // A soft warm glow round the chest that ticks with the arcs (lit in about half of the 8 Hz windows), so the charge
        // reads from the gameplay camera even when no arc faces it.
        FPart& Glow = State.Parts.AddDefaulted_GetRef();
        Glow.Name = TEXT("Glow");
        Glow.Kind = FPart::Sprite;
        Glow.Lifetime = TEXT("User.Duration");
        Glow.Paint = Flicker;
        Glow.Set(TEXT("Particles.Position"), OnBody(TEXT("0.55"), TEXT("0.0"), TEXT("0.0")))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * User.Width * 3.2"))
            .Set(TEXT("Particles.Color"), Tinted(1.4f, FString::Printf(TEXT("%s * (0.25 + 0.5 * step(0.5, %s))"), StateOn,
                *FTrunk::Hash(TEXT("7.0"), TEXT("floor(Particles.Age * 8.0)"), 33.f))));
        // Two short kinked arcs crawling over the body, each lit in about a third of its 8 Hz windows (a charge that ticks).
        const int32 Points = 4;
        FPart& Arcs = State.Parts.AddDefaulted_GetRef();
        Arcs.Name = TEXT("Arcs");
        Arcs.Count = FString::Printf(TEXT("int(lerp(2.0, 3.0, User.Focus) + 0.01) * %d"), Points);
        Arcs.Lifetime = TEXT("User.Duration");
        Arcs.Paint = Arc;
        Arcs.TileCm = 30.f;
        Arcs.bRibbonIds = true;
        {
            const FString Step = TEXT("floor(Particles.Age * 8.0)");
            const FString R = TEXT("(float(Particles.LDRibbon) + 500.0)");
            const auto H = [&](const FString& Index, float Salt) { return FTrunk::Hash(Index, Step, Salt); };
            const FString K = FString::Printf(TEXT("(Particles.LDJ + %s * 5.0)"), *R);
            const FString Height = FString::Printf(TEXT("(0.2 + 0.65 * %s + (Particles.LDJ / %d.0 - 0.5) * 0.22)"), *H(R, 1.f), Points - 1);
            const FString Angle = FString::Printf(TEXT("(%s * 6.2832 + (Particles.LDJ / %d.0 - 0.5) * 1.4 + (%s - 0.5) * 0.5)"), *H(R, 2.f), Points - 1, *H(K, 3.f));
            Arcs.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), OnBody(Height, Angle, FString::Printf(TEXT("(1.05 + 0.15 * %s)"), *H(K, 4.f))))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("5.0 * step(0.45, %s)"), *H(R, 5.f)))
                .Set(TEXT("Particles.Color"), Tinted(3.2f, StateOn));
        }
        return State;
    }

    FSystemParts ChillStateParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Shard = ChainPaint(TEXT("MI_LDChillState_Shard"), Top, SliceTexture(LDSliceVfx::ETex::IceShard), nullptr, 0.f, IcePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Mist = ChainPaint(TEXT("MI_LDChillState_Mist"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, ColdMistPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Frost = ChainPaint(TEXT("MI_LDChillState_Frost"), Material, SliceTexture(LDSliceVfx::ETex::IceFrost), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, FrostPalette, 1.f, 0.f, Error);
        FSystemParts State;
        State.System = LDSliceVfx::ESystem::ChillState;
        if (!Shard || !Mist || !Frost) { return State; }
        // Small crystals grown on the shins and knees (fixed, glinting), cold mist rolling off and sinking round the feet, a
        // frosted ring of floor.
        FPart& Crystals = State.Parts.AddDefaulted_GetRef();
        Crystals.Name = TEXT("Crystals");
        Crystals.Kind = FPart::Sprite;
        Crystals.Count = TEXT("int(lerp(5.0, 9.0, User.Focus) + 0.5)");
        Crystals.Lifetime = TEXT("User.Duration");
        Crystals.Paint = Shard;
        Crystals.SubImages = FVector2D(2., 2.);
        Crystals.Set(TEXT("Particles.SubImageIndex"), FString::Printf(TEXT("floor(%s * 3.99)"), *Random(331.f)))
            .Set(TEXT("Particles.Position"), OnBody(FString::Printf(TEXT("(0.04 + 0.3 * %s)"), *Random(332.f)), FString::Printf(TEXT("(%s * 6.2832)"), *Random(333.f)), TEXT("0.9")))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (10.0 + 10.0 * %s) * %s"), *Random(334.f), StateOn))
            .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("(%s * 2.0 - 1.0) * 35.0"), *Random(335.f)))
            .Set(TEXT("Particles.Color"), Glowing(1.3f, FString::Printf(TEXT("%s * (0.75 + 0.25 * step(0.85, frac(Particles.Age * 0.7 + %s)))"), StateOn, *Random(336.f))));
        FPart& Haze = State.Parts.AddDefaulted_GetRef();
        Haze.Name = TEXT("Mist");
        Haze.Kind = FPart::Sprite;
        Haze.Count = TEXT("int(lerp(2.0, 4.0, User.Focus) + 0.5)");
        Haze.Lifetime = TEXT("User.Duration");
        Haze.Paint = Mist;
        {
            const FString T = Cycle(.6f, 341.f);
            Haze.Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + float3(0.0, 0.0, -1.0) * 30.0 * %s"),
                    *OnBody(TEXT("0.35"), FString::Printf(TEXT("(%s * 6.2832)"), *Random(342.f)), FString::Printf(TEXT("(0.6 + 1.0 * %s)"), *T)), *T))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (30.0 + 50.0 * %s)"), *T))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(343.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.f, FString::Printf(TEXT("0.22 * %s * sin(%s * 3.1416)"), StateOn, *T)));
        }
        FPart& Ring = State.Parts.AddDefaulted_GetRef();
        Ring.Name = TEXT("Frost");
        Ring.Lifetime = TEXT("User.Duration");
        Ring.Paint = Frost;
        Floor(Ring, TEXT("float3(1.0, 0.0, 0.0)"));
        Ring.Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 1.5)"))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * User.Width * 2.8"))
            .Set(TEXT("Particles.Color"), Glowing(1.1f, FString::Printf(TEXT("0.6 * %s"), StateOn)));
        return State;
    }

    const FPalette RotPalette{FLinearColor(.88f, .95f, .55f), FLinearColor(.5f, .6f, .2f), FLinearColor(.12f, .13f, .04f), 1.6f, 1.f};
    const FPalette MurkPalette{FLinearColor(.6f, .66f, .32f), FLinearColor(.34f, .38f, .16f), FLinearColor(.16f, .15f, .08f), 1.f, 1.f};

    FSystemParts RotStateParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Bubble = ChainPaint(TEXT("MI_LDRotState_Bubble"), Top, SliceTexture(LDSliceVfx::ETex::RotBubble), nullptr, 0.f, RotPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Murk = ChainPaint(TEXT("MI_LDRotState_Murk"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, MurkPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Glow = ChainPaint(TEXT("MI_LDRotState_Aura"), Material, SliceTexture(LDSliceVfx::ETex::ChainAGroundGlow), nullptr, 0.f, RotPalette, 1.f, 0.f, Error);
        FSystemParts State;
        State.System = LDSliceVfx::ESystem::RotState;
        if (!Bubble || !Murk || !Glow) { return State; }
        StateAura(State, TEXT("Aura"), Glow, 1.2f, .5f);
        // Bubbles swelling on the body and popping (each grows over its cycle, then is gone), and a sour murk hanging round
        // the waist that drifts slowly round the body.
        FPart& Bubbles = State.Parts.AddDefaulted_GetRef();
        Bubbles.Name = TEXT("Bubbles");
        Bubbles.Kind = FPart::Sprite;
        Bubbles.Count = TEXT("int(lerp(4.0, 8.0, User.Focus) + 0.5)");
        Bubbles.Lifetime = TEXT("User.Duration");
        Bubbles.Paint = Bubble;
        {
            const FString T = Cycle(.9f, 351.f);
            const FString A = FString::Printf(TEXT("(%s * 6.2832 + floor(Particles.Age * 0.9 + %s) * 2.1)"), *Random(352.f), *Random(351.f));
            const FString H = FString::Printf(TEXT("(0.2 + 0.6 * frac(%s + floor(Particles.Age * 0.9 + %s) * 0.43))"), *Random(353.f), *Random(351.f));
            Bubbles.Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + float3(0.0, 0.0, 12.0) * %s"), *OnBody(H, A, TEXT("1.0")), *T))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (8.0 + 18.0 * %s) * step(%s, 0.92)"), *T, *T))
                .Set(TEXT("Particles.Color"), Glowing(1.6f, StateOn));
        }
        FPart& Haze = State.Parts.AddDefaulted_GetRef();
        Haze.Name = TEXT("Murk");
        Haze.Kind = FPart::Sprite;
        Haze.Count = TEXT("int(lerp(2.0, 4.0, User.Focus) + 0.5)");
        Haze.Lifetime = TEXT("User.Duration");
        Haze.Paint = Murk;
        {
            const FString A = FString::Printf(TEXT("(%s * 6.2832 + Particles.Age * 0.6)"), *Random(361.f));
            Haze.Set(TEXT("Particles.Position"), OnBody(FString::Printf(TEXT("(0.3 + 0.2 * %s)"), *Random(362.f)), A, TEXT("1.2")))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (50.0 + 20.0 * sin(Particles.Age * 1.3 + %s * 6.0))"), *Random(363.f)))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0 + Particles.Age * 15.0"), *Random(364.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.2f, FString::Printf(TEXT("0.42 * %s"), StateOn)));
        }
        return State;
    }

    // ------------------------------------------------------------------ ground states (puddle, ice, fire floor, miasma)
    // A looping system per public ground state round User.Start (its floor centre) at radius User.Width, User.Intensity its
    // strength (fading out over the state's last third), User.Focus 1 when the local player laid it. Each element its own
    // surface and motion; all keep one continuous readable edge (danger information, the same for others' states) and only
    // thin their decoration when the state is another player's (§2.2's rule for the field).
    const TCHAR* const GroundOn = TEXT("(saturate(Particles.Age / 0.3) * saturate(User.Intensity / 2.2))");

    // A continuous ribbon ring at the edge.
    void EdgeRing(FPart& Part, const TCHAR* Name, UMaterialInstanceConstant* Paint, float Scale, float Width, const FString& Colour)
    {
        const int32 Points = 48;
        Part.Name = Name;
        Part.Count = FString::Printf(TEXT("%d"), Points);
        Part.Lifetime = TEXT("User.Duration");
        Part.Paint = Paint;
        Part.TileCm = 60.f;
        Part.Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), Circle(FString::Printf(TEXT("User.Width * %.3f"), Scale), Points, 3.f))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("%.1f"), Width))
            .Set(TEXT("Particles.Color"), Colour);
    }

    // A floor sheet over the whole disc with the soft round edge (material mode 2).
    void Sheet(FPart& Part, const TCHAR* Name, UMaterialInstanceConstant* Paint, float Scale, const FString& Colour)
    {
        Part.Name = Name;
        Part.Lifetime = TEXT("User.Duration");
        Part.Paint = Paint;
        Floor(Part, TEXT("float3(1.0, 0.0, 0.0)"));
        Part.Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 1.5)"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * User.Width * %.3f"), 2.f * Scale))
            .Set(TEXT("Particles.Color"), Colour);
    }

    // A random point inside the disc (radius share up to Reach), redrawn every cycle of Rate per second; Phase staggers the
    // particles (the same stagger as the part's own cycle, so a point moves only while its particle is invisible).
    FString InDisc(float Rate, float Salt, float Reach, float Phase)
    {
        const FString Cycle = FString::Printf(TEXT("floor(Particles.Age * %.2f + float(Particles.UniqueID) * %.2f)"), Rate, Phase);
        const FString A = FTrunk::Hash(Own, Cycle, Salt + 1.f), R = FTrunk::Hash(Own, Cycle, Salt + 2.f);
        return FString::Printf(TEXT("(User.Start + float3(cos(%s * 6.2832), sin(%s * 6.2832), 0.0) * User.Width * %.2f * sqrt(%s))"), *A, *A, Reach, *R);
    }

    FSystemParts PuddleParts(UMaterial& Material, FString& Error)
    {
        UMaterialInstanceConstant* Wet = ChainPaint(TEXT("MI_LDPuddle_Wet"), Material, SliceTexture(LDSliceVfx::ETex::WaterWet), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, WetPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Sheen = ChainPaint(TEXT("MI_LDPuddle_Sheen"), Material, SliceTexture(LDSliceVfx::ETex::WaterFlow), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, WaterPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDPuddle_Line"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, WaterPalette, 1.f, 0.f, Error);
        FSystemParts Ground;
        Ground.System = LDSliceVfx::ESystem::PuddleGround;
        if (!Wet || !Sheen || !Line) { return Ground; }
        // Standing water: the floor darkened as soaked, a faint cool sheen over it, a wet bright edge and rings spreading
        // from drops that land in it.
        Sheet(Ground.Parts.AddDefaulted_GetRef(), TEXT("Soak"), Wet, 1.f, FString::Printf(TEXT("float4(1.0, 1.0, 1.0, 0.0) * %s + float4(0.0, 0.0, 0.0, 0.85 * %s)"), Bright, GroundOn));
        // The standing water itself: a cool blue body over the soaked floor, with the flowing sheen on top.
        Sheet(Ground.Parts.AddDefaulted_GetRef(), TEXT("Water"), Sheen, .97f, Tinted(.8f, FString::Printf(TEXT("0.32 * %s"), GroundOn)));
        Sheet(Ground.Parts.AddDefaulted_GetRef(), TEXT("Sheen"), Sheen, .9f, Tinted(1.5f, FString::Printf(TEXT("0.3 * %s * (0.8 + 0.2 * sin(Particles.Age * 2.0))"), GroundOn)));
        EdgeRing(Ground.Parts.AddDefaulted_GetRef(), TEXT("Edge"), Line, .99f, 9.f, Tinted(1.8f, FString::Printf(TEXT("0.85 * %s"), GroundOn)));
        const int32 Points = 16;
        FPart& Rings = Ground.Parts.AddDefaulted_GetRef();
        Rings.Name = TEXT("Ripples");
        Rings.Count = FString::Printf(TEXT("int(lerp(3.0, 5.0, User.Focus) + 0.01) * %d"), Points);
        Rings.Lifetime = TEXT("User.Duration");
        Rings.Paint = Line;
        Rings.TileCm = 60.f;
        Rings.bRibbonIds = true;
        {
            const FString R = TEXT("(float(Particles.LDRibbon) + 700.0)");
            const FString T = TEXT("frac(Particles.Age * 0.7 + float(Particles.LDRibbon) * 0.37)");
            const FString Cycle = TEXT("floor(Particles.Age * 0.7 + float(Particles.LDRibbon) * 0.37)");
            const FString A = FTrunk::Hash(R, Cycle, 3.f), D = FTrunk::Hash(R, Cycle, 4.f);
            const FString Centre = FString::Printf(TEXT("(User.Start + float3(cos(%s * 6.2832), sin(%s * 6.2832), 0.0) * User.Width * 0.75 * sqrt(%s))"), *A, *A, *D);
            const FString Theta = FString::Printf(TEXT("(Particles.LDJ / %d.0 * 6.2832)"), Points - 1);
            Rings.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + float3(cos(%s), sin(%s), 0.0) * (8.0 + 55.0 * %s) + float3(0.0, 0.0, 2.5)"), *Centre, *Theta, *Theta, *T))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("4.5 * (1.0 - %s)"), *T))
                .Set(TEXT("Particles.Color"), Tinted(2.f, FString::Printf(TEXT("0.85 * (1.0 - %s) * %s"), *T, GroundOn)));
        }
        return Ground;
    }

    FSystemParts IceParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Frost = ChainPaint(TEXT("MI_LDIce_Frost"), Material, SliceTexture(LDSliceVfx::ETex::IceFrost), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, FrostPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDIce_Line"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, IcePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Shard = ChainPaint(TEXT("MI_LDIce_Shard"), Top, SliceTexture(LDSliceVfx::ETex::IceShard), nullptr, 0.f, IcePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Glint = ChainPaint(TEXT("MI_LDIce_Glint"), Top, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, IcePalette, 1.f, 0.f, Error);
        FSystemParts Ground;
        Ground.System = LDSliceVfx::ESystem::IceGround;
        if (!Frost || !Line || !Shard || !Glint) { return Ground; }
        // A frozen sheet: frost needles over a pale glaze, a crisp cold edge, small crystals standing round it and glints
        // that wink across the surface. Hard and still, where the puddle ripples.
        Sheet(Ground.Parts.AddDefaulted_GetRef(), TEXT("Glaze"), Frost, 1.f, Glowing(1.1f, FString::Printf(TEXT("0.7 * %s"), GroundOn)));
        EdgeRing(Ground.Parts.AddDefaulted_GetRef(), TEXT("Edge"), Line, .99f, 7.f, Glowing(1.8f, FString::Printf(TEXT("0.85 * %s"), GroundOn)));
        FPart& Crystals = Ground.Parts.AddDefaulted_GetRef();
        Crystals.Name = TEXT("Crystals");
        Crystals.Kind = FPart::Sprite;
        Crystals.Count = TEXT("int(lerp(7.0, 14.0, User.Focus) + 0.5)");
        Crystals.Lifetime = TEXT("User.Duration");
        Crystals.Paint = Shard;
        Crystals.SubImages = FVector2D(2., 2.);
        {
            const FString Angle = FString::Printf(TEXT("((%s + %s * 0.5) / max(float(int(lerp(7.0, 14.0, User.Focus) + 0.5)), 1.0) * 6.2832)"), Own, *Random(401.f));
            Crystals.Set(TEXT("Particles.SubImageIndex"), FString::Printf(TEXT("floor(%s * 3.99)"), *Random(402.f)))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * User.Width * (0.93 + 0.06 * %s) + float3(0.0, 0.0, 8.0)"), *Angle, *Angle, *Random(403.f)))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (14.0 + 12.0 * %s) * saturate(Particles.Age / 0.25)"), *Random(404.f)))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("(%s * 2.0 - 1.0) * 30.0"), *Random(405.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.3f, GroundOn));
        }
        FPart& Glints = Ground.Parts.AddDefaulted_GetRef();
        Glints.Name = TEXT("Glints");
        Glints.Kind = FPart::Sprite;
        Glints.Count = TEXT("int(lerp(3.0, 6.0, User.Focus) + 0.5)");
        Glints.Lifetime = TEXT("User.Duration");
        Glints.Paint = Glint;
        {
            const FString T = TEXT("frac(Particles.Age * 0.9 + float(Particles.UniqueID) * 0.29)");
            Glints.Set(TEXT("Particles.Position"), InDisc(.9f, 411.f, .85f, .29f) + TEXT(" + float3(0.0, 0.0, 3.0)"))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * 26.0 * sin(%s * 3.1416)"), *T))
                .Set(TEXT("Particles.Color"), Glowing(2.f, FString::Printf(TEXT("0.8 * %s"), GroundOn)));
        }
        return Ground;
    }

    FSystemParts FireGroundParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Char = ChainPaint(TEXT("MI_LDFireGround_Char"), Material, SliceTexture(LDSliceVfx::ETex::FireChar), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, CharPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Flame = ChainPaint(TEXT("MI_LDFireGround_Flame"), Material, SliceTexture(LDSliceVfx::ETex::FireTongue), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Ember = ChainPaint(TEXT("MI_LDFireGround_Ember"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDFireGround_Line"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, FirePalette, 1.f, 0.f, Error);
        FSystemParts Ground;
        Ground.System = LDSliceVfx::ESystem::FireGround;
        if (!Char || !Flame || !Ember || !Line) { return Ground; }
        // A burning floor: scorched ground whose cracks pulse, a fringe of low flames licking along the edge, a few tongues
        // flaring inside and embers rising. The flames sit on the edge, so the danger line burns; the inside smoulders.
        Sheet(Ground.Parts.AddDefaulted_GetRef(), TEXT("Char"), Char, 1.f,
            FString::Printf(TEXT("float4(1.0, 1.0, 1.0, 0.0) * %s * (0.6 + 0.4 * sin(Particles.Age * 3.1)) + float4(0.0, 0.0, 0.0, 0.6 * %s)"), Bright, GroundOn));
        EdgeRing(Ground.Parts.AddDefaulted_GetRef(), TEXT("Edge"), Line, .99f, 5.f, Glowing(1.4f, FString::Printf(TEXT("0.7 * %s"), GroundOn)));
        FPart& Fringe = Ground.Parts.AddDefaulted_GetRef();
        Fringe.Name = TEXT("Fringe");
        Fringe.Count = TEXT("int(lerp(12.0, 22.0, User.Focus) + 0.5)");
        Fringe.Lifetime = TEXT("User.Duration");
        Tongues(Fringe, Flame);
        {
            const FString N = TEXT("max(float(int(lerp(12.0, 22.0, User.Focus) + 0.5)), 1.0)");
            const FString Angle = FString::Printf(TEXT("((%s + %s * 0.6) / %s * 6.2832)"), Own, *Random(421.f), *N);
            const FString T = FString::Printf(TEXT("frac(Particles.Age * 1.6 + %s)"), *Random(422.f));
            Fringe.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * User.Width * 0.97 + float3(0.0, 0.0, 2.0)"), *Angle, *Angle))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(0.55, 1.0) * (30.0 + 25.0 * %s) * (0.35 + 0.65 * sin(%s * 3.1416))"), *Random(423.f), *T))
                .Set(TEXT("Particles.Color"), Glowing(1.3f, GroundOn));
        }
        FPart& Flares = Ground.Parts.AddDefaulted_GetRef();
        Flares.Name = TEXT("Flares");
        Flares.Count = TEXT("int(lerp(2.0, 5.0, User.Focus) + 0.5)");
        Flares.Lifetime = TEXT("User.Duration");
        Tongues(Flares, Flame);
        {
            const FString T = TEXT("frac(Particles.Age * 1.1 + float(Particles.UniqueID) * 0.31)");
            Flares.Set(TEXT("Particles.Position"), InDisc(1.1f, 431.f, .7f, .31f) + TEXT(" + float3(0.0, 0.0, 2.0)"))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(0.5, 1.0) * 40.0 * sin(%s * 3.1416)"), *T))
                .Set(TEXT("Particles.Color"), Glowing(1.2f, FString::Printf(TEXT("0.8 * %s"), GroundOn)));
        }
        FPart& Embers = Ground.Parts.AddDefaulted_GetRef();
        Embers.Name = TEXT("Embers");
        Embers.Kind = FPart::Sprite;
        Embers.Count = TEXT("int(lerp(4.0, 10.0, User.Focus) + 0.5)");
        Embers.Lifetime = TEXT("User.Duration");
        Embers.Paint = Ember;
        Embers.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString T = TEXT("frac(Particles.Age * 0.8 + float(Particles.UniqueID) * 0.37)");
            Embers.Set(TEXT("Particles.Velocity"), TEXT("float3(0.0, 0.0, 120.0)"))
                .Set(TEXT("Particles.Position"), InDisc(.8f, 441.f, .95f, .37f) + FString::Printf(TEXT(" + float3(sin(%s * 8.0) * 10.0, 0.0, 10.0 + 140.0 * %s)"), *T, *T))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(14.0, 3.0)"))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(2.2f, FString::Printf(TEXT("(1.0 - %s) * %s"), *T, GroundOn)));
        }
        return Ground;
    }

    FSystemParts MiasmaParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Murk = ChainPaint(TEXT("MI_LDMiasma_Floor"), Material, SliceTexture(LDSliceVfx::ETex::RotMurk), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, MurkPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Puff = ChainPaint(TEXT("MI_LDMiasma_Puff"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, MurkPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Bubble = ChainPaint(TEXT("MI_LDMiasma_Bubble"), Top, SliceTexture(LDSliceVfx::ETex::RotBubble), nullptr, 0.f, RotPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDMiasma_Line"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, RotPalette, 1.f, 0.f, Error);
        FSystemParts Ground;
        Ground.System = LDSliceVfx::ESystem::MiasmaGround;
        if (!Murk || !Puff || !Bubble || !Line) { return Ground; }
        // A sour cloud on the floor: a murky stain, low clouds that churn slowly inside it, bubbles that swell and pop, and
        // an edge of thicker haze over a thin sickly line, so the cloud has a border without a hard ring.
        Sheet(Ground.Parts.AddDefaulted_GetRef(), TEXT("Stain"), Murk, 1.f, Glowing(1.3f, FString::Printf(TEXT("0.8 * %s"), GroundOn)));
        EdgeRing(Ground.Parts.AddDefaulted_GetRef(), TEXT("Haze"), Line, 1.f, 34.f, Glowing(1.2f, FString::Printf(TEXT("0.45 * %s"), GroundOn)));
        EdgeRing(Ground.Parts.AddDefaulted_GetRef(), TEXT("Edge"), Line, .99f, 7.f, Glowing(1.8f, FString::Printf(TEXT("0.8 * %s"), GroundOn)));
        FPart& Clouds = Ground.Parts.AddDefaulted_GetRef();
        Clouds.Name = TEXT("Clouds");
        Clouds.Kind = FPart::Sprite;
        Clouds.Count = TEXT("int(lerp(4.0, 8.0, User.Focus) + 0.5)");
        Clouds.Lifetime = TEXT("User.Duration");
        Clouds.Paint = Puff;
        {
            const FString A = FString::Printf(TEXT("(%s * 6.2832 + Particles.Age * 0.25)"), *Random(451.f));
            Clouds.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * User.Width * (0.2 + 0.5 * %s) + float3(0.0, 0.0, 25.0)"), *A, *A, *Random(452.f)))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * User.Width * (0.7 + 0.2 * sin(Particles.Age * 0.9 + %s * 6.0))"), *Random(453.f)))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0 + Particles.Age * 12.0"), *Random(454.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.3f, FString::Printf(TEXT("0.5 * %s"), GroundOn)));
        }
        FPart& Bubbles = Ground.Parts.AddDefaulted_GetRef();
        Bubbles.Name = TEXT("Bubbles");
        Bubbles.Kind = FPart::Sprite;
        Bubbles.Count = TEXT("int(lerp(5.0, 12.0, User.Focus) + 0.5)");
        Bubbles.Lifetime = TEXT("User.Duration");
        Bubbles.Paint = Bubble;
        {
            const FString T = TEXT("frac(Particles.Age * 0.7 + float(Particles.UniqueID) * 0.23)");
            Bubbles.Set(TEXT("Particles.Position"), InDisc(.7f, 461.f, .85f, .23f) + TEXT(" + float3(0.0, 0.0, 4.0)"))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (12.0 + 30.0 * %s) * step(%s, 0.9)"), *T, *T))
                .Set(TEXT("Particles.Color"), Glowing(1.8f, GroundOn));
        }
        return Ground;
    }

    // ------------------------------------------------------------------ a visible body's fall (a kill)
    // The fall of a body (User.Start the floor under it, User.End its chest): a single bright jolt at the chest, a thin
    // warm-white shock ring that rolls out over the floor and thins, dust kicked up round the feet and settling, and a soft
    // dark scuff left where it fell that fades over two seconds. Weight comes from the ring and the dust, not from a flash
    // or a star; it reads the same for any body.
    const FPalette DustPalette{FLinearColor(.7f, .66f, .6f), FLinearColor(.48f, .44f, .39f), FLinearColor(.22f, .2f, .18f), 1.f, 1.f};
    const FPalette ScuffPalette{FLinearColor(.2f, .18f, .16f), FLinearColor(.1f, .09f, .08f), FLinearColor(0.f, 0.f, 0.f), 1.f, 1.f};

    FSystemParts KnockoutParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDKnockout_Ring"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Dust = ChainPaint(TEXT("MI_LDKnockout_Dust"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, DustPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Scuff = ChainPaint(TEXT("MI_LDKnockout_Scuff"), Material, SliceTexture(LDSliceVfx::ETex::WaterWet), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, ScuffPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Flash = ChainPaint(TEXT("MI_LDKnockout_Flash"), Top, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        FSystemParts Fall;
        Fall.System = LDSliceVfx::ESystem::Knockout;
        if (!Line || !Dust || !Scuff || !Flash) { return Fall; }
        const int32 Points = 40;
        FPart& Ring = Fall.Parts.AddDefaulted_GetRef();
        Ring.Name = TEXT("Ring");
        Ring.Count = FString::Printf(TEXT("%d"), Points);
        Ring.Lifetime = TEXT("0.5");
        Ring.Paint = Line;
        Ring.TileCm = 60.f;
        {
            const FString Out = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.42), 2.5))");
            Ring.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), Circle(FString::Printf(TEXT("(25.0 + 150.0 * %s)"), *Out), Points, 3.f))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("12.0 * (1.0 - 0.8 * %s)"), *Out))
                .Set(TEXT("Particles.Color"), Tinted(2.2f, FString::Printf(TEXT("(1.0 - %s)"), *Out)));
        }
        FPart& Kick = Fall.Parts.AddDefaulted_GetRef();
        Kick.Name = TEXT("Dust");
        Kick.Kind = FPart::Sprite;
        Kick.Count = TEXT("int(lerp(5.0, 9.0, User.Focus) + 0.5)");
        Kick.Lifetime = FString::Printf(TEXT("1.0 + 0.4 * %s"), *Random(501.f));
        Kick.Paint = Dust;
        {
            const FString Angle = FString::Printf(TEXT("(%s * 6.2832)"), *Random(502.f));
            const FString Out = TEXT("(1.0 - exp(-4.0 * Particles.Age))");
            Kick.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * (30.0 + 110.0 * %s * (0.7 + 0.3 * %s)) + float3(0.0, 0.0, 15.0 + 25.0 * %s)"),
                    *Angle, *Angle, *Out, *Random(503.f), *Out))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * (40.0 + 70.0 * sqrt(Particles.NormalizedAge))"))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0 + Particles.Age * 30.0"), *Random(504.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.f, TEXT("0.42 * saturate(Particles.Age / 0.05) * (1.0 - Particles.NormalizedAge)")));
        }
        FPart& Mark = Fall.Parts.AddDefaulted_GetRef();
        Mark.Name = TEXT("Scuff");
        Mark.Lifetime = TEXT("User.Duration");
        Mark.Paint = Scuff;
        Floor(Mark, TEXT("float3(1.0, 0.0, 0.0)"));
        Mark.Set(TEXT("Particles.Position"), TEXT("User.Start + float3(0.0, 0.0, 1.5)"))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * 170.0 * (0.7 + 0.3 * saturate(Particles.Age / 0.2))"))
            .Set(TEXT("Particles.Color"), Glowing(1.f, TEXT("0.55 * saturate(Particles.Age / 0.08) * (1.0 - saturate((Particles.Age - 0.6) / max(User.Duration - 0.65, 0.1)))")));
        FPart& Jolt = Fall.Parts.AddDefaulted_GetRef();
        Jolt.Name = TEXT("Jolt");
        Jolt.Kind = FPart::Sprite;
        Jolt.Lifetime = TEXT("0.1");
        Jolt.Paint = Flash;
        Jolt.Set(TEXT("Particles.Position"), TEXT("User.End"))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * (50.0 + 40.0 * Particles.NormalizedAge)"))
            .Set(TEXT("Particles.Color"), Tinted(2.4f, TEXT("(1.0 - Particles.NormalizedAge)")));
        return Fall;
    }

    // ------------------------------------------------------------------ skill windups without a design of their own
    // Two families by the skill's target, both reading round the body without covering it: low opacity at the centre,
    // the anticipation at the hand or the feet and pointing where the skill goes. User.Duration is the windup; the
    // effect peaks as it ends and is gone at the release.
    //   CastAim    a directed skill (unit, line, flight, dash, ground point, cone): User.Start the casting hand, User.End the
    //              floor a step ahead of the feet along the facing. A small soft gather at the hand with motes drawn into
    //              it, and a thin line on the floor that grows out from the feet towards the facing.
    //   CastBrace  a self-centred skill: User.Start the floor under the feet. A thin ring at the feet that draws inwards
    //              from about 95 to 50 cm, a few motes rising off it; nothing inside it.
    FSystemParts CastAimParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Glow = ChainPaint(TEXT("MI_LDCastAim_Glow"), Material, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Mote = ChainPaint(TEXT("MI_LDCastAim_Mote"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDCastAim_Line"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        FSystemParts Cast;
        Cast.System = LDSliceVfx::ESystem::CastAim;
        if (!Glow || !Mote || !Line) { return Cast; }
        const FString P = TEXT("saturate(Particles.Age / max(User.Duration, 0.05))");
        FPart& Gather = Cast.Parts.AddDefaulted_GetRef();
        Gather.Name = TEXT("Gather");
        Gather.Kind = FPart::Sprite;
        Gather.Lifetime = TEXT("User.Duration + 0.02");
        Gather.Paint = Glow;
        Gather.Set(TEXT("Particles.Position"), TEXT("User.Start"))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (12.0 + 18.0 * %s)"), *P))
            .Set(TEXT("Particles.Color"), Tinted(1.6f, FString::Printf(TEXT("0.2 + 0.45 * %s"), *P)));
        FPart& Motes = Cast.Parts.AddDefaulted_GetRef();
        Motes.Name = TEXT("Motes");
        Motes.Kind = FPart::Sprite;
        Motes.Count = TEXT("int(lerp(3.0, 6.0, User.Focus) + 0.5)");
        Motes.Lifetime = TEXT("User.Duration + 0.02");
        Motes.Paint = Mote;
        Motes.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString Born = FString::Printf(TEXT("(%s * max(User.Duration - 0.1, 0.0))"), *Random(601.f));
            const FString T = FString::Printf(TEXT("saturate((Particles.Age - %s) / 0.1)"), *Born);
            const FString From = FString::Printf(TEXT("normalize(float3(%s, %s, %s) * 2.0 - 1.0 + float3(0.0, 0.0, 0.001))"), *Random(602.f), *Random(603.f), *Random(604.f));
            Motes.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * 38.0 * (1.0 - %s)"), *From, *T))
                .Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("-%s * 300.0"), *From))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(14.0, 3.5) * step(%s, Particles.Age) * step(%s, 0.99)"), *Born, *T))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Tinted(2.f, TEXT("0.8")));
        }
        FPart& Aim = Cast.Parts.AddDefaulted_GetRef();
        Aim.Name = TEXT("Aim");
        Aim.Count = TEXT("12");
        Aim.Lifetime = TEXT("User.Duration + 0.02");
        Aim.Paint = Line;
        Aim.TileCm = 60.f;
        {
            const FString Dir = TEXT("normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0))");
            const FString Along = FString::Printf(TEXT("(%s / 11.0)"), Own);
            const FString Reach = FString::Printf(TEXT("(40.0 + 150.0 * (1.0 - pow(1.0 - %s, 2.0)))"), *P);
            Aim.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * (%s * %s - 40.0) + float3(0.0, 0.0, 2.5)"), *Dir, *Along, *Reach))
                // Thin at the feet, widest just behind the head, so it reads as a direction and not a lane.
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(2.0 + 5.0 * sin(%s * 2.6))"), *Along))
                .Set(TEXT("Particles.Color"), Tinted(1.5f, FString::Printf(TEXT("(0.25 + 0.55 * %s) * (0.4 + 0.6 * %s)"), *P, *Along)));
        }
        return Cast;
    }

    FSystemParts CastBraceParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDCastBrace_Ring"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Mote = ChainPaint(TEXT("MI_LDCastBrace_Mote"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        FSystemParts Cast;
        Cast.System = LDSliceVfx::ESystem::CastBrace;
        if (!Line || !Mote) { return Cast; }
        const FString P = TEXT("saturate(Particles.Age / max(User.Duration, 0.05))");
        const int32 Points = 36;
        FPart& Ring = Cast.Parts.AddDefaulted_GetRef();
        Ring.Name = TEXT("Ring");
        Ring.Count = FString::Printf(TEXT("%d"), Points);
        Ring.Lifetime = TEXT("User.Duration + 0.02");
        Ring.Paint = Line;
        Ring.TileCm = 60.f;
        Ring.Set(TEXT("Particles.RibbonLinkOrder"), Own)
            .Set(TEXT("Particles.Position"), Circle(FString::Printf(TEXT("(95.0 - 45.0 * pow(%s, 1.5))"), *P), Points, 3.f))
            .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(3.0 + 4.0 * %s)"), *P))
            .Set(TEXT("Particles.Color"), Tinted(1.6f, FString::Printf(TEXT("(0.3 + 0.5 * %s) * saturate(Particles.Age / 0.03)"), *P)));
        FPart& Rise = Cast.Parts.AddDefaulted_GetRef();
        Rise.Name = TEXT("Rise");
        Rise.Kind = FPart::Sprite;
        Rise.Count = TEXT("int(lerp(4.0, 8.0, User.Focus) + 0.5)");
        Rise.Lifetime = TEXT("User.Duration + 0.02");
        Rise.Paint = Mote;
        Rise.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString Angle = FString::Printf(TEXT("(%s * 6.2832)"), *Random(611.f));
            Rise.Set(TEXT("Particles.Velocity"), TEXT("float3(0.0, 0.0, 200.0)"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + float3(cos(%s), sin(%s), 0.0) * (95.0 - 45.0 * pow(%s, 1.5)) + float3(0.0, 0.0, 6.0 + 60.0 * %s * %s)"),
                    *Angle, *Angle, *P, *P, *Random(612.f)))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(12.0, 3.0)"))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Tinted(1.8f, FString::Printf(TEXT("0.7 * %s"), *P)));
        }
        return Cast;
    }

    // A heavy physical strike's shared values: a warm-white impact core over amber, dark shadowed dust with a lighter rim,
    // and a dark scuff for the ground's response. The skills differ by shape and motion; these give them mass.
    const FPalette ImpactPalette{FLinearColor(1.f, .96f, .88f), FLinearColor(1.f, .74f, .42f), FLinearColor(.45f, .22f, .08f), 2.4f, 1.3f};
    const FPalette HeavyDustPalette{FLinearColor(.62f, .58f, .52f), FLinearColor(.34f, .31f, .28f), FLinearColor(.12f, .11f, .1f), 1.f, 1.f};

    // The impact punch of a physical hit: a warm core at the contact for three frames (0-100 ms, inside the sync table's
    // 0-180 ms victim flash) of Size cm, and its light.
    void ImpactCore(FSystemParts& Hit, UMaterialInstanceConstant* Flash, float Size, float LightRadius, const FString& At)
    {
        FPart& Core = Hit.Parts.AddDefaulted_GetRef();
        Core.Name = TEXT("Core");
        Core.Kind = FPart::Sprite;
        Core.Lifetime = TEXT("0.1");
        Core.Paint = Flash;
        Core.Set(TEXT("Particles.Position"), At)
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * %.1f * (1.0 - 0.3 * Particles.NormalizedAge)"), Size))
            .Set(TEXT("Particles.Color"), Glowing(2.6f, TEXT("(1.0 - pow(Particles.NormalizedAge, 2.0))")));
        FPart& Light = Hit.Parts.AddDefaulted_GetRef();
        Light.Name = TEXT("Light");
        Light.Kind = FPart::Light;
        Light.Lifetime = TEXT("0.14");
        Light.Set(TEXT("Particles.Position"), At)
            .Set(TEXT("Particles.LightRadius"), FString::Printf(TEXT("%.1f"), LightRadius))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 0.75, 0.45, 1.0) * float4(6.0, 6.0, 6.0, 1.0) * %s * (1.0 - Particles.NormalizedAge)"), Bright));
    }

    // ------------------------------------------------------------------ active_elbow (a close shove that knocks back 300 cm)
    // Physical, no element: weight comes from shape and motion in the strike's ink, not colour. Release: a curved pressure
    // sheet shoved out in front of the body with two thinner echoes behind it and dust kicked from the planted foot. Hit: a
    // compressed shock ring at the chest facing the push, a dust plume dragged along the knock-back path as the body slides
    // and a few chips flung the same way.
    FSystemParts ElbowReleaseParts(UMaterial& Material, FString& Error)
    {
        UMaterialInstanceConstant* Sheet = ChainPaint(TEXT("MI_LDElbow_Wave"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 1.f, NeutralPalette, 1.f, 3.f, Error);
        UMaterialInstanceConstant* Dust = ChainPaint(TEXT("MI_LDElbow_Dust"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, HeavyDustPalette, 1.f, 0.f, Error);
        FSystemParts Release;
        Release.System = LDSliceVfx::ESystem::ElbowRelease;
        if (!Sheet || !Dust) { return Release; }
        const FString Fwd = TEXT("normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0))");
        const FString Side = FString::Printf(TEXT("normalize(cross(%s, float3(0.0, 0.0, 1.0)))"), *Fwd);
        const int32 Points = 14;
        FPart& Waves = Release.Parts.AddDefaulted_GetRef();
        Waves.Name = TEXT("Waves");
        Waves.Count = FString::Printf(TEXT("int(lerp(2.0, 3.0, User.Focus) + 0.01) * %d"), Points);
        Waves.Lifetime = TEXT("0.24");
        Waves.Paint = Sheet;
        Waves.TileCm = 50.f;
        Waves.bRibbonIds = true;
        {
            const FString R = TEXT("float(Particles.LDRibbon)");
            const FString Lag = FString::Printf(TEXT("(%s * 0.03)"), *R);
            const FString T = FString::Printf(TEXT("saturate((Particles.Age - %s) / 0.13)"), *Lag);
            const FString Out = FString::Printf(TEXT("(1.0 - pow(1.0 - %s, 2.0))"), *T);
            const FString A = FString::Printf(TEXT("((Particles.LDJ / %d.0 - 0.5) * 2.0)"), Points - 1);
            // A shallow arc bowed forward at its middle, wider as it travels.
            Waves.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * ((30.0 + 130.0 * %s) * (1.0 - 0.25 * %s * %s) - %s * 18.0) + %s * %s * (35.0 + 45.0 * %s) + float3(0.0, 0.0, -10.0 * %s)"),
                    *Fwd, *Out, *A, *A, *R, *Side, *A, *Out, *R))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(40.0 - 24.0 * %s) * (1.0 - 0.45 * %s) * step(%s, Particles.Age) * (1.0 - abs(%s) * 0.6)"), *Out, *R, *Lag, *A))
                .Set(TEXT("Particles.Color"), Tinted(2.2f, FString::Printf(TEXT("(1.0 - %s) * (1.0 - 0.35 * %s)"), *T, *R)));
        }
        FPart& Kick = Release.Parts.AddDefaulted_GetRef();
        Kick.Name = TEXT("Kick");
        Kick.Kind = FPart::Sprite;
        Kick.Count = TEXT("int(lerp(4.0, 7.0, User.Focus) + 0.5)");
        Kick.Lifetime = TEXT("0.9");
        Kick.Paint = Dust;
        // Dust shoved out ahead of the body by the wave (and a little kicked back from the planted foot).
        Kick.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * (20.0 + 150.0 * (1.0 - exp(-5.0 * Particles.Age)) * (0.5 + 0.5 * %s)) + %s * (%s * 2.0 - 1.0) * 55.0 + float3(0.0, 0.0, -80.0 + 35.0 * Particles.NormalizedAge)"),
                *Fwd, *Random(703.f), *Side, *Random(701.f)))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * (60.0 + 100.0 * sqrt(Particles.NormalizedAge))"))
            .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(702.f)))
            .Set(TEXT("Particles.Color"), Glowing(.9f, TEXT("0.8 * saturate(Particles.Age / 0.04) * (1.0 - Particles.NormalizedAge)")));
        return Release;
    }

    FSystemParts ElbowHitParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Ring = ChainPaint(TEXT("MI_LDElbowHit_Ring"), Top, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Dust = ChainPaint(TEXT("MI_LDElbowHit_Dust"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, HeavyDustPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Chip = ChainPaint(TEXT("MI_LDElbowHit_Chip"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Flash = ChainPaint(TEXT("MI_LDElbowHit_Flash"), Top, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Scuff = ChainPaint(TEXT("MI_LDElbowHit_Skid"), Material, SliceTexture(LDSliceVfx::ETex::WaterWet), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, ScuffPalette, 1.f, 0.f, Error);
        FSystemParts Hit;
        Hit.System = LDSliceVfx::ESystem::ElbowHit;
        if (!Ring || !Dust || !Chip || !Flash || !Scuff) { return Hit; }
        ImpactCore(Hit, Flash, 130.f, 320.f, TEXT("User.End - normalize(User.End - User.Start) * 15.0"));
        // The slide's skid: dark marks along the knock-back path where the feet drag.
        FPart& Skid = Hit.Parts.AddDefaulted_GetRef();
        Skid.Name = TEXT("Skid");
        Skid.Count = TEXT("3");
        Skid.Lifetime = TEXT("User.Duration");
        Skid.Paint = Scuff;
        Floor(Skid, TEXT("normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0))"));
        Skid.Set(TEXT("Particles.Position"), TEXT("float3(User.End.x, User.End.y, User.End.z - 93.0) + normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0)) * (float(Particles.UniqueID) + 0.5) * 95.0"))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(120.0, 60.0) * step((float(Particles.UniqueID) + 0.5) / 3.0 * 0.25, Particles.Age)"))
            .Set(TEXT("Particles.SpriteRotation"), Quarter)
            .Set(TEXT("Particles.Color"), Glowing(1.f, TEXT("0.8 * (1.0 - saturate((Particles.Age - 0.5) / max(User.Duration - 0.55, 0.1)))")));
        const FString Dir = TEXT("normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0))");
        const FString Side = FString::Printf(TEXT("normalize(cross(%s, float3(0.0, 0.0, 1.0)))"), *Dir);
        const FString Scale = FString::Printf(TEXT("(%s * lerp(0.8, 1.0, User.Focus))"), LinkScale);
        // The shock ring stands in the plane across the push, opening from 20 to 95 cm in 0.12 s.
        const int32 Points = 24;
        FPart& Shock = Hit.Parts.AddDefaulted_GetRef();
        Shock.Name = TEXT("Shock");
        Shock.Count = FString::Printf(TEXT("%d"), Points);
        Shock.Lifetime = TEXT("0.16");
        Shock.Paint = Ring;
        Shock.TileCm = 50.f;
        {
            const FString Theta = FString::Printf(TEXT("(%s / %d.0 * 6.2832)"), Own, Points - 1);
            const FString Open = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.12), 2.0))");
            Shock.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * (8.0 + 25.0 * %s) + (%s * cos(%s) + float3(0.0, 0.0, 1.0) * sin(%s) * 0.8) * (20.0 + 75.0 * %s) * %s"),
                    *Dir, *Open, *Side, *Theta, *Theta, *Open, *Scale))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(30.0 - 22.0 * %s) * %s"), *Open, *Scale))
                .Set(TEXT("Particles.Color"), Glowing(2.f, FString::Printf(TEXT("(1.0 - %s)"), *Open)));
        }
        FPart& Plume = Hit.Parts.AddDefaulted_GetRef();
        Plume.Name = TEXT("Plume");
        Plume.Kind = FPart::Sprite;
        Plume.Count = TEXT("int(lerp(5.0, 9.0, User.Focus) + 0.5)");
        Plume.Lifetime = TEXT("1.1");
        Plume.Paint = Dust;
        {
            // Each puff is left where the sliding body was at its moment (300 cm over 0.25 s), then spreads and settles.
            const FString K = FString::Printf(TEXT("((%s + 0.5) / max(float(int(lerp(5.0, 9.0, User.Focus) + 0.5)), 1.0))"), Own);
            const FString Born = FString::Printf(TEXT("(%s * 0.25)"), *K);
            const FString Tau = FString::Printf(TEXT("max(Particles.Age - %s, 0.0)"), *Born);
            Plume.Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.End.x, User.End.y, User.End.z - 85.0) + %s * 280.0 * %s + %s * (%s * 2.0 - 1.0) * 20.0 + float3(0.0, 0.0, 20.0 * sqrt(%s))"),
                    *Dir, *K, *Side, *Random(711.f), *Tau))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (60.0 + 100.0 * sqrt(%s)) * step(%s, Particles.Age)"), *Tau, *Born))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(712.f)))
                .Set(TEXT("Particles.Color"), Glowing(.9f, FString::Printf(TEXT("0.85 * saturate(%s / 0.05) * (1.0 - saturate(%s / 0.85))"), *Tau, *Tau)));
        }
        FPart& Chips = Hit.Parts.AddDefaulted_GetRef();
        Chips.Name = TEXT("Chips");
        Chips.Kind = FPart::Sprite;
        Chips.Count = TEXT("int(lerp(4.0, 8.0, User.Focus) + 0.5)");
        Chips.Lifetime = FString::Printf(TEXT("0.3 + 0.2 * %s"), *Random(721.f));
        Chips.Paint = Chip;
        Chips.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString V = FString::Printf(TEXT("(normalize(%s * 1.6 + %s * (%s * 2.0 - 1.0) + float3(0.0, 0.0, 0.2 + 0.6 * %s)) * (380.0 + 300.0 * %s))"),
                *Dir, *Side, *Random(722.f), *Random(723.f), *Random(724.f));
            Chips.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s + float3(0.0, 0.0, -900.0) * Particles.Age"), *V))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * Particles.Age + float3(0.0, 0.0, -450.0) * Particles.Age * Particles.Age"), *V))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(30.0, 7.0) * %s"), *Scale))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(2.f, TEXT("(1.0 - Particles.NormalizedAge)")));
        }
        return Hit;
    }

    // ------------------------------------------------------------------ the other physical contact skills
    // Each its own motion in the strike's ink: pants_down lashes a cord out and yanks it back, crotch_grab skims low along
    // the floor, bail_out slips through with charged fists, charged_ram drives a wedge down its path.
    const TCHAR* const ShotFwd = TEXT("normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0))");
    const TCHAR* const ShotSide = TEXT("normalize(cross(normalize(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0) + float3(0.0001, 0.0, 0.0)), float3(0.0, 0.0, 1.0)))");
    const TCHAR* const ShotLength = TEXT("length(float3((User.End - User.Start).x, (User.End - User.Start).y, 0.0))");

    // Dust left along a path as the body passes (S0..S1 of the way between Start and End over Pass seconds).
    void Wake(FPart& Part, UMaterialInstanceConstant* Dust, const TCHAR* Count, float Pass, float Salt)
    {
        Part.Name = TEXT("Wake");
        Part.Kind = FPart::Sprite;
        Part.Count = Count;
        Part.Lifetime = TEXT("1.0");
        Part.Paint = Dust;
        const FString K = FString::Printf(TEXT("((float(Particles.UniqueID) + 0.5) / max(%s, 1.0))"), Count);
        const FString Born = FString::Printf(TEXT("(%s * %.3f)"), *K, Pass);
        const FString Tau = FString::Printf(TEXT("max(Particles.Age - %s, 0.0)"), *Born);
        Part.Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.Start.x, User.Start.y, User.Start.z - 85.0) + %s * %s * %s + %s * (%s * 2.0 - 1.0) * 22.0 + float3(0.0, 0.0, 18.0 * sqrt(%s))"),
                ShotFwd, ShotLength, *K, ShotSide, *Random(Salt), *Tau))
            .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (50.0 + 90.0 * sqrt(%s)) * step(%s, Particles.Age)"), *Tau, *Born))
            .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(Salt + 1.f)))
            .Set(TEXT("Particles.Color"), Glowing(.9f, FString::Printf(TEXT("0.75 * saturate(%s / 0.05) * (1.0 - saturate(%s / 0.85))"), *Tau, *Tau)));
    }

    FSystemParts PantsReleaseParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Cord = ChainPaint(TEXT("MI_LDPants_Cord"), Top, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Shade = ChainPaint(TEXT("MI_LDPants_Shade"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, HeavyDustPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Flash = ChainPaint(TEXT("MI_LDPants_Snap"), Top, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        FSystemParts Release;
        Release.System = LDSliceVfx::ESystem::PantsRelease;
        if (!Cord || !Shade || !Flash) { return Release; }
        // The cord's tip flies out to the reach in 0.08 s with a whip running down it, snaps, then is yanked back to the hand
        // by 0.32 s.
        const int32 Points = 18;
        const FString Out = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.08), 2.0))");
        const FString Back = TEXT("pow(saturate((Particles.Age - 0.12) / 0.2), 1.6)");
        const FString Reach = FString::Printf(TEXT("(%s * (%s - %s))"), ShotLength, *Out, *Back);
        const FString T = FString::Printf(TEXT("(%s / %d.0)"), Own, Points - 1);
        // The cord over its own dark shadow on the floor (so it reads on the bright floor and has weight).
        const auto Cordage = [&](FPart& Lash, const TCHAR* Name, UMaterialInstanceConstant* Paint, float Drop, float Width, const FString& Colour)
        {
            Lash.Name = Name;
            Lash.Count = FString::Printf(TEXT("%d"), Points);
            Lash.Lifetime = TEXT("0.34");
            Lash.Paint = Paint;
            Lash.TileCm = 50.f;
            Lash.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * %s * %s + %s * sin(%s * 9.0 - Particles.Age * 50.0) * 18.0 * (1.0 - %s) * %s + float3(0.0, 0.0, -30.0 * %s - %.1f)"),
                    ShotFwd, *Reach, *T, ShotSide, *T, *Out, *T, *T, Drop))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(%.1f + %.1f * %s) * step(0.01, %s)"), Width, Width * .6f, *T, *Reach))
                .Set(TEXT("Particles.Color"), Colour);
        };
        Cordage(Release.Parts.AddDefaulted_GetRef(), TEXT("Shadow"), Shade, 55.f, 20.f, Glowing(.8f, TEXT("0.75")));
        Cordage(Release.Parts.AddDefaulted_GetRef(), TEXT("Lash"), Cord, 0.f, 10.f, Glowing(2.f, TEXT("1.0")));
        FPart& Snap = Release.Parts.AddDefaulted_GetRef();
        Snap.Name = TEXT("Snap");
        Snap.Kind = FPart::Sprite;
        Snap.Lifetime = TEXT("0.2");
        Snap.Paint = Flash;
        Snap.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * %s + float3(0.0, 0.0, -30.0)"), ShotFwd, ShotLength))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * 90.0 * step(0.07, Particles.Age)"))
            .Set(TEXT("Particles.Color"), Glowing(2.6f, TEXT("saturate(1.0 - (Particles.Age - 0.07) / 0.09)")));
        return Release;
    }

    FSystemParts PantsHitParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Cord = ChainPaint(TEXT("MI_LDPantsHit_Cord"), Top, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Streak = ChainPaint(TEXT("MI_LDPantsHit_Tug"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Flash = ChainPaint(TEXT("MI_LDPantsHit_Flash"), Top, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Dust = ChainPaint(TEXT("MI_LDPantsHit_Dust"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, HeavyDustPalette, 1.f, 0.f, Error);
        FSystemParts Hit;
        Hit.System = LDSliceVfx::ESystem::PantsHit;
        if (!Cord || !Streak || !Flash || !Dust) { return Hit; }
        ImpactCore(Hit, Flash, 90.f, 240.f, TEXT("float3(User.End.x, User.End.y, User.End.z - 60.0)"));
        // The yank stirs dust round the feet.
        FPart& Stir = Hit.Parts.AddDefaulted_GetRef();
        Stir.Name = TEXT("Stir");
        Stir.Kind = FPart::Sprite;
        Stir.Count = TEXT("int(lerp(3.0, 5.0, User.Focus) + 0.5)");
        Stir.Lifetime = TEXT("0.8");
        Stir.Paint = Dust;
        {
            const FString A = FString::Printf(TEXT("(%s * 6.2832)"), *Random(735.f));
            Stir.Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.End.x, User.End.y, User.End.z - 80.0) + float3(cos(%s), sin(%s), 0.0) * (25.0 + 60.0 * (1.0 - exp(-4.0 * Particles.Age))) + float3(0.0, 0.0, 15.0 * Particles.NormalizedAge)"), *A, *A))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * (50.0 + 70.0 * sqrt(Particles.NormalizedAge))"))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(736.f)))
                .Set(TEXT("Particles.Color"), Glowing(.9f, TEXT("0.75 * saturate(Particles.Age / 0.04) * (1.0 - Particles.NormalizedAge)")));
        }
        // Two bands cinch round the ankles (the root takes hold), and tug streaks pull from the body back towards the caster.
        const int32 Points = 20;
        FPart& Cinch = Hit.Parts.AddDefaulted_GetRef();
        Cinch.Name = TEXT("Cinch");
        Cinch.Count = FString::Printf(TEXT("%d"), Points * 2);
        Cinch.Lifetime = TEXT("0.6");
        Cinch.Paint = Cord;
        Cinch.TileCm = 40.f;
        Cinch.bRibbonIds = true;
        {
            const FString Theta = FString::Printf(TEXT("(Particles.LDJ / %d.0 * 6.2832)"), Points - 1);
            const FString Close = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.12), 2.0))");
            Cinch.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.End.x, User.End.y, User.End.z - 78.0 + float(Particles.LDRibbon) * 14.0) + float3(cos(%s), sin(%s), 0.0) * (60.0 - 30.0 * %s)"),
                    *Theta, *Theta, *Close))
                .Set(TEXT("Particles.RibbonWidth"), TEXT("12.0"))
                .Set(TEXT("Particles.Color"), Glowing(2.f, TEXT("(1.0 - saturate((Particles.Age - 0.35) / 0.25))")));
        }
        FPart& Tug = Hit.Parts.AddDefaulted_GetRef();
        Tug.Name = TEXT("Tug");
        Tug.Kind = FPart::Sprite;
        Tug.Count = TEXT("int(lerp(3.0, 6.0, User.Focus) + 0.5)");
        Tug.Lifetime = TEXT("0.25");
        Tug.Paint = Streak;
        Tug.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString Back = FString::Printf(TEXT("(-%s)"), ShotFwd);
            Tug.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s * 500.0"), *Back))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.End + %s * 300.0 * Particles.Age + %s * (%s * 2.0 - 1.0) * 30.0 + float3(0.0, 0.0, (%s * 2.0 - 1.0) * 30.0)"),
                    *Back, ShotSide, *Random(731.f), *Random(732.f)))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(60.0, 9.0)"))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(2.f, TEXT("(1.0 - Particles.NormalizedAge)")));
        }
        return Hit;
    }

    FSystemParts GrabReleaseParts(UMaterial& Material, FString& Error)
    {
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDGrab_Line"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Dust = ChainPaint(TEXT("MI_LDGrab_Dust"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, HeavyDustPalette, 1.f, 0.f, Error);
        FSystemParts Release;
        Release.System = LDSliceVfx::ESystem::GrabRelease;
        if (!Line || !Dust) { return Release; }
        // Three low speed lines skim the floor along the 0.16 s dash as the body passes, and it brakes in a skid of dust.
        const int32 Points = 10;
        FPart& Lines = Release.Parts.AddDefaulted_GetRef();
        Lines.Name = TEXT("Skim");
        Lines.Count = FString::Printf(TEXT("%d"), Points * 3);
        Lines.Lifetime = TEXT("0.45");
        Lines.Paint = Line;
        Lines.TileCm = 60.f;
        Lines.bRibbonIds = true;
        {
            const FString R = TEXT("(float(Particles.LDRibbon) - 1.0)");
            const FString T = FString::Printf(TEXT("(Particles.LDJ / %d.0)"), Points - 1);
            const FString Head = TEXT("saturate(Particles.Age / 0.16)");
            Lines.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.Start.x, User.Start.y, User.Start.z - 80.0 + abs(%s) * 12.0) + %s * %s * %s * %s + %s * %s * 26.0"),
                    *R, ShotFwd, ShotLength, *Head, *T, ShotSide, *R))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(5.0 + 9.0 * %s) * (1.0 - saturate((Particles.Age - 0.18) / 0.25))"), *T))
                .Set(TEXT("Particles.Color"), Tinted(2.f, FString::Printf(TEXT("(0.3 + 0.7 * %s)"), *T)));
        }
        Wake(Release.Parts.AddDefaulted_GetRef(), Dust, TEXT("5.0"), .16f, 741.f);
        return Release;
    }

    FSystemParts GrabHitParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Ring = ChainPaint(TEXT("MI_LDGrabHit_Ring"), Top, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Spark = ChainPaint(TEXT("MI_LDGrabHit_Spark"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Flash = ChainPaint(TEXT("MI_LDGrabHit_Flash"), Top, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Dust = ChainPaint(TEXT("MI_LDGrabHit_Dust"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, HeavyDustPalette, 1.f, 0.f, Error);
        FSystemParts Hit;
        Hit.System = LDSliceVfx::ESystem::GrabHit;
        if (!Ring || !Spark || !Flash || !Dust) { return Hit; }
        ImpactCore(Hit, Flash, 110.f, 260.f, TEXT("float3(User.End.x, User.End.y, User.End.z - 38.0)"));
        FPart& Puff = Hit.Parts.AddDefaulted_GetRef();
        Puff.Name = TEXT("Puff");
        Puff.Kind = FPart::Sprite;
        Puff.Count = TEXT("int(lerp(2.0, 4.0, User.Focus) + 0.5)");
        Puff.Lifetime = TEXT("0.7");
        Puff.Paint = Dust;
        Puff.Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.End.x, User.End.y, User.End.z - 50.0) + float3((%s * 2.0 - 1.0) * 30.0, (%s * 2.0 - 1.0) * 30.0, -20.0 * Particles.NormalizedAge)"), *Random(755.f), *Random(756.f)))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * (50.0 + 70.0 * sqrt(Particles.NormalizedAge))"))
            .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0"), *Random(757.f)))
            .Set(TEXT("Particles.Color"), Glowing(.9f, TEXT("0.75 * saturate(Particles.Age / 0.04) * (1.0 - Particles.NormalizedAge)")));
        // A low, sharp pop at hip height (a small flat ring snapping open) and sparks that droop and fall: a mean, low blow.
        const int32 Points = 18;
        FPart& Pop = Hit.Parts.AddDefaulted_GetRef();
        Pop.Name = TEXT("Pop");
        Pop.Count = FString::Printf(TEXT("%d"), Points);
        Pop.Lifetime = TEXT("0.12");
        Pop.Paint = Ring;
        Pop.TileCm = 40.f;
        {
            const FString Theta = FString::Printf(TEXT("(%s / %d.0 * 6.2832)"), Own, Points - 1);
            const FString Open = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.1), 2.0))");
            Pop.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.End.x, User.End.y, User.End.z - 38.0) + float3(cos(%s), sin(%s), 0.0) * (12.0 + 55.0 * %s)"), *Theta, *Theta, *Open))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("20.0 * (1.0 - 0.8 * %s)"), *Open))
                .Set(TEXT("Particles.Color"), Glowing(2.f, FString::Printf(TEXT("(1.0 - %s)"), *Open)));
        }
        FPart& Droop = Hit.Parts.AddDefaulted_GetRef();
        Droop.Name = TEXT("Droop");
        Droop.Kind = FPart::Sprite;
        Droop.Count = TEXT("int(lerp(4.0, 8.0, User.Focus) + 0.5)");
        Droop.Lifetime = FString::Printf(TEXT("0.35 + 0.2 * %s"), *Random(751.f));
        Droop.Paint = Spark;
        Droop.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString A = FString::Printf(TEXT("(%s * 6.2832)"), *Random(752.f));
            const FString V = FString::Printf(TEXT("(float3(cos(%s), sin(%s), 0.0) * (160.0 + 120.0 * %s) + float3(0.0, 0.0, 60.0))"), *A, *A, *Random(753.f));
            Droop.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s + float3(0.0, 0.0, -1100.0) * Particles.Age"), *V))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.End.x, User.End.y, User.End.z - 38.0) + %s * Particles.Age + float3(0.0, 0.0, -550.0) * Particles.Age * Particles.Age"), *V))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(28.0, 6.0)"))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(2.f, TEXT("(1.0 - Particles.NormalizedAge)")));
        }
        return Hit;
    }

    FSystemParts BailReleaseParts(UMaterial& Material, FString& Error)
    {
        UMaterialInstanceConstant* Line = ChainPaint(TEXT("MI_LDBail_Slip"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), nullptr, 0.f, NeutralPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Glow = ChainPaint(TEXT("MI_LDBail_Fist"), Material, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Dust = ChainPaint(TEXT("MI_LDBail_Dust"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, HeavyDustPalette, 1.f, 0.f, Error);
        FSystemParts Release;
        Release.System = LDSliceVfx::ESystem::BailRelease;
        if (!Line || !Glow || !Dust) { return Release; }
        Wake(Release.Parts.AddDefaulted_GetRef(), Dust, TEXT("5.0"), .2f, 745.f);
        // Two slipstream lines at shoulder height trail the 0.2 s dash through, and at its end both fists light up and stay
        // lit a moment (the empowered punch it sets up).
        const int32 Points = 10;
        FPart& Slip = Release.Parts.AddDefaulted_GetRef();
        Slip.Name = TEXT("Slip");
        Slip.Count = FString::Printf(TEXT("%d"), Points * 2);
        Slip.Lifetime = TEXT("0.45");
        Slip.Paint = Line;
        Slip.TileCm = 60.f;
        Slip.bRibbonIds = true;
        {
            const FString R = TEXT("(float(Particles.LDRibbon) * 2.0 - 1.0)");
            const FString T = FString::Printf(TEXT("(Particles.LDJ / %d.0)"), Points - 1);
            const FString Head = TEXT("saturate(Particles.Age / 0.2)");
            const FString Tail = TEXT("saturate((Particles.Age - 0.12) / 0.25)");
            Slip.Set(TEXT("Particles.LDRibbon"), FString::Printf(TEXT("int(floor(%s / %d.0))"), Own, Points))
                .Set(TEXT("Particles.LDJ"), FString::Printf(TEXT("(%s - float(Particles.LDRibbon) * %d.0)"), Own, Points))
                .Set(TEXT("Particles.RibbonLinkOrder"), TEXT("Particles.LDJ"))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * %s * lerp(%s, %s, %s) + %s * %s * 32.0 + float3(0.0, 0.0, 10.0)"),
                    ShotFwd, ShotLength, *Tail, *Head, *T, ShotSide, *R))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("(4.0 + 10.0 * %s)"), *T))
                .Set(TEXT("Particles.Color"), Tinted(2.2f, FString::Printf(TEXT("(0.3 + 0.7 * %s)"), *T)));
        }
        FPart& Fists = Release.Parts.AddDefaulted_GetRef();
        Fists.Name = TEXT("Fists");
        Fists.Kind = FPart::Sprite;
        Fists.Count = TEXT("2");
        Fists.Lifetime = TEXT("0.75");
        Fists.Paint = Glow;
        Fists.Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * %s + %s * (float(Particles.UniqueID) * 2.0 - 1.0) * 24.0 + float3(0.0, 0.0, 5.0)"), ShotFwd, ShotLength, ShotSide))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * 60.0 * step(0.18, Particles.Age) * (0.85 + 0.15 * sin(Particles.Age * 30.0))"))
            .Set(TEXT("Particles.Color"), Glowing(2.6f, TEXT("(1.0 - saturate((Particles.Age - 0.45) / 0.3))")));
        return Release;
    }

    FSystemParts RamReleaseParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Air = ChainPaint(TEXT("MI_LDRam_Air"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 1.f, ImpactPalette, 1.f, 4.f, Error);
        UMaterialInstanceConstant* Shade = ChainPaint(TEXT("MI_LDRam_Shade"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 1.f, HeavyDustPalette, 1.f, 3.f, Error);
        UMaterialInstanceConstant* Dust = ChainPaint(TEXT("MI_LDRam_Dust"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, HeavyDustPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Skid = ChainPaint(TEXT("MI_LDRam_Skid"), Material, SliceTexture(LDSliceVfx::ETex::WaterWet), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, ScuffPalette, 1.f, 0.f, Error);
        FSystemParts Release;
        Release.System = LDSliceVfx::ESystem::RamRelease;
        if (!Air || !Shade || !Dust || !Skid) { return Release; }
        // The charge drives a body-wide wedge down its 460 cm path in 0.28 s: a dark shadow of displaced dust hugging the floor
        // under a warm pressure front, a bow wave of dark dust shoved out to both sides as it passes, skid marks torn into the
        // floor behind it and dust hanging in the wake.
        const int32 Points = 11;
        const FString Travel = FString::Printf(TEXT("(%s * (1.0 - pow(1.0 - saturate(Particles.Age / 0.28), 1.5)))"), ShotLength);
        const FString End = TEXT("(1.0 - saturate((Particles.Age - 0.28) / 0.12))");
        const auto Wedge = [&](FPart& Part, const TCHAR* Name, UMaterialInstanceConstant* Paint, float Lift, float Behind, float Half, float Width, const FString& Colour)
        {
            Part.Name = Name;
            Part.Count = FString::Printf(TEXT("%d"), Points);
            Part.Lifetime = TEXT("0.42");
            Part.Paint = Paint;
            Part.TileCm = 80.f;
            const FString A = FString::Printf(TEXT("((%s / %d.0 - 0.5) * 2.0)"), Own, Points - 1);
            Part.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("User.Start + %s * (%s + 55.0 - %.1f - abs(%s) * 55.0) + %s * %s * %.1f + float3(0.0, 0.0, %.1f)"),
                    ShotFwd, *Travel, Behind, *A, ShotSide, *A, Half, Lift))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("%.1f * (1.0 - abs(%s) * 0.55) * %s"), Width, *A, *End))
                .Set(TEXT("Particles.Color"), Colour);
        };
        Wedge(Release.Parts.AddDefaulted_GetRef(), TEXT("Shadow"), Shade, -80.f, 22.f, 105.f, 130.f, Glowing(.8f, TEXT("1.0")));
        Wedge(Release.Parts.AddDefaulted_GetRef(), TEXT("Front"), Air, -30.f, 0.f, 85.f, 60.f, Glowing(1.4f, TEXT("0.9")));
        // Bow wave: dust thrown out sideways from the front as it passes each point.
        FPart& Bow = Release.Parts.AddDefaulted_GetRef();
        Bow.Name = TEXT("Bow");
        Bow.Kind = FPart::Sprite;
        Bow.Count = TEXT("int(lerp(8.0, 14.0, User.Focus) + 0.5)");
        Bow.Lifetime = TEXT("1.2");
        Bow.Paint = Dust;
        {
            const FString K = TEXT("((float(Particles.UniqueID) + 0.5) / max(float(int(lerp(8.0, 14.0, User.Focus) + 0.5)), 1.0))");
            const FString Born = FString::Printf(TEXT("(%s * 0.28)"), *K);
            const FString Tau = FString::Printf(TEXT("max(Particles.Age - %s, 0.0)"), *Born);
            const FString Out = FString::Printf(TEXT("(1.0 - exp(-3.5 * %s))"), *Tau);
            const FString Sign = TEXT("(step(0.5, frac(float(Particles.UniqueID) * 0.5)) * 2.0 - 1.0)");
            Bow.Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.Start.x, User.Start.y, User.Start.z - 80.0) + %s * %s * %s + %s * %s * (40.0 + 130.0 * %s) + float3(0.0, 0.0, 15.0 + 45.0 * %s)"),
                    ShotFwd, ShotLength, *K, ShotSide, *Sign, *Out, *Out))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(1.0, 1.0) * (90.0 + 140.0 * sqrt(saturate(%s / 1.0))) * step(%s, Particles.Age)"), *Tau, *Born))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0 + %s * 40.0"), *Random(791.f), *Tau))
                .Set(TEXT("Particles.Color"), Glowing(.9f, FString::Printf(TEXT("1.0 * saturate(%s / 0.04) * (1.0 - saturate(%s / 0.95))"), *Tau, *Tau)));
        }
        // Skid marks: two dark tracks torn into the floor behind the front, fading over a second and a half.
        FPart& Marks = Release.Parts.AddDefaulted_GetRef();
        Marks.Name = TEXT("Skid");
        Marks.Count = TEXT("8");
        Marks.Lifetime = TEXT("1.6");
        Marks.Paint = Skid;
        Floor(Marks, ShotFwd);
        {
            const FString K = TEXT("((floor(float(Particles.UniqueID) / 2.0) + 0.5) / 4.0)");
            const FString Track = TEXT("(frac(float(Particles.UniqueID) * 0.5) * 4.0 - 1.0)");
            const FString Born = FString::Printf(TEXT("(%s * 0.28)"), *K);
            Marks.Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.Start.x, User.Start.y, User.Start.z - 93.0) + %s * %s * %s + %s * %s * 22.0"),
                    ShotFwd, ShotLength, *K, ShotSide, *Track))
                .Set(TEXT("Particles.SpriteSize"), FString::Printf(TEXT("float2(%s / 4.0 * 1.3, 46.0) * step(%s, Particles.Age)"), ShotLength, *Born))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(1.f, TEXT("0.9 * (1.0 - saturate((Particles.Age - 0.6) / 1.0))")));
        }
        Wake(Release.Parts.AddDefaulted_GetRef(), Dust, TEXT("7.0"), .28f, 761.f);
        return Release;
    }

    FSystemParts RamHitParts(UMaterial& Material, UMaterial& Top, FString& Error)
    {
        UMaterialInstanceConstant* Flash = ChainPaint(TEXT("MI_LDRamHit_Flash"), Top, SliceTexture(LDSliceVfx::ETex::ChainAFlash), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Ring = ChainPaint(TEXT("MI_LDRamHit_Ring"), Material, SliceTexture(LDSliceVfx::ETex::ChainAProfile), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 1.f, ImpactPalette, 1.f, 3.f, Error);
        UMaterialInstanceConstant* Chip = ChainPaint(TEXT("MI_LDRamHit_Chip"), Top, SliceTexture(LDSliceVfx::ETex::ChainASparkStreak), nullptr, 0.f, ImpactPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Dust = ChainPaint(TEXT("MI_LDRamHit_Dust"), Material, SliceTexture(LDSliceVfx::ETex::SoftPuff), nullptr, 0.f, HeavyDustPalette, 1.f, 0.f, Error);
        UMaterialInstanceConstant* Scuff = ChainPaint(TEXT("MI_LDRamHit_Scuff"), Material, SliceTexture(LDSliceVfx::ETex::WaterWet), SliceTexture(LDSliceVfx::ETex::DissolveNoise), 2.f, ScuffPalette, 1.f, 0.f, Error);
        FSystemParts Hit;
        Hit.System = LDSliceVfx::ESystem::RamHit;
        if (!Flash || !Ring || !Chip || !Dust || !Scuff) { return Hit; }
        const FString Floor0 = TEXT("float3(User.End.x, User.End.y, User.End.z - 92.0)");
        // The impact punch: a body-wide warm core at the contact for three frames (0-100 ms, inside C's 0-180 ms victim flash),
        // with its light.
        FPart& Core = Hit.Parts.AddDefaulted_GetRef();
        Core.Name = TEXT("Core");
        Core.Kind = FPart::Sprite;
        Core.Lifetime = TEXT("0.1");
        Core.Paint = Flash;
        Core.Set(TEXT("Particles.Position"), TEXT("User.End - normalize(User.End - User.Start) * 20.0"))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * (170.0 - 50.0 * Particles.NormalizedAge)"))
            .Set(TEXT("Particles.Color"), Glowing(2.6f, TEXT("(1.0 - pow(Particles.NormalizedAge, 2.0))")));
        FPart& Light = Hit.Parts.AddDefaulted_GetRef();
        Light.Name = TEXT("Light");
        Light.Kind = FPart::Light;
        Light.Lifetime = TEXT("0.14");
        Light.Set(TEXT("Particles.Position"), TEXT("User.End"))
            .Set(TEXT("Particles.LightRadius"), TEXT("380.0"))
            .Set(TEXT("Particles.Color"), FString::Printf(TEXT("float4(1.0, 0.75, 0.45, 1.0) * float4(7.0, 7.0, 7.0, 1.0) * %s * (1.0 - Particles.NormalizedAge)"), Bright));
        // The ground answers: a thick warm shock ring rolls out over the floor, a ring of dark dust heaves up behind it, and a
        // crater scuff stays.
        const int32 Points = 32;
        FPart& Shock = Hit.Parts.AddDefaulted_GetRef();
        Shock.Name = TEXT("Shock");
        Shock.Count = FString::Printf(TEXT("%d"), Points);
        Shock.Lifetime = TEXT("0.32");
        Shock.Paint = Ring;
        Shock.TileCm = 60.f;
        {
            const FString Theta = FString::Printf(TEXT("(%s / %d.0 * 6.2832)"), Own, Points - 1);
            const FString Open = TEXT("(1.0 - pow(1.0 - saturate(Particles.Age / 0.28), 2.5))");
            Shock.Set(TEXT("Particles.RibbonLinkOrder"), Own)
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + float3(cos(%s), sin(%s), 0.0) * (40.0 + 150.0 * %s) + float3(0.0, 0.0, 3.0)"), *Floor0, *Theta, *Theta, *Open))
                .Set(TEXT("Particles.RibbonWidth"), FString::Printf(TEXT("34.0 * (1.0 - 0.75 * %s)"), *Open))
                .Set(TEXT("Particles.Color"), Glowing(1.8f, FString::Printf(TEXT("(1.0 - %s)"), *Open)));
        }
        FPart& Heave = Hit.Parts.AddDefaulted_GetRef();
        Heave.Name = TEXT("Heave");
        Heave.Kind = FPart::Sprite;
        Heave.Count = TEXT("int(lerp(7.0, 12.0, User.Focus) + 0.5)");
        Heave.Lifetime = TEXT("1.1");
        Heave.Paint = Dust;
        {
            const FString A = FString::Printf(TEXT("((float(Particles.UniqueID) + %s * 0.6) / max(float(int(lerp(7.0, 12.0, User.Focus) + 0.5)), 1.0) * 6.2832)"), *Random(801.f));
            const FString Out = TEXT("(1.0 - exp(-4.0 * Particles.Age))");
            Heave.Set(TEXT("Particles.Position"), FString::Printf(TEXT("%s + float3(cos(%s), sin(%s), 0.0) * (40.0 + 120.0 * %s) + float3(0.0, 0.0, 20.0 + 55.0 * %s)"), *Floor0, *A, *A, *Out, *Out))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * (70.0 + 110.0 * sqrt(Particles.NormalizedAge))"))
                .Set(TEXT("Particles.SpriteRotation"), FString::Printf(TEXT("%s * 360.0 + Particles.Age * 30.0"), *Random(802.f)))
                .Set(TEXT("Particles.Color"), Glowing(1.f, TEXT("0.72 * saturate(Particles.Age / 0.04) * (1.0 - Particles.NormalizedAge)")));
        }
        FPart& Chips = Hit.Parts.AddDefaulted_GetRef();
        Chips.Name = TEXT("Chips");
        Chips.Kind = FPart::Sprite;
        Chips.Count = TEXT("int(lerp(8.0, 14.0, User.Focus) + 0.5)");
        Chips.Lifetime = FString::Printf(TEXT("0.45 + 0.25 * %s"), *Random(771.f));
        Chips.Paint = Chip;
        Chips.Align = ENiagaraSpriteAlignment::VelocityAligned;
        {
            const FString A = FString::Printf(TEXT("(%s * 6.2832)"), *Random(772.f));
            const FString V = FString::Printf(TEXT("(float3(cos(%s), sin(%s), 0.0) * (160.0 + 220.0 * %s) + float3(0.0, 0.0, 480.0 + 300.0 * %s))"), *A, *A, *Random(773.f), *Random(774.f));
            Chips.Set(TEXT("Particles.Velocity"), FString::Printf(TEXT("%s + float3(0.0, 0.0, -1100.0) * Particles.Age"), *V))
                .Set(TEXT("Particles.Position"), FString::Printf(TEXT("float3(User.End.x, User.End.y, User.End.z - 80.0) + %s * Particles.Age + float3(0.0, 0.0, -550.0) * Particles.Age * Particles.Age"), *V))
                .Set(TEXT("Particles.SpriteSize"), TEXT("float2(30.0, 7.0)"))
                .Set(TEXT("Particles.SpriteRotation"), Quarter)
                .Set(TEXT("Particles.Color"), Glowing(2.f, TEXT("(1.0 - Particles.NormalizedAge)")));
        }
        FPart& Crater = Hit.Parts.AddDefaulted_GetRef();
        Crater.Name = TEXT("Crater");
        Crater.Lifetime = TEXT("User.Duration");
        Crater.Paint = Scuff;
        Floor(Crater, TEXT("float3(1.0, 0.0, 0.0)"));
        Crater.Set(TEXT("Particles.Position"), Floor0 + TEXT(" + float3(0.0, 0.0, 1.5)"))
            .Set(TEXT("Particles.SpriteSize"), TEXT("float2(1.0, 1.0) * 200.0 * (0.6 + 0.4 * saturate(Particles.Age / 0.15))"))
            .Set(TEXT("Particles.Color"), Glowing(1.f, TEXT("0.65 * saturate(Particles.Age / 0.05) * (1.0 - saturate((Particles.Age - 0.5) / max(User.Duration - 0.55, 0.1)))")));
        return Hit;
    }

    // The field's material: one Custom node (Code) over the first UV, the Particle Color's four channels (PR, PG, PB, PA:
    // FieldParts says what they carry), the sprite's size, the time, the edge's crackle noise and the charged floor's Web
    // and Energy textures, returning the emissive colour and the coverage. Translucent, unlit, two-sided; the palette is
    // the chain's (warm-white core, pale-gold body, a dark amber fringe that keeps the edge readable on the bright floor).
    UMaterial* BuildFieldMaterial(const TCHAR* Name, const TCHAR* Code, UTexture2D* Noise, UTexture2D* WebTexture, UTexture2D* EnergyTexture,
        FString& Error)
    {
        const FString PackageName = GeneratedPackage(Name);
        Retire(PackageName);
        UMaterial* Material = NewObject<UMaterial>(CreatePackage(*PackageName), *FPackageName::GetShortName(PackageName), RF_Public | RF_Standalone);
        auto* UV = AddExpression<UMaterialExpressionTextureCoordinate>(*Material);
        auto* Vein = AddExpression<UMaterialExpressionTextureObjectParameter>(*Material);
        Vein->ParameterName = LDSliceVfx::NoiseParameter;
        Vein->Texture = Noise;
        Vein->SamplerType = SAMPLERTYPE_LinearColor;
        const auto Extra = [&](const TCHAR* Parameter, UTexture2D* Texture)
        {
            auto* Object = AddExpression<UMaterialExpressionTextureObjectParameter>(*Material);
            Object->ParameterName = Parameter;
            Object->Texture = Texture;
            Object->SamplerType = SAMPLERTYPE_LinearColor;
            return Object;
        };
        auto* Web = Extra(TEXT("Web"), WebTexture);
        auto* Energy = Extra(TEXT("Energy"), EnergyTexture);
        auto* Particle = AddExpression<UMaterialExpressionParticleColor>(*Material);
        auto* Size = AddExpression<UMaterialExpressionParticleSize>(*Material);
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
        Bind(TEXT("Size"), Size, 0);
        Bind(TEXT("Web"), Web, 0);
        Bind(TEXT("Energy"), Energy, 0);
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

    // The electric field ground state and the conduct reaction's splash, at its authoritative radius User.Width round
    // User.Start on the floor: a living electric edge over a charged floor.
    //   Field   one disc on the floor 1.12 x the radius:
    //           edge   a living electric band on the radius (hot core, crackle re-drawn at 12 Hz, 12 Hz tremble, three
    //                  travelling surges), warm gold rather than white, over a narrow dark-amber under-edge just outside it
    //                  (contrast on sunlit floors); its dark stretches dim to a third instead of vanishing, so the danger
    //                  edge never breaks
    //           floor  the field_web crack web in world space (about 320 cm per tile, two samples at different scales),
    //                  re-randomised three times a second with a slow drift, shown in soft patches (field_energy_noise
    //                  R), with some web cells flaring briefly (B); low contrast, fading out near the edge
    //           Particle Color: R brightness (the opening flash), G the floor's share, B the edge's width share, A coverage.
    //           Opens by expanding from 75 % of the radius with a bright edge, the floor filling in after it; at the end
    //           the floor goes first, then the edge narrows, tightens and fades.
    //   Arches  arcs leaping 35-60 cm high between two points of the edge 50-90 cm apart (decoration: seven for the own or
    //           focused field, three for others'). Others' fields keep the same edge; their floor web is fainter.
    // The arcs that reach the people standing inside are the runtime's reduced chain hops (ULDImpactFxSubsystem::Bite).
    FSystemParts FieldParts(UMaterial& Bolts, FString& Error)
    {
        UMaterial* Material = BuildFieldMaterial(TEXT("M_LDField"),
            TEXT("float K = 1.12;\r\n")
            TEXT("float2 q = (UV * 2.0 - 1.0) * K;\r\n")
            TEXT("float r = length(q);\r\n")
            TEXT("float R = max(Size.x * 0.5 / K, 1.0);\r\n")
            TEXT("float2 P = q * R;\r\n")
            TEXT("float ang = frac(atan2(q.y, q.x) / 6.2832 + 1.0);\r\n")
            TEXT("float along = ang * 6.2832 * R / 60.0;\r\n")
            TEXT("float t = floor(Time * 12.0);\r\n")
            // The edge band.
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
            SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise), SliceTexture(LDSliceVfx::ETex::FieldWeb),
            SliceTexture(LDSliceVfx::ETex::FieldEnergyNoise), Error);
        const FPalette Palette{FLinearColor(1.f, .93f, .78f), FLinearColor(1.f, .68f, .26f), FLinearColor(.22f, .1f, .03f), 2.8f, 1.2f};
        UMaterialInstanceConstant* Arc = ChainPaint(TEXT("MI_LDField_Arc"), Bolts, SliceTexture(LDSliceVfx::ETex::ChainAProfile),
            SliceTexture(LDSliceVfx::ETex::ChainAEnergyNoise), 1.f, Palette, 1.f, 1.6f, Error);
        UMaterialInstanceConstant* Paint = Material ? ChainPaintOf(TEXT("MI_LDField_Disc"), *Material, Error) : nullptr;
        FSystemParts Field;
        Field.System = LDSliceVfx::ESystem::Field;
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
        // Arches over the edge.
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
            else if (Part.LoopSeconds > 0.f)
            {
                Enum(Steps, Part.Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Behavior"), TEXT("Infinite"));
                Float(Steps, Part.Name, EmitterUpdate, TEXT("EmitterState"), TEXT("Loop Duration"), Part.LoopSeconds);
            }
            Expression(Steps, Part.Name, EmitterUpdate, TEXT("SpawnBurst_Instantaneous"), TEXT("Spawn Count"), *Part.Count);
            Expression(Steps, Part.Name, ParticleSpawn, TEXT("InitializeParticle"), TEXT("Lifetime"), *Part.Lifetime);
            TArray<TPair<FString, FString>> AtSpawn = Part.SpawnOnly;
            AtSpawn.Append(Part.Attributes);
            SetAttributes(Steps, Part.Name, ParticleSpawn, AtSpawn);
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
    if (!Chain || !Top || !BuildBodyGlowMaterial(Error)) { return false; }
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
        [&]() { return FieldParts(*Chain, Error); },
        [&]() { return HoseTellParts(*Chain, Error); },
        [&]() { return HoseJetParts(*Chain, *Top, Error); },
        [&]() { return WaterHitParts(*Chain, *Top, Error); },
        [&]() { return ZapChargeParts(*Chain, *Top, Error); },
        [&]() { return SeetheTellParts(*Chain, *Top, Error); },
        [&]() { return SeetheBurstParts(*Chain, *Top, Error); },
        [&]() { return FireHitParts(*Chain, *Top, Error); },
        [&]() { return ColaTellParts(*Chain, Error); },
        [&]() { return ColaBlastParts(*Chain, *Top, Error); },
        [&]() { return ChillHitParts(*Chain, *Top, Error); },
        [&]() { return BurnStateParts(*Chain, *Top, Error); },
        [&]() { return WetStateParts(*Chain, *Top, Error); },
        [&]() { return ShockStateParts(*Chain, *Top, Error); },
        [&]() { return ChillStateParts(*Chain, *Top, Error); },
        [&]() { return RotStateParts(*Chain, *Top, Error); },
        [&]() { return PuddleParts(*Chain, Error); },
        [&]() { return IceParts(*Chain, *Top, Error); },
        [&]() { return FireGroundParts(*Chain, *Top, Error); },
        [&]() { return MiasmaParts(*Chain, *Top, Error); },
        [&]() { return KnockoutParts(*Chain, *Top, Error); },
        [&]() { return CastAimParts(*Chain, *Top, Error); },
        [&]() { return CastBraceParts(*Chain, *Top, Error); },
        [&]() { return ElbowReleaseParts(*Chain, Error); },
        [&]() { return ElbowHitParts(*Chain, *Top, Error); },
        [&]() { return PantsReleaseParts(*Chain, *Top, Error); },
        [&]() { return PantsHitParts(*Chain, *Top, Error); },
        [&]() { return GrabReleaseParts(*Chain, Error); },
        [&]() { return GrabHitParts(*Chain, *Top, Error); },
        [&]() { return BailReleaseParts(*Chain, Error); },
        [&]() { return RamReleaseParts(*Chain, *Top, Error); },
        [&]() { return RamHitParts(*Chain, *Top, Error); }};
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
