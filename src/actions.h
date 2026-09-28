#pragma once
#include "stdafx.h"
#include "config.h"

struct ParsedChord {
    bool win = false;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    std::vector<UINT> vks;
};

void         ExecuteSlot(const Slot& s);
bool         ParseChord(const std::wstring& keys, ParsedChord& out);
std::wstring NormalizeChord(const std::wstring& keys);  // "" if invalid
std::wstring VkDisplayName(UINT vk);                    // human name for any VK
bool         IsLockWorkstationChord(const ParsedChord& c);  // exact Win+L
