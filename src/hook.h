#pragma once
#include "stdafx.h"

void InstallMouseHook();
void RemoveMouseHook();
void EnsureMouseHook();
void ResetHookState();
void InstallKbdHook();    // while the wheel is open (Esc / swallow input)
void RemoveKbdHook();

