#include "PatrolAreaVolume.h"
#include "Components/BoxComponent.h"

APatrolAreaVolume::APatrolAreaVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->SetupAttachment(Root);

	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetGenerateOverlapEvents(false);
}

FVector APatrolAreaVolume::GetRandomPointInArea() const
{
	if (!Bounds)
		return GetActorLocation();

	const FVector Origin = Bounds->GetComponentLocation();
	const FVector Extent = Bounds->GetScaledBoxExtent();

	return Origin + FVector(
		FMath::FRandRange(-Extent.X, Extent.X),
		FMath::FRandRange(-Extent.Y, Extent.Y),
		FMath::FRandRange(-Extent.Z, Extent.Z)
	);
}
