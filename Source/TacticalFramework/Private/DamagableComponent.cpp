#include "DamagableComponent.h"
#include "GameFramework/Actor.h"

UDamagableComponent::UDamagableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDamagableComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentHealth = MaxHealth;

	AActor* Owner = GetOwner();
	if (!Owner) return;
	Owner->OnTakeAnyDamage.AddDynamic(
		this,
		&UDamagableComponent::OnTakeAnyDamage
	);
}

FString UDamagableComponent::GetReadableName() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
		return TEXT("INVALID");

	if (!DamageableTag.IsNone())
		return DamageableTag.ToString();

	return Owner->GetName();
}

void UDamagableComponent::OnTakeAnyDamage(
	AActor* DamagedActor,
	float Damage,
	const UDamageType* DamageTypeRef,
	AController* InstigatedBy,
	AActor* DamageCauser
)
{
	if (Damage <= 0.f)
		return;

	if (StructuralState == EStructuralState::Destroyed)
		return;

	CurrentHealth = FMath::Max(CurrentHealth - Damage, 0);
	OnDamageTaken.Broadcast(Damage, CurrentHealth);
	UpdateStructuralState();
}

void UDamagableComponent::SetHighlightEnabled(bool bEnabled)
{
	AActor* Owner = GetOwner();
	if (!Owner)
		return;

	TArray<UPrimitiveComponent*> PrimComponents;
	Owner->GetComponents<UPrimitiveComponent>(PrimComponents);

	for (UPrimitiveComponent* Comp : PrimComponents)
	{
		if (!Comp)
			continue;

		Comp->SetRenderCustomDepth(bEnabled);
		Comp->SetCustomDepthStencilValue(1);
	}
}

void UDamagableComponent::UpdateStructuralState()
{
	if (MaxHealth <= 0.f)
		return;

	const float HealthRatio = CurrentHealth / MaxHealth;

	EStructuralState NewState;

	if (HealthRatio <= DestroyedThreshold)
	{
		NewState = EStructuralState::Destroyed;
	}
	else if (HealthRatio <= DamagedThreshold)
	{
		NewState = EStructuralState::Damaged;
	}
	else
	{
		NewState = EStructuralState::Intact;
	}

	if (NewState != StructuralState)
	{
		StructuralState = NewState;
		OnStructuralStateChanged.Broadcast(StructuralState);
	}

#if WITH_EDITOR
	AActor* Owner = GetOwner();
	if (!Owner) return;
	UE_LOG(LogTemp, Display,
		TEXT("[%s] StructuralState -> %s (Health: %.1f / %.1f)"),
		*Owner->GetName(),
		*UEnum::GetValueAsString(StructuralState),
		CurrentHealth,
		MaxHealth
	);
#endif
}
