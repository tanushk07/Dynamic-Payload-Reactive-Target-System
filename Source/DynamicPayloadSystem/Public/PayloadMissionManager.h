#pragma once

#include "CoreMinimal.h"
#include "FGameLogEntry.h"
#include "ELogType.h"
#include "ELogSeverity.h"
#include "EStructuralState.h"
#include "GameFramework/Actor.h"
#include "PayloadMissionManager.generated.h"

UENUM(BlueprintType)
enum class EPayloadMissionState : uint8
{
	NotStarted,
	InProgress,
	Success,
	Failed
};

/**
 * What a mission target looked like before anyone shot at it.
 *
 * A destroyed target despawns (DamagableComponent::DestroyDelay), so a retry
 * cannot simply revive what is left in the world - the actor is gone. Recording
 * the class and the starting transform is what makes rebuilding it possible.
 */
USTRUCT()
struct FMissionTargetSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<AActor> TargetClass;

	UPROPERTY()
	FTransform SpawnTransform = FTransform::Identity;

	/** Weak on purpose: this is the actor that may have despawned, and holding
	 *  a hard reference would keep a destroyed target alive in memory. */
	UPROPERTY()
	TWeakObjectPtr<AActor> LiveActor;
};

UCLASS()
class DYNAMICPAYLOADSYSTEM_API APayloadMissionManager : public AActor
{
	GENERATED_BODY()

public:
	APayloadMissionManager();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission")
	float MissionDuration = 30.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission")
	float RemainingTime = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Mission")
	EPayloadMissionState MissionState = EPayloadMissionState::NotStarted;

	FTimerHandle MissionTimerHandle;
	void StartMission();
	void TickMissionTimer();
	void FailMission(const FString& Reason);

	UPROPERTY()
	TArray<AActor*> DamageableTargets;

	UFUNCTION()
	void OnTargetStructuralStateChanged(EStructuralState NewState);

	void SucceedMission();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission")
	int32 MaxAttempts = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission")
	int32 AttemptsRemaining = 0;

	bool bWaitingForLastPayload = false;
	bool bTimerFrozen = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission|Summary")
	int32 InitialTargetCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission|Summary")
	int32 TargetsDestroyedCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission|Summary")
	float TotalDamageInflicted = 0.f;

	UFUNCTION()
	void OnTargetDamageTaken(float DamageAmount, float RemainingHealth);

	void EmitMissionLog(const FString& Message, ELogSeverity Severity);
	void HandleAllTargetsDestroyed();

	UPROPERTY()
	TArray<FGameLogEntry> PendingMissionLogs;

	/** The original line-up, recorded the first time a mission starts and reused
	 *  by every subsequent reset. Never re-captured from a world that has
	 *  already been fought over, or each retry would bake in the last one's
	 *  losses and the mission would get quietly easier every time. */
	UPROPERTY()
	TArray<FMissionTargetSnapshot> MissionTargetSnapshots;

	UPROPERTY()
	FTransform PlayerRestartTransform = FTransform::Identity;

	/** Captured at mission start so the drone can be REBUILT, not just moved.
	 *  A kamikaze destroys the pawn outright, so on retry there is nothing left
	 *  to teleport - the class is the only way back. */
	UPROPERTY()
	TSubclassOf<APawn> CapturedPlayerPawnClass;

	/** Payload setup carried over to the replacement drone, so a respawn does
	 *  not silently discard what the configuration screen injected. */
	UPROPERTY()
	TSubclassOf<class APayload> CapturedPayloadClass;

	bool bCapturedKamikazeMode = false;

	bool bMissionSnapshotCaptured = false;

	/** Records the starting line-up. No-op once it has run. */
	void CaptureMissionSnapshot();

	/** Teleports the player pawn home and clears its physics state. */
	void ResetPlayerToStart();

	/** Per-instance "we've already warned about this" flag. Was a function-static
	 *  bool previously, which persisted across PIE sessions — so devs who fixed
	 *  the HUD-implements-interface issue would never see the diagnostic again
	 *  in subsequent PIE sessions even if the issue regressed. */
	bool bWarnedAboutMissingInterface = false;

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnMissionTimeUpdated,
		float, RemainingTime
	);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnMissionStateChanged,
		EPayloadMissionState, NewState
	);

	/** Broadcast once when the mission resolves (Success or Failed). Carries the
	 *  end-of-mission summary so a results screen can display it without polling. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
		FOnMissionResolved,
		EPayloadMissionState, FinalState,
		int32, InitialTargets,
		int32, TargetsDestroyed,
		float, TotalDamage
	);

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionTimeUpdated OnMissionTimeUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionStateChanged OnMissionStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionResolved OnMissionResolved;

	UFUNCTION(BlueprintCallable, Category = "Mission")
	void NotifyAttemptConsumed();

	/** Register a target that came into existence after StartMission ran.
	 *  Safe to call from anywhere — guarded against double-registration and
	 *  silently ignores calls outside of the InProgress state. */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	void RegisterMissionTarget(class ATargetActor* Target);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission Mode")
	bool bMissionModeEnabled = false;

	bool IsMissionModeActive() const { return MissionState == EPayloadMissionState::InProgress; }
	bool IsMissionSystemEnabled() const { return bMissionModeEnabled; }
	bool IsMissionActive() const { return bMissionModeEnabled && MissionState == EPayloadMissionState::InProgress; }

	void DisableAllTargetHighlights();

	UPROPERTY(EditAnywhere, Category = "Mission")
	bool bQuitGameOnFailure = true;

	UFUNCTION()
	void NotifyDroneDestroyed();

	void NotifyKamikazeTriggered();

	FTimerHandle CountdownTimerHandle;
	UPROPERTY(EditAnywhere, Category = "Mission")
	int32 CountdownStartTime = 3;
	int32 CountdownTimeRemaining;

	void HandleMissionStart();
	void TickCountdown();

	UFUNCTION(BlueprintCallable, Category = "Mission")
	void RetryMission();

	/**
	 * Put the world back the way it was when the mission first started:
	 * every target alive and intact at its original transform, every vehicle
	 * returned to its start with movement state cleared, and the player back
	 * at the PlayerStart with no leftover velocity.
	 *
	 * Idempotent - calling it twice in a row is harmless.
	 */
	UFUNCTION(BlueprintCallable, Category = "Mission")
	void ResetMissionWorld();

	/** Set false to make Retry reset only the clock and counters, leaving the
	 *  world as the player left it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission")
	bool bResetWorldOnRetry = true;

	/** Also return the player pawn to the PlayerStart on a world reset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission")
	bool bResetPlayerOnRetry = true;

	UPROPERTY(EditAnywhere, Category = "Mission")
	float QuitDelaySeconds = 3.0f;
	FTimerHandle QuitGameTimerHandle;
	UFUNCTION()
	void QuitGameDelayed();

	UFUNCTION(BlueprintCallable, Category = "Mission")
	void RequestMissionStart();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission")
	float PayloadRespawnDelay = 2.0f;
	FTimerHandle PayloadRespawnTimerHandle;
	void RespawnPayloadDelayed();

	void NotifyLastPayloadResolved();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission")
	float LastPayloadStateChangeDelay = 3.0f;
	FTimerHandle LastPayloadResolveTimerHandle;
	void DeferredResolveLastPayload();

	/**
	 * Hard upper bound (seconds) on how long the mission may sit in the
	 * "waiting for the last payload" state before it is force-resolved.
	 * This is a safety backstop: the normal resolution path
	 * (NotifyLastPayloadResolved -> DeferredResolveLastPayload) should
	 * almost always fire first. The watchdog only matters when the last
	 * payload leaves play through a path that never calls back into the
	 * mission manager (Blueprint Destroy, owning pawn destroyed, level
	 * streaming, EndPlay during travel, pooling/reuse, etc.). Set this
	 * comfortably ABOVE the longest realistic payload flight + fuse time
	 * so it never pre-empts a legitimate resolution.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mission",
		meta = (ClampMin = "1.0"))
	float LastPayloadWatchdogTimeout = 10.0f;
	FTimerHandle LastPayloadWatchdogHandle;

	/**
	 * Spawns/attaches a payload on the first actor that actually has a
	 * UPayloadAttachmentComponent. Centralizes the carrier lookup so the
	 * "iterate, test for the component, then stop" logic exists in exactly
	 * one place (the previous inline loops broke out of the iterator after
	 * the first actor regardless of whether it had the component, so they
	 * only worked by luck of actor iteration order).
	 * @return true if a carrier was found and SpawnAndAttachPayload() ran.
	 */
	bool SpawnPayloadOnCarrier();

	/** Enters the "waiting for last payload" state from a single place:
	 *  sets flags, freezes the timer, and arms the watchdog. */
	void EnterWaitingForLastPayload();

	/** Arms (or re-arms) the watchdog timer. */
	void StartLastPayloadWatchdog();

	/** Fired only if nothing resolved the waiting state in time. */
	UFUNCTION()
	void OnLastPayloadWatchdogExpired();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float ConfiguredFuseTime = 3.0f;

	/** Toggle debug logging for mission manager diagnostics (editor only). */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bShowDebug = false;
};