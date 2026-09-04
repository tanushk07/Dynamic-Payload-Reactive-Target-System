#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FGameLogEntry.h"
#include "MissionLogReceiver.h"
#include "MissionLogHUD.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnMissionLogReceived,
	const FGameLogEntry&, Entry
);

/**
 * A ready-made HUD that receives mission log entries and re-broadcasts them
 * to Blueprint.
 *
 * APayloadMissionManager delivers its log through IMissionLogReceiver, and it
 * only delivers to the HUD. If the HUD does not implement that interface every
 * mission message is silently discarded - which is easy to hit, because the
 * default AHUD does not implement it and nothing fails loudly.
 *
 * Set this (or a Blueprint child of it) as the game mode's HUD Class, bind
 * OnMissionLog, and mission events reach the UI with no glue code.
 */
UCLASS(Blueprintable, ClassGroup = (Mission))
class DYNAMICPAYLOADSYSTEM_API AMissionLogHUD : public AHUD, public IMissionLogReceiver
{
	GENERATED_BODY()

public:
	/** Fires once per mission log entry, in the order the mission emitted them
	 *  (including any queued before this HUD existed). */
	UPROPERTY(BlueprintAssignable, Category = "Mission|Log")
	FOnMissionLogReceived OnMissionLog;

	/** Recent entries, newest last. Lets a widget created after the fact show
	 *  history instead of starting blank. */
	UPROPERTY(BlueprintReadOnly, Category = "Mission|Log")
	TArray<FGameLogEntry> LogHistory;

	/** Cap on LogHistory. 0 disables history entirely. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Mission|Log",
		meta = (ClampMin = "0"))
	int32 MaxLogHistory = 64;

	/** Most recent message as a plain string, for direct widget binding. */
	UFUNCTION(BlueprintPure, Category = "Mission|Log")
	FString GetLastMessage() const;

	virtual void PushGameLog_Implementation(const FGameLogEntry& Entry) override;
};
