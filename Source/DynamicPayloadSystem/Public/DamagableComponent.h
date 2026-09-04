#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EStructuralState.h"
#include "DamagableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnStructuralStateChanged,
	EStructuralState,
	NewState
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnDamageTaken,
	float, DamageAmount,
	float, RemainingHealth
);

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class DYNAMICPAYLOADSYSTEM_API UDamagableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDamagableComponent();

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTakeAnyDamage(
		AActor* DamagedActor,
		float Damage,
		const class UDamageType* DamageType,
		class AController* InstigatedBy,
		AActor* DamageCauser
	);

	void UpdateStructuralState();

public:
	UPROPERTY(VisibleAnywhere, Category = "Health")
	float CurrentHealth;

	UPROPERTY(EditAnywhere, Category = "Health",
		meta = (ClampMin = "0.01",
			ToolTip = "Maximum health. Must be > 0 — at 0 the structural-state update short-circuits and the actor can never transition to Destroyed."))
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName DamageableTag;

	FString GetReadableName() const;
	void SetHighlightEnabled(bool bEnabled);

	/** Restore this target to full health and the Intact state.
	 *
	 *  Cancels any despawn already scheduled by DestroyDelay, so a target can be
	 *  brought back during the seconds between "destroyed" and "gone". Broadcasts
	 *  OnStructuralStateChanged only on a real transition, which is what puts the
	 *  mesh, collision and ticking back (see ATargetActor).
	 *
	 *  Safe to call on an undamaged target: it becomes a no-op. */
	UFUNCTION(BlueprintCallable, Category = "Damage")
	void Revive();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Damage")
	EStructuralState StructuralState = EStructuralState::Intact;

	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float DamagedThreshold = 0.7f;

	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float DestroyedThreshold = 0.25f;

	/** Seconds between entering the Destroyed state and the owning actor being despawned.
	 *  Set to 0 to keep the actor alive forever (the user is responsible for cleanup).
	 *  Set to >0 to auto-destroy via SetLifeSpan after the destroyed-state visuals play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage",
		meta = (ClampMin = "0.0"))
	float DestroyDelay = 2.f;

	UPROPERTY(BlueprintAssignable, Category = "Damage")
	FOnStructuralStateChanged OnStructuralStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Damage")
	FOnDamageTaken OnDamageTaken;

	/** Toggle debug logging for damage and structural state changes (editor only). */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bShowDebug = false;
};
