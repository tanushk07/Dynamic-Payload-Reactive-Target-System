#pragma once

#include "CoreMinimal.h"
#include "ELogType.h"
#include "ELogSeverity.h"
#include "FGameLogEntry.generated.h"

USTRUCT(BlueprintType)
struct FGameLogEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	ELogType LogType = ELogType::Explosion;

	UPROPERTY(BlueprintReadWrite)
	ELogSeverity Severity = ELogSeverity::Critical;

	UPROPERTY(BlueprintReadWrite)
	FText Message = FText::GetEmpty();

	UPROPERTY(BlueprintReadWrite)
	float TimeStamp = 0.0f;

	FGameLogEntry() = default;
};
