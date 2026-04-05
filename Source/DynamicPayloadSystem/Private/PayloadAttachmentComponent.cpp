#include "PayloadAttachmentComponent.h"
#include "Payload.h"
#include "PayloadMissionManager.h"
#include "DamagableComponent.h"
#include "TargetActor.h"
#include "Components/StaticMeshComponent.h"
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

	// Bind kamikaze hit detection to the trigger mesh
	if (bKamikazeMode && !KamikazeTriggerMeshName.IsNone())
	{
		TArray<UActorComponent*> Children;
		GetOwner()->GetComponents(UPrimitiveComponent::StaticClass(), Children);
		for (UActorComponent* Child : Children)
		{
			if (Child->GetFName() == KamikazeTriggerMeshName)
			{
				KamikazeTriggerMesh = Cast<UPrimitiveComponent>(Child);
				break;
			}
		}

		if (KamikazeTriggerMesh)
		{
			KamikazeTriggerMesh->SetGenerateOverlapEvents(true);
			KamikazeTriggerMesh->OnComponentBeginOverlap.AddDynamic(
				this, &UPayloadAttachmentComponent::OnKamikazeOverlap);
		}
	}

	if (bAutoSpawnOnBeginPlay && PayloadClass)
	{
		SpawnAndAttachPayload();
	}
}

void UPayloadAttachmentComponent::SpawnAndAttachPayload()
{
	if (AttachedPayload || !PayloadClass)
		return;

	// Mission mode gate
	APayloadMissionManager* MissionManager = nullptr;
	for (TActorIterator<APayloadMissionManager> It(GetWorld()); It; ++It)
	{
		MissionManager = *It;
		break;
	}

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

	PayloadConstraint = NewObject<UPhysicsConstraintComponent>(GetOwner(), TEXT("PayloadConstraint"));
	PayloadConstraint->RegisterComponent();
	PayloadConstraint->AttachToComponent(OwnerRoot, FAttachmentTransformRules::KeepRelativeTransform);
	PayloadConstraint->SetRelativeLocation(FVector::ZeroVector);

	// Ball-joint constraint
	PayloadConstraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0);
	PayloadConstraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0);
	PayloadConstraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0);

	PayloadConstraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Free, 0);
	PayloadConstraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Free, 0);
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

	// Clear ignore lists so payload can collide with targets
	OwnerRoot->MoveIgnoreActors.Remove(AttachedPayload);
	PayloadMesh->MoveIgnoreActors.Remove(GetOwner());

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
	for (TActorIterator<APayloadMissionManager> It(GetWorld()); It; ++It)
	{
		It->NotifyAttemptConsumed();
		break;
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

void UPayloadAttachmentComponent::OnKamikazeOverlap(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == GetOwner())
		return;

	if (AttachedPayload && OtherActor == AttachedPayload)
		return;

	if (!bKamikazeMode)
		return;

	if (!AttachedPayload)
		return;

	// Velocity threshold check
	UPrimitiveComponent* OwnerRoot = GetOwnerRootMesh();
	if (OwnerRoot)
	{
		const float Speed_ms = OwnerRoot->GetComponentVelocity().Size() / 100.0f;
		if (Speed_ms < MinKamikazeSpeed_ms)
			return;
	}

	UDamagableComponent* DamageComp = OtherActor->FindComponentByClass<UDamagableComponent>();
	if (!DamageComp)
		return;

	ATargetActor* Target = Cast<ATargetActor>(OtherActor);
	if (Target && !Target->bIsMissionTarget)
		return;

	DestroyPhysicsConstraint();

	// Notify mission manager
	for (TActorIterator<APayloadMissionManager> It(GetWorld()); It; ++It)
	{
		It->NotifyKamikazeTriggered();
		break;
	}

	const FVector ExplosionLocation = KamikazeTriggerMesh ?
		KamikazeTriggerMesh->GetComponentLocation() : GetOwner()->GetActorLocation();
	AttachedPayload->Explode(ExplosionLocation);
	AttachedPayload = nullptr;
	CachedPayloadMass = 0.0f;

	GetOwner()->Destroy();
}

UPrimitiveComponent* UPayloadAttachmentComponent::GetOwnerRootMesh() const
{
	if (!GetOwner())
		return nullptr;

	return Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
}
