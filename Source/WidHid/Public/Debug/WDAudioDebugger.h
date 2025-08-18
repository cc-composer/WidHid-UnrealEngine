// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#if !UE_BUILD_SHIPPING
#include "ImGuiModule.h"
#endif

#include "WDAudioDebugger.generated.h"

class UAkComponent;

UCLASS()
class WIDHID_API UWDAudioDebugger : public UWorldSubsystem
{
	GENERATED_BODY()

#if UE_BUILD_SHIPPING
protected:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return false; }
#else // if !UE_BUILD_SHIPPING

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	void Update();

private:
	FImGuiDelegateHandle ImGuiDelegateHandle;
	
	TArray<TWeakObjectPtr<UAkComponent>> AmbientEmitters;

	// How often, in seconds, should we update the emitters (AkAmbientSounds) in the world.
	// This does use an actor iterator for all AkAmbientSounds, so this is a potentially slow operation if a lot of actors are in the world.
	double AmbientEmitterRefreshRate = 5.0;
	double LastTimeAmbientEmittersRefreshed = 0.0; // Tracked by the world's GetTimeSeconds().
	
private:
	void DrawAmbientEmitterDebugger();

	void PopulateAmbientEmitters();
	

#endif // UE_BUILD_SHIPPING
};
