#pragma once
#include "stdafx.h"

void RegisterWheelClass();
void WheelShowAt(POINT center);
bool WheelIsOpen();
void WheelUpdateHover(POINT screenPt);
void WheelCommitHover();
void WheelCancel();
void WheelSwitchPage(int dir);
