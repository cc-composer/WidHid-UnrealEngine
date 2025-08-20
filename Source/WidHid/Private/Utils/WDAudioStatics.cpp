// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#include "Utils/WDAudioStatics.h"

#include "GameFramework/Character.h"

ACharacter* UWDAudioStatics::GetLocallyViewedPawn(const UObject* WorldContextObject)
{
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
    {
        if (APlayerController* LocalPC = World->GetFirstPlayerController())
        {
            return Cast<ACharacter>(LocalPC->GetViewTarget());
        }
    }

    return nullptr;
}
