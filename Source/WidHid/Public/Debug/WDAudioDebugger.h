// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#if !UE_BUILD_SHIPPING
#include "ImGuiModule.h"
#endif

#include "WDAudioDebugger.generated.h"

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

	void Update();

private:
	FImGuiDelegateHandle ImGuiDelegateHandle;

private:
	void DrawAmbientEmitterDebugger();

#endif // UE_BUILD_SHIPPING
};
