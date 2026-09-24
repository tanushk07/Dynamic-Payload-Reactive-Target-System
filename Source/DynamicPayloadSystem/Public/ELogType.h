// Copyright Tanushk Nirmal 2026 All Rights Reserved.

#pragma once

#include "ELogType.generated.h"

UENUM(BlueprintType)
enum class ELogType : uint8
{
	Explosion UMETA(DisplayName = "Explosion"),
	Mission   UMETA(DisplayName = "Mission")
};
