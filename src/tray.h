#pragma once
#include "stdafx.h"

void TrayInit(HWND hwnd);
void TrayUpdate(bool show);
void TrayRemove();
void TrayHandle(WPARAM wp, LPARAM lp);
