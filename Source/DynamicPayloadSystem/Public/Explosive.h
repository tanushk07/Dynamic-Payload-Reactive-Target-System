#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "Engine/OverlapResult.h"
#include "Explosive.generated.h"

class UNiagaraComponent;
class USoundBase;
class UCameraShakeBase;
class UDamageType;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnExplosionTriggered,
	FVector, ExplosionLocation,
	float, ExplosionRadius
);

UCLASS()
class DYNAMICPAYLOADSYSTEM_API AExplosive : public AActor
{
	GENERATED_BODY()

public:
	AExplosive();

protected:
	virtual void BeginPlay() override;

	/** Re-entry guard. Explode() is BlueprintCallable, so Blueprint code can
	 *  call it externally — without this guard, double-call applies damage twice. */
	bool bHasDetonated = false;

	/* ================= Root & FX ================= */

	UPROPERTY(VisibleAnywhere, Category = "Explosion")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, Category = "Explosion")
	UNiagaraComponent* ExplosionVFX;

	UPROPERTY(EditAnywhere, Category = "Explosion")
	USoundBase* ExplosionSound;

	/* ================= Core Logic ================= */

	UFUNCTION(BlueprintCallable, Category = "Explosion")
	void Explode();

	TArray<FOverlapResult> ScanForTargets() const;
	void ApplyPhysicsImpulses(const TArray<FOverlapResult>& Overlaps);
	void ApplyDamageToActors(const TArray<FOverlapResult>& Overlaps);
	void PlayExplosionEffects();
	void DrawExplosionDebug() const;

	float CalculateFinalDamage(AActor* Victim, const FVector& ExplosionPos);
	float ComputeDirectionalFactor(const FVector& ToTarget) const;
	/** Trace from this actor's location to TargetPoint, ignoring this actor and
	 *  optionally an additional actor (typically the victim, so the trace doesn't
	 *  self-hit the target's collision when the endpoint is on its surface). */
	bool HasLineOfSight(const FVector& TargetPoint, const AActor* IgnoreActor = nullptr) const;

	/* ================= Unit Conversion ================= */

	FORCEINLINE float MetersToUU(float Meters) const
	{
		return Meters * 100.f;
	}

public:

	/* ================= Explosion Parameters (SI) ================= */

	UPROPERTY(EditAnywhere, Category = "Explosion|SI")
	float ExplosionRadius_m = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Explosion|SI")
	float ExplosionImpulse_Ns = 3000.0f;

	UPROPERTY(EditAnywhere, Category = "Explosion")
	TEnumAsByte<ERadialImpulseFalloff> Falloff = ERadialImpulseFalloff::RIF_Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|SI",
		meta = (ClampMin = "0.1",
			ToolTip = "Seconds the actor lives after detonation. Must outlast your Niagara FX or the VFX will pop."))
	float DestroyDelay_s = 3.0f;

	/** If true, detonates automatically on BeginPlay. Disabled by default so designers
	 *  can preview/place explosives in a level without them firing. APayload sets this
	 *  to true on spawned instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	bool bExplodeOnBeginPlay = false;

	/* ================= Damage Parameters (SI) ================= */

	UPROPERTY(EditAnywhere, Category = "Damage|SI")
	float MaxDamage = 10.0f;

	/** Damage values below this threshold are not applied. Prevents spamming
	 *  ApplyDamage with sub-noise values for distant targets. Was a hard-coded
	 *  1.0 before — now configurable. */
	UPROPERTY(EditAnywhere, Category = "Damage|SI",
		meta = (ClampMin = "0.0"))
	float MinDamageToApply = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Damage|SI")
	float InnerRadius_m = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Damage|SI")
	float OuterRadius_m = 6.0f;

	UPROPERTY(EditAnywhere, Category = "Damage")
	TSubclassOf<UDamageType> DamageType;

	/* ================= Camera Shake ================= */

	/** Optional camera shake class. If set, triggers automatically via PlayWorldCameraShake. */
	UPROPERTY(EditAnywhere, Category = "Explosion|Camera")
	TSubclassOf<UCameraShakeBase> CameraShake;

	UPROPERTY(EditAnywhere, Category = "Explosion|Camera")
	float MaxShakeRadius = 2000.f;

	/* ================= Events ================= */

	/** Broadcast when this explosive detonates. Game code can subscribe for custom reactions. */
	UPROPERTY(BlueprintAssignable, Category = "Explosion")
	FOnExplosionTriggered OnExplosionTriggered;
};
