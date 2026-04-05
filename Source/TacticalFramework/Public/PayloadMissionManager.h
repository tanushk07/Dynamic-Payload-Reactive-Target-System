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

UCLASS()
class TACTICALFRAMEWORK_API APayloadMissionManager : public AActor
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

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnMissionTimeUpdated,
		float, RemainingTime
	);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnMissionStateChanged,
		EPayloadMissionState, NewState
	);

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionTimeUpdated OnMissionTimeUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMissionStateChanged OnMissionStateChanged;

	UFUNCTION(BlueprintCallable)
	void NotifyAttemptConsumed();

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
	float ConfiguredFuseTime = 3.0f;
};
