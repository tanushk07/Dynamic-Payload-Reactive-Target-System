#include "TargetActor.h"
#include "DamagableComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TargetBehaviorComponent.h"
#include "MovableTargetComponent.h"
#include "PayloadMissionManager.h"
#include "EngineUtils.h"

ATargetActor::ATargetActor()
{
	// Tick disabled — Tick() body is empty. Subclasses that need ticking
	// should set bCanEverTick = true in their constructor.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	GroundFrame = CreateDefaultSubobject<USceneComponent>(TEXT("GroundFrame"));
	GroundFrame->SetupAttachment(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(GroundFrame);

	Mesh->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh->SetGenerateOverlapEvents(true);

	DamagableComponent =
		CreateDefaultSubobject<UDamagableComponent>(TEXT("DamagableComponent"));

	BehaviorComponent =
		CreateDefaultSubobject<UTargetBehaviorComponent>(TEXT("BehaviorComponent"));

	MovementComponent =
		CreateDefaultSubobject<UMovableTargetComponent>(TEXT("MovementComponent"));
}

void ATargetActor::BeginPlay()
{
	Super::BeginPlay();

	// Snapshot the untouched starting state before anything can damage or move
	// this actor. Everything a mission retry restores is read back from here.
	InitialTransform = GetActorTransform();
	bInitialActorTickEnabled = IsActorTickEnabled();
	if (Mesh)
	{
		InitialCollisionEnabled = Mesh->GetCollisionEnabled();
	}
	if (MovementComponent)
	{
		bInitialMovementTickEnabled = MovementComponent->IsComponentTickEnabled();
	}

	if (!IntactMesh && Mesh)
	{
		IntactMesh = Mesh->GetStaticMesh();
	}

	if (Mesh)
	{
		Mesh->bRenderCustomDepth = bIsMissionTarget;
		Mesh->MarkRenderStateDirty();
	}

	if (DamagableComponent)
	{
		DamagableComponent->OnStructuralStateChanged.AddDynamic(
			this,
			&ATargetActor::HandleStructuralStateChanged
		);
	}

	// Late-spawn registration: if a mission manager exists and the mission is
	// already InProgress, this target gets folded into the active mission.
	// RegisterMissionTarget is a no-op outside InProgress, so targets that
	// spawn before mission start are picked up by StartMission's iterator.
	if (bIsMissionTarget)
	{
		for (TActorIterator<APayloadMissionManager> It(GetWorld()); It; ++It)
		{
			It->RegisterMissionTarget(this);
			break;
		}
	}
}

void ATargetActor::HandleStructuralStateChanged(EStructuralState NewState)
{
	if (!Mesh) return;

	UStaticMesh* NewMesh = nullptr;

	switch (NewState)
	{
	case EStructuralState::Intact:
		NewMesh = IntactMesh;
		break;
	case EStructuralState::Damaged:
		NewMesh = DamagedMesh;
		break;
	case EStructuralState::Destroyed:
		NewMesh = DestroyedMesh;
		break;
	}

	if (NewMesh)
	{
		Mesh->SetStaticMesh(NewMesh);
		Mesh->bRenderCustomDepth = bIsMissionTarget;
		Mesh->MarkRenderStateDirty();

		if (MovementComponent)
		{
			MovementComponent->CacheOwnerBounds();
		}
	}

	if (NewState == EStructuralState::Destroyed)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->bRenderCustomDepth = false;
		Mesh->MarkRenderStateDirty();

		if (MovementComponent)
		{
			MovementComponent->SetComponentTickEnabled(false);
		}
		SetActorTickEnabled(false);
	}
	else
	{
		// Coming back from Destroyed - a mission retry reviving this target.
		// Everything the branch above switched off has to be switched back on,
		// otherwise a revived truck is invisible to traces and never moves.
		Mesh->SetCollisionEnabled(InitialCollisionEnabled);
		Mesh->bRenderCustomDepth = bIsMissionTarget;
		Mesh->MarkRenderStateDirty();

		if (MovementComponent)
		{
			MovementComponent->SetComponentTickEnabled(bInitialMovementTickEnabled);
		}
		SetActorTickEnabled(bInitialActorTickEnabled);
	}
}
