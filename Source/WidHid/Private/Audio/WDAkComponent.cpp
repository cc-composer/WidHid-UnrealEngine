// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#include "Audio/WDAkComponent.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace WDAkComponent
{
#if ENABLE_DRAW_DEBUG
	constexpr bool bAudioObstructionDebug = false;
	static TAutoConsoleVariable<bool> CVarAudioObstructionDebug(TEXT("wd.AudioObstructionDebug"), bAudioObstructionDebug, TEXT("(Visible = 1; Not Visible = 0) Show a 3d representation of all emitters' obstruction points and total obstruction value."));
#endif
}

void UWDAkComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Need to do a basic override of the AkComponent's tick function in order to avoid the original obstruction implementation, but still maintain the rest of its functionality.
	FAkAudioDevice* AkAudioDevice = FAkAudioDevice::Get();
	UWorld* World = GetWorld();
	if (AkAudioDevice && AkAudioDevice->WorldSpatialAudioVolumesUpdated(World))
	{
		UpdateSpatialAudioRoom(GetComponentLocation());
		
		// Find and apply all AkReverbVolumes at this location
		if (bUseReverbVolumes && AkAudioDevice->GetMaxAuxBus() > 0)
		{
			UpdateAkLateReverbComponentList(GetComponentLocation());
		}
	}

	if (AkAudioDevice && bUseReverbVolumes && AkAudioDevice->GetMaxAuxBus() > 0)
	{
		ApplyAkReverbVolumeList(DeltaTime);
	}
		
	if (IsAutoDestroying() && bEventPosted && !HasActiveEvents())
	{
		DestroyComponent();
	}

	if (ObstructionData.ObstructionUpdateRate >= 0.0f && World->TimeSince(LastObstructionUpdateTime) >= ObstructionData.ObstructionUpdateRate)
	{
		UpdateObstruction();
		LastObstructionUpdateTime = World->GetTimeSeconds();
	}
}

void UWDAkComponent::BeginPlay()
{
	Super::BeginPlay();

	const float& HalfHeight = ObstructionData.ObstructionRadius;
	
	// Drawing 8 points in a cube formation around this component's owner.
	RelativeObstructionPointLocations = TArray<FVector>(
		{
			{HalfHeight, HalfHeight, HalfHeight},
			{HalfHeight, -HalfHeight, HalfHeight},
			{-HalfHeight, HalfHeight, HalfHeight},
			{-HalfHeight, -HalfHeight, HalfHeight},
			{HalfHeight, HalfHeight, -HalfHeight},
			{HalfHeight, -HalfHeight, -HalfHeight},
			{-HalfHeight, HalfHeight, -HalfHeight},
			{-HalfHeight, -HalfHeight, -HalfHeight}
		});
	RelativeObstructionPointLocations.Shrink();

	constexpr int32 NumElements = 8;
	ObstructionPoints.Init(FWDObstructionPoint(), NumElements);
	ObstructionPoints.Shrink();

	PointsOfObstructionTraceHandle.Init(FTraceHandle(), NumElements);
	PointsOfObstructionTraceHandle.Shrink();

	ObstructionFromPointTraceHandle.Init(FTraceHandle(), NumElements);
	ObstructionFromPointTraceHandle.Shrink();
}

void UWDAkComponent::UpdateObstruction()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	
	if (!ListenerObstructionTraceHandle.IsValid())
	{
		// Step 1. Trace a line directly from the emitter to the listener to see if they're obstructed in the first place.
		CheckForObstructionsToListener();
	}

	if (bObstructed)
	{
		// Step 2. Trace a line from the emitter to pre-designated points around the emitter.
		// We do traces here because we don't want these points to end up on the other side of a wall from the emitter.
		TracePointsOfObstruction();

		// Step 3. Trace a line from ALL of our newly-place points to the listener.
		// Those that hit an object are considered to be "obstructed points".
		// After each of these points are evaluated, this emitter's current obstruction level is re-calculated.
		TraceObstructionFromPoints();
	}

#if ENABLE_DRAW_DEBUG	
	if (WDAkComponent::CVarAudioObstructionDebug.GetValueOnGameThread())
	{
		const UWorld* World = GetWorld();
		FlushPersistentDebugLines(World);
		FlushDebugStrings(World);
		
		if (!bObstructed)
		{
			DrawDebugString(GetWorld(), GetComponentLocation(), "Not Obstructed");
		}
		else
		{		
			for (const FWDObstructionPoint& Point : ObstructionPoints)
			{
				const FVector& Center = Point.CurrentLocation;
				constexpr float Radius = 10.0f;
				constexpr int32 Segments = 5;
				const FColor Color = Point.bIsObstructed ? FColor::Green : FColor::Red;
				constexpr bool bPersistentLines = true;

				DrawDebugSphere(World, Center, Radius, Segments, Color, bPersistentLines);
			}

			const FString Text = FString::FromInt(static_cast<int32>(CurrentObstruction * 100.0f)) + "%";
			const FColor Color = FColor::MakeRedToGreenColorFromScalar(CurrentObstruction);

			DrawDebugString(World, GetComponentLocation(), Text, /* TestBaseActor */ nullptr, Color);
		}
	}

#endif
}

void UWDAkComponent::CheckForObstructionsToListener()
{
	const FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
	if (!AudioDevice)
	{
		return;
	}

	const UAkComponent* Listener = AudioDevice->GetSpatialAudioListener();
	if (!Listener)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Start = GetComponentLocation();
	const FVector End = Listener->GetComponentLocation();

	// May want to consider tracing complex at some point.
	FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bIgnoreTouches = true;

	const FTraceDelegate Delegate = FTraceDelegate::CreateUObject(this, &ThisClass::EvaluateObstruction);

	// TODO: Need to update the collision channel to something more appropriate, but visibility works for now.
	ListenerObstructionTraceHandle = World->AsyncLineTraceByChannel(EAsyncTraceType::Test, Start, End, ECollisionChannel::ECC_Visibility, QueryParams, FCollisionResponseParams::DefaultResponseParam, &Delegate);
}

void UWDAkComponent::EvaluateObstruction(const FTraceHandle& Handle, FTraceDatum& Datum)
{
	// Just need to check if we hit anything at all.
	bObstructed = !Datum.OutHits.IsEmpty();
	ListenerObstructionTraceHandle.Invalidate();
}

void UWDAkComponent::TracePointsOfObstruction()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Start = Owner->GetActorLocation() + ObstructionData.ObstructionOffset;

	FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bIgnoreTouches = true;

	const FTraceDelegate Delegate = FTraceDelegate::CreateUObject(this, &ThisClass::EvaluatePointsOfObstruction);

	for (int32 i = 0; i < RelativeObstructionPointLocations.Num(); ++i)
	{
		FTraceHandle& Handle = PointsOfObstructionTraceHandle[i];
		
		// Only trace this point if it's not currently being traced (since this is being done async).
		if (!Handle.IsValid())
		{
			// Using the offset locations initialized in BeginPlay(), trace towards points in a cube shape around this emitter's owner.
			const FVector End = Start + RelativeObstructionPointLocations[i];

			Handle = World->AsyncLineTraceByChannel(EAsyncTraceType::Single, Start, End, ECollisionChannel::ECC_Visibility, QueryParams, FCollisionResponseParams::DefaultResponseParam, &Delegate, i);
		}
	}
}

void UWDAkComponent::EvaluatePointsOfObstruction(const FTraceHandle& Handle, FTraceDatum& Datum)
{
	const uint32 Index = Datum.UserData;

	// If the trace didn't hit anything, simply place the point at this trace's end location.
	ObstructionPoints[Index].CurrentLocation = Datum.OutHits.IsEmpty() ? Datum.End : Datum.OutHits[0].Location;
	PointsOfObstructionTraceHandle[Index].Invalidate();
}

void UWDAkComponent::TraceObstructionFromPoints()
{
	const FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
	if (!AudioDevice)
	{
		return;
	}

	const UAkComponent* Listener = AudioDevice->GetSpatialAudioListener();
	if (!Listener)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector ListenerLocation = Listener->GetComponentLocation();

	FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bIgnoreTouches = true;

	const FTraceDelegate Delegate = FTraceDelegate::CreateUObject(this, &ThisClass::EvaluateObstructionFromPoint);

	for (int32 i = 0; i < RelativeObstructionPointLocations.Num(); ++i)
	{
		FTraceHandle& Handle = ObstructionFromPointTraceHandle[i];

		// Do not make a new trace if one is already being performed for this point.
		if (!Handle.IsValid())
		{
			const FWDObstructionPoint& Point = ObstructionPoints[i];
			
			const FVector Start = Point.CurrentLocation;
			const FVector& End = ListenerLocation;

			Handle = World->AsyncLineTraceByChannel(EAsyncTraceType::Test, Start, End, ECollisionChannel::ECC_Visibility, QueryParams, FCollisionResponseParams::DefaultResponseParam, &Delegate, i);
		}
	}
}

void UWDAkComponent::EvaluateObstructionFromPoint(const FTraceHandle& Handle, FTraceDatum& Datum)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	
	const uint32 Index = Datum.UserData;

	ObstructionPoints[Index].bIsObstructed = !Datum.OutHits.IsEmpty();
	ObstructionFromPointTraceHandle[Index].Invalidate();

	int32 TotalObstructedPoints = 0;
	for (const FWDObstructionPoint& Point : ObstructionPoints)
	{
		if (Point.bIsObstructed)
		{
			++TotalObstructedPoints;
		}
	}

	const float Obstruction = static_cast<float>(TotalObstructedPoints) / static_cast<float>(ObstructionPoints.Num());
	if (Obstruction != CurrentObstruction)
	{
		CurrentObstruction = Obstruction;
		SetRTPCValue(ObstructionParameter, CurrentObstruction * 100.0f, /* Interpolation */ 0, /* Optional parameter name */ FString());
	}
}
