#pragma once

#include "CoreMinimal.h"
#include "ELogType.h"
#include "ELogSeverity.h"
#include "FGameLogEntry.generated.h"

USTRUCT(BlueprintType)
struct FGameLogEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Mission|Log")
	ELogType LogType = ELogType::Explosion;

	UPROPERTY(BlueprintReadWrite, Category = "Mission|Log")
	ELogSeverity Severity = ELogSeverity::Critical;

	UPROPERTY(BlueprintReadWrite, Category = "Mission|Log")
	FText Message = FText::GetEmpty();

	UPROPERTY(BlueprintReadWrite, Category = "Mission|Log")
	float TimeStamp = 0.0f;

	FGameLogEntry() = default;
};
