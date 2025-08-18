// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#include "Debug/WDAudioDebugger.h"

#if !UE_BUILD_SHIPPING
#include "AkAmbientSound.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "imgui.h"
#include "EngineUtils.h"

namespace WDAudioDebugger
{
	constexpr bool bAudioDebuggerVisible = false;
	static TAutoConsoleVariable<bool> CVarAudioDebuggerVisible(TEXT("wd.AudioDebuggerVisible"), bAudioDebuggerVisible, TEXT("(Visible = 1; Not Visible = 0) Show window with various audio debug functionality and information."));

	float GetDistanceFromListener(const FVector& Location)
	{
		const FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
		if (!AudioDevice)
		{
			return 0.0f;
		}

		const UAkComponent* Listener = AudioDevice->GetSpatialAudioListener();
		if (!Listener)
		{
			return 0.0f;
		}

		return FVector::Distance(Location, Listener->GetComponentLocation());
	}
	
	FColor GetEmitterDistanceColor(const UAkComponent* Emitter, const int SoundMaxAttenuation)
	{
		// Sounds with no attenuation (2D sounds) can't be tracked in physical space.
		// This prevents a division by 0 later.
		// If a 3d sound is being marked as a 2D sound, you may want to check if there are multiple actions in the event that could be producing a 0 attenuation value.
		if (!Emitter || SoundMaxAttenuation == 0)
		{
			return FColor::Black;
		}
		
		const float PercentageOfAttenuation = GetDistanceFromListener(Emitter->GetComponentLocation() / static_cast<float>(SoundMaxAttenuation));

		// A gradual gradient from red to green as the listener gets closer to the emitter's center.
		return FColor::MakeRedToGreenColorFromScalar(1.0f - PercentageOfAttenuation);
	}

	bool InAudibleRange(const FVector& TargetLocation, const UAkAudioEvent* Sound)
	{
		if (!Sound)
		{
			return false;
		}

		return GetDistanceFromListener(TargetLocation) <= Sound->MaxAttenuationRadius;
	}
}

void UWDAudioDebugger::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	FImGuiModule& ImGuiModule = FImGuiModule::Get();
	ImGuiDelegateHandle = ImGuiModule.AddWorldImGuiDelegate(GetWorld(), FImGuiDelegate::CreateUObject(this, &ThisClass::Update));
}

void UWDAudioDebugger::Deinitialize()
{
	ImGuiDelegateHandle.Reset();

	Super::Deinitialize();
}

void UWDAudioDebugger::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	PopulateAmbientEmitters();
}

void UWDAudioDebugger::Update()
{
	bool bIsWindowOpen = WDAudioDebugger::CVarAudioDebuggerVisible.GetValueOnGameThread();
	if (bIsWindowOpen)
	{
		const bool bWasWindowOpen = bIsWindowOpen;
		
		if (ImGui::Begin("Audio Debugger", &bIsWindowOpen))
		{
			// Might not keep this since this will flush debug shapes outside of this debugger too, but it works for now.
			UWorld* World = GetWorld();
			FlushDebugStrings(World);
			FlushPersistentDebugLines(World);
			
			DrawAmbientEmitterDebugger();
			ImGui::End();
		}

		// If the status of our window has changed for any reason, make sure our Cvar is reset accordingly.
		if (bIsWindowOpen != bWasWindowOpen)
		{
			WDAudioDebugger::CVarAudioDebuggerVisible->Set(false);
		}
	}
}

void UWDAudioDebugger::DrawAmbientEmitterDebugger()
{
	if (ImGui::CollapsingHeader("Ambient Emitter Debugger"))
	{
		// Ambient emitters should be refreshed every so often since they could be spawning/despawning from the world.
		const UWorld* World = GetWorld();
		const double CurrentTime = World->GetTimeSeconds();
		if (CurrentTime - LastTimeAmbientEmittersRefreshed >= AmbientEmitterRefreshRate)
		{
			PopulateAmbientEmitters();
		}
		
		for (auto Itr = AmbientEmitters.CreateIterator(); ++Itr; Itr)
		{
			const UAkComponent* Emitter = Itr->Get();
			if (Emitter)
			{
				if (Emitter->AkAudioEvent)
				{
					// Only display debug objects and details for sounds that are actually within audible range.
					if (!WDAudioDebugger::InAudibleRange(Emitter->GetComponentLocation(), Emitter->AkAudioEvent))
					{
						continue;
					}

					// Draw a sphere at the location of the emitter, shifting it from red to green as the listener gets closer to it.
					const FVector Location = Emitter->GetComponentLocation();
					constexpr float Radius = 10.0f;
					constexpr int32 Segments = 5;
					const FColor Color = WDAudioDebugger::GetEmitterDistanceColor(Emitter, Emitter->AkAudioEvent->MaxAttenuationRadius);
					constexpr bool bPersistentLines = true;
					DrawDebugSphere(World, Location, Radius, Segments, Color, bPersistentLines);

					// Display the emitter's name underneath the sphere.
					const FString Text = Emitter->GetOwner()->GetActorNameOrLabel();
					const FVector TextOffset = FVector(0.0f, 0.0f, 10.0f);
					DrawDebugString(World, Location - TextOffset, Text, nullptr, Color);
				}
				else // if (!Emitter->AkAudioEvent)
				{
					const AActor* Owner = Emitter->GetOwner();
					const FString Name = Owner ? Owner->GetActorNameOrLabel() : Emitter->GetName();
					const ImVec4 Color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

					ImGui::TextColored(Color, "%s does not have an event associated to it. No sound is being played.", *Name);
				}
			}
			else // if (!Emitter)
			{
				// This emitter is no longer existent/valid.
				Itr.RemoveCurrent();
				continue;
			}
		}
	}
}

void UWDAudioDebugger::PopulateAmbientEmitters()
{
	// Empty the array, but keep the slack. It's to be expected that at least a grand majority of the elements being emptied will be refilled.
	// Especially since we're already consistently doing cleanup in DrawAmbientEmitterDebugger().
	AmbientEmitters.Empty(AmbientEmitters.GetSlack());
	
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	
	for (TActorIterator<AAkAmbientSound> Itr(World); Itr; ++Itr)
	{
		if (const AAkAmbientSound* AmbientSound = *Itr)
		{
			AmbientEmitters.Add(AmbientSound->AkComponent);
		}
	}

	LastTimeAmbientEmittersRefreshed = World->GetTimeSeconds();
}
#endif // !UE_BUILD_SHIPPING