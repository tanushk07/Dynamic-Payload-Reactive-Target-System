#include "TargetBehaviorComponent.h"
#include "DamagableComponent.h"
#include "GameFramework/Actor.h"
#include "DynamicPayloadSystemModule.h"

UTargetBehaviorComponent::UTargetBehaviorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTargetBehaviorComponent::BeginPlay()
{
	Super::BeginPlay();

	UDamagableComponent* Damageable =
		GetOwner()->FindComponentByClass<UDamagableComponent>();

	if (Damageable)
	{
		Damageable->OnStructuralStateChanged.AddDynamic(
			this,
			&UTargetBehaviorComponent::HandleStructuralStateChanged
		);
	}
}

void UTargetBehaviorComponent::HandleStructuralStateChanged(
	EStructuralState NewState)
{
	switch (NewState)
	{
	case EStructuralState::Intact:
		SpeedMultiplier = 1.0f;
		bCanMove = true;
		break;

	case EStructuralState::Damaged:
		SpeedMultiplier = 0.5f;
		bCanMove = true;
		break;

	case EStructuralState::Destroyed:
		SpeedMultiplier = 0.0f;
		bCanMove = false;
		break;
	}

#if WITH_EDITOR
	if (bShowDebug)
	{
		UE_LOG(LogDynamicPayload, Display,
			TEXT("[%s] Behavior updated -> SpeedMultiplier: %.2f, CanMove: %s"),
			*GetOwner()->GetName(),
			SpeedMultiplier,
			bCanMove ? TEXT("true") : TEXT("false")
		);
	}
#endif
	OnMovementCapabilityChanged.Broadcast(SpeedMultiplier, bCanMove);
}
