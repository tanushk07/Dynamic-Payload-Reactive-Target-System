#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EStructuralState.h"
#include "TargetActor.generated.h"

class UDamagableComponent;
class UTargetBehaviorComponent;
class UMovableTargetComponent;

UCLASS()
class DYNAMICPAYLOADSYSTEM_API ATargetActor : public AActor
{
	GENERATED_BODY()

public:
	ATargetActor();

protected:
	virtual void BeginPlay() override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UDamagableComponent* DamagableComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UTargetBehaviorComponent* BehaviorComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UMovableTargetComponent* MovementComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* GroundFrame;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Visuals")
	UStaticMesh* IntactMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Visuals")
	UStaticMesh* DamagedMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage|Visuals")
	UStaticMesh* DestroyedMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission")
	bool bIsMissionTarget = true;

	/** World transform this target had at BeginPlay.
	 *
	 *  A destroyed target despawns, so reviving it is not enough - the mission
	 *  manager has to rebuild it, and it needs the ORIGINAL transform to do so.
	 *  Sampling the actor later would capture wherever a convoy had driven to
	 *  by then, which is not where the mission started. */
	UFUNCTION(BlueprintPure, Category = "Mission")
	FTransform GetInitialTransform() const { return InitialTransform; }

protected:
	UFUNCTION()
	void HandleStructuralStateChanged(EStructuralState NewState);

	FTransform InitialTransform;

	/* Captured at BeginPlay so a revived target is restored to how THIS actor
	   was configured, rather than to hardcoded defaults that would silently
	   override a Blueprint shipping different collision or tick settings. */
	TEnumAsByte<ECollisionEnabled::Type> InitialCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	bool bInitialActorTickEnabled = false;
	bool bInitialMovementTickEnabled = true;
};
