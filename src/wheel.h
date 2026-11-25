#pragma once
#include "stdafx.h"

void RegisterWheelClass();
// remoteInput: the gesture arrived through injected input, i.e. somebody is
// driving this machine from another one (see the capture affinity below)
void WheelShowAt(POINT center, bool remoteInput = false);
bool WheelIsOpen();
void WheelUpdateHover(POINT screenPt);
void WheelCommitHover();
void WheelCancel();
void WheelSwitchPage(int dir);
