// Copyright Tanushk Nirmal 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PatrolAreaVolume.generated.h"

class UBoxComponent;

UCLASS()
class DYNAMICPAYLOADSYSTEM_API APatrolAreaVolume : public AActor
{
	GENERATED_BODY()

public:
	APatrolAreaVolume();

	FVector GetRandomPointInArea() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PatrolArea")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PatrolArea")
	UBoxComponent* Bounds;
};
