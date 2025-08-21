// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#include "Debug/WDAudioDebugger.h"

#if !UE_BUILD_SHIPPING
#include "AkAmbientSound.h"
#include "AkAudioEvent.h"
#include "AkComponent.h"
#include "AkGameplayStatics.h"
#include "AkStateValue.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "EngineUtils.h"
#include "imgui.h"
#include "Utils/WDAudioConfig.h"
#include "Utils/WDAudioStatics.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"
#include "Wwise/API/WwiseSpatialAudioAPI.h"

// A few helper functions, as well as a CVAR to activate the debugger from the console.
#pragma region Helper

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

#pragma endregion

#pragma region Overrides

void UWDAudioDebugger::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	FImGuiModule& ImGuiModule = FImGuiModule::Get();
	ImGuiDelegateHandle = ImGuiModule.AddWorldImGuiDelegate(GetWorld(), FImGuiDelegate::CreateUObject(this, &ThisClass::Update));
}

void UWDAudioDebugger::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	PopulateAmbientEmitters();

	if (const UWDAudioConfig* Config = GetDefault<UWDAudioConfig>())
	{
		MixStates = Config->MixStates;
	}

	UAkAudioEvent::OnEventPosted.AddUObject(this, &ThisClass::EventPosted);
}

void UWDAudioDebugger::Deinitialize()
{
	ImGuiDelegateHandle.Reset();
	UAkAudioEvent::OnEventPosted.RemoveAll(this);

	for (const FWDAudioDebugMixState& MixState : MixStates)
	{
		UAkGameplayStatics::SetState(MixState.NeutralState);
	}

	Super::Deinitialize();
}

#pragma endregion

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
#if ENABLE_DRAW_DEBUG
			FlushDebugStrings(World);
			FlushPersistentDebugLines(World);
#endif
			
			DrawRecentlyPostedEvents();
			DrawAmbientEmitterDebugger();
			DrawMixStates();
			DrawCharacterAnimationDebugger();

			ImGui::End();
		}

		// If the status of our window has changed for any reason, make sure our Cvar is reset accordingly.
		if (bIsWindowOpen != bWasWindowOpen)
		{
			WDAudioDebugger::CVarAudioDebuggerVisible->Set(false);
		}
	}
}

#pragma region Ambient Emitter Debugger

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

#if ENABLE_DRAW_DEBUG
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
#endif

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

			// Obstruction, Occlusion, Diffraction, and Transmission
			const FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
			const UAkComponent* Listener = AudioDevice ? AudioDevice->GetSpatialAudioListener() : nullptr;
			const FWDAudioObstructionData Data = FWDAudioObstructionData::Calculate(Emitter, Listener);
			
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%d", static_cast<int>(Data.Obstruction * 100.0f));
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%d", static_cast<int>(Data.Occlusion * 100.0f));
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%d", static_cast<int>(Data.Diffraction * 100.0f));
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%d", static_cast<int>(Data.Transmission * 100.0f));
		}

		ImGui::EndTable();
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

#pragma endregion

#pragma region Mix States

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
			continue;
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

#pragma endregion

#pragma region Character Animation

void UWDAudioDebugger::DrawCharacterAnimationDebugger()
{
	// This isn't specific to audio, so can be used in a more general debugger, but it is very useful for systems that use a lot of audio animation notifies. 
	// Heavily simplifies the process of figuring out which animations are currently playing.
	if (ImGui::CollapsingHeader("Character Animation Debugger"))
	{
		// This currently only tracks the animations of the local pawn, but this method can done with anything that has a skeletal mesh you have access to.
		if (ImGui::BeginTable("Current Local Animations", /* Columns */ 3))
		{
			ImGui::TableSetupColumn("Animation Instance");
			ImGui::TableSetupColumn("Animation");
			ImGui::TableSetupColumn("Weight");
			ImGui::TableHeadersRow();

			const ACharacter* LocalCharacter = UWDAudioStatics::GetLocallyViewedPawn(this);
			if (!LocalCharacter)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No valid viewed pawn.");
				return;
			}

			const USkeletalMeshComponent* Mesh = LocalCharacter->GetMesh();
			if (!Mesh)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Cannot find a valid skeletal mesh for %s.", TCHAR_TO_ANSI(*LocalCharacter->GetActorNameOrLabel()));
				return;
			}

			UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
			if (!AnimInstance)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No valid animation instance for %s's skeletal mesh.", TCHAR_TO_ANSI(*LocalCharacter->GetActorNameOrLabel()));
				return;
			}

			// Animations from an active blendspace.
			const FAnimInstanceProxy::FSyncGroupMap& SyncGroupMap = AnimInstance->GetSyncGroupMapRead();
			const ANSICHAR* AnimInstanceName = TCHAR_TO_ANSI(*AnimInstance->GetName());
			for (const TTuple<FName, FAnimGroupInstance>& SyncGroupPair : SyncGroupMap)
			{
				for (const FAnimTickRecord& Record : SyncGroupPair.Value.ActivePlayers)
				{
					if (!Record.bIsExclusiveLeader && Record.EffectiveBlendWeight > 0.0f)
					{
						const ANSICHAR* AnimationName = TCHAR_TO_ANSI(*Record.SourceAsset->GetName());
						const float Weight = Record.EffectiveBlendWeight * 100.0f;
						DrawAnimationData(AnimInstanceName, AnimationName, Weight);
					}
				}
			}

			// Individual animations
			const TArray<FAnimTickRecord>& UngroupedActivePlayers = AnimInstance->GetUngroupedActivePlayersRead();
			for (const FAnimTickRecord& Record : UngroupedActivePlayers)
			{
				if (Record.EffectiveBlendWeight > 0.0f)
				{
					const ANSICHAR* AnimationName = TCHAR_TO_ANSI(*Record.SourceAsset->GetName());
					const float Weight = Record.EffectiveBlendWeight * 100.0f;
					DrawAnimationData(AnimInstanceName, AnimationName, Weight);
				}
			}

			// Basic animation montages
			for (const FAnimMontageInstance* MontageInstance : AnimInstance->MontageInstances)
			{
				if (MontageInstance && MontageInstance->Montage)
				{
					const ANSICHAR* AnimationName = TCHAR_TO_ANSI(*MontageInstance->Montage->GetName());
					const float Weight = MontageInstance->GetWeight() * 100.0f;
					DrawAnimationData(AnimInstanceName, AnimationName, Weight);
				}
			}

			ImGui::EndTable();
		}
	}
}

void UWDAudioDebugger::DrawAnimationData(const ANSICHAR* AnimationInstanceName, const ANSICHAR* AnimationName, const float AnimationWeight)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn();

	ImGui::Text(AnimationInstanceName);
	ImGui::TableNextColumn();

	ImGui::Text(AnimationName);
	ImGui::TableNextColumn();

	ImGui::Text("%f", AnimationWeight);
}

#pragma endregion

#pragma region Event Tracker

void UWDAudioDebugger::DrawRecentlyPostedEvents()
{
	if (!bDisplayingEventWindow)
	{
		ImGui::Selectable("Show Recently Posted Events", &bDisplayingEventWindow);
	}
	else
	{
		ImGui::Selectable("Hide Event Window", &bDisplayingEventWindow);
		if (ImGui::Begin("Event Window", &bDisplayingEventWindow, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (ImGui::BeginTable("Last 100 Events", /* Columns */ 3))
			{
				ImGui::TableSetupColumn("World Time Posted");
				ImGui::TableSetupColumn("Event Name");
				ImGui::TableSetupColumn("Game Object Name");
				ImGui::TableHeadersRow();

				for (const FWDAudioDebugEventInformation& PostedEvent : Last100PostedEvents)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					// World Time Posted
					ImGui::Text("%f", PostedEvent.WorldTimePosted);
					ImGui::TableNextColumn();

					// Event - Colored red if the event is invalid.
					const UAkAudioEvent* Event = PostedEvent.Event;
					const FString EventName = Event ? Event->GetName() : "Invalid Event";
					const ImVec4 EventColor = Event ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
					ImGui::TextColored(EventColor, TCHAR_TO_ANSI(*EventName));
					ImGui::TableNextColumn();

					// Game Object (UAkComponent) - Colored red if the event AND game object is invalid.
					const UAkGameObject* GameObject = PostedEvent.GameObject;
					FString GameObjectName = Event ? EventName : "Invalid Game Object";
					if (GameObject)
					{
						GameObjectName = GameObject->GetOwner() ? GameObject->GetOwner()->GetActorNameOrLabel() : GameObject->GetName();
					}
					const ImVec4 ObjectColor = Event || GameObject ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
					ImGui::TextColored(ObjectColor, TCHAR_TO_ANSI(*GameObjectName));
				}

				ImGui::EndTable();
			}

			ImGui::End();
		}
	}

	ImGui::Spacing();
}

void UWDAudioDebugger::EventPosted(UAkAudioEvent* Event, UAkGameObject* GameObject)
{
	const UWorld* World = GetWorld();
	const double WorldTime = World ? World->GetTimeSeconds() : -1.0;
	
	Last100PostedEvents.Add({ WorldTime, Event, GameObject });
	if (Last100PostedEvents.Num() > 100)
	{
		Last100PostedEvents.RemoveAt(0);
	}
}

#pragma endregion

#endif // !UE_BUILD_SHIPPING