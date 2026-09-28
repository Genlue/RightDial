#include "stdafx.h"
#include "actions.h"
#include "app.h"

struct NamedVk { const wchar_t* name; UINT vk; };
static const NamedVk kNamed[] = {
    { L"enter", VK_RETURN }, { L"return", VK_RETURN },
    { L"tab", VK_TAB }, { L"space", VK_SPACE }, { L"spacebar", VK_SPACE },
    { L"esc", VK_ESCAPE }, { L"escape", VK_ESCAPE },
    { L"backspace", VK_BACK }, { L"bksp", VK_BACK }, { L"back", VK_BACK },
    { L"delete", VK_DELETE }, { L"del", VK_DELETE },
    { L"insert", VK_INSERT }, { L"ins", VK_INSERT },
    { L"home", VK_HOME }, { L"end", VK_END },
    { L"pageup", VK_PRIOR }, { L"pgup", VK_PRIOR },
    { L"pagedown", VK_NEXT }, { L"pgdn", VK_NEXT },
    { L"up", VK_UP }, { L"down", VK_DOWN }, { L"left", VK_LEFT }, { L"right", VK_RIGHT },
    { L"prtsc", VK_SNAPSHOT }, { L"printscreen", VK_SNAPSHOT }, { L"print", VK_SNAPSHOT }, { L"snapshot", VK_SNAPSHOT },
    { L"capslock", VK_CAPITAL }, { L"caps", VK_CAPITAL },
    { L"numlock", VK_NUMLOCK },
    { L"scrolllock", VK_SCROLL }, { L"scroll", VK_SCROLL },
    { L"pause", VK_PAUSE }, { L"break", VK_PAUSE },
    { L"numpad0", VK_NUMPAD0 }, { L"numpad1", VK_NUMPAD1 }, { L"numpad2", VK_NUMPAD2 },
    { L"numpad3", VK_NUMPAD3 }, { L"numpad4", VK_NUMPAD4 }, { L"numpad5", VK_NUMPAD5 },
    { L"numpad6", VK_NUMPAD6 }, { L"numpad7", VK_NUMPAD7 }, { L"numpad8", VK_NUMPAD8 },
    { L"numpad9", VK_NUMPAD9 },
    { L"num0", VK_NUMPAD0 }, { L"num1", VK_NUMPAD1 }, { L"num2", VK_NUMPAD2 },
    { L"num3", VK_NUMPAD3 }, { L"num4", VK_NUMPAD4 }, { L"num5", VK_NUMPAD5 },
    { L"num6", VK_NUMPAD6 }, { L"num7", VK_NUMPAD7 }, { L"num8", VK_NUMPAD8 },
    { L"num9", VK_NUMPAD9 },
    { L"add", VK_ADD }, { L"sub", VK_SUBTRACT }, { L"mul", VK_MULTIPLY }, { L"multiply", VK_MULTIPLY },
    { L"div", VK_DIVIDE }, { L"divide", VK_DIVIDE }, { L"dec", VK_DECIMAL },
    { L"comma", VK_OEM_COMMA }, { L"period", VK_OEM_PERIOD }, { L"dot", VK_OEM_PERIOD },
    { L"slash", VK_OEM_2 }, { L"semicolon", VK_OEM_1 }, { L"quote", VK_OEM_7 },
    { L"minus", VK_OEM_MINUS }, { L"equal", VK_OEM_PLUS }, { L"plus", VK_OEM_PLUS },
    { L"lbracket", VK_OEM_4 }, { L"rbracket", VK_OEM_6 }, { L"bracketleft", VK_OEM_4 }, { L"bracketright", VK_OEM_6 },
    { L"backslash", VK_OEM_5 }, { L"tilde", VK_OEM_3 }, { L"grave", VK_OEM_3 },
};

static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

static bool TokenToVk(const std::wstring& tok, UINT& vk, bool& extraShift) {
    std::wstring t = ToLower(tok);
    if (t.empty()) return false;
    if (t == L"+" || t == L"plus") { vk = VK_OEM_PLUS; return true; }
    if (t == L"-" || t == L"minus") { vk = VK_OEM_MINUS; return true; }
    if (t == L"=" || t == L"equal") { vk = VK_OEM_PLUS; return true; }
    if (t.size() == 1) {
        wchar_t c = t[0];
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) { vk = (UINT)toupper(c); return true; }
        SHORT sc = VkKeyScanW(c);
        if (sc != -1) {
            vk = (UINT)(sc & 0xFF);
            extraShift = (sc >> 8) & 1;
            return vk != 0;
        }
        return false;
    }
    if (t[0] == L'f' && t.size() <= 3) {
        int n = _wtoi(t.c_str() + 1);
        if (n >= 1 && n <= 24) { vk = VK_F1 + n - 1; return true; }
        return false;
    }
    for (auto& e : kNamed)
        if (t == e.name) { vk = e.vk; return true; }
    return false;
}

bool ParseChord(const std::wstring& keysIn, ParsedChord& out) {
    std::wstring keys = keysIn;
    for (auto& c : keys) {
        if (c == 0xFF0B) c = L'+';       // fullwidth '+'
        else if (c == 0x3000) c = L' ';  // fullwidth space
    }

    std::vector<std::wstring> toks;
    size_t start = 0;
    while (start <= keys.size()) {
        size_t p = keys.find(L'+', start);
        if (p == std::wstring::npos) { toks.push_back(keys.substr(start)); break; }
        // Handle "++" e.g. "Ctrl++"
        if (p == start && !toks.empty()) {
            toks.push_back(L"+");
            start = p + 1;
            continue;
        }
        toks.push_back(keys.substr(start, p - start));
        start = p + 1;
    }
    // trim
    for (auto& t : toks) {
        while (!t.empty() && iswspace(t.front())) t.erase(t.begin());
        while (!t.empty() && iswspace(t.back())) t.pop_back();
    }
    toks.erase(std::remove_if(toks.begin(), toks.end(), [](const std::wstring& s) { return s.empty(); }), toks.end());
    if (toks.empty()) return false;

    ParsedChord c;
    for (size_t i = 0; i < toks.size(); i++) {
        std::wstring t = ToLower(toks[i]);
        if (t == L"ctrl" || t == L"control") {
            c.ctrl = true;
        } else if (t == L"alt" || t == L"menu") {
            c.alt = true;
        } else if (t == L"shift") {
            c.shift = true;
        } else if (t == L"win" || t == L"windows" || t == L"cmd" || t == L"super") {
            c.win = true;
        } else {
            UINT vk = 0;
            bool extraShift = false;
            if (!TokenToVk(toks[i], vk, extraShift)) return false;
            if (extraShift) c.shift = true;
            if (vk != 0) {
                if (c.vks.empty() || c.vks.back() != vk) {
                    c.vks.push_back(vk);
                }
            }
        }
    }
    if (c.vks.empty() && !c.win && !c.ctrl && !c.alt && !c.shift) return false;
    out = c;
    return true;
}

static std::wstring VkDisplay(UINT vk) {
    switch (vk) {
    case VK_RETURN: return L"Enter";
    case VK_TAB: return L"Tab";
    case VK_SPACE: return L"Space";
    case VK_ESCAPE: return L"Esc";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    case VK_SNAPSHOT: return L"PrtSc";
    case VK_CAPITAL: return L"CapsLock";
    case VK_NUMLOCK: return L"NumLock";
    case VK_SCROLL: return L"ScrollLock";
    case VK_PAUSE: return L"Pause";
    case VK_NUMPAD0: return L"Num0";
    case VK_NUMPAD1: return L"Num1";
    case VK_NUMPAD2: return L"Num2";
    case VK_NUMPAD3: return L"Num3";
    case VK_NUMPAD4: return L"Num4";
    case VK_NUMPAD5: return L"Num5";
    case VK_NUMPAD6: return L"Num6";
    case VK_NUMPAD7: return L"Num7";
    case VK_NUMPAD8: return L"Num8";
    case VK_NUMPAD9: return L"Num9";
    case VK_ADD: return L"Num+";
    case VK_SUBTRACT: return L"Num-";
    case VK_MULTIPLY: return L"Num*";
    case VK_DIVIDE: return L"Num/";
    case VK_DECIMAL: return L"Num.";
    case VK_OEM_COMMA: return L",";
    case VK_OEM_PERIOD: return L".";
    case VK_OEM_2: return L"/";
    case VK_OEM_1: return L";";
    case VK_OEM_7: return L"'";
    case VK_OEM_MINUS: return L"-";
    case VK_OEM_PLUS: return L"=";
    case VK_OEM_4: return L"[";
    case VK_OEM_6: return L"]";
    case VK_OEM_5: return L"\\";
    case VK_OEM_3: return L"`";
    }
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::wstring(1, (wchar_t)vk);
    wchar_t buf[32] = {};
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16;
    if (sc && GetKeyNameTextW((LONG)sc, buf, 31) > 0 && buf[0]) return buf;
    wchar_t n[16]; swprintf_s(n, L"VK%02X", vk);
    return n;
}

std::wstring VkDisplayName(UINT vk) {
    return VkDisplay(vk);
}

std::wstring NormalizeChord(const std::wstring& keys) {
    ParsedChord c;
    if (!ParseChord(keys, c)) return L"";
    std::wstring s;
    if (c.win)   s += L"Win+";
    if (c.ctrl)  s += L"Ctrl+";
    if (c.alt)   s += L"Alt+";
    if (c.shift) s += L"Shift+";
    for (size_t i = 0; i < c.vks.size(); i++) {
        if (i > 0) s += L"+";
        s += VkDisplay(c.vks[i]);
    }
    if (c.vks.empty() && !s.empty() && s.back() == L'+') s.pop_back();
    return s;
}

static void SendChord(const ParsedChord& c) {
    std::vector<INPUT> in;
    auto isExt = [](WORD vk) {
        return vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT ||
               vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
               vk == VK_PRIOR || vk == VK_NEXT || vk == VK_RCONTROL || vk == VK_RMENU ||
               vk == VK_SNAPSHOT;
    };
    auto key = [&](WORD vk, bool up) {
        INPUT inp = {};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        inp.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (isExt(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
        in.push_back(inp);
    };

    if (c.win)   key(VK_LWIN, false);
    if (c.ctrl)  key(VK_CONTROL, false);
    if (c.alt)   key(VK_MENU, false);
    if (c.shift) key(VK_SHIFT, false);

    for (UINT vk : c.vks) key((WORD)vk, false);
    for (auto it = c.vks.rbegin(); it != c.vks.rend(); ++it) key((WORD)*it, true);

    if (c.shift) key(VK_SHIFT, true);
    if (c.alt)   key(VK_MENU, true);
    if (c.ctrl)  key(VK_CONTROL, true);
    if (c.win)   key(VK_LWIN, true);

    if (!in.empty()) {
        SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
    }
}

// Win+L is handled by winlogon on the secure desktop, which ignores synthetic
// input — it can never be injected. The supported equivalent is the
// LockWorkStation API, which ExecuteSlot routes this exact chord to.
bool IsLockWorkstationChord(const ParsedChord& c) {
    return c.win && !c.ctrl && !c.alt && !c.shift &&
           c.vks.size() == 1 && c.vks[0] == 'L';
}

static bool IsDirOrExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

void ExecuteSlot(const Slot& s) {
    switch (s.type) {
    case SlotType::Hotkey: {
        ParsedChord c;
        if (!ParseChord(s.keys, c)) { MessageBeep(MB_ICONWARNING); break; }
        if (IsLockWorkstationChord(c)) { LockWorkStation(); break; }
        SendChord(c);
        break;
    }
    case SlotType::File: {
        if (!IsDirOrExists(s.path)) { MessageBeep(MB_ICONWARNING); break; }
        wchar_t dir[MAX_PATH] = L"";
        std::wstring p = s.path;
        size_t sl = p.find_last_of(L'\\');
        if (sl != std::wstring::npos && !(GetFileAttributesW(p.c_str()) & FILE_ATTRIBUTE_DIRECTORY))
            wcsncpy_s(dir, p.substr(0, sl).c_str(), _TRUNCATE);
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_UNICODE;
        sei.lpVerb = L"open";
        sei.lpFile = p.c_str();
        sei.lpDirectory = dir[0] ? dir : nullptr;
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
        break;
    }
    case SlotType::Url: {
        std::wstring u = s.url;
        if (!u.empty() && u.find(L"://") == std::wstring::npos && u.rfind(L"mailto:", 0) != 0)
            u = L"https://" + u;
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_UNICODE;
        sei.lpVerb = L"open";
        sei.lpFile = u.c_str();
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
        break;
    }
    }
}
