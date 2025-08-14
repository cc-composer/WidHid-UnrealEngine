// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGame/WidHidGameMode.h"

#include "BaseGame/WidHidCharacter.h"
#include "UObject/ConstructorHelpers.h"

AWidHidGameMode::AWidHidGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
