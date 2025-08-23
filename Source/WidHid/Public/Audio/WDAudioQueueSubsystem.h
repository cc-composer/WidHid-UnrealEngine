// Copyright © 2025. This project is developed by Jake Gamelin and is openly licensed via CC0.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "WDAudioQueueSubsystem.generated.h"

class UAkAudioEvent;

USTRUCT(BlueprintType)
struct WIDHID_API FWDQueueAudio
{
	GENERATED_BODY()

	FWDQueueAudio() = default;
	FWDQueueAudio(UAkAudioEvent* Audio) : AudioEvent(Audio) {}

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UAkAudioEvent> AudioEvent;
};

UCLASS()
class WIDHID_API UWDAudioQueueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;

public:	
	UFUNCTION(BlueprintCallable, Category = "Audio|Queue")
	void Enqueue(const FWDQueueAudio& QueueAudio);

public:
	const std::atomic<double>& GetNextAllowedPlayTime() const { return NextAllowedPlayTime; }
	void SetNextAllowedPlayTime(const double Time) { NextAllowedPlayTime = Time; }

private:
	void DequeueNext();
	void Play(const FWDQueueAudio& QueueAudio);

protected:
	bool CanBeDequeued(const FWDQueueAudio& QueueAudio) const;

private:
	// This value needs extra thread safety since it is accessed by both the game and audio thread.
	std::atomic<double> NextAllowedPlayTime = 0.0;
	
	TArray<FWDQueueAudio> Queue;

	FTimerHandle QueueTimerHandle;
};
