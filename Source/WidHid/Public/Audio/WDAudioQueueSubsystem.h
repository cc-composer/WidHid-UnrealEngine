// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "WDAudioQueueSubsystem.generated.h"

WIDHID_API DECLARE_LOG_CATEGORY_EXTERN(LogWDQueue, Display, All);

class UAkAudioEvent;

USTRUCT(BlueprintType)
struct WIDHID_API FWDQueueAudio
{
	GENERATED_BODY()

	FWDQueueAudio() = default;
	FWDQueueAudio(UAkAudioEvent* Audio) : AudioEvent(Audio) {}

	// The sound to be placed in the queue.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UAkAudioEvent> AudioEvent = nullptr;

	// Higher priority sounds will play from the queue sooner than lower priority sounds.
	// Sounds with equal priority will play in the order that they entered the queue.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "1", UIMin = "1", ClampMax = "10", UIMax = "10"))
	int32 Priority = 5;

	// Maximum time this sound can remain in the queue until it is ejected.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxAllowedQueueTime = 5.0f;

public:
	double TimeQueued = 0.0;
};

UCLASS()
class WIDHID_API UWDAudioQueueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;

public:	
	// Place a sound into the queue. 
	// Sounds of higher priority will play sooner than sounds of lower priority. Sounds of equal priority will play in order of when they were placed in the queue.
	UFUNCTION(BlueprintCallable, Category = "Audio|Queue")
	void Enqueue(FWDQueueAudio QueueAudio);

	// Allow new sounds to be added into the queue.
	UFUNCTION(BlueprintCallable, Category = "Audio|Queue")
	void OpenQueue() { bQueueOpen = true; }

	// Disallow sounds from being added into the queue.
	// NOTE: This does not stop sounds currently in the queue from being played. See FreezeQueue().
	UFUNCTION(BlueprintCallable, Category = "Audio|Queue")
	void CloseQueue() { bQueueOpen = false; }

	// Disallow the queue from playing any sounds.
	// NOTE: This does not stop sounds from being placed into the queue. See CloseQueue().
	UFUNCTION(BlueprintCallable, Category = "Audio|Queue")
	void FreezeQueue() { bQueueFrozen = true; }

	// Allow the queue to continue playing sounds.
	UFUNCTION(BlueprintCallable, Category = "Audio|Queue")
	void UnfreezeQueue() { bQueueFrozen = false; }

public:
	const std::atomic<double>& GetNextAllowedPlayTime() const { return NextAllowedPlayTime; }
	void SetNextAllowedPlayTime(const double Time) { NextAllowedPlayTime = Time; }

private:
	void DequeueNext();
	void Play(const FWDQueueAudio& QueueAudio);

protected:
	bool CanBeDequeued(const FWDQueueAudio& QueueAudio) const;

private:
	// This value needs extra thread safety since it is accessed by both the game and AK thread.
	std::atomic<double> NextAllowedPlayTime = 0.0;
	
	TArray<FWDQueueAudio> Queue;

	FTimerHandle QueueTimerHandle;

	uint8 bQueueOpen : 1 = true;
	uint8 bQueueFrozen : 1 = false;
};
