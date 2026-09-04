#include "PayloadMissionManager.h"
#include "DamagableComponent.h"
#include "DynamicPayloadSystemModule.h"
#include "PayloadAttachmentComponent.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Payload.h"
#include "TargetActor.h"
#include "MissionLogReceiver.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameModeBase.h"
#include "Components/PrimitiveComponent.h"
#include "MovableTargetComponent.h"

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
	if (!bMissionModeEnabled)
		return;

	// A finished mission leaves MissionState at Success or Failed, and
	// StartMission refuses to run unless it is NotStarted. Nothing used to put
	// it back, so every Play after the first mission was silently dead: the
	// countdown ran, StartMission returned immediately, and therefore no
	// targets were registered, no snapshot was captured, and - because
	// SpawnAndAttachPayload is gated on the mission being InProgress - the
	// drone never received a payload either.
	//
	// RetryMission already performs exactly the teardown a fresh start needs
	// (timers, counters, delegate bindings, world reset) and calls
	// HandleMissionStart itself, so hand off to it rather than duplicating.
	if (MissionState != EPayloadMissionState::NotStarted)
	{
		RetryMission();
		return;
	}

	HandleMissionStart();
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
		if (bShowDebug)
		{
			UE_LOG(LogDynamicPayload, Error, TEXT("[Mission] Cannot start - No PayloadClass assigned in PayloadAttachmentComponent!"));
		}
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

	// Reset here, at the moment the mission actually begins, rather than only
	// when Retry was pressed. The countdown runs for several seconds and the
	// vehicles keep driving through it, so a world reset performed before the
	// countdown has already been undone by the time the player takes control -
	// which looks exactly like the reset never happened. No-op on the first
	// mission, when no snapshot exists yet.
	ResetMissionWorld();

	TotalDamageInflicted = 0.f;
	TargetsDestroyedCount = 0;

	EmitMissionLog(TEXT("Mission Started"), ELogSeverity::Info);

	// Discover all mission targets currently in the world. Late-spawned targets
	// self-register via ATargetActor::BeginPlay.
	for (TActorIterator<ATargetActor> It(GetWorld()); It; ++It)
	{
		RegisterMissionTarget(*It);
	}
	InitialTargetCount = DamageableTargets.Num();

	// First run only - see CaptureMissionSnapshot.
	CaptureMissionSnapshot();

	GetWorld()->GetTimerManager().SetTimer(
		MissionTimerHandle,
		this,
		&APayloadMissionManager::TickMissionTimer,
		1.0f,
		true
	);

	// Spawn payload on the first actor that actually has a
	// PayloadAttachmentComponent (see SpawnPayloadOnCarrier).
	SpawnPayloadOnCarrier();
}

void APayloadMissionManager::RegisterMissionTarget(ATargetActor* Target)
{
	if (!Target || !Target->bIsMissionTarget)
		return;

	if (MissionState != EPayloadMissionState::InProgress)
		return;

	if (DamageableTargets.Contains(Target))
		return;

	UDamagableComponent* DC = Target->DamagableComponent;
	if (!DC)
		return;

	DamageableTargets.Add(Target);

	DC->OnStructuralStateChanged.AddDynamic(
		this, &APayloadMissionManager::OnTargetStructuralStateChanged);
	DC->OnDamageTaken.AddDynamic(
		this, &APayloadMissionManager::OnTargetDamageTaken);

	DC->SetHighlightEnabled(true);
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
	OnMissionResolved.Broadcast(
		MissionState, InitialTargetCount, TargetsDestroyedCount, TotalDamageInflicted);

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

			// Cancel the despawn that DestroyDelay just scheduled.
			//
			// A target's behaviour lives in per-instance references set by the
			// level author - the spline it drives, the leader it follows, its
			// movement mode. None of that is in the class defaults, so an actor
			// rebuilt from its class comes back unable to move, and a rebuilt
			// leader leaves every follower pointing at a destroyed actor.
			//
			// Keeping the wreck means a retry can Revive() the ORIGINAL actor
			// with all of that intact. The wreck stays visible in the meantime,
			// which is what a destroyed vehicle should look like anyway.
			if (bResetWorldOnRetry)
			{
				Actor->SetLifeSpan(0.f);
			}

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
	OnMissionResolved.Broadcast(
		MissionState, InitialTargetCount, TargetsDestroyedCount, TotalDamageInflicted);

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

	// Disable highlights BEFORE Empty()-ing the list — the function iterates
	// DamageableTargets internally, so clearing first made the call a no-op.
	DisableAllTargetHighlights();
	DamageableTargets.Empty();

	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		// UE auto-cancels timers when the target UObject dies, so these are
		// defensive — but explicit cleanup keeps state predictable when the
		// manager is destroyed before its timer windows close (level streaming,
		// sub-level unload, world travel).
		TM.ClearTimer(MissionTimerHandle);
		TM.ClearTimer(LastPayloadResolveTimerHandle);
		TM.ClearTimer(LastPayloadWatchdogHandle);
		TM.ClearTimer(CountdownTimerHandle);
		TM.ClearTimer(QuitGameTimerHandle);
		TM.ClearTimer(PayloadRespawnTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void APayloadMissionManager::NotifyAttemptConsumed()
{
	if (!bMissionModeEnabled)
		return;

	if (MissionState != EPayloadMissionState::InProgress)
		return;

	// Clamp so BP code that calls SpawnAndAttachPayload manually after attempts
	// hit zero (then detaches) doesn't drive the counter negative. Every downstream
	// gate uses <= 0 so negativity wouldn't matter for correctness, but it breaks
	// UI displays that show the count.
	AttemptsRemaining = FMath::Max(AttemptsRemaining - 1, 0);

	EmitMissionLog(
		FString::Printf(TEXT("Attempt consumed. \nRemaining attempts: %d"), AttemptsRemaining),
		ELogSeverity::Warning
	);

	if (AttemptsRemaining <= 0 && DamageableTargets.Num() > 0)
	{
		EnterWaitingForLastPayload();
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

	// Respawn payload on the first actor that actually has a
	// PayloadAttachmentComponent (see SpawnPayloadOnCarrier).
	SpawnPayloadOnCarrier();
}

void APayloadMissionManager::NotifyLastPayloadResolved()
{
	if (!bWaitingForLastPayload)
		return;

	bTimerFrozen = true;

	if (DamageableTargets.Num() == 0)
	{
		// Nothing left to destroy - resolve now instead of returning and
		// making the player wait out the full watchdog timeout.
		DeferredResolveLastPayload();
		return;
	}

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
	// Both the normal resolve timer and the watchdog can race to call this.
	// This guard makes it idempotent: whichever fires first transitions
	// MissionState out of InProgress; the second call early-returns here.
	if (MissionState != EPayloadMissionState::InProgress)
		return;

	// We are resolving now — cancel every pending path into this function
	// so a stale timer/watchdog can't fire a second time into a new state.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LastPayloadResolveTimerHandle);
		World->GetTimerManager().ClearTimer(LastPayloadWatchdogHandle);
	}

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

bool APayloadMissionManager::SpawnPayloadOnCarrier()
{
	UWorld* World = GetWorld();
	if (!World)
		return false;

	// The possessed pawn first. A world scan picks whichever carrier the actor
	// iterator happens to reach first, and any flow that spawns a fresh pawn
	// without destroying the old one (returning to the menu and pressing Play
	// again) leaves stale drones behind - so the payload would attach to a
	// ghost the player is not flying, and appear never to spawn at all.
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* PlayerPawn = PC->GetPawn())
		{
			if (UPayloadAttachmentComponent* PawnComp =
				PlayerPawn->FindComponentByClass<UPayloadAttachmentComponent>())
			{
				PawnComp->SpawnAndAttachPayload();
				return true;
			}
		}
	}

	// No possessed carrier (AI-driven or headless): fall back to a world scan.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (UPayloadAttachmentComponent* PayloadComp =
			It->FindComponentByClass<UPayloadAttachmentComponent>())
		{
			PayloadComp->SpawnAndAttachPayload();
			return true;   // stop ONLY once we've found an actual carrier
		}
		// no break here: keep scanning until a carrier is found
	}

	return false;
}

void APayloadMissionManager::EnterWaitingForLastPayload()
{
	// Single, canonical entry into the "attempts exhausted, a payload may
	// still be live" state. Every caller funnels through here so the
	// watchdog is ALWAYS armed whenever bWaitingForLastPayload is true.
	//
	// Idempotency: once we're in the waiting state, additional calls (e.g. BP
	// code that manually respawns + detaches a payload after attempts are
	// already exhausted) are no-ops. Otherwise each re-entry would reset the
	// 10s watchdog and a determined caller could indefinitely postpone
	// resolution, breaking the watchdog's "hard upper bound" promise.
	if (bWaitingForLastPayload)
	{
		return;
	}
	bWaitingForLastPayload = true;
	bTimerFrozen = true;
	StartLastPayloadWatchdog();
}

void APayloadMissionManager::StartLastPayloadWatchdog()
{
	UWorld* World = GetWorld();
	if (!World)
		return;

	World->GetTimerManager().ClearTimer(LastPayloadWatchdogHandle);
	World->GetTimerManager().SetTimer(
		LastPayloadWatchdogHandle,
		this,
		&APayloadMissionManager::OnLastPayloadWatchdogExpired,
		LastPayloadWatchdogTimeout,
		false
	);
}

void APayloadMissionManager::OnLastPayloadWatchdogExpired()
{
	// If the normal path already resolved the mission, MissionState is no
	// longer InProgress and there is nothing to do.
	if (MissionState != EPayloadMissionState::InProgress)
		return;

	EmitMissionLog(
		TEXT("Last-payload watchdog expired - force-resolving mission"),
		ELogSeverity::Warning
	);

	// Route through the single shared exit point. State is evaluated from
	// DamageableTargets exactly as a normal resolution would.
	DeferredResolveLastPayload();
}

void APayloadMissionManager::EmitMissionLog(
	const FString& Message,
	ELogSeverity Severity
)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FGameLogEntry Log;
	Log.LogType = ELogType::Mission;
	Log.Severity = Severity;
	Log.Message = FText::FromString(Message);
	Log.TimeStamp = World->GetTimeSeconds();

	APlayerController* PC = World->GetFirstPlayerController();
	AHUD* HUD = PC ? PC->GetHUD() : nullptr;

	if (!HUD)
	{
		// No HUD yet — queue and flush when one becomes available.
		PendingMissionLogs.Add(Log);
		return;
	}

	if (!HUD->GetClass()->ImplementsInterface(UMissionLogReceiver::StaticClass()))
	{
		// HUD exists but won't receive logs. Drop the entry rather than queuing
		// it, otherwise PendingMissionLogs would grow unbounded for the entire
		// session. Warn once per mission manager instance — outside WITH_EDITOR
		// so shipping builds get the diagnostic too. Without it, consumers see
		// silent log drop and have no signal pointing at the missing interface.
		if (!bWarnedAboutMissingInterface)
		{
			UE_LOG(LogDynamicPayload, Warning,
				TEXT("[Mission] HUD '%s' does not implement IMissionLogReceiver — mission logs will be dropped."),
				*HUD->GetClass()->GetName());
			bWarnedAboutMissingInterface = true;
		}
		PendingMissionLogs.Reset();
		return;
	}

	// Drain any logs that were queued before the HUD existed. We flush in order
	// so the consumer always sees mission events in the sequence they occurred.
	for (const FGameLogEntry& Pending : PendingMissionLogs)
	{
		IMissionLogReceiver::Execute_PushGameLog(HUD, Pending);
	}
	PendingMissionLogs.Reset();

	IMissionLogReceiver::Execute_PushGameLog(HUD, Log);
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

	// A kamikaze that takes out the LAST target has won, and losing the drone
	// to its own blast must not overwrite that. Destroyed targets are removed
	// from DamageableTargets as they die, so an empty list here means the
	// field is clear and HandleAllTargetsDestroyed is already queued for the
	// next tick - bow out and let it resolve as a success.
	if (DamageableTargets.Num() == 0)
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

	// Only enter the waiting state if attempts are genuinely exhausted AND
	// targets remain. Previously this was set unconditionally on every
	// kamikaze, which froze the mission even when the player still had
	// attempts left and should simply have respawned.
	if (AttemptsRemaining <= 0 && DamageableTargets.Num() > 0)
	{
		// Schedule resolution explicitly here. Do NOT rely on the
		// subsequent APayload::Explode() incidentally calling
		// NotifyLastPayloadResolved() in the right order - that coupling
		// was the original latent hang. EnterWaitingForLastPayload() arms
		// the watchdog; we additionally arm the normal (shorter) resolve
		// timer so a clean kamikaze still resolves promptly.
		EnterWaitingForLastPayload();

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(LastPayloadResolveTimerHandle);
			World->GetTimerManager().SetTimer(
				LastPayloadResolveTimerHandle,
				this,
				&APayloadMissionManager::DeferredResolveLastPayload,
				LastPayloadStateChangeDelay,
				false
			);
		}
	}
}

void APayloadMissionManager::CaptureMissionSnapshot()
{
	if (bMissionSnapshotCaptured)
		return;

	MissionTargetSnapshots.Reset();
	for (AActor* Actor : DamageableTargets)
	{
		if (!IsValid(Actor))
			continue;

		FMissionTargetSnapshot Snapshot;
		Snapshot.TargetClass = Actor->GetClass();
		Snapshot.LiveActor = Actor;

		// Prefer the transform the target recorded at BeginPlay. Reading the
		// actor's transform here would capture wherever a convoy had driven to
		// during the countdown, not where the level author placed it.
		if (const ATargetActor* Target = Cast<ATargetActor>(Actor))
		{
			Snapshot.SpawnTransform = Target->GetInitialTransform();
		}
		else
		{
			Snapshot.SpawnTransform = Actor->GetActorTransform();
		}

		MissionTargetSnapshots.Add(Snapshot);
	}

	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				PlayerRestartTransform = Pawn->GetActorTransform();
				CapturedPlayerPawnClass = Pawn->GetClass();

				// Remember what the configuration screen injected, so the
				// replacement drone is armed the same way as the original.
				if (const UPayloadAttachmentComponent* Attach =
					Pawn->FindComponentByClass<UPayloadAttachmentComponent>())
				{
					CapturedPayloadClass = Attach->PayloadClass;
					bCapturedKamikazeMode = Attach->bKamikazeMode;
				}
			}
		}
	}

	bMissionSnapshotCaptured = true;
}

void APayloadMissionManager::ResetPlayerToStart()
{
	UWorld* World = GetWorld();
	if (!World)
		return;

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
		return;

	// Where home is. A PlayerStart in the level wins; the transform captured at
	// mission start is the fallback for levels without one.
	FTransform Destination = PlayerRestartTransform;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Destination = It->GetActorTransform();
		break;
	}

	APawn* OldPawn = PC->GetPawn();

	// Which class to rebuild from. The captured class is authoritative because
	// OldPawn may not exist at all.
	TSubclassOf<APawn> PawnClass = CapturedPlayerPawnClass;
	if (!PawnClass && OldPawn)
	{
		PawnClass = OldPawn->GetClass();
	}
	if (!PawnClass)
	{
		if (const AGameModeBase* GameMode = World->GetAuthGameMode())
		{
			PawnClass = GameMode->DefaultPawnClass;
		}
	}

	if (!PawnClass)
	{
		// Nothing to rebuild from. Move whatever is there and bail.
		if (OldPawn)
		{
			if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(OldPawn->GetRootComponent()))
			{
				if (Prim->IsSimulatingPhysics())
				{
					Prim->SetPhysicsLinearVelocity(FVector::ZeroVector);
					Prim->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
				}
			}
			OldPawn->TeleportTo(Destination.GetLocation(), Destination.Rotator(), false, true);
		}
		return;
	}

	// Carry the injected payload setup forward. Read it from the live pawn when
	// there is one, otherwise fall back to what was captured at mission start.
	TSubclassOf<APayload> PayloadClassToApply = CapturedPayloadClass;
	bool bKamikazeToApply = bCapturedKamikazeMode;
	if (OldPawn)
	{
		if (const UPayloadAttachmentComponent* OldAttach =
			OldPawn->FindComponentByClass<UPayloadAttachmentComponent>())
		{
			if (OldAttach->PayloadClass)
			{
				PayloadClassToApply = OldAttach->PayloadClass;
			}
			bKamikazeToApply = OldAttach->bKamikazeMode;
		}
	}

	// Rebuild rather than teleport. Teleporting only works when a pawn still
	// exists, and a kamikaze destroys it outright - which left the player with
	// no drone at all after a retry. A fresh pawn also guarantees no leftover
	// velocity, attitude, or half-attached payload from the failed attempt.
	if (OldPawn)
	{
		PC->UnPossess();
		OldPawn->Destroy();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = PC;

	APawn* NewPawn = World->SpawnActor<APawn>(
		PawnClass, Destination.GetLocation(), Destination.Rotator(), SpawnParams);

	if (!NewPawn)
	{
		UE_LOG(LogDynamicPayload, Error,
			TEXT("[Mission] Could not respawn the player pawn (%s); the player is left without one."),
			*PawnClass->GetName());
		return;
	}

	if (UPayloadAttachmentComponent* NewAttach =
		NewPawn->FindComponentByClass<UPayloadAttachmentComponent>())
	{
		if (PayloadClassToApply)
		{
			NewAttach->PayloadClass = PayloadClassToApply;
		}
		NewAttach->bKamikazeMode = bKamikazeToApply;
	}

	PC->Possess(NewPawn);
}

void APayloadMissionManager::ResetMissionWorld()
{
	if (!bResetWorldOnRetry)
		return;

	UWorld* World = GetWorld();
	if (!World)
		return;

	for (FMissionTargetSnapshot& Snapshot : MissionTargetSnapshots)
	{
		AActor* Actor = Snapshot.LiveActor.Get();

		if (!IsValid(Actor))
		{
			// Last resort. Normally unreachable: OnTargetStructuralStateChanged
			// cancels the lifespan of destroyed targets precisely so they are
			// still here to revive. We only get here if the target left play by
			// some other route (bResetWorldOnRetry toggled mid-mission, a
			// Blueprint Destroy, level streaming).
			//
			// A class-default rebuild CANNOT restore the level author's
			// per-instance wiring - spline, convoy leader, movement mode - so
			// the replacement will sit still. Warn rather than fail silently.
			if (!Snapshot.TargetClass)
				continue;

			UE_LOG(LogDynamicPayload, Warning,
				TEXT("[Mission] Target '%s' left play and had to be rebuilt from its class. "
					 "Level-instance movement settings (spline, convoy leader, mode) are lost "
					 "and it will not move."),
				*Snapshot.TargetClass->GetName());

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			Actor = World->SpawnActor<AActor>(
				Snapshot.TargetClass, Snapshot.SpawnTransform, SpawnParams);

			Snapshot.LiveActor = Actor;
			if (!Actor)
				continue;
		}
		else if (UDamagableComponent* DC = Actor->FindComponentByClass<UDamagableComponent>())
		{
			DC->Revive();
		}

		// Runs for respawned and revived targets alike: a freshly spawned actor
		// is already home, but this also clears the movement state, and a
		// survivor needs both.
		if (UMovableTargetComponent* MC = Actor->FindComponentByClass<UMovableTargetComponent>())
		{
			MC->ResetToStart();
		}
	}

	if (bResetPlayerOnRetry)
	{
		ResetPlayerToStart();
	}
}

void APayloadMissionManager::RetryMission()
{
	GetWorld()->GetTimerManager().ClearTimer(MissionTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(CountdownTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(QuitGameTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(LastPayloadResolveTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(LastPayloadWatchdogHandle);

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

	// Clear any pre-HUD logs queued during the failed mission so they don't
	// flush into the HUD at retry start.
	PendingMissionLogs.Reset();

	// Deliberately after the unbind above: reviving a target broadcasts a
	// structural-state change, and this manager should not be listening to the
	// mission it is in the middle of tearing down.
	ResetMissionWorld();

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