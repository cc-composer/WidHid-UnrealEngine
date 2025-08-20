// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "WDAudioStatics.generated.h"

/**
 * 
 */
UCLASS()
class WIDHID_API UWDAudioStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

	/**
	 * Gets the current view target of the local controller.
	 * This works for characters currently being controlled OR spectated by the player.
	 */
	UFUNCTION(BlueprintPure, Category = "WidHid", meta = (WorldContext = "WorldContextObject"))
	static ACharacter* GetLocallyViewedPawn(const UObject* WorldContextObject);
};
