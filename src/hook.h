#pragma once
#include "stdafx.h"

void InstallMouseHook();
void RemoveMouseHook();
void EnsureMouseHook();
void ResetHookState();
void InstallKbdHook();    // while the wheel is open (Esc / swallow input)
void RemoveKbdHook();

// Called on the message thread (never from inside the hook) to emit the click
// that the hook had to swallow while it waited to see whether the press was a
// plain click or a drag.
void InjectTriggerClickNow();

// The hook recognized a drag and asked for the wheel to be opened. Returns true
// if the wheel should still be shown (the button was not released in the
// meantime) and hands the input state back to normal handling.
bool HookWheelLaunchReady();

