// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#include "Audio/WDAudioQueueSubsystem.h"

#include "AkAudioEvent.h"
#include "Logging/StructuredLog.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Utils/WDAudioConfig.h"

DEFINE_LOG_CATEGORY(LogWDQueue);

static constexpr double TimeBetweenQueuedAudio = 0.5;

namespace WDAudioQueueSubsystem
{
#if !UE_BUILD_SHIPPING
	FAutoConsoleCommand TestAudioQueueCommand(TEXT("wd.TestAudioQueue"), TEXT("Play a series of sounds from the UWDAudioQueueSubsystem. You may modify the test sounds through the WDAudio Config section of the Project Settings."), FConsoleCommandDelegate::CreateLambda([]
		{
			const UWorld* World = GWorld;
			if (!World)
			{
				return;
			}

			const UWDAudioConfig* Config = GetDefault<UWDAudioConfig>();
			if (!Config)
			{
				return;
			}

			UWDAudioQueueSubsystem* AudioQueueSubsystem = World->GetSubsystem<UWDAudioQueueSubsystem>();
			if (!AudioQueueSubsystem)
			{
				return;
			}

			for (const FWDQueueAudio QueueAudio : Config->AudioTestQueue)
			{
				AudioQueueSubsystem->Enqueue(QueueAudio);
			}
		}));
#endif
};

static void EndOfEventCallback(AkCallbackType Type, AkCallbackInfo* CallbackInfo)
{
	// Be careful in this function. We are in the AK event manager thread here, not the game thread, so we're prone to race conditions.
	if (Type == AkCallbackType::AK_EndOfEvent && CallbackInfo)
	{
		if (UWDAudioQueueSubsystem* QueueSubsystem = reinterpret_cast<UWDAudioQueueSubsystem*>(CallbackInfo->pCookie))
		{
			if (const UWorld* World = QueueSubsystem->GetWorld())
			{
				const double Time = World->GetTimeSeconds();
				QueueSubsystem->SetNextAllowedPlayTime(Time);

				UE_LOGFMT(LogWDQueue, Verbose, "{Function}: Queued sound ended. Next allowed play time set as {Time}", __FUNCTION__, Time);
			}
		}
	}
}

void UWDAudioQueueSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);

	const FTimerDelegate DequeueDelegate = FTimerDelegate::CreateUObject(this, &ThisClass::DequeueNext);
	constexpr float Rate = 0.1f;
	constexpr bool bLooping = true;
	World.GetTimerManager().SetTimer(QueueTimerHandle, DequeueDelegate, Rate, bLooping);
}

void UWDAudioQueueSubsystem::Deinitialize()
{
	QueueTimerHandle.Invalidate();
	
	Super::Deinitialize();
}

void UWDAudioQueueSubsystem::Enqueue(FWDQueueAudio QueueAudio)
{
	if (!bQueueOpen)
	{
		return;
	}

	if (!QueueAudio.AudioEvent)
	{
		UE_LOGFMT(LogWDQueue, Error, "{Function}: An invalid event was attempted to be added to the queue.", __FUNCTION__);
		return;
	}
	
	const double CurrentTime = GetWorld()->GetTimeSeconds();
	QueueAudio.TimeQueued = CurrentTime;
	
	int32 InsertIndex = Queue.Num();
	for (int32 i = 0; i < Queue.Num(); ++i)
	{
		if (QueueAudio.Priority >= Queue[i].Priority)
		{
			InsertIndex = QueueAudio.Priority > Queue[i].Priority ? i : i + 1;
			break;
		}
	}

	Queue.Insert(QueueAudio, InsertIndex);
	UE_LOGFMT(LogWDQueue, Verbose, "{Function}: {Event} queued at index {Index}", __FUNCTION__, QueueAudio.AudioEvent->GetName(), InsertIndex);
}

void UWDAudioQueueSubsystem::DequeueNext()
{
	if (Queue.IsEmpty() || bQueueFrozen)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR(__FUNCTION__);
	
	const double CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime >= GetNextAllowedPlayTime() + TimeBetweenQueuedAudio)
	{
		for (auto Itr = Queue.CreateIterator(); Itr; ++Itr)
		{
			const FWDQueueAudio QueueAudio = *Itr;

			// This is to taste on whether you want invalid elements to remain in the queue.
			// Depends on the implementation.
			Itr.RemoveCurrent();
			if (CanBeDequeued(QueueAudio))
			{
				Play(QueueAudio);
				break;
			}

			// Iterator is invalidated, don't reference it beyond here.
		}
	}
}

void UWDAudioQueueSubsystem::Play(const FWDQueueAudio& QueueAudio)
{
	if (UAkAudioEvent* AudioEvent = QueueAudio.AudioEvent)
	{
		const AkCallbackFunc Callback = &EndOfEventCallback;
		
		if (AudioEvent->PostAmbient(/* Delegate */ nullptr, Callback, this, AkCallbackType::AK_EndOfEvent, /* Latent Action */ nullptr))
		{
			// The maximum duration isn't necessarily going to be the actual length of the sound due to things like random containers.
			// The play time will be updated to a more appropriate value during EndOfEventCallback()
			const double CurrentTime = GetWorld()->GetTimeSeconds();
			SetNextAllowedPlayTime(CurrentTime + AudioEvent->MaximumDuration);

			UE_LOGFMT(LogWDQueue, Verbose, "{Function}: {Event} played from the queue.", __FUNCTION__, QueueAudio.AudioEvent->GetName());
		}
	}
}

bool UWDAudioQueueSubsystem::CanBeDequeued(const FWDQueueAudio& QueueAudio) const
{
	if (!QueueAudio.AudioEvent)
	{
		UE_LOGFMT(LogWDQueue, Error, "{Function}: An invalid event was attempted to be played from the queue.", __FUNCTION__);
		return false;
	}

	if (GetWorld()->TimeSince(QueueAudio.TimeQueued) > QueueAudio.MaxAllowedQueueTime)
	{
		UE_LOGFMT(LogWDQueue, Warning, "{Function}: {Event} expired from the queue.", __FUNCTION__, QueueAudio.AudioEvent->GetName());
		return false;
	}

	return true;
}
