#pragma once

#include "CoreMinimal.h"
#include "FExplosionLogEntry.generated.h"

USTRUCT(BlueprintType)
struct FExplosionLogEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Explosion")
	FString DroneName;

	UPROPERTY(BlueprintReadOnly, Category = "Explosion")
	FString PayloadName;

	UPROPERTY(BlueprintReadOnly, Category = "Explosion")
	FString DamagedActorName;

	UPROPERTY(BlueprintReadOnly, Category = "Explosion")
	FString Damage;

	UPROPERTY(BlueprintReadOnly, Category = "Explosion")
	FString CurrentHealth;

	UPROPERTY(BlueprintReadOnly, Category = "Explosion")
	FString Distance;
};
