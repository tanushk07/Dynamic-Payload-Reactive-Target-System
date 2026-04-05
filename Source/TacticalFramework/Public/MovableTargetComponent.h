#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MovableTargetComponent.generated.h"

class APatrolAreaVolume;
class UTargetBehaviorComponent;
class USplineComponent;

UENUM(BlueprintType)
enum class ETargetMovementMode : uint8
{
	None,
	PatrolSpline,
	PatrolArea,
	ConvoyFollow
};

UENUM(BlueprintType)
enum class EPatrolAreaState : uint8
{
	Idle,
	Moving,
	Waiting
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TACTICALFRAMEWORK_API UMovableTargetComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMovableTargetComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/* ========================================================================
	   MOVEMENT CONFIGURATION
	   ======================================================================== */

	UPROPERTY(EditAnywhere, Category = "Movement")
	float BaseSpeed = 300.f;

	UPROPERTY(EditAnywhere, Category = "Movement")
	ETargetMovementMode MovementMode = ETargetMovementMode::None;

	UPROPERTY(EditAnywhere, Category = "Movement")
	float YawTurnSpeed = 120.f;

	/* ========================================================================
	   SPLINE PATROL
	   ======================================================================== */

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Movement|Spline")
	AActor* PatrolSplineActor = nullptr;

	/* ========================================================================
	   PATROL AREA (Realistic Turning)
	   ======================================================================== */

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Movement|PatrolArea")
	APatrolAreaVolume* PatrolArea = nullptr;

	UPROPERTY(EditAnywhere, Category = "Movement|PatrolArea")
	float ArrivalTolerance = 50.f;

	UPROPERTY(EditAnywhere, Category = "Movement|PatrolArea")
	float WaitTimeAtPoint = 2.f;

	/** Minimum turning radius in cm. Determines arc curvature.
		Typical: 500 (car), 800 (truck), 1200 (semi-trailer) */
	UPROPERTY(EditAnywhere, Category = "Movement|PatrolArea",
		meta = (ClampMin = "100.0"))
	float MinTurningRadius = 800.f;

	UPROPERTY(EditAnywhere, Category = "Movement|PatrolArea",
		meta = (ClampMin = "10.0"))
	float MinPatrolSpeed = 40.f;

	UPROPERTY(EditAnywhere, Category = "Movement|PatrolArea",
		meta = (ClampMin = "0.0"))
	float MinWaypointDistance = 0.f;

	UPROPERTY(EditAnywhere, Category = "Movement|PatrolArea",
		meta = (ClampMin = "1.0"))
	float OrbitTimeoutDuration = 8.f;

	/* ========================================================================
	   CONVOY FOLLOWING (Distance-Maintaining)
	   ======================================================================== */

	UPROPERTY(EditInstanceOnly, Category = "Movement|Convoy")
	AActor* ConvoyLeader = nullptr;

	UPROPERTY(EditAnywhere, Category = "Movement|Convoy")
	float FollowDistance = 500.f;

	UPROPERTY(EditAnywhere, Category = "Movement|Convoy",
		meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ConvoyFollowStiffness = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Movement|Convoy",
		meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float ConvoyMaxSpeedMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Movement|Convoy",
		meta = (ClampMin = "50.0"))
	float MinConvoyFollowDistance = 200.f;

	UPROPERTY(EditAnywhere, Category = "Movement|Convoy")
	float ConvoyForwardTraceRange = 600.f;

	/* ========================================================================
	   GROUND ALIGNMENT
	   ======================================================================== */

	UPROPERTY(EditAnywhere, Category = "Grounding")
	bool bUseGroundAlignment = true;

	UPROPERTY(EditAnywhere, Category = "Grounding")
	float GroundOffset = 0.f;

	UPROPERTY(EditAnywhere, Category = "Grounding")
	float GroundAlignInterpSpeed = 6.f;

	UPROPERTY(EditAnywhere, Category = "Grounding")
	float FrontTraceOffset = 200.f;

	UPROPERTY(EditAnywhere, Category = "Grounding")
	float RearTraceOffset = 200.f;

	UPROPERTY(EditAnywhere, Category = "Grounding")
	float TraceHeight = 1000.f;

	/* ========================================================================
	   PUBLIC API
	   ======================================================================== */

	UFUNCTION(BlueprintCallable, Category = "Movement")
	float GetDistanceAlongSpline() const { return DistanceAlongSpline; }

	UFUNCTION(BlueprintCallable, Category = "Movement")
	USplineComponent* ResolveActiveSpline() const;

	/** Recalculates cached bounds from mesh. Call after mesh swap. */
	void CacheOwnerBounds();

private:
	/* ========================================================================
	   INTERNAL STATE
	   ======================================================================== */

	UPROPERTY()
	UTargetBehaviorComponent* BehaviorComponent;

	float SpeedMultiplier = 1.0f;
	bool bCanMove = true;

	USplineComponent* PatrolSpline = nullptr;
	float DistanceAlongSpline = 0.f;
	bool bOnSpline = false;

	EPatrolAreaState PatrolState = EPatrolAreaState::Idle;
	FVector CurrentDestination = FVector::ZeroVector;
	float WaitTimeRemaining = 0.f;
	float OrbitTimer = 0.f;
	float LastDistanceToTarget = 0.f;

	float ConvoyDistanceAlongSpline = 0.f;
	bool bConvoyInitialized = false;
	bool bConvoyOnSpline = false;

	float DesiredMovementYaw = 0.f;

	float CachedLateralExtent = 100.f;
	float CachedLongitudinalExtent = 200.f;
	float SmoothedZ = 0.f;
	bool bZInitialized = false;

	/* ========================================================================
	   MOVEMENT IMPLEMENTATION
	   ======================================================================== */

	void ArcSteerToward(float DeltaTime, const FVector& TargetLocation, float Speed, bool bApplyTurnPenalty = true);

	void InitializeSplineMovement();
	void MoveAlongSpline(float DeltaTime);

	void MoveInPatrolArea(float DeltaTime);
	void UpdatePatrolMovement(float DeltaTime, float Speed);
	float CalculateTurnAngle(const FVector& CurrentDir, const FVector& TargetDir) const;

	void MoveInConvoy(float DeltaTime);
	bool FindRootSpline(USplineComponent*& OutSpline, float& OutDistance);
	float CalculateCumulativeFollowDistance() const;
	void InitializeConvoyPosition(USplineComponent* Spline, float DesiredDistance);
	void UpdateConvoyMovement(float DeltaTime, USplineComponent* Spline,
		float DesiredDistance, float SplineLength);
	void DrawConvoyDebug(USplineComponent* Spline, float DesiredDistance,
		const FVector& CurrentLoc, const FVector& Tangent, float Gap);
	float GetForwardClearance() const;

	void AlignToGround(float DeltaTime);
	bool TraceGround(const FVector& Origin, const FVector& Offset, FHitResult& OutHit);
	void ApplyGroundAlignment(const FHitResult& FrontHit, const FHitResult& RearHit,
		const FHitResult& LeftHit, const FHitResult& RightHit, float DeltaTime);

	UFUNCTION()
	void HandleBehaviorUpdated(float NewSpeedMultiplier, bool bNewCanMove);

	void ValidateConvoySetup();
	void AutoChainDuplicateFollowers();
};
