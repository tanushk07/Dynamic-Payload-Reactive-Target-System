#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "Sound/SoundBase.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraShakeBase.h"
#include "Explosive.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnExplosionTriggered,
	FVector, ExplosionLocation,
	float, ExplosionRadius
);

UCLASS()
class TACTICALFRAMEWORK_API AExplosive : public AActor
{
	GENERATED_BODY()

public:
	AExplosive();

protected:
	virtual void BeginPlay() override;

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
	bool HasLineOfSight(AActor* Target) const;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion|SI")
	float DestroyDelay_s = 0.2f;

	/* ================= Damage Parameters (SI) ================= */

	UPROPERTY(EditAnywhere, Category = "Damage|SI")
	float MaxDamage = 1.0f;

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
