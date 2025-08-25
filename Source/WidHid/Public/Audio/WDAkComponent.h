// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#pragma once

#include "CoreMinimal.h"
#include "AkComponent.h"

#include "WDAkComponent.generated.h"

struct FWDObstructionPoint
{
	FVector CurrentLocation = FVector::ZeroVector;
	uint8 bIsObstructed : 1 = false;
};

USTRUCT(BlueprintType)
struct WIDHID_API FWDAudioObstructionData
{
	GENERATED_BODY()

	// The frequency (in seconds) at which obstruction is evaluated and updated. If this value is negative, then obstruction is disabled.
	UPROPERTY(EditAnywhere)
	float ObstructionUpdateRate = -1.0f;

	// The offset from the AkComponent's owner's location to calculate obstruction from.
	UPROPERTY(EditAnywhere)
	FVector ObstructionOffset = FVector::ZeroVector;

	// The half size of the "obstruction shape" that is drawn around this emitter.
	UPROPERTY(EditAnywhere)
	float ObstructionRadius = 100.0f;
};

UCLASS(ClassGroup = "WidHid", meta = (BlueprintSpawnableComponent))
class WIDHID_API UWDAkComponent : public UAkComponent
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	float GetCurrentObstruction() const { return CurrentObstruction; }

protected:
	void UpdateObstruction();
	double LastObstructionUpdateTime = 0.0f;

	void CheckForObstructionsToListener();
	void EvaluateObstruction(const FTraceHandle& Handle, FTraceDatum& Datum);
	uint8 bObstructed : 1 = false;
	FTraceHandle ListenerObstructionTraceHandle;

	void TracePointsOfObstruction();
	void EvaluatePointsOfObstruction(const FTraceHandle& Handle, FTraceDatum& Datum);
	TArray<FTraceHandle> PointsOfObstructionTraceHandle;

	void TraceObstructionFromPoints();
	void EvaluateObstructionFromPoint(const FTraceHandle& Handle, FTraceDatum& Datum);
	TArray<FTraceHandle> ObstructionFromPointTraceHandle;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Obstruction")
	FWDAudioObstructionData ObstructionData;

	// You could use Wwise's default obstruction values, but I prefer having more control with RTPCs, especially with slew rates.
	UPROPERTY(Transient)
	TObjectPtr<UAkRtpc> ObstructionParameter;

private:
	TArray<FVector> RelativeObstructionPointLocations;
	TArray<FWDObstructionPoint> ObstructionPoints;
	float CurrentObstruction = 0.0f;
};
