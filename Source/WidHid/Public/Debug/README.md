# Audio Debugger
![Audio Debugger](https://github.com/user-attachments/assets/68faf373-24ed-43ec-8353-14e7d61e182d)

## Features

* Display ambient emitter information as well as show visuals of ambient emitters in 3D space.
* Allow individual mix states to be muted or soloed.
* Keep track of all animations currently being used on the local character. Great for finding animations when using audio animation notifies.
* Keep track of the last 100 events posted.

## How to use in Unreal Engine

1. While playing, open the console with `
2. Type in the command `wd.AudioDebuggerVisible 1`
3. Use *Ctrl + `* to gain mouse control.

## The Code
<img width="2048" height="2882" alt="carbon" src="https://github.com/user-attachments/assets/83cb3a5b-3577-466e-baa1-dc4c07fc88e6" />
`This feature makes use of the UnrealImGui plugin. Dear ImGui is developed by ocornut.`

`UnrealImGui is developed by segross and modified by benui-dev for usage in Unreal Engine 5.`

This code showcases:
* An understanding of the framework for making an organized UI that is easily accessible in a video game environment.
* Utilizes Wwise Sound Engine and Spatial Audio structures to analyze obstruction, occlusion, diffraction, and transmission.
* Effective use of global states to easily debug sounds per-category.
* A deeper look into how animations are tracked.
* Cross-communication between scripts across modules.

### Ambient Emitter Debugger
This debugger involves 2 things:
1. Uses DrawDebugSphere and DrawDebugString to present 3d representations of ambient emitters in the world. These emitters are refreshed every 5 seconds in case new emitters are spawned. This debugger also actively removes any emitters from the array if they are no longer valid.
2. Uses an ImGui table to showcase details of the emitter's sound, location, distance from the local listener, obstruction, occlusion, diffraction, and transmission. The obstruction/occlusion data is calculated separately in an FWDAudioObstructionData::Calculate static function.

### Mix States
* FWDAudioDebugMixState is created to store 2 state values (a muted state and a neutral state) and to track whether that state is currently soloed, muted, or neither.
* A table is created to show the name of the mix state and 2 checkboxes for Mute and Solo.
* If a mix state is soloed:
    * Other states are marked as muted. The only exception are other mix states that are currently soloed.
* If a mix state is unsoloed:
    * If there are no other soloed mix states, then all other mix states are unmuted.
    * If there is other soloed mix states, then the unsoloed mix state becomes muted.
* If a mix state is muted:
    * If it was soloed, it follows the same rules as if it was being unsoloed.
* No special circumstances occur if a mix state is unmuted.

### Character Animation Debugger
* This debugger follows the same standards as Unreal's animation debug menu (Console Command: `ShowDebug ANIMATION`). The original code can be found in AnimInstance.cpp: `UAnimInstance::DisplayDebug()`
* The Character Animation Debugger currently shows the active blendspaces and animation montages on the local character's animation instance. This can be expanded to anything with a skeletal mesh.

### Recently Posted Events
* To have knowledge of events being posted, what emitter they were posted on, and the time they were posted, `UAkAudioEvent::PostOnGameObject` was outfitted with a custom multicast delegate that is received by the debugger.
* `DrawRecentlyPostedEvents()` presents the last 100 events to be posted, and also tracks if the event was invalid or if it was posted on an invalid game object.
