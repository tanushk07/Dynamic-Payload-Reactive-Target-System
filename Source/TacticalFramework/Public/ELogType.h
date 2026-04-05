#pragma once

#include "ELogType.generated.h"

UENUM(BlueprintType)
enum class ELogType : uint8
{
	Explosion UMETA(DisplayName = "Explosion"),
	Mission   UMETA(DisplayName = "Mission")
};
