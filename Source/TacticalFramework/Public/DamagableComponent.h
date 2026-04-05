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
class TACTICALFRAMEWORK_API UDamagableComponent : public UActorComponent
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

	UPROPERTY(EditAnywhere, Category = "Health")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName DamageableTag;

	FString GetReadableName() const;
	void SetHighlightEnabled(bool bEnabled);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Damage")
	EStructuralState StructuralState = EStructuralState::Intact;

	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float DamagedThreshold = 0.7f;

	UPROPERTY(EditDefaultsOnly, Category = "Damage")
	float DestroyedThreshold = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	float DestroyDelay = 2.f;

	UPROPERTY(BlueprintAssignable, Category = "Damage")
	FOnStructuralStateChanged OnStructuralStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Damage")
	FOnDamageTaken OnDamageTaken;
};
