#pragma once

#include "EStructuralState.generated.h"

UENUM(BlueprintType)
enum class EStructuralState : uint8
{
	Intact     UMETA(DisplayName = "Intact"),
	Damaged    UMETA(DisplayName = "Damaged"),
	Destroyed  UMETA(DisplayName = "Destroyed")
};
