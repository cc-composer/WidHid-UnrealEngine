// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#include "Utils/WDAudioStatics.h"

#include "AkComponent.h"
#include "GameFramework/Character.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"
#include "Wwise/API/WwiseSpatialAudioAPI.h"

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

FWDAudioObstructionData FWDAudioObstructionData::Calculate(const UAkComponent* Emitter, const UAkComponent* Listener)
{
    FWDAudioObstructionData Data = FWDAudioObstructionData();
    if (!Emitter || !Listener)
    {
        return Data;
    }

    const AkGameObjectID EmitterID = Emitter->GetAkGameObjectID();
    const AkGameObjectID ListenerID = Listener->GetAkGameObjectID();

    if (const IWwiseSoundEngineAPI* SoundEngine = IWwiseSoundEngineAPI::Get())
    {
        if (SoundEngine->Query)
        {
            SoundEngine->Query->GetObjectObstructionAndOcclusion(EmitterID, ListenerID, Data.Obstruction, Data.Occlusion);
        }
    }

    if (IWwiseSpatialAudioAPI* SpatialAudio = IWwiseSpatialAudioAPI::Get())
    {
        AkVector64 ListenerPosition(0.0, 0.0, 0.0);
        AkVector64 EmitterPosition(0.0, 0.0, 0.0);
        AkDiffractionPathInfo DiffractionPathInfo[3];
        AkUInt32 ArraySize = 3;

        SpatialAudio->QueryDiffractionPaths(EmitterID, 0 /* PositionIndex */, ListenerPosition, EmitterPosition, DiffractionPathInfo, ArraySize);

        // The first path is the direct path from the emitter to the listener, defining the "transmission loss" in all of the geometry it passes through.
        // As long as there is no line of sight between the emitter and the listener, the second path is the shortest path through Acoustic Portals from
        // the emitter to the listener, defining the smallest viable "diffraction".
        if (ArraySize >= 2)
        {
            // 100%+ diffraction paths are always discarded since those are considered to be out of audible range.
            if (DiffractionPathInfo[1].diffraction < 1.0f)
            {
                Data.Diffraction = DiffractionPathInfo[1].diffraction * 100.0f;
            }

            Data.Transmission = DiffractionPathInfo[0].transmissionLoss * 100.0f;
        }
    }

    return Data;
}
