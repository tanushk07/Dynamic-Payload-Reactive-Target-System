#include "MovableTargetComponent.h"
#include "TargetBehaviorComponent.h"
#include "PatrolAreaVolume.h"
#include "TargetActor.h"
#include "Components/SplineComponent.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#endif

UMovableTargetComponent::UMovableTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMovableTargetComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner && Owner->GetRootComponent() &&
		Owner->GetRootComponent()->Mobility != EComponentMobility::Movable)
	{
#if WITH_EDITOR
		UE_LOG(LogTemp, Warning,
			TEXT("[Movement] %s has non-Movable root — disabling MovableTargetComponent"),
			*Owner->GetName());
#endif
		SetComponentTickEnabled(false);
		return;
	}

	CacheOwnerBounds();
	SmoothedZ = GetOwner()->GetActorLocation().Z;
	bZInitialized = false;
	DesiredMovementYaw = GetOwner()->GetActorRotation().Yaw;

	BehaviorComponent = GetOwner()->FindComponentByClass<UTargetBehaviorComponent>();
	if (BehaviorComponent)
	{
		SpeedMultiplier = BehaviorComponent->SpeedMultiplier;
		bCanMove = BehaviorComponent->bCanMove;
		BehaviorComponent->OnMovementCapabilityChanged.AddDynamic(
			this, &UMovableTargetComponent::HandleBehaviorUpdated
		);
	}

	if (PatrolSplineActor)
	{
		PatrolSpline = PatrolSplineActor->FindComponentByClass<USplineComponent>();
		if (PatrolSpline)
		{
			InitializeSplineMovement();
		}
	}

	if (MovementMode == ETargetMovementMode::ConvoyFollow)
	{
		ValidateConvoySetup();
		AutoChainDuplicateFollowers();
	}
}

void UMovableTargetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bCanMove)
		return;

	switch (MovementMode)
	{
	case ETargetMovementMode::PatrolSpline:
		MoveAlongSpline(DeltaTime);
		break;
	case ETargetMovementMode::PatrolArea:
		MoveInPatrolArea(DeltaTime);
		break;
	case ETargetMovementMode::ConvoyFollow:
		MoveInConvoy(DeltaTime);
		break;
	case ETargetMovementMode::None:
	default:
		return;
	}

	if (bUseGroundAlignment)
	{
		AlignToGround(DeltaTime);
	}
}

/* ============================================================================
   SHARED ARC-STEERING
   ============================================================================ */

void UMovableTargetComponent::ArcSteerToward(float DeltaTime, const FVector& TargetLocation, float Speed, bool bApplyTurnPenalty)
{
	AActor* OwnerActor = GetOwner();
	FVector CurrentLocation = OwnerActor->GetActorLocation();
	FVector ToTarget = TargetLocation - CurrentLocation;
	ToTarget.Z = 0.f;
	float Distance = ToTarget.Size2D();

	if (Distance < 0.1f)
		return;

	FVector DesiredDirection = ToTarget.GetSafeNormal2D();
	FVector CurrentForward = OwnerActor->GetActorForwardVector();
	CurrentForward.Z = 0.f;
	CurrentForward.Normalize();

	float AngleToTarget = CalculateTurnAngle(CurrentForward, DesiredDirection);
	float TurnDirection = FVector::CrossProduct(CurrentForward, DesiredDirection).Z > 0.f ? 1.f : -1.f;

	float EffectiveSpeed;
	float TurningRadius;

	if (bApplyTurnPenalty)
	{
		float SpeedFactor = FMath::Clamp(1.f - (AngleToTarget / 180.f), 0.3f, 1.f);
		EffectiveSpeed = FMath::Max(Speed * SpeedFactor, MinPatrolSpeed);
		TurningRadius = MinTurningRadius;
	}
	else
	{
		EffectiveSpeed = Speed;
		float Omega = FMath::DegreesToRadians(YawTurnSpeed);
		TurningRadius = EffectiveSpeed / FMath::Max(Omega, 0.001f);
	}

	float MoveDistance = FMath::Min(EffectiveSpeed * DeltaTime, Distance);
	float Omega = EffectiveSpeed / FMath::Max(TurningRadius, 1.f);
	float MaxTurnThisFrame = Omega * DeltaTime;
	float DesiredTurnRad = FMath::DegreesToRadians(AngleToTarget);
	float ActualTurnRad = FMath::Min(MaxTurnThisFrame, DesiredTurnRad);

	FVector NewLocation;
	float NewYaw;

	if (Distance < ArrivalTolerance)
	{
		NewLocation = CurrentLocation + DesiredDirection * MoveDistance;
		NewYaw = DesiredDirection.Rotation().Yaw;
	}
	else if (AngleToTarget > 0.5f)
	{
		float ArcMoveDistance = FMath::Min(EffectiveSpeed * DeltaTime, Distance);
		float ArcAngleRad = ArcMoveDistance / FMath::Max(TurningRadius, 1.f);

		FVector Right = OwnerActor->GetActorRightVector();
		FVector TurnCenter = CurrentLocation + (Right * TurnDirection * TurningRadius);

		float CurrentYaw = OwnerActor->GetActorRotation().Yaw;
		NewYaw = CurrentYaw + (FMath::RadiansToDegrees(ArcAngleRad) * TurnDirection);

		FVector FromCenter = CurrentLocation - TurnCenter;
		FQuat Rotation = FQuat(FVector::UpVector, ArcAngleRad * TurnDirection);
		FVector RotatedFromCenter = Rotation.RotateVector(FromCenter);
		NewLocation = TurnCenter + RotatedFromCenter;
	}
	else
	{
		NewYaw = OwnerActor->GetActorRotation().Yaw;
		NewLocation = CurrentLocation + CurrentForward * MoveDistance;
	}

	NewLocation.Z = CurrentLocation.Z;

	OwnerActor->SetActorLocation(NewLocation, false);
	DesiredMovementYaw = NewYaw;
}

/* ============================================================================
   SPLINE PATROL MOVEMENT
   ============================================================================ */

void UMovableTargetComponent::InitializeSplineMovement()
{
	FVector ActorLocation = GetOwner()->GetActorLocation();
	float InputKey = PatrolSpline->FindInputKeyClosestToWorldLocation(ActorLocation);
	DistanceAlongSpline = PatrolSpline->GetDistanceAlongSplineAtSplineInputKey(InputKey);

	FVector SplinePoint = PatrolSpline->GetLocationAtDistanceAlongSpline(
		DistanceAlongSpline, ESplineCoordinateSpace::World);
	float DistToSpline = FVector::Dist2D(ActorLocation, SplinePoint);

	bOnSpline = (DistToSpline < ArrivalTolerance);
}

void UMovableTargetComponent::MoveAlongSpline(float DeltaTime)
{
	if (!PatrolSpline)
		return;

	const float Speed = BaseSpeed * SpeedMultiplier;
	const float SplineLength = PatrolSpline->GetSplineLength();

	if (!bOnSpline)
	{
		FVector SplineTarget = PatrolSpline->GetLocationAtDistanceAlongSpline(
			DistanceAlongSpline, ESplineCoordinateSpace::World);

		ArcSteerToward(DeltaTime, SplineTarget, Speed);

		FVector CurrentLoc = GetOwner()->GetActorLocation();
		float NewInputKey = PatrolSpline->FindInputKeyClosestToWorldLocation(CurrentLoc);
		DistanceAlongSpline = PatrolSpline->GetDistanceAlongSplineAtSplineInputKey(NewInputKey);

		FVector SplinePoint = PatrolSpline->GetLocationAtDistanceAlongSpline(
			DistanceAlongSpline, ESplineCoordinateSpace::World);

		float DistToSpline = FVector::Dist2D(CurrentLoc, SplinePoint);

		if (DistToSpline < ArrivalTolerance * 2.f)
		{
			bOnSpline = true;
			FVector Tangent = PatrolSpline->GetDirectionAtDistanceAlongSpline(
				DistanceAlongSpline, ESplineCoordinateSpace::World);
			DesiredMovementYaw = Tangent.Rotation().Yaw;
		}
		return;
	}

	DistanceAlongSpline = FMath::Min(DistanceAlongSpline + Speed * DeltaTime, SplineLength);

	FVector TargetLocation = PatrolSpline->GetLocationAtDistanceAlongSpline(
		DistanceAlongSpline, ESplineCoordinateSpace::World
	);
	GetOwner()->SetActorLocation(TargetLocation);

	FVector Tangent = PatrolSpline->GetDirectionAtDistanceAlongSpline(
		DistanceAlongSpline, ESplineCoordinateSpace::World
	);
	DesiredMovementYaw = Tangent.Rotation().Yaw;
}

/* ============================================================================
   PATROL AREA MOVEMENT
   ============================================================================ */

void UMovableTargetComponent::MoveInPatrolArea(float DeltaTime)
{
	if (!PatrolArea)
		return;

	const float Speed = BaseSpeed * SpeedMultiplier;

	switch (PatrolState)
	{
	case EPatrolAreaState::Idle:
	{
		AActor* Owner = GetOwner();
		FVector Forward2D = Owner->GetActorForwardVector();
		Forward2D.Z = 0.f;
		Forward2D.Normalize();
		FVector VehicleLoc = Owner->GetActorLocation();

		float EffectiveMinDist = (MinWaypointDistance > 0.f)
			? MinWaypointDistance
			: MinTurningRadius * 2.f;

		bool bFoundGoodPoint = false;
		for (int32 i = 0; i < 10; ++i)
		{
			FVector Candidate = PatrolArea->GetRandomPointInArea();
			Candidate.Z = VehicleLoc.Z;

			float DistToCandidate = FVector::Dist2D(VehicleLoc, Candidate);
			if (DistToCandidate < EffectiveMinDist)
				continue;

			FVector ToCandidate = (Candidate - VehicleLoc).GetSafeNormal2D();
			float Dot = FVector::DotProduct(Forward2D, ToCandidate);
			if (Dot > 0.f)
			{
				CurrentDestination = Candidate;
				bFoundGoodPoint = true;
				break;
			}
			if (DistToCandidate > EffectiveMinDist * 2.f)
			{
				CurrentDestination = Candidate;
				bFoundGoodPoint = true;
				break;
			}
		}
		if (!bFoundGoodPoint)
		{
			CurrentDestination = PatrolArea->GetRandomPointInArea();
			CurrentDestination.Z = VehicleLoc.Z;
		}

		OrbitTimer = 0.f;
		LastDistanceToTarget = FVector::Dist2D(VehicleLoc, CurrentDestination);

		PatrolState = EPatrolAreaState::Moving;
		break;
	}

	case EPatrolAreaState::Moving:
#if WITH_EDITOR
		DrawDebugSphere(GetWorld(), CurrentDestination, 60.f, 12, FColor::Magenta, false, 0.f);
#endif
		UpdatePatrolMovement(DeltaTime, Speed);
		break;

	case EPatrolAreaState::Waiting:
		WaitTimeRemaining -= DeltaTime;
		if (WaitTimeRemaining <= 0.f)
		{
			PatrolState = EPatrolAreaState::Idle;
		}
		break;
	}
}

void UMovableTargetComponent::UpdatePatrolMovement(float DeltaTime, float Speed)
{
	FVector CurrentLocation = GetOwner()->GetActorLocation();
	float Distance = FVector::Dist2D(CurrentLocation, CurrentDestination);

	if (Distance <= ArrivalTolerance)
	{
		PatrolState = EPatrolAreaState::Waiting;
		WaitTimeRemaining = WaitTimeAtPoint;
		return;
	}

	OrbitTimer += DeltaTime;
	if (OrbitTimer >= OrbitTimeoutDuration)
	{
		float Progress = LastDistanceToTarget - Distance;
		if (Progress < LastDistanceToTarget * 0.2f)
		{
			PatrolState = EPatrolAreaState::Idle;
			return;
		}
		OrbitTimer = 0.f;
		LastDistanceToTarget = Distance;
	}

	ArcSteerToward(DeltaTime, CurrentDestination, Speed);
}

float UMovableTargetComponent::CalculateTurnAngle(const FVector& CurrentDir, const FVector& TargetDir) const
{
	float DotProduct = FVector::DotProduct(CurrentDir, TargetDir);
	float AngleRadians = FMath::Acos(FMath::Clamp(DotProduct, -1.f, 1.f));
	return FMath::RadiansToDegrees(AngleRadians);
}

/* ============================================================================
   CONVOY MOVEMENT
   ============================================================================ */

void UMovableTargetComponent::MoveInConvoy(float DeltaTime)
{
	if (!ConvoyLeader)
		return;

	USplineComponent* RootSpline = nullptr;
	float RootSplineDistance = 0.f;

	if (!FindRootSpline(RootSpline, RootSplineDistance))
		return;

	float CumulativeDistance = CalculateCumulativeFollowDistance();
	float DesiredDistance = RootSplineDistance - CumulativeDistance;

	float SplineLength = RootSpline->GetSplineLength();
	DesiredDistance = FMath::Clamp(DesiredDistance, 0.f, SplineLength);

	if (!bConvoyInitialized)
	{
		InitializeConvoyPosition(RootSpline, DesiredDistance);
		return;
	}

	UpdateConvoyMovement(DeltaTime, RootSpline, DesiredDistance, SplineLength);
}

bool UMovableTargetComponent::FindRootSpline(USplineComponent*& OutSpline, float& OutDistance)
{
	UMovableTargetComponent* Current = ConvoyLeader->FindComponentByClass<UMovableTargetComponent>();

	while (Current)
	{
		if (Current->MovementMode == ETargetMovementMode::PatrolSpline && Current->PatrolSpline)
		{
			OutSpline = Current->PatrolSpline;
			OutDistance = Current->GetDistanceAlongSpline();
			return true;
		}

		if (Current->MovementMode == ETargetMovementMode::ConvoyFollow && Current->ConvoyLeader)
		{
			Current = Current->ConvoyLeader->FindComponentByClass<UMovableTargetComponent>();
		}
		else
		{
			break;
		}
	}

	return false;
}

float UMovableTargetComponent::CalculateCumulativeFollowDistance() const
{
	float Total = 0.f;
	const UMovableTargetComponent* Current = this;

	while (Current && Current->MovementMode == ETargetMovementMode::ConvoyFollow)
	{
		Total += Current->FollowDistance;

		if (Current->ConvoyLeader)
		{
			UMovableTargetComponent* LeaderComp =
				Current->ConvoyLeader->FindComponentByClass<UMovableTargetComponent>();

			if (LeaderComp && LeaderComp->MovementMode == ETargetMovementMode::PatrolSpline)
				break;

			Current = LeaderComp;
		}
		else
		{
			break;
		}
	}

	return Total;
}

void UMovableTargetComponent::InitializeConvoyPosition(USplineComponent* Spline, float DesiredDistance)
{
	FVector CurrentLocation = GetOwner()->GetActorLocation();
	ConvoyDistanceAlongSpline = DesiredDistance;

	FVector SplinePoint = Spline->GetLocationAtDistanceAlongSpline(
		DesiredDistance, ESplineCoordinateSpace::World);
	float DistToSpline = FVector::Dist2D(CurrentLocation, SplinePoint);

	bConvoyOnSpline = (DistToSpline < ArrivalTolerance);
	bConvoyInitialized = true;
}

void UMovableTargetComponent::UpdateConvoyMovement(float DeltaTime, USplineComponent* Spline,
	float DesiredDistance, float SplineLength)
{
	const float Speed = BaseSpeed * SpeedMultiplier;

	float ForwardClearance = GetForwardClearance();
	float CollisionBrake = 1.f;
	if (ForwardClearance < MinConvoyFollowDistance)
	{
		CollisionBrake = FMath::Clamp(ForwardClearance / MinConvoyFollowDistance, 0.f, 1.f);
	}

	if (!bConvoyOnSpline)
	{
		FVector LeaderPos = ConvoyLeader->GetActorLocation();
		FVector CurrentLoc = GetOwner()->GetActorLocation();
		float DistToLeader = FVector::Dist2D(CurrentLoc, LeaderPos);

		float DistError = DistToLeader - FollowDistance;
		float ApproachSpeed = FMath::Clamp(
			Speed + DistError * ConvoyFollowStiffness,
			0.f,
			Speed * ConvoyMaxSpeedMultiplier
		);

		if (ApproachSpeed > 1.f)
		{
			ArcSteerToward(DeltaTime, LeaderPos, ApproachSpeed, false);
		}
		else
		{
			FVector ToLeader = (LeaderPos - CurrentLoc).GetSafeNormal2D();
			if (!ToLeader.IsNearlyZero())
			{
				DesiredMovementYaw = ToLeader.Rotation().Yaw;
			}
		}

		float NearestKey = Spline->FindInputKeyClosestToWorldLocation(GetOwner()->GetActorLocation());
		float NearestDist = Spline->GetDistanceAlongSplineAtSplineInputKey(NearestKey);
		FVector NearestSplinePoint = Spline->GetLocationAtDistanceAlongSpline(
			NearestDist, ESplineCoordinateSpace::World);

		float DistToSpline = FVector::Dist2D(GetOwner()->GetActorLocation(), NearestSplinePoint);

		if (DistToSpline < ArrivalTolerance * 2.f && DesiredDistance > ArrivalTolerance)
		{
			bConvoyOnSpline = true;
			ConvoyDistanceAlongSpline = NearestDist;
			FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(
				NearestDist, ESplineCoordinateSpace::World);
			DesiredMovementYaw = Tangent.Rotation().Yaw;
		}
		return;
	}

	float DistanceGap = DesiredDistance - ConvoyDistanceAlongSpline;
	float ClampedGap = FMath::Clamp(DistanceGap, -FollowDistance, FollowDistance);

	float SpeedAdjustment = ClampedGap * ConvoyFollowStiffness;
	float MinSpeed = (DistanceGap >= 0.f) ? Speed * 0.3f : 0.f;
	float TargetSpeed = FMath::Clamp(
		Speed + SpeedAdjustment,
		MinSpeed,
		Speed * ConvoyMaxSpeedMultiplier
	);

	ConvoyDistanceAlongSpline += TargetSpeed * DeltaTime;
	ConvoyDistanceAlongSpline = FMath::Clamp(ConvoyDistanceAlongSpline, 0.f, SplineLength);

	FVector SplineLocation = Spline->GetLocationAtDistanceAlongSpline(
		ConvoyDistanceAlongSpline, ESplineCoordinateSpace::World
	);
	FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(
		ConvoyDistanceAlongSpline, ESplineCoordinateSpace::World
	);

	GetOwner()->SetActorLocation(SplineLocation);
	DesiredMovementYaw = Tangent.Rotation().Yaw;

#if WITH_EDITOR
	DrawConvoyDebug(Spline, DesiredDistance, GetOwner()->GetActorLocation(), Tangent, DistanceGap);
#endif
}

float UMovableTargetComponent::GetForwardClearance() const
{
	AActor* OwnerActor = GetOwner();
	FVector Start = OwnerActor->GetActorLocation() + FVector(0, 0, 100.f);

	FVector Forward = OwnerActor->GetActorForwardVector();
	Forward.Z = 0.f;
	Forward.Normalize();

	FVector End = Start + Forward * ConvoyForwardTraceRange;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(OwnerActor);

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (*It != OwnerActor && It->FindComponentByClass<UMovableTargetComponent>())
		{
			Params.AddIgnoredActor(*It);
		}
	}

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
#if WITH_EDITOR
		DrawDebugLine(GetWorld(), Start, Hit.ImpactPoint, FColor::Orange, false, 0.f, 0, 3.f);
#endif
		return Hit.Distance;
	}

	return ConvoyForwardTraceRange;
}

void UMovableTargetComponent::DrawConvoyDebug(USplineComponent* Spline, float DesiredDistance,
	const FVector& CurrentLoc, const FVector& Tangent, float Gap)
{
#if WITH_EDITOR
	DrawDebugLine(GetWorld(), CurrentLoc, CurrentLoc + Tangent * 200.f,
		FColor::Cyan, false, 0.f, 0, 5.f);

	FVector DesiredLocation = Spline->GetLocationAtDistanceAlongSpline(
		DesiredDistance, ESplineCoordinateSpace::World
	);

	DrawDebugSphere(GetWorld(), DesiredLocation, 50.f, 12, FColor::Green, false, 0.f);
	DrawDebugSphere(GetWorld(), CurrentLoc, 40.f, 12, FColor::Yellow, false, 0.f);

	DrawDebugLine(GetWorld(), CurrentLoc, DesiredLocation,
		Gap > 0.f ? FColor::Red : FColor::Blue, false, 0.f, 0, 2.f);
#endif
}

/* ============================================================================
   GROUND ALIGNMENT
   ============================================================================ */

void UMovableTargetComponent::CacheOwnerBounds()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
		return;

	if (ATargetActor* TargetActor = Cast<ATargetActor>(OwnerActor))
	{
		if (TargetActor->Mesh && TargetActor->Mesh->GetStaticMesh())
		{
			FBoxSphereBounds LocalBounds = TargetActor->Mesh->GetStaticMesh()->GetBounds();
			FVector Scale = TargetActor->Mesh->GetComponentScale();
			float ScaledX = LocalBounds.BoxExtent.X * Scale.X;
			float ScaledY = LocalBounds.BoxExtent.Y * Scale.Y;

			CachedLongitudinalExtent = FMath::Max(FMath::Max(ScaledX, ScaledY), 50.f);
			CachedLateralExtent = FMath::Max(FMath::Min(ScaledX, ScaledY), 30.f);

			return;
		}
	}

	FBox ActorBounds = OwnerActor->GetComponentsBoundingBox(true);
	FVector Extents = ActorBounds.GetExtent();
	CachedLongitudinalExtent = FMath::Max(FMath::Max(Extents.X, Extents.Y), 50.f);
	CachedLateralExtent = FMath::Max(FMath::Min(Extents.X, Extents.Y), 30.f);
}

void UMovableTargetComponent::AlignToGround(float DeltaTime)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
		return;

	FVector ActorLocation = OwnerActor->GetActorLocation();
	FVector Forward = OwnerActor->GetActorForwardVector();
	FVector Right = OwnerActor->GetActorRightVector();

	FHitResult FrontHit, RearHit, LeftHit, RightHit;

	FVector FrontOffset = Forward * FrontTraceOffset;
	FVector RearOffset = -Forward * RearTraceOffset;
	FVector LeftOffset = -Right * CachedLateralExtent;
	FVector RightOffset = Right * CachedLateralExtent;

	bool bFront = TraceGround(ActorLocation, FrontOffset, FrontHit);
	bool bRear = TraceGround(ActorLocation, RearOffset, RearHit);
	bool bLeft = TraceGround(ActorLocation, LeftOffset, LeftHit);
	bool bRight = TraceGround(ActorLocation, RightOffset, RightHit);

	if (!bFront || !bRear)
		return;

	float SumZ = FrontHit.ImpactPoint.Z + RearHit.ImpactPoint.Z;
	int HitCount = 2;

	if (bLeft)
	{
		SumZ += LeftHit.ImpactPoint.Z;
		HitCount++;
	}
	if (bRight)
	{
		SumZ += RightHit.ImpactPoint.Z;
		HitCount++;
	}

	float TargetZ = (SumZ / HitCount) + GroundOffset;

	if (!bZInitialized || FMath::Abs(SmoothedZ - TargetZ) > 500.f)
	{
		SmoothedZ = TargetZ;
		bZInitialized = true;
	}
	else
	{
		SmoothedZ = FMath::FInterpTo(SmoothedZ, TargetZ, DeltaTime, GroundAlignInterpSpeed);
	}

	ActorLocation.Z = SmoothedZ;
	OwnerActor->SetActorLocation(ActorLocation, false, nullptr, ETeleportType::TeleportPhysics);

	float CurrentYaw = OwnerActor->GetActorRotation().Yaw;
	float SmoothedYaw = FMath::FixedTurn(CurrentYaw, DesiredMovementYaw, YawTurnSpeed * DeltaTime);
	OwnerActor->SetActorRotation(FRotator(0, SmoothedYaw, 0));

	if (bLeft && bRight)
	{
		ApplyGroundAlignment(FrontHit, RearHit, LeftHit, RightHit, DeltaTime);
	}
	else
	{
		FHitResult DummyLeft = FrontHit;
		FHitResult DummyRight = FrontHit;
		ApplyGroundAlignment(FrontHit, RearHit, DummyLeft, DummyRight, DeltaTime);
	}
}

bool UMovableTargetComponent::TraceGround(const FVector& Origin, const FVector& Offset, FHitResult& OutHit)
{
	FVector TraceStart = Origin + Offset + FVector(0, 0, TraceHeight);
	FVector TraceEnd = Origin + Offset - FVector(0, 0, TraceHeight);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());

	bool bHit = GetWorld()->LineTraceSingleByChannel(
		OutHit, TraceStart, TraceEnd, ECC_WorldStatic, Params
	);

#if WITH_EDITOR
	FColor TraceColor = bHit ? FColor::Green : FColor::Red;
	DrawDebugLine(GetWorld(), TraceStart, bHit ? OutHit.ImpactPoint : TraceEnd,
		TraceColor, false, 0.f, 0, 2.f);
	if (bHit)
	{
		DrawDebugSphere(GetWorld(), OutHit.ImpactPoint, 10.f, 4, FColor::Yellow, false, 0.f);
	}
#endif

	return bHit;
}

void UMovableTargetComponent::ApplyGroundAlignment(
	const FHitResult& FrontHit, const FHitResult& RearHit,
	const FHitResult& LeftHit, const FHitResult& RightHit, float DeltaTime)
{
	float PitchDelta = FrontHit.ImpactPoint.Z - RearHit.ImpactPoint.Z;
	float PitchDistance = FrontTraceOffset + RearTraceOffset;
	float Pitch = FMath::RadiansToDegrees(FMath::Atan2(PitchDelta, PitchDistance));

	float RollDelta = LeftHit.ImpactPoint.Z - RightHit.ImpactPoint.Z;
	float RollDistance = CachedLateralExtent * 2.f;
	float Roll = FMath::RadiansToDegrees(FMath::Atan2(RollDelta, FMath::Max(RollDistance, 1.f)));

	if (ATargetActor* TargetActor = Cast<ATargetActor>(GetOwner()))
	{
		if (USceneComponent* GroundFrame = TargetActor->GroundFrame)
		{
			FRotator Current = GroundFrame->GetRelativeRotation();
			FRotator Target(Pitch, 0.f, Roll);
			GroundFrame->SetRelativeRotation(
				FMath::RInterpTo(Current, Target, DeltaTime, GroundAlignInterpSpeed)
			);
		}
	}
}

/* ============================================================================
   UTILITIES
   ============================================================================ */

void UMovableTargetComponent::HandleBehaviorUpdated(float NewSpeedMultiplier, bool bNewCanMove)
{
	SpeedMultiplier = NewSpeedMultiplier;
	bCanMove = bNewCanMove;
}

USplineComponent* UMovableTargetComponent::ResolveActiveSpline() const
{
	if (PatrolSpline)
		return PatrolSpline;

	if (MovementMode == ETargetMovementMode::ConvoyFollow && ConvoyLeader)
	{
		if (UMovableTargetComponent* LeaderMove =
			ConvoyLeader->FindComponentByClass<UMovableTargetComponent>())
		{
			return LeaderMove->ResolveActiveSpline();
		}
	}

	return nullptr;
}

void UMovableTargetComponent::ValidateConvoySetup()
{
	AActor* OwnerActor = GetOwner();

	if (!ConvoyLeader)
	{
#if WITH_EDITOR
		UE_LOG(LogTemp, Error,
			TEXT("[Convoy] %s: No ConvoyLeader assigned"), *OwnerActor->GetName());
#endif
		return;
	}

	if (ConvoyLeader == OwnerActor)
	{
#if WITH_EDITOR
		UE_LOG(LogTemp, Error,
			TEXT("[Convoy] %s: Cannot follow itself"), *OwnerActor->GetName());
#endif
		ConvoyLeader = nullptr;
		return;
	}

	if (PatrolSplineActor || PatrolArea)
	{
		PatrolSplineActor = nullptr;
		PatrolSpline = nullptr;
		PatrolArea = nullptr;
	}
}

void UMovableTargetComponent::AutoChainDuplicateFollowers()
{
	if (!ConvoyLeader || MovementMode != ETargetMovementMode::ConvoyFollow)
		return;

	AActor* OwnerActor = GetOwner();

	TArray<AActor*> SameLeaderFollowers;

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* OtherActor = *It;
		if (OtherActor == OwnerActor)
			continue;

		UMovableTargetComponent* OtherMove = OtherActor->FindComponentByClass<UMovableTargetComponent>();
		if (OtherMove &&
			OtherMove->MovementMode == ETargetMovementMode::ConvoyFollow &&
			OtherMove->ConvoyLeader == ConvoyLeader)
		{
			SameLeaderFollowers.Add(OtherActor);
		}
	}

	if (SameLeaderFollowers.Num() == 0)
		return;

	FVector LeaderLoc = ConvoyLeader->GetActorLocation();
	SameLeaderFollowers.Add(OwnerActor);

	SameLeaderFollowers.Sort([&LeaderLoc](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), LeaderLoc)
			< FVector::DistSquared(B.GetActorLocation(), LeaderLoc);
	});

	for (int32 i = 1; i < SameLeaderFollowers.Num(); i++)
	{
		UMovableTargetComponent* ThisMove =
			SameLeaderFollowers[i]->FindComponentByClass<UMovableTargetComponent>();
		AActor* PreviousInChain = SameLeaderFollowers[i - 1];

		if (ThisMove && ThisMove->ConvoyLeader != PreviousInChain)
		{
			ThisMove->ConvoyLeader = PreviousInChain;
		}
	}
}
