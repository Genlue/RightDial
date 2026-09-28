// standalone probe: verifies the Win+L recognition used to route the lock
// hotkey to LockWorkStation(). Win+L is processed by winlogon on the secure
// desktop and ignores synthetic input, so exact Win+L (in any spelling or
// modifier order) must match, while similar chords must not. Not part of the
// product build; does not call LockWorkStation itself.
#include "stdafx.h"
#include "actions.h"
#include <cstdio>

static int fails = 0;

static void CheckKeys(const wchar_t* keys, bool wantLock, bool wantValid = true) {
    ParsedChord c;
    bool valid = ParseChord(keys, c);
    if (valid != wantValid) {
        printf("FAIL parse(%ls): valid=%d want %d\n", keys, valid, wantValid);
        fails++;
        return;
    }
    if (!valid) return;
    bool lock = IsLockWorkstationChord(c);
    if (lock != wantLock) {
        printf("FAIL chord(%ls): lock=%d want %d\n", keys, lock, wantLock);
        fails++;
        return;
    }
    printf("ok  %-16ls -> %ls\n", keys, lock ? L"LockWorkStation" : L"SendInput");
}

int main() {
    // exact Win+L in every accepted spelling / modifier order
    CheckKeys(L"Win+L", true);
    CheckKeys(L"win+l", true);
    CheckKeys(L"Windows+L", true);
    CheckKeys(L"Cmd+L", true);
    CheckKeys(L"Super+L", true);
    CheckKeys(L"L+Win", true);

    // near-misses must still go through SendInput (they are injectable)
    CheckKeys(L"Win+Shift+L", false);
    CheckKeys(L"Win+Ctrl+L", false);
    CheckKeys(L"Win+Alt+L", false);
    CheckKeys(L"Ctrl+L", false);
    CheckKeys(L"Alt+L", false);
    CheckKeys(L"Shift+L", false);
    CheckKeys(L"Win+K", false);
    CheckKeys(L"Win+D", false);
    CheckKeys(L"L", false);
    CheckKeys(L"Ctrl+V", false);

    // display normalization of the preset spelling is unchanged
    std::wstring norm = NormalizeChord(L"Win+L");
    if (norm != L"Win+L") { printf("FAIL NormalizeChord(Win+L) = %ls\n", norm.c_str()); fails++; }
    else printf("ok  NormalizeChord    -> Win+L\n");

    if (fails) { printf("test_lockchord: %d FAIL\n", fails); return 1; }
    printf("test_lockchord: all pass\n");
    return 0;
}
