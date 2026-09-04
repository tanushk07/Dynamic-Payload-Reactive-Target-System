#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Payload.generated.h"

class UStaticMeshComponent;
class AExplosive;
class APayloadMissionManager;

UCLASS()
class DYNAMICPAYLOADSYSTEM_API APayload : public AActor
{
	GENERATED_BODY()

public:
	APayload();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Payload")
	UStaticMeshComponent* PayloadMesh;

	UPROPERTY(EditAnywhere, Category = "Explosion")
	TSubclassOf<AExplosive> ExplosionClass;

	bool bHasExploded = false;
	bool bIsArmed = false;
	FTimerHandle FuseTimerHandle;

	UPROPERTY()
	APayloadMissionManager* CachedMissionManager = nullptr;

	/** If true, the armed payload detonates on physical contact with any actor
	 *  that has a UDamagableComponent (in addition to the fuse timer). */
	UPROPERTY(EditAnywhere, Category = "Explosion")
	bool bExplodeOnHit = false;

public:
	void Explode(const FVector& ExplosionLocation);

	UPROPERTY(EditAnywhere, Category = "Explosion")
	float FuseTime = 3.0f;

	UFUNCTION()
	void Arm();

	void OnFuseExpired();

	UFUNCTION()
	void OnPayloadHit(
		UPrimitiveComponent* HitComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit
	);

	/** Toggle debug logging for payload arm/fuse/impact events (editor only). */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bShowDebug = false;
};
