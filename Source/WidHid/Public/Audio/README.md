# Audio Obstruction System


## Features

At a rate set by the implementer, obstruction is calculated using the following 4 steps:
1. Tracing a line directly from the emitter to the listener to see if they're obstructed at all.
2. Tracing 8 lines from the emitter to locations around the emitter in a cube shape.
	* If the trace hits something, then a point is placed at the impact location.
	* If the trace does not hit something, then a point is placed at the end of the trace.
3. Tracing 8 lines from the newly formed points to the listener. All traces that hit something are considered to be obstructed.
4. Calculate our current obstruction by dividing all obstructed points from our total points (8).

## How to use in Unreal Engine

1. Place a WDAk component on any actor.
2. In the `Audio > Obstruction` section, modify the Obstruction Data.
	* Obstruction Update Rate represents how often obstruction is recalculated. Negative values disable obstruction.
	* Since obstruction calculation is done asynchronously, values of 0 or very close to 0 won't necessarily speed up the calculation process.
	* Obstruction Offset is a modifier of the WDAk component's location to mark the center of the 'obstruction cube'.
	* Obstruction Radius represents how large the 'obstruction cube' will be.

To see a visual representation of the obstruction points and each emitter's obstruction value:
1. In PIE, open the console with `.
2. Run the `wd.AudioObstructionDebug 1` command. This can be disabled with `wd.AudioObstructionDebug 0`.

## The Code


This code showcases:
* A streamlined workflow of calculating obstruction.
* Use of asynchronous traces at multiple locations while also keeping an accurate record of each point's obstruction level with minimal delays.
* Allows the ability to track CPU usage of the system from within Unreal Insights.


# Audio Queue System


## Features

* Enqueue individual sounds to be played in chronological order with short space in between each sound.
* Priority system that allows sounds of higher priority to be placed at the front of the queue.
* Expiration system that allows sounds to be removed from the queue if they stay too long.

## How to use in Unreal Engine

1. Get the (U)AudioQueueSubsystem. In C++, this can be accomplished with the UWorld function `GetSubsystem<UAudioQueueSubsystem>()`.
2. Use the Enqueue function. You will have to build (F)WDAudioQueue data with an audio event, the audio's priority, and how long the audio can remain in the queue.

To test a basic form of the queue system:
1. In Project Settings, find WDAudioConfig in the Project section.
2. You may modify the Audio Test Queue variable in the Debug section to your liking. It has already been populated with a default set of sounds to start.
3. In PIE, open the console with `.
4. Run the `wd.TestAudioQueue` command.

## The Code


This code showcases:
* Use of Wwise end of event callbacks and thread safety between the game thread and ak event manager thread.
* A priority queue that actively cleans itself of invalid or expired elements.
* Accurate timing for queuing new elements, even with randomized content.

### Enqueuing
* Elements with valid audio are inserted into the queue.
* Elements with higher priority are placed at the front of the queue. Elements of equal priority are played in a first in, first out format.
* If the queue is closed, no elements are allowed to be placed into the queue.

### Dequeuing
* DequeueNext is called from a timer that is set to go off every 100 milliseconds. The Rate of this timer can be modified in the `UWDAudioQueueSubsystem::OnWorldBeginPlay` function.
* Dequeue becomes available on the next allowed play time, in addition to a `TimeBetweenQueuedAudio` buffer.
	* `NextAllowedPlayTime` is modified in two locations: `UWDAudioQueueSubsystem::Play` after a sound is successfully played from the queue and `EndOfEventCallback` after a sound has completed.
	* `TimeBetweenQueuedAudio` is currently a static global variable, but should be placed inside of a config instead.
* If the queue is frozen, no elements are allowed to be played from the queue.

### Playing Sounds and End of Event
* After a sound is dequeued, it is played as a 2D unspatialized sound, then sets the next allowed play time at the current world time plus the maximum length of the sound.
* Since the maximum duration of a sound isn't necessarily going to be the actual length of the sound (because of random containers in Wwise), the next allowed play time is set again in the `EndOfEventCallback` function.
* Be careful with the `NextAllowedPlayTime` variable. It is set as atomic for the purposes of thread safety, since the variable is gotten from the game thread, and set from both the game thread and ak event manager thread.

### Closing and Freezing the Queue
* Calling `CloseQueue` will restrict sounds from entering into the queue, but still allows sounds already in the queue to be played. The queue can be opened again with `OpenQueue`.
* Calling `FreezeQueue` will restrict sounds from being played from the queue, but still allows sounds to be placed into the queue. The queue can be unfrozen with `UnfreezeQueue`.