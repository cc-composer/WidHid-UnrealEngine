// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#if !UE_BUILD_SHIPPING
#include "ImGuiModule.h"
#endif

#include "WDAudioDebugger.generated.h"

class UAkComponent;
class UAkStateValue;

USTRUCT(BlueprintType)
struct WIDHID_API FWDAudioDebugMixState
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<UAkStateValue> NeutralState = nullptr;

	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<UAkStateValue> MuteState = nullptr;

	bool bMuted = false;
	bool bSoloed = false;
};

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
	
private:
	// Ambient Emitter Debugger
	void DrawAmbientEmitterDebugger();
	void PopulateAmbientEmitters();

	TArray<TWeakObjectPtr<UAkComponent>> AmbientEmitters;

	// How often, in seconds, should we update the emitters (AkAmbientSounds) in the world.
	// This does use an actor iterator for all AkAmbientSounds, so this is a potentially slow operation if a lot of actors are in the world.
	double AmbientEmitterRefreshRate = 5.0;
	double LastTimeAmbientEmittersRefreshed = 0.0; // Tracked by the world's GetTimeSeconds().

private:
	// Mix States
	void DrawMixStates();
	void PostMuted(FWDAudioDebugMixState& MixState);
	void PostSoloed(FWDAudioDebugMixState& MixState);

private:
	// Character Animation Debugger
	void DrawCharacterAnimationDebugger();

#endif // UE_BUILD_SHIPPING

	// This must be placed outside of UE_BUILD_SHIPPING.
	// Unreal throws compilation errors when UPROPERTYs are wrapped with most macros.
	// WITH_EDITORONLY_DATA is one of the few exceptions.
private:
	// UPROPERTYs are necessary to ensure these don't get tossed from garbage collection.
	
	UPROPERTY(Transient)
	TArray<FWDAudioDebugMixState> MixStates;
};
