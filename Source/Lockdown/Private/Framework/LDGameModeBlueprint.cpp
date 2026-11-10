#include "Framework/LDGameMode.h"
#include "Core/LDLog.h"

#include "Combat/LDCombatDirector.h"
#include "Development/LDBuildBlueprint.h"
#include "Development/LDDemoContentCatalog.h"
#include "Framework/LDBuildBlueprintComponent.h"
#include "Framework/LDDemoEntry.h"
#include "Framework/LDPlayerController.h"

// #198 §10: the authority composes each human contender's own blueprint from Combat's ledger and the frozen result, and
// offers it only on that contender's owning connection. AI contenders get none; nobody receives another player's build.
void ALDGameMode::PublishBuildBlueprints()
{
    if (!HasAuthority() || !bResultLocked || !ResultView.ResultId.IsValid()) { return; }
    FString Product, ProductError;
    const bool bProduct = LDDemoEntry::ReadProductVersion(Product, ProductError);
    for (const auto& Pair : Contenders)
    {
        ALDPlayerController* Controller = Pair.Key.Get();
        const FLDContenderRecord& Contender = Pair.Value;
        if (!Controller || !Controller->BuildBlueprints || Contender.Kind != ELDContenderKind::Human || Contender.bLeft) { continue; }
        const FGuid& Id = Contender.Session;
        const FLDResultRow* Row = ResultView.Rows.FindByPredicate([&Id](const FLDResultRow& Candidate) { return Candidate.ContenderId == Id; });
        TArray<FLDBlueprintVersion> Versions;
        TArray<uint8> Bytes;
        FString Failure;
        if (!Row)
        {
            Failure = TEXT("本局结果里没有你的记录，无法生成构筑蓝图。");
        }
        else if (!bProduct)
        {
            Failure = FString(TEXT("房主无法读取产品版本，无法生成构筑蓝图：")) + ProductError;
        }
        else if (!DemoContent.IsValid())
        {
            Failure = TEXT("本局内容目录不可用，无法生成构筑蓝图。");
        }
        else
        {
            // A contender with no change and no fact still has its match-start build: LedgerVersion observes it first.
            if (Combat) { Combat->LedgerVersion(Id); }
            if (!Combat || !Combat->GetBuildLedger().Close(Id, ResultView.EndedAtMs, Versions))
            {
                Failure = TEXT("本局没有记录到你的构筑版本，无法生成构筑蓝图。");
            }
        }
        if (Failure.IsEmpty())
        {
            const FLDDemoContentIdentity& Identity = DemoContent->Get().Identity;
            LDBuildBlueprint::FHeader Header;
            Header.ProductVersion = Product;
            Header.WireProtocol = int32(LDDemoEntry::WireProtocol);
            Header.CatalogRoot = Identity.Root.ToHex();
            Header.CatalogSchemaVersion = Identity.SchemaVersion;
            Header.HandlerRevision = Identity.HandlerRevision;
            Header.ProjectionRevision = Identity.ProjectionRevision;
            Header.MatchId = ResultView.ResultId;
            Header.EndReason = ResultView.Reason == ELDMatchEndReason::GuardSucceeded ? TEXT("guard_succeeded") : TEXT("host_ended");
            Header.Outcome = !ResultView.Winner.IsValid() ? TEXT("no_winner") : ResultView.Winner == Id ? TEXT("won") : TEXT("lost");
            Header.RecordedAtUtc = FDateTime::UtcNow();
            Header.StartedAtMs = ResultView.StartedAtMs;
            Header.EndedAtMs = ResultView.EndedAtMs;
            Header.FinalLevel = Row->FinalLevel;
            Header.Kills = int64(Row->Kills);
            Header.Deaths = int64(Row->Deaths);
            Header.Assists = int64(Row->Assists);
            FLDBlueprintRecord Record;
            FString Error;
            if (!LDBuildBlueprint::Compose(Header, Versions, Record, Error) || !LDBuildBlueprint::Serialize(Record, Bytes, Error))
            {
                Failure = FString(TEXT("房主无法生成构筑蓝图：")) + Error;
                Bytes.Reset();
            }
            else if (Bytes.Num() > LDBuildBlueprint::MaxTransferBytes)
            {
                Failure = FString::Printf(TEXT("构筑蓝图有 %d 字节，超过传输上限，没有发送。"), Bytes.Num());
                Bytes.Reset();
            }
        }
        UE_LOG(LogLDMatch, Display, TEXT("LD_BUILD_BLUEPRINT_OFFER generation=%u contender=%s versions=%d bytes=%d failure=%s"),
            MatchContext.Generation, *LDLog::Id(Id), Versions.Num(), Bytes.Num(), Failure.IsEmpty() ? TEXT("-") : *LDLog::Quote(Failure));
        Controller->BuildBlueprints->OfferFromAuthority(ResultView.ResultId, MoveTemp(Bytes), Failure);
    }
}
