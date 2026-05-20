#include "Explosive.h"
#include "DamagableComponent.h"
#include "DynamicPayloadSystemModule.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Camera/CameraShakeBase.h"
#include "Kismet/GameplayStatics.h"
#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#endif
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

AExplosive::AExplosive()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	ExplosionVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ExplosionFX"));
	ExplosionVFX->SetupAttachment(RootComponent);
	// UNiagaraComponent defaults to bAutoActivate=true — without disabling it,
	// the FX plays at spawn time before Explode() and again on the explicit
	// Activate() in PlayExplosionEffects, or it plays at all for editor-placed
	// explosives that have bExplodeOnBeginPlay=false.
	ExplosionVFX->SetAutoActivate(false);
}

void AExplosive::BeginPlay()
{
	Super::BeginPlay();

	if (!bExplodeOnBeginPlay)
	{
		return;
	}

#if WITH_EDITOR
	DrawExplosionDebug();
#endif
	Explode();
}

void AExplosive::Explode()
{
	// Re-entry guard — Explode is BlueprintCallable and could be invoked twice
	// (BeginPlay + manual BP call, or two damage reactions).
	if (bHasDetonated)
	{
		return;
	}
	bHasDetonated = true;

	const FVector ExplosionLocation = GetActorLocation();
#if WITH_EDITOR
	UE_LOG(LogDynamicPayload, Warning, TEXT("=== EXPLOSION TRIGGERED at %s ==="), *ExplosionLocation.ToString());
#endif

	TArray<FOverlapResult> Overlaps = ScanForTargets();

	if (Overlaps.Num() > 0)
	{
		ApplyPhysicsImpulses(Overlaps);
		ApplyDamageToActors(Overlaps);
	}

	PlayExplosionEffects();
	SetLifeSpan(DestroyDelay_s);
}

TArray<FOverlapResult> AExplosive::ScanForTargets() const
{
	TArray<FOverlapResult> LocalOverlaps;
	UWorld* World = GetWorld();
	if (!World) return LocalOverlaps;

	FCollisionShape Sphere =
		FCollisionShape::MakeSphere(MetersToUU(FMath::Max(ExplosionRadius_m, OuterRadius_m)));

	FCollisionObjectQueryParams Params;
	Params.AddObjectTypesToQuery(ECC_PhysicsBody);
	Params.AddObjectTypesToQuery(ECC_Pawn);
	Params.AddObjectTypesToQuery(ECC_WorldDynamic);

	World->OverlapMultiByObjectType(
		LocalOverlaps,
		GetActorLocation(),
		FQuat::Identity,
		Params,
		Sphere
	);

	return LocalOverlaps;
}

void AExplosive::ApplyPhysicsImpulses(const TArray<FOverlapResult>& Overlaps)
{
	for (const FOverlapResult& Result : Overlaps)
	{
		UPrimitiveComponent* Comp = Result.GetComponent();
		if (Comp && Comp->IsSimulatingPhysics())
		{
			Comp->AddRadialImpulse(
				GetActorLocation(),
				MetersToUU(ExplosionRadius_m),
				ExplosionImpulse_Ns,
				Falloff,
				true
			);
		}
	}
}

void AExplosive::ApplyDamageToActors(const TArray<FOverlapResult>& Overlaps)
{
	TSet<AActor*> ProcessedActors;

	for (const FOverlapResult& Result : Overlaps)
	{
		AActor* HitActor = Result.GetActor();
		if (!HitActor || HitActor == this || ProcessedActors.Contains(HitActor))
			continue;

		UDamagableComponent* DamageComp =
			HitActor->FindComponentByClass<UDamagableComponent>();
		if (!DamageComp) continue;

		const float FinalDamage =
			CalculateFinalDamage(HitActor, GetActorLocation());

		if (FinalDamage > MinDamageToApply)
		{
			// InstigatorController (rather than nullptr) so kill credit, AI
			// perception, and scoring systems can attribute the damage to the
			// pawn that originally dropped the payload.
			UGameplayStatics::ApplyDamage(
				HitActor,
				FinalDamage,
				GetInstigatorController(),
				this,
				DamageType
			);
		}

		ProcessedActors.Add(HitActor);
	}
}

float AExplosive::CalculateFinalDamage(AActor* Victim, const FVector& ExplosionPos)
{
	FVector ClosestPoint = Victim->GetActorLocation();
	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(
		Victim->GetRootComponent()
	);
	if (!PrimComp)
	{
		PrimComp = Victim->FindComponentByClass<UPrimitiveComponent>();
	}
	if (PrimComp)
	{
		PrimComp->GetClosestPointOnCollision(ExplosionPos, ClosestPoint);
	}

	const float Distance = FVector::Dist(ExplosionPos, ClosestPoint);

	const float InnerUU = MetersToUU(InnerRadius_m);
	const float OuterUU = MetersToUU(OuterRadius_m);

	float DistanceFactor = 1.f;
	if (Distance > InnerUU)
	{
		if (OuterUU <= InnerUU)
		{
			DistanceFactor = 0.f;
		}
		else
		{
			DistanceFactor = 1.f - FMath::Clamp(
				(Distance - InnerUU) / (OuterUU - InnerUU),
				0.f, 1.f
			);
		}
	}

	const FVector ToTarget = ClosestPoint - ExplosionPos;
	const float DirectionalFactor = ComputeDirectionalFactor(ToTarget);
	// Pass Victim so the trace ignores the target's own collision — otherwise
	// the line ends on the target's hull, reports "blocked", and every visible
	// target silently gets 0.3x damage.
	const float VisibilityFactor = HasLineOfSight(ClosestPoint, Victim) ? 1.f : 0.3f;

	const float BlastDamage =
		MaxDamage * DistanceFactor * VisibilityFactor;

	const float ShrapnelDamage =
		MaxDamage * 0.4f * DirectionalFactor * VisibilityFactor;

	return BlastDamage + ShrapnelDamage;
}

void AExplosive::PlayExplosionEffects()
{
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			ExplosionSound,
			GetActorLocation()
		);
	}
	if (ExplosionVFX && ExplosionVFX->GetAsset())
	{
		ExplosionVFX->Activate(true);
	}

	// Camera shake: if user assigned a CameraShake class on the Blueprint, trigger it automatically
	if (CameraShake)
	{
		UGameplayStatics::PlayWorldCameraShake(
			this,
			CameraShake,
			GetActorLocation(),
			0.f,
			MaxShakeRadius
		);
	}

	// Broadcast delegate for game code to react with custom logic
	OnExplosionTriggered.Broadcast(GetActorLocation(), ExplosionRadius_m);
}

float AExplosive::ComputeDirectionalFactor(const FVector& ToTarget) const
{
	const FVector Forward = GetActorForwardVector();
	const FVector Dir = ToTarget.GetSafeNormal();
	const float Dot = FVector::DotProduct(Forward, Dir);
	return FMath::Lerp(0.85f, 1.0f, (Dot + 1.f) * 0.5f);
}

bool AExplosive::HasLineOfSight(const FVector& TargetPoint, const AActor* IgnoreActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		// Defensive: assume visible if no world (only reachable during teardown).
		return true;
	}

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	if (IgnoreActor)
	{
		Params.AddIgnoredActor(IgnoreActor);
	}

	const bool bBlocked = World->LineTraceSingleByChannel(
		Hit,
		GetActorLocation(),
		TargetPoint,
		ECC_Visibility,
		Params
	);

	return !bBlocked;
}

void AExplosive::DrawExplosionDebug() const
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World) return;

	const FVector Center = GetActorLocation();

	DrawDebugSphere(World, Center, MetersToUU(ExplosionRadius_m), 32, FColor::Red, false, 2.0f);
	DrawDebugSphere(World, Center, MetersToUU(InnerRadius_m), 32, FColor::Green, false, 2.0f);
	DrawDebugSphere(World, Center, MetersToUU(OuterRadius_m), 32, FColor::Yellow, false, 2.0f);
#endif
}
