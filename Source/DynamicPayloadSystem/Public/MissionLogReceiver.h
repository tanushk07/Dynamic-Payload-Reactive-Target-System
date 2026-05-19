#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FGameLogEntry.h"
#include "MissionLogReceiver.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UMissionLogReceiver : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implement on a HUD (or any UObject) to receive mission log entries from
 * APayloadMissionManager. Replaces the previous "find a UFUNCTION named
 * PushGameLog by string" reflection contract with a compile-time interface.
 */
class DYNAMICPAYLOADSYSTEM_API IMissionLogReceiver
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Mission|Log")
	void PushGameLog(const FGameLogEntry& Entry);
};
