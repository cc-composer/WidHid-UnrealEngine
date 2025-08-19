// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "WDAudioConfig.generated.h"

struct FWDAudioDebugMixState;

UCLASS(Config = "WidHid Audio", DefaultConfig)
class WIDHID_API UWDAudioConfig : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Project"); }

	UPROPERTY(EditDefaultsOnly, Config, Category = "Debug")
	TArray<FWDAudioDebugMixState> MixStates;
};