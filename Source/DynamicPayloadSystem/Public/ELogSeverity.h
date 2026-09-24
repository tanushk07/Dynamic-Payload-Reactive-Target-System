// Copyright Tanushk Nirmal 2026 All Rights Reserved.

#pragma once

#include "ELogSeverity.generated.h"

UENUM(BlueprintType)
enum class ELogSeverity : uint8
{
	Info     UMETA(DisplayName = "Info"),
	Warning  UMETA(DisplayName = "Warning"),
	Critical UMETA(DisplayName = "Critical")
};
