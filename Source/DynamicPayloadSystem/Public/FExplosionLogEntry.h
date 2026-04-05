#pragma once

#include "CoreMinimal.h"
#include "FExplosionLogEntry.generated.h"

USTRUCT(BlueprintType)
struct FExplosionLogEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString DroneName;

	UPROPERTY(BlueprintReadOnly)
	FString PayloadName;

	UPROPERTY(BlueprintReadOnly)
	FString DamagedActorName;

	UPROPERTY(BlueprintReadOnly)
	FString Damage;

	UPROPERTY(BlueprintReadOnly)
	FString CurrentHealth;

	UPROPERTY(BlueprintReadOnly)
	FString Distance;
};
