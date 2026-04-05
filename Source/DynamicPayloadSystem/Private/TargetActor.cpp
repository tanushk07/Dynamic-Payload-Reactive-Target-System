#include "TargetActor.h"
#include "DamagableComponent.h"
#include "TargetBehaviorComponent.h"
#include "MovableTargetComponent.h"

ATargetActor::ATargetActor()
{
	PrimaryActorTick.bCanEverTick = true;

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
}

void ATargetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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
}
