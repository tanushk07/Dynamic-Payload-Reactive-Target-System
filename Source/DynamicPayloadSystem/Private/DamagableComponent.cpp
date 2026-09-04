#include "DamagableComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DynamicPayloadSystemModule.h"
#include "GameFramework/Actor.h"

UDamagableComponent::UDamagableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDamagableComponent::BeginPlay()
{
	Super::BeginPlay();

	// Defensive: a designer or Blueprint setter that bypasses the ClampMin meta
	// (e.g. SetMaxHealth at runtime) can still drive MaxHealth to 0. Snap it
	// up so UpdateStructuralState never short-circuits.
	if (MaxHealth <= 0.f)
	{
#if WITH_EDITOR
		if (bShowDebug)
		{
			UE_LOG(LogDynamicPayload, Warning,
				TEXT("[Damagable] %s: MaxHealth was %.2f; clamped to 1.0 to avoid softlock"),
				*GetOwner()->GetName(), MaxHealth);
		}
#endif
		MaxHealth = 1.f;
	}

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

	CurrentHealth = FMath::Max(CurrentHealth - Damage, 0.f);
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
	// Runtime safety: BeginPlay snaps MaxHealth up to 1.0 if a designer left it
	// at zero, but BP code can still call Set MaxHealth = 0 at runtime. Snap
	// here too so UpdateStructuralState never short-circuits permanently.
	if (MaxHealth <= 0.f)
	{
#if WITH_EDITOR
		if (bShowDebug)
		{
			UE_LOG(LogDynamicPayload, Warning,
				TEXT("[Damagable] %s: MaxHealth was set to <= 0 at runtime; clamped to 1.0"),
				GetOwner() ? *GetOwner()->GetName() : TEXT("(no owner)"));
		}
#endif
		MaxHealth = 1.f;
	}

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

		// Wire DestroyDelay through SetLifeSpan so the actor self-despawns
		// after the destroyed-state visuals (mesh swap, glow off) have played.
		// SetLifeSpan(0) means "don't auto-destroy", so a user can opt out by
		// setting DestroyDelay = 0 and handling cleanup themselves.
		if (NewState == EStructuralState::Destroyed && DestroyDelay > 0.f)
		{
			if (AActor* Owner = GetOwner())
			{
				Owner->SetLifeSpan(DestroyDelay);
			}
		}
	}

#if WITH_EDITOR
	if (bShowDebug)
	{
		AActor* Owner = GetOwner();
		if (Owner)
		{
			UE_LOG(LogDynamicPayload, Display,
				TEXT("[%s] StructuralState -> %s (Health: %.1f / %.1f)"),
				*Owner->GetName(),
				*UEnum::GetValueAsString(StructuralState),
				CurrentHealth,
				MaxHealth
			);
		}
	}
#endif
}
