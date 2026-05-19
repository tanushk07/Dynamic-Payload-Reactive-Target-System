#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PayloadAttachmentComponent.generated.h"

class APayload;
class APayloadMissionManager;
class UPhysicsConstraintComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnPayloadStateChanged,
	bool, bPayloadAttached
);

UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class DYNAMICPAYLOADSYSTEM_API UPayloadAttachmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPayloadAttachmentComponent();

	// ================= Configuration =================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload")
	TSubclassOf<APayload> PayloadClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload")
	FVector AttachOffset = FVector(0, 0, -30);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload")
	bool bAutoSpawnOnBeginPlay = false;

	/** Kamikaze mode: owner explodes payload on collision with damageable target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload")
	bool bKamikazeMode = false;

	/** Name of the child mesh component used as kamikaze trigger (e.g. "CopperPin") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload", meta = (EditCondition = "bKamikazeMode"))
	FName KamikazeTriggerMeshName = TEXT("CopperPin");

	/** Minimum speed (m/s) for kamikaze explosion to trigger */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload", meta = (EditCondition = "bKamikazeMode", ClampMin = "0"))
	float MinKamikazeSpeed_ms = 5.0f;

	// ================= Dangling Physics =================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload|Dangling")
	bool bEnableDanglingPhysics = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload|Dangling", meta = (EditCondition = "bEnableDanglingPhysics", ClampMin = "0", ClampMax = "90"))
	float SwingAngleLimit = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload|Dangling", meta = (EditCondition = "bEnableDanglingPhysics"))
	float PayloadCrossSection_cm2 = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload|Dangling", meta = (EditCondition = "bEnableDanglingPhysics"))
	bool bTransferVelocityOnDetach = true;

	// ================= State =================

	UPROPERTY(BlueprintReadOnly, Category = "Payload")
	APayload* AttachedPayload = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Payload")
	float CachedPayloadMass = 0.0f;

	UPROPERTY()
	UPhysicsConstraintComponent* PayloadConstraint = nullptr;

	// ================= Events =================

	/** Fired when payload is attached or detached. Game code can react (e.g. update UI, adjust physics). */
	UPROPERTY(BlueprintAssignable, Category = "Payload")
	FOnPayloadStateChanged OnPayloadStateChanged;

	// ================= Functions =================

	UFUNCTION(BlueprintCallable, Category = "Payload")
	void SpawnAndAttachPayload();

	UFUNCTION(BlueprintCallable, Category = "Payload")
	void DetachPayload();

	UFUNCTION(BlueprintPure, Category = "Payload")
	float GetPayloadMass() const { return CachedPayloadMass; }

	UFUNCTION(BlueprintPure, Category = "Payload")
	FVector GetPayloadOffset() const { return AttachOffset; }

	UFUNCTION(BlueprintPure, Category = "Payload")
	bool HasPayload() const { return AttachedPayload != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Payload")
	APayload* GetAttachedPayload() const { return AttachedPayload; }

	UFUNCTION(BlueprintPure, Category = "Payload")
	bool IsDanglingEnabled() const { return bEnableDanglingPhysics && AttachedPayload != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Payload|Dangling")
	FVector GetPayloadWorldPosition() const;

	UFUNCTION(BlueprintPure, Category = "Payload|Dangling")
	FVector GetPayloadLocalOffset() const;

	UFUNCTION(BlueprintPure, Category = "Payload|Dangling")
	float GetPayloadCrossSection_m2() const { return PayloadCrossSection_cm2 * 0.0001f; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnKamikazeOverlap(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

private:
	UPrimitiveComponent* GetOwnerRootMesh() const;
	void CreatePhysicsConstraint();
	void DestroyPhysicsConstraint();

	UPROPERTY()
	UPrimitiveComponent* KamikazeTriggerMesh = nullptr;

	UPROPERTY()
	APayloadMissionManager* CachedMissionManager = nullptr;
};
