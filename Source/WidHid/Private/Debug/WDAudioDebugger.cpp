// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#include "Debug/WDAudioDebugger.h"

#if !UE_BUILD_SHIPPING
#include "AkAmbientSound.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkGameplayStatics.h"
#include "AkStateValue.h"
#include "EngineUtils.h"
#include "imgui.h"
#include "Utils/WDAudioConfig.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"
#include "Wwise/API/WwiseSpatialAudioAPI.h"

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

	if (const UWDAudioConfig* Config = GetDefault<UWDAudioConfig>())
	{
		MixStates = Config->MixStates;
	}
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
			DrawMixStates();
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
	// TODO: Cleanup
	if (ImGui::CollapsingHeader("Ambient Emitter Debugger"))
	{
		// Ambient emitters should be refreshed every so often since they could be spawning/despawning from the world.
		const UWorld* World = GetWorld();
		const double CurrentTime = World->GetTimeSeconds();
		if (CurrentTime - LastTimeAmbientEmittersRefreshed >= AmbientEmitterRefreshRate)
		{
			PopulateAmbientEmitters();
		}
		
		ImGui::BeginTable("Audible Ambient Emitters", 7);
		
		ImGui::TableSetupColumn("Sound");
		ImGui::TableSetupColumn("Location");
		ImGui::TableSetupColumn("Distance");
		ImGui::TableSetupColumn("Obstruction");
		ImGui::TableSetupColumn("Occlusion");
		ImGui::TableSetupColumn("Diffraction");
		ImGui::TableSetupColumn("Transmission");
		ImGui::TableHeadersRow();

		for (auto Itr = AmbientEmitters.CreateIterator(); ++Itr; Itr)
		{
			const UAkComponent* Emitter = Itr->Get();
			if (!Emitter)
			{
				// This emitter is no longer existent/valid.
				Itr.RemoveCurrent();
				continue;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			if (!Emitter->AkAudioEvent)
			{
				const AActor* Owner = Emitter->GetOwner();
				const FString Name = Owner ? Owner->GetActorNameOrLabel() : Emitter->GetName();
				const ImVec4 Color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

				ImGui::TextColored(Color, "%s does not have an event associated to it. No sound is being played.", *Name);
				continue;
			}
			
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

			// Sound Name
			ImGui::Text("%s", TCHAR_TO_ANSI(*Text));

			// Location
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d, %d, %d",
				static_cast<int>(Location.X),
				static_cast<int>(Location.Y),
				static_cast<int>(Location.Z));

			// Distance
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%d", static_cast<int>(WDAudioDebugger::GetDistanceFromListener(Location)));

			// Obstruction and Occlusion
			const AkGameObjectID EmitterID = Emitter->GetAkGameObjectID();
			AkGameObjectID ListenerID = 0;
			if (FAkAudioDevice* AudioDevice = FAkAudioDevice::Get())
			{
				if (const UAkComponent* Listener = AudioDevice->GetSpatialAudioListener())
				{
					ListenerID = Listener->GetAkGameObjectID();
				}
			}

			AkReal32 ObstructionLevel = 0.0f;
			AkReal32 OcclusionLevel = 0.0f;
			if (const IWwiseSoundEngineAPI* SoundEngine = IWwiseSoundEngineAPI::Get())
			{
				if (SoundEngine->Query)
				{					
					SoundEngine->Query->GetObjectObstructionAndOcclusion(EmitterID, ListenerID, ObstructionLevel, OcclusionLevel);
				}
			}

			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%d", static_cast<int>(ObstructionLevel * 100.0f));
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%d", static_cast<int>(OcclusionLevel * 100.0f));

			// Diffraction and Transmission
			AkReal32 DiffractionLevel = 0.0f;
			AkReal32 TransmissionLevel = 0.0f;
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
						DiffractionLevel = DiffractionPathInfo[1].diffraction * 100.0f;
					}

					TransmissionLevel = DiffractionPathInfo[0].transmissionLoss * 100.0f;
				}
			}

			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%d", static_cast<int>(DiffractionLevel * 100.0f));
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%d", static_cast<int>(TransmissionLevel * 100.0f));
		}

		ImGui::EndTable();
	}
}

void UWDAudioDebugger::DrawMixStates()
{
	if (ImGui::CollapsingHeader("Mix States"))
	{
		for (FWDAudioDebugMixState& MixState : MixStates)
		{
			if (const UAkStateValue* MuteState = MixState.MuteState)
			{
				FString GroupName = FString();
				FString ValueName = FString();
				const float Spacing = ImGui::GetStyle().ItemInnerSpacing.x;

				// The full state name should be [Category]-Muted
				// For example, Ambience-Muted, which will give us a group name of Ambience.
				MuteState->SplitAssetName(GroupName, ValueName);

				if (ImGui::BeginTable("Mix States", /* Columns */ 3, /* Flags */ 0, /* Outer Size */ ImVec2(220.0f, 0.0f)))
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					
					ImGui::Text(TCHAR_TO_ANSI(*GroupName));

					ImGui::TableNextColumn();

					// ImGui uses strings as IDs when identifying UI elements. If we wrote "Solo" for all checkboxes, then selecting one of those checkboxes will check them all simultaneously.
					// Everything after the ## will not show up in the actual UI text, BUT it will still augment the ID so we can still have multiple checkboxes with the same label.
					const FString SoloLabel = FString::Printf(TEXT("Solo##%s"), *GroupName);
					if (ImGui::Checkbox(TCHAR_TO_ANSI(*SoloLabel), &MixState.bSoloed))
					{
						PostSoloed(MixState);
					}

					ImGui::TableNextColumn();

					const FString MuteLabel = FString::Printf(TEXT("Mute##%s"), *GroupName);
					if (ImGui::Checkbox(TCHAR_TO_ANSI(*MuteLabel), &MixState.bMuted))
					{
						PostMuted(MixState);
					}

					ImGui::EndTable();
				}
			}
		}
	}
}

void UWDAudioDebugger::PostSoloed(FWDAudioDebugMixState& MixState)
{
	if (MixState.bSoloed)
	{
		// Can't be simultaneously muted and soloed.
		MixState.bMuted = false;

		UAkGameplayStatics::SetState(MixState.NeutralState);
	}

	bool bAnotherStateSoloed = false;

	// If this state was just soloed, all other non-soloed states should be muted.
	// Otherwise, all other states should be unmuted.
	for (FWDAudioDebugMixState& OtherMixState : MixStates)
	{
		if (MixState.MuteState == OtherMixState.MuteState)
		{
			continue;
		}

		if (OtherMixState.bSoloed)
		{
			bAnotherStateSoloed = true;
		}

		OtherMixState.bMuted = MixState.bSoloed;

		const UAkStateValue* StateToSet = MixState.bSoloed ? OtherMixState.MuteState : OtherMixState.NeutralState;
		UAkGameplayStatics::SetState(StateToSet);
	}

	if (!MixState.bSoloed && bAnotherStateSoloed)
	{
		// When unsoloing a mix state, it should be muted if another state is currently soloed.
		UAkGameplayStatics::SetState(MixState.MuteState);
	}
}

void UWDAudioDebugger::PostMuted(FWDAudioDebugMixState& MixState)
{
	const UAkStateValue* StateToSet = MixState.bMuted ? MixState.MuteState : MixState.NeutralState;
	UAkGameplayStatics::SetState(StateToSet);

	// If this state was soloed, unsolo it and unmute all other states.
	if (MixState.bMuted && MixState.bSoloed)
	{
		MixState.bSoloed = false;

		for (FWDAudioDebugMixState& OtherMixState : MixStates)
		{
			if (MixState.MuteState == OtherMixState.MuteState)
			{
				continue;
			}

			OtherMixState.bMuted = false;
			UAkGameplayStatics::SetState(OtherMixState.NeutralState);
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