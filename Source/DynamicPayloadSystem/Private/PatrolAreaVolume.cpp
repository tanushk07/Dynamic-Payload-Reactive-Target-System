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

	// Z is intentionally the volume centre — ground-based patrols zero the
	// callsite's Z anyway, and the alternative (randomising Z too) wastes
	// entropy and produces unreachable waypoints for ground vehicles.
	return FVector(
		Origin.X + FMath::FRandRange(-Extent.X, Extent.X),
		Origin.Y + FMath::FRandRange(-Extent.Y, Extent.Y),
		Origin.Z
	);
}
