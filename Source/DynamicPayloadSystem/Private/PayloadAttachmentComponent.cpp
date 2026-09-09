#include "PayloadAttachmentComponent.h"
#include "DynamicPayloadSystemModule.h"
#include "Payload.h"
#include "PayloadMissionManager.h"
#include "DamagableComponent.h"
#include "TargetActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

UPayloadAttachmentComponent::UPayloadAttachmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPayloadAttachmentComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache mission manager reference once
	for (TActorIterator<APayloadMissionManager> It(GetWorld()); It; ++It)
	{
		CachedMissionManager = *It;
		break;
	}

	// Always bind. bKamikazeMode is switched on by the configuration screen
	// well after BeginPlay, so gating the binding on it armed nothing.
	RefreshKamikazeBinding();

	if (bAutoSpawnOnBeginPlay && PayloadClass)
	{
		SpawnAndAttachPayload();
	}
}

void UPayloadAttachmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Only clean up a charge still riding on this carrier. One that was
	// released has already been handed over to the world.
	if (AttachedPayload)
	{
		AttachedPayload->Destroy();
		AttachedPayload = nullptr;
		CachedPayloadMass = 0.0f;
	}

	Super::EndPlay(EndPlayReason);
}

void UPayloadAttachmentComponent::SpawnAndAttachPayload()
{
	if (AttachedPayload || !PayloadClass)
		return;

	// Mission mode gate
	APayloadMissionManager* MissionManager = CachedMissionManager;

	if (MissionManager && MissionManager->IsMissionSystemEnabled())
	{
		if (!MissionManager->IsMissionModeActive())
			return;
	}

	UPrimitiveComponent* OwnerRoot = GetOwnerRootMesh();
	if (!OwnerRoot)
		return;

	FVector SpawnLocation = GetOwner()->GetActorLocation() +
		GetOwner()->GetActorRotation().RotateVector(AttachOffset);

	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.Instigator = Cast<APawn>(GetOwner());
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AttachedPayload = GetWorld()->SpawnActor<APayload>(
		PayloadClass,
		SpawnLocation,
		GetOwner()->GetActorRotation(),
		Params
	);

	if (!AttachedPayload)
		return;

	// Configure fuse time from mission manager (if available)
	if (MissionManager)
	{
		AttachedPayload->FuseTime = MissionManager->ConfiguredFuseTime;
	}

	UStaticMeshComponent* PayloadMesh =
		Cast<UStaticMeshComponent>(AttachedPayload->GetRootComponent());

	if (!PayloadMesh)
	{
		AttachedPayload->Destroy();
		AttachedPayload = nullptr;
		return;
	}

	PayloadMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PayloadMesh->SetCollisionProfileName(TEXT("PhysicsActor"));

	// Enable physics briefly to read mass
	PayloadMesh->SetSimulatePhysics(true);
	CachedPayloadMass = PayloadMesh->GetMass();

	if (CachedPayloadMass <= 0.0f)
	{
		CachedPayloadMass = 1.0f;
	}

	// Make owner and payload ignore each other's collision
	OwnerRoot->IgnoreActorWhenMoving(AttachedPayload, true);
	PayloadMesh->IgnoreActorWhenMoving(GetOwner(), true);
	OwnerRoot->IgnoreComponentWhenMoving(PayloadMesh, true);
	PayloadMesh->IgnoreComponentWhenMoving(OwnerRoot, true);

	ECollisionChannel OwnerChannel = OwnerRoot->GetCollisionObjectType();
	PayloadMesh->SetCollisionResponseToChannel(OwnerChannel, ECR_Ignore);
	PayloadMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	if (bEnableDanglingPhysics)
	{
		// DANGLING MODE: Use physics constraint — payload keeps simulating
		PayloadMesh->SetEnableGravity(true);
		CreatePhysicsConstraint();
	}
	else
	{
		// KINEMATIC MODE: Disable physics and attach as child
		PayloadMesh->SetSimulatePhysics(false);
		PayloadMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		AttachedPayload->AttachToComponent(
			OwnerRoot,
			FAttachmentTransformRules::KeepWorldTransform
		);
	}

	// Notify listeners (UI, custom physics, etc.)
	OnPayloadStateChanged.Broadcast(true);
}

void UPayloadAttachmentComponent::CreatePhysicsConstraint()
{
	if (PayloadConstraint || !AttachedPayload)
		return;

	UPrimitiveComponent* OwnerRoot = GetOwnerRootMesh();
	UStaticMeshComponent* PayloadMesh =
		Cast<UStaticMeshComponent>(AttachedPayload->GetRootComponent());

	if (!OwnerRoot || !PayloadMesh)
		return;

	// MakeUniqueObjectName so successive Spawn→Detach→Spawn cycles (mission retry,
	// multi-attempt missions) don't collide on a static FName and assert in shipping.
	const FName UniqueName = MakeUniqueObjectName(
		GetOwner(), UPhysicsConstraintComponent::StaticClass(), TEXT("PayloadConstraint"));
	PayloadConstraint = NewObject<UPhysicsConstraintComponent>(GetOwner(), UniqueName);
	PayloadConstraint->RegisterComponent();
	PayloadConstraint->AttachToComponent(OwnerRoot, FAttachmentTransformRules::KeepRelativeTransform);
	PayloadConstraint->SetRelativeLocation(FVector::ZeroVector);

	// Ball-joint constraint
	PayloadConstraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0);
	PayloadConstraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0);
	PayloadConstraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0);

	// Wire SwingAngleLimit (0..90) into the physics constraint so the editable
	// UPROPERTY actually controls swing range.
	// - SwingAngleLimit = 0  → constraint locked (no swing — kinematic-like behavior)
	// - SwingAngleLimit > 0  → swing limited to ±SwingAngleLimit degrees from the
	//   constraint's primary axis on both swing axes
	// - Set to 90 to recover the original "essentially free" swing behavior.
	// Twist is always free — a bomb's spin around its hanging axis isn't gameplay-relevant.
	if (SwingAngleLimit > 0.f)
	{
		PayloadConstraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Limited, SwingAngleLimit);
		PayloadConstraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Limited, SwingAngleLimit);
	}
	else
	{
		PayloadConstraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0);
		PayloadConstraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0);
	}
	PayloadConstraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Free, 0);

	PayloadConstraint->SetConstrainedComponents(OwnerRoot, NAME_None, PayloadMesh, NAME_None);

	PayloadConstraint->ConstraintInstance.SetRefPosition(EConstraintFrame::Frame1, AttachOffset);
	PayloadConstraint->ConstraintInstance.SetRefPosition(EConstraintFrame::Frame2, FVector::ZeroVector);

	PayloadConstraint->SetDisableCollision(true);
}

void UPayloadAttachmentComponent::DestroyPhysicsConstraint()
{
	if (PayloadConstraint)
	{
		PayloadConstraint->BreakConstraint();
		PayloadConstraint->DestroyComponent();
		PayloadConstraint = nullptr;
	}
}

void UPayloadAttachmentComponent::DetachPayload()
{
	if (!AttachedPayload)
		return;

	UPrimitiveComponent* OwnerRoot = GetOwnerRootMesh();
	UStaticMeshComponent* PayloadMesh =
		Cast<UStaticMeshComponent>(AttachedPayload->GetRootComponent());

	if (!OwnerRoot || !PayloadMesh)
		return;

	// Mirror everything SpawnAndAttachPayload set up. Previously only the two
	// MoveIgnoreActors entries were cleared, which left the payload silently
	// ignoring the owner's collision channel — meaning a dropped payload could
	// fall through the drone that dropped it.
	OwnerRoot->IgnoreActorWhenMoving(AttachedPayload, false);
	PayloadMesh->IgnoreActorWhenMoving(GetOwner(), false);
	OwnerRoot->IgnoreComponentWhenMoving(PayloadMesh, false);
	PayloadMesh->IgnoreComponentWhenMoving(OwnerRoot, false);

	ECollisionChannel OwnerChannel = OwnerRoot->GetCollisionObjectType();
	PayloadMesh->SetCollisionResponseToChannel(OwnerChannel, ECR_Block);
	// We deliberately leave the camera channel as Ignore — a projectile
	// blocking the player camera is undesirable in every scenario.

	if (bEnableDanglingPhysics)
	{
		DestroyPhysicsConstraint();

		if (bTransferVelocityOnDetach)
		{
			FVector OwnerVelocity = OwnerRoot->GetComponentVelocity();
			FVector CurrentVelocity = PayloadMesh->GetComponentVelocity();
			if (CurrentVelocity.SizeSquared() < OwnerVelocity.SizeSquared())
			{
				PayloadMesh->SetPhysicsLinearVelocity(OwnerVelocity, true);
			}
		}
	}
	else
	{
		// Kinematic mode: detach and enable physics
		FVector OwnerVelocity = OwnerRoot->GetComponentVelocity();
		AttachedPayload->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		PayloadMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		PayloadMesh->SetSimulatePhysics(true);
		PayloadMesh->SetEnableGravity(true);
		PayloadMesh->SetPhysicsLinearVelocity(OwnerVelocity);
	}

	APayload* DetachedPayload = AttachedPayload;

	AttachedPayload = nullptr;
	CachedPayloadMass = 0.0f;

	DetachedPayload->Arm();

	// Notify listeners
	OnPayloadStateChanged.Broadcast(false);

	// Notify mission manager
	if (CachedMissionManager)
	{
		CachedMissionManager->NotifyAttemptConsumed();
	}
}

FVector UPayloadAttachmentComponent::GetPayloadWorldPosition() const
{
	if (!AttachedPayload)
	{
		return GetOwner()->GetActorLocation() +
			GetOwner()->GetActorRotation().RotateVector(AttachOffset);
	}

	return AttachedPayload->GetActorLocation();
}

FVector UPayloadAttachmentComponent::GetPayloadLocalOffset() const
{
	if (!AttachedPayload || !GetOwner())
	{
		return AttachOffset;
	}

	FVector WorldOffset = AttachedPayload->GetActorLocation() - GetOwner()->GetActorLocation();
	return GetOwner()->GetActorRotation().UnrotateVector(WorldOffset);
}

void UPayloadAttachmentComponent::RefreshKamikazeBinding()
{
	if (bKamikazeBindingDone)
		return;

	AActor* Owner = GetOwner();
	if (!Owner)
		return;

	if (!KamikazeTriggerMeshName.IsNone())
	{
		TArray<UActorComponent*> Children;
		Owner->GetComponents(UPrimitiveComponent::StaticClass(), Children);
		for (UActorComponent* Child : Children)
		{
			if (Child->GetFName() == KamikazeTriggerMeshName)
			{
				KamikazeTriggerMesh = Cast<UPrimitiveComponent>(Child);
				break;
			}
		}
	}

	// Fall back to the root body rather than arming nothing. A misspelled or
	// empty KamikazeTriggerMeshName used to disable kamikaze silently.
	if (!KamikazeTriggerMesh)
	{
		KamikazeTriggerMesh = GetOwnerRootMesh();
	}

	if (!KamikazeTriggerMesh)
	{
		UE_LOG(LogDynamicPayload, Warning,
			TEXT("[Payload] %s has no usable kamikaze trigger mesh ('%s' not found and no root primitive); kamikaze cannot fire."),
			*Owner->GetName(), *KamikazeTriggerMeshName.ToString());
		return;
	}

	KamikazeTriggerMesh->SetGenerateOverlapEvents(true);

	// Simulating bodies only report blocking contact when this is on, and it
	// is off by default. Without it the Hit path below never fires either.
	KamikazeTriggerMesh->SetNotifyRigidBodyCollision(true);

	// A skeletal mesh does not simulate through its own BodyInstance - it
	// simulates through the per-bone bodies of its physics asset, and the call
	// above never reaches those. The drone's root is exactly this case, so
	// without this the Hit path stays silent no matter what else is set.
	if (USkeletalMeshComponent* SkeletalTrigger = Cast<USkeletalMeshComponent>(KamikazeTriggerMesh))
	{
		SkeletalTrigger->SetAllBodiesNotifyRigidBodyCollision(true);
	}

	KamikazeTriggerMesh->OnComponentBeginOverlap.AddDynamic(
		this, &UPayloadAttachmentComponent::OnKamikazeOverlap);
	KamikazeTriggerMesh->OnComponentHit.AddDynamic(
		this, &UPayloadAttachmentComponent::OnKamikazeHit);

	bKamikazeBindingDone = true;
}

void UPayloadAttachmentComponent::OnKamikazeOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	TryKamikazeDetonate(OtherActor, OtherComp);
}

void UPayloadAttachmentComponent::OnKamikazeHit(
	UPrimitiveComponent* HitComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	TryKamikazeDetonate(OtherActor, OtherComp);
}

bool UPayloadAttachmentComponent::TryKamikazeDetonate(
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp)
{
	if (!OtherActor || OtherActor == GetOwner())
		return false;

	// Only solid geometry counts as a strike. Targets routinely carry
	// query-only decoration - health widgets, selection volumes, audio ranges -
	// that overlaps the drone long before the hull does and can extend metres
	// past the vehicle. Those used to detonate the payload in open air near the
	// target, which read as the fuze going off at random.
	//
	// Physics collision is the discriminator: a hull has it, a UI widget does
	// not. Checked here rather than in the handlers so the overlap and hit
	// paths cannot diverge.
	if (OtherComp)
	{
		const ECollisionEnabled::Type Solidity = OtherComp->GetCollisionEnabled();
		if (Solidity != ECollisionEnabled::QueryAndPhysics &&
			Solidity != ECollisionEnabled::PhysicsOnly)
		{
			return false;
		}
	}

	if (AttachedPayload && OtherActor == AttachedPayload)
		return false;

	if (!bKamikazeMode)
		return false;

	if (!AttachedPayload)
		return false;

	// Velocity threshold check
	UPrimitiveComponent* OwnerRoot = GetOwnerRootMesh();
	if (OwnerRoot)
	{
		const float Speed_ms = OwnerRoot->GetComponentVelocity().Size() / 100.0f;
		if (Speed_ms < MinKamikazeSpeed_ms)
			return false;
	}

	UDamagableComponent* DamageComp = OtherActor->FindComponentByClass<UDamagableComponent>();
	if (!DamageComp)
		return false;

	ATargetActor* Target = Cast<ATargetActor>(OtherActor);
	if (Target && !Target->bIsMissionTarget)
		return false;

	DestroyPhysicsConstraint();

	// Notify mission manager
	if (CachedMissionManager)
	{
		CachedMissionManager->NotifyKamikazeTriggered();
	}

	const FVector ExplosionLocation = KamikazeTriggerMesh ?
		KamikazeTriggerMesh->GetComponentLocation() : GetOwner()->GetActorLocation();
	AttachedPayload->Explode(ExplosionLocation);
	AttachedPayload = nullptr;
	CachedPayloadMass = 0.0f;

	// Symmetry with DetachPayload — UI / game code that listens for payload
	// state changes should learn that the kamikaze payload is gone too.
	OnPayloadStateChanged.Broadcast(false);

	// Deliberately AFTER Explode(): the blast has to land, and any target it
	// destroys has to be struck off the mission's list, before the mission is
	// told the drone is gone. Reversed, a kamikaze that cleared the final
	// target would be recorded as a failure.
	//
	// Without this call the drone simply vanished: the mission stayed
	// InProgress with no pawn and no carrier to respawn a payload onto, and
	// nothing resolved until the clock ran out.
	if (CachedMissionManager)
	{
		CachedMissionManager->NotifyDroneDestroyed();
	}

	GetOwner()->Destroy();
	return true;
}

UPrimitiveComponent* UPayloadAttachmentComponent::GetOwnerRootMesh() const
{
	if (!GetOwner())
		return nullptr;

	return Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
}
