#include "PayloadMissionManager.h"
#include "DamagableComponent.h"
#include "PayloadAttachmentComponent.h"
#include "EngineUtils.h"
#include "Payload.h"
#include "TargetActor.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

APayloadMissionManager::APayloadMissionManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void APayloadMissionManager::BeginPlay()
{
	Super::BeginPlay();
	if (!GetWorld())
		return;

	if (bMissionModeEnabled)
	{
		RequestMissionStart();
	}
}

void APayloadMissionManager::RequestMissionStart()
{
	if (bMissionModeEnabled)
	{
		HandleMissionStart();
	}
}

void APayloadMissionManager::HandleMissionStart()
{
	if (!bMissionModeEnabled)
		return;

	// Find any actor with a PayloadAttachmentComponent that has a valid PayloadClass
	bool bHasValidPayload = false;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (UPayloadAttachmentComponent* PayloadComp = It->FindComponentByClass<UPayloadAttachmentComponent>())
		{
			if (PayloadComp->PayloadClass)
			{
				bHasValidPayload = true;
				break;
			}
		}
	}

	if (!bHasValidPayload)
	{
#if WITH_EDITOR
		UE_LOG(LogTemp, Error, TEXT("[Mission] Cannot start - No PayloadClass assigned in PayloadAttachmentComponent!"));
#endif
		return;
	}

	CountdownTimeRemaining = CountdownStartTime;

	GetWorld()->GetTimerManager().SetTimer(
		CountdownTimerHandle,
		this,
		&APayloadMissionManager::TickCountdown,
		1.0f,
		true
	);
}

void APayloadMissionManager::TickCountdown()
{
	CountdownTimeRemaining--;

	if (CountdownTimeRemaining < 0)
	{
		GetWorld()->GetTimerManager().ClearTimer(CountdownTimerHandle);
		StartMission();
	}
}

void APayloadMissionManager::StartMission()
{
	if (!bMissionModeEnabled)
		return;

	if (MissionState != EPayloadMissionState::NotStarted)
		return;

	AttemptsRemaining = MaxAttempts;

	MissionState = EPayloadMissionState::InProgress;
	OnMissionStateChanged.Broadcast(MissionState);
	RemainingTime = MissionDuration;

	TotalDamageInflicted = 0.f;
	TargetsDestroyedCount = 0;

	EmitMissionLog(TEXT("Mission Started"), ELogSeverity::Info);

	// Discover all mission targets
	for (TActorIterator<ATargetActor> It(GetWorld()); It; ++It)
	{
		ATargetActor* TargetActor = *It;
		if (!TargetActor || !TargetActor->bIsMissionTarget)
			continue;

		if (UDamagableComponent* DC = TargetActor->DamagableComponent)
		{
			DamageableTargets.Add(TargetActor);

			DC->OnStructuralStateChanged.AddDynamic(
				this,
				&APayloadMissionManager::OnTargetStructuralStateChanged
			);

			DC->OnDamageTaken.AddDynamic(
				this,
				&APayloadMissionManager::OnTargetDamageTaken
			);
		}
	}
	InitialTargetCount = DamageableTargets.Num();

	GetWorld()->GetTimerManager().SetTimer(
		MissionTimerHandle,
		this,
		&APayloadMissionManager::TickMissionTimer,
		1.0f,
		true
	);

	// Spawn payload on any actor with PayloadAttachmentComponent
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (UPayloadAttachmentComponent* PayloadComp = It->FindComponentByClass<UPayloadAttachmentComponent>())
		{
			PayloadComp->SpawnAndAttachPayload();
		}
		break;
	}

	// Enable glow on all damageable targets
	for (AActor* Actor : DamageableTargets)
	{
		if (!Actor)
			continue;

		if (UDamagableComponent* DC =
			Actor->FindComponentByClass<UDamagableComponent>())
		{
			DC->SetHighlightEnabled(true);
		}
	}
}

void APayloadMissionManager::TickMissionTimer()
{
	if (MissionState != EPayloadMissionState::InProgress)
		return;

	if (bTimerFrozen)
		return;

	RemainingTime--;
	OnMissionTimeUpdated.Broadcast(RemainingTime);

	if (RemainingTime <= 0.f)
	{
		FailMission(TEXT("Time expired"));
	}
}

void APayloadMissionManager::FailMission(const FString& Reason)
{
	if (MissionState != EPayloadMissionState::InProgress)
		return;

	bWaitingForLastPayload = false;
	bTimerFrozen = false;

	MissionState = EPayloadMissionState::Failed;
	OnMissionStateChanged.Broadcast(MissionState);

	GetWorld()->GetTimerManager().ClearTimer(MissionTimerHandle);

	EmitMissionLog(
		FString::Printf(TEXT("Mission Failed: %s"), *Reason),
		ELogSeverity::Critical
	);
	DisableAllTargetHighlights();

	if (bQuitGameOnFailure)
	{
		GetWorld()->GetTimerManager().SetTimer(
			QuitGameTimerHandle,
			this,
			&APayloadMissionManager::QuitGameDelayed,
			QuitDelaySeconds,
			false
		);
	}
}

void APayloadMissionManager::OnTargetStructuralStateChanged(EStructuralState NewState)
{
	if (NewState != EStructuralState::Destroyed)
		return;

	if (MissionState != EPayloadMissionState::InProgress)
		return;

	for (int32 i = DamageableTargets.Num() - 1; i >= 0; --i)
	{
		AActor* Actor = DamageableTargets[i];
		if (!Actor) { DamageableTargets.RemoveAt(i); continue; }

		UDamagableComponent* DC = Actor->FindComponentByClass<UDamagableComponent>();
		if (DC && DC->StructuralState == EStructuralState::Destroyed)
		{
			TargetsDestroyedCount++;
			DC->SetHighlightEnabled(false);
			DamageableTargets.RemoveAt(i);
		}
	}

	if (DamageableTargets.Num() == 0)
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(
			this, &APayloadMissionManager::HandleAllTargetsDestroyed
		);
	}
}

void APayloadMissionManager::HandleAllTargetsDestroyed()
{
	EmitMissionLog(TEXT("All mission targets destroyed"), ELogSeverity::Info);

	bTimerFrozen = true;

	GetWorld()->GetTimerManager().ClearTimer(LastPayloadResolveTimerHandle);

	GetWorld()->GetTimerManager().SetTimer(
		LastPayloadResolveTimerHandle,
		this,
		&APayloadMissionManager::DeferredResolveLastPayload,
		LastPayloadStateChangeDelay,
		false
	);
}

void APayloadMissionManager::SucceedMission()
{
	if (MissionState != EPayloadMissionState::InProgress)
		return;

	bWaitingForLastPayload = false;
	bTimerFrozen = false;
	MissionState = EPayloadMissionState::Success;
	OnMissionStateChanged.Broadcast(MissionState);

	GetWorld()->GetTimerManager().ClearTimer(MissionTimerHandle);

	EmitMissionLog(TEXT("Mission Successful"), ELogSeverity::Critical);
	DisableAllTargetHighlights();
}

void APayloadMissionManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (AActor* Actor : DamageableTargets)
	{
		if (Actor)
		{
			if (UDamagableComponent* DC = Actor->FindComponentByClass<UDamagableComponent>())
			{
				DC->OnStructuralStateChanged.RemoveDynamic(
					this,
					&APayloadMissionManager::OnTargetStructuralStateChanged
				);
				DC->OnDamageTaken.RemoveDynamic(
					this,
					&APayloadMissionManager::OnTargetDamageTaken
				);
			}
		}
	}

	DamageableTargets.Empty();

	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MissionTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(LastPayloadResolveTimerHandle);
	}
	DisableAllTargetHighlights();

	Super::EndPlay(EndPlayReason);
}

void APayloadMissionManager::NotifyAttemptConsumed()
{
	if (!bMissionModeEnabled)
		return;

	if (MissionState != EPayloadMissionState::InProgress)
		return;

	AttemptsRemaining--;

	EmitMissionLog(
		FString::Printf(TEXT("Attempt consumed. \nRemaining attempts: %d"), AttemptsRemaining),
		ELogSeverity::Warning
	);

	if (AttemptsRemaining <= 0 && DamageableTargets.Num() > 0)
	{
		bWaitingForLastPayload = true;
		return;
	}
	if (DamageableTargets.Num() == 0)
	{
		return;
	}
}

void APayloadMissionManager::RespawnPayloadDelayed()
{
	if (MissionState != EPayloadMissionState::InProgress)
		return;

	if (DamageableTargets.Num() == 0)
		return;

	if (AttemptsRemaining <= 0 || bWaitingForLastPayload)
		return;

	// Respawn payload on any actor with PayloadAttachmentComponent
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (UPayloadAttachmentComponent* PayloadComp = It->FindComponentByClass<UPayloadAttachmentComponent>())
		{
			PayloadComp->SpawnAndAttachPayload();
		}
		break;
	}
}

void APayloadMissionManager::NotifyLastPayloadResolved()
{
	if (!bWaitingForLastPayload)
		return;

	bTimerFrozen = true;

	if (DamageableTargets.Num() == 0)
		return;

	GetWorld()->GetTimerManager().SetTimer(
		LastPayloadResolveTimerHandle,
		this,
		&APayloadMissionManager::DeferredResolveLastPayload,
		LastPayloadStateChangeDelay,
		false
	);
}

void APayloadMissionManager::DeferredResolveLastPayload()
{
	if (MissionState != EPayloadMissionState::InProgress)
		return;

	if (DamageableTargets.Num() == 0)
	{
		SucceedMission();
	}
	else
	{
		const int32 TargetsLeft = DamageableTargets.Num();
		FailMission(
			FString::Printf(
				TEXT("\nAll attempts used.\n%d target%s remaining"),
				TargetsLeft,
				TargetsLeft == 1 ? TEXT("") : TEXT("s")
			)
		);
	}
}

void APayloadMissionManager::EmitMissionLog(
	const FString& Message,
	ELogSeverity Severity
)
{
	FGameLogEntry Log;
	Log.LogType = ELogType::Mission;
	Log.Severity = Severity;
	Log.Message = FText::FromString(Message);
	Log.TimeStamp = GetWorld()->GetTimeSeconds();

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		PendingMissionLogs.Add(Log);
		return;
	}

	AHUD* HUD = PC->GetHUD();
	if (!HUD)
	{
		PendingMissionLogs.Add(Log);
		return;
	}

	UFunction* Func = HUD->FindFunction(FName("PushGameLog"));
	if (Func)
	{
		struct { FGameLogEntry Entry; } Params;
		Params.Entry = Log;
		HUD->ProcessEvent(Func, &Params);
	}
}

void APayloadMissionManager::DisableAllTargetHighlights()
{
	for (AActor* Actor : DamageableTargets)
	{
		if (!Actor)
			continue;

		if (UDamagableComponent* DC =
			Actor->FindComponentByClass<UDamagableComponent>())
		{
			DC->SetHighlightEnabled(false);
		}
	}
}

void APayloadMissionManager::NotifyDroneDestroyed()
{
	if (!bMissionModeEnabled)
		return;

	if (MissionState != EPayloadMissionState::InProgress)
		return;

	FailMission(TEXT("Drone destroyed"));
}

void APayloadMissionManager::NotifyKamikazeTriggered()
{
	if (!bMissionModeEnabled)
		return;

	if (MissionState != EPayloadMissionState::InProgress)
		return;

	AttemptsRemaining = FMath::Max(AttemptsRemaining - 1, 0);

	EmitMissionLog(
		FString::Printf(TEXT("Kamikaze triggered.\nAttempt consumed. Remaining: %d"), AttemptsRemaining),
		ELogSeverity::Warning
	);

	bWaitingForLastPayload = true;
}

void APayloadMissionManager::RetryMission()
{
	GetWorld()->GetTimerManager().ClearTimer(MissionTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(CountdownTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(QuitGameTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(LastPayloadResolveTimerHandle);

	bWaitingForLastPayload = false;
	bTimerFrozen = false;
	MissionState = EPayloadMissionState::NotStarted;
	OnMissionStateChanged.Broadcast(MissionState);

	RemainingTime = MissionDuration;
	AttemptsRemaining = MaxAttempts;
	TotalDamageInflicted = 0.f;
	TargetsDestroyedCount = 0;
	InitialTargetCount = 0;

	for (AActor* Actor : DamageableTargets)
	{
		if (Actor)
		{
			if (UDamagableComponent* DC = Actor->FindComponentByClass<UDamagableComponent>())
			{
				DC->OnStructuralStateChanged.RemoveDynamic(
					this,
					&APayloadMissionManager::OnTargetStructuralStateChanged
				);
				DC->OnDamageTaken.RemoveDynamic(
					this,
					&APayloadMissionManager::OnTargetDamageTaken
				);
			}
		}
	}
	DamageableTargets.Empty();

	HandleMissionStart();
}

void APayloadMissionManager::QuitGameDelayed()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC)
	{
		PC->ConsoleCommand(TEXT("quit"));
	}
}

void APayloadMissionManager::OnTargetDamageTaken(float DamageAmount, float RemainingHealth)
{
	if (MissionState != EPayloadMissionState::InProgress)
		return;

	TotalDamageInflicted += DamageAmount;
}
