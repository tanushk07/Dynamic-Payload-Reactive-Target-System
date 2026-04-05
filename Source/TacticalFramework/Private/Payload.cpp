#include "Payload.h"
#include "Explosive.h"
#include "Components/StaticMeshComponent.h"
#include "PayloadMissionManager.h"
#include "EngineUtils.h"
#include "DamagableComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

APayload::APayload()
{
	PrimaryActorTick.bCanEverTick = false;
	PayloadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PayloadMesh"));
	RootComponent = PayloadMesh;

	PayloadMesh->SetSimulatePhysics(false);
	PayloadMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PayloadMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
}

void APayload::BeginPlay()
{
	Super::BeginPlay();
}

void APayload::Explode(const FVector& ExplosionLocation)
{
	if (bHasExploded)
		return;

	bHasExploded = true;

	GetWorld()->GetTimerManager().ClearTimer(FuseTimerHandle);

	if (ExplosionClass)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, ExplosionLocation);

		AExplosive* Explosive = GetWorld()->SpawnActorDeferred<AExplosive>(
			ExplosionClass,
			SpawnTransform,
			this,
			GetInstigator(),
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn
		);

		if (Explosive)
		{
			UGameplayStatics::FinishSpawningActor(Explosive, SpawnTransform);
		}
	}

	for (TActorIterator<APayloadMissionManager> It(GetWorld()); It; ++It)
	{
		It->RespawnPayloadDelayed();
		It->NotifyLastPayloadResolved();
		break;
	}

	Destroy();
}

void APayload::Arm()
{
	if (bIsArmed || bHasExploded)
		return;

	bIsArmed = true;

#if WITH_EDITOR
	UE_LOG(LogTemp, Warning, TEXT("Payload armed"));
#endif

	if (PayloadDynamicBehaviour)
	{
		PayloadMesh->OnComponentHit.AddDynamic(
			this, &APayload::OnPayloadHit
		);
	}

	GetWorld()->GetTimerManager().SetTimer(
		FuseTimerHandle,
		this,
		&APayload::OnFuseExpired,
		FuseTime,
		false
	);
}

void APayload::OnFuseExpired()
{
	if (bHasExploded)
		return;

#if WITH_EDITOR
	UE_LOG(LogTemp, Warning, TEXT("Payload fuse expired"));
#endif

	Explode(GetActorLocation());
}

void APayload::OnPayloadHit(
	UPrimitiveComponent* HitComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit
)
{
	if (!bIsArmed || bHasExploded)
		return;

	if (!OtherActor || OtherActor == this)
		return;

	UDamagableComponent* DamageComp = OtherActor->FindComponentByClass<UDamagableComponent>();
	if (!DamageComp) return;

#if WITH_EDITOR
	UE_LOG(LogTemp, Warning, TEXT("Payload impact detected"));
#endif

	Explode(Hit.ImpactPoint);
}
