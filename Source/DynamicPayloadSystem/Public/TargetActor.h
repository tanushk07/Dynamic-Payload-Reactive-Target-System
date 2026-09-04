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

protected:
	UFUNCTION()
	void HandleStructuralStateChanged(EStructuralState NewState);
};
