#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EStructuralState.h"
#include "TargetBehaviorComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TACTICALFRAMEWORK_API UTargetBehaviorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTargetBehaviorComponent();

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleStructuralStateChanged(EStructuralState NewState);

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float SpeedMultiplier = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bCanMove = true;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FOnMovementCapabilityChanged,
		float, SpeedMultiplier,
		bool, bCanMove
	);

	UPROPERTY(BlueprintAssignable)
	FOnMovementCapabilityChanged OnMovementCapabilityChanged;
};
