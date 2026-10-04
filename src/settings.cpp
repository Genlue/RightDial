#include "stdafx.h"
#include "settings.h"
#include "app.h"
#include "tray.h"
#include "actions.h"
#include "render.h"
#include "hook.h"
#include "lucide_icons.h"

#include <commdlg.h>

static HWND s_hwnd = nullptr;
static UINT s_dpi = 96;
static HFONT s_font = nullptr;
static HWND s_tab = nullptr, s_panel1 = nullptr, s_panel2 = nullptr, s_panel3 = nullptr;
static int  s_curPage = 0, s_curSlot = -1;
static bool s_noSync = false;   // suppress EN_CHANGE while loading values into controls

static const wchar_t* MAIN_CLASS = L"RightDialSettings";

static HWND CtrlOf(int id) { return GetDlgItem(s_hwnd, id); }
static HWND P1Of(int id) { return GetDlgItem(s_panel1, id); }
static HWND P2Of(int id) { return GetDlgItem(s_panel2, id); }
static HWND P3Of(int id) { return GetDlgItem(s_panel3, id); }

static int S(int v) { return MulDiv(v, (int)s_dpi, 96); }

// ---------- shared page/slot helpers ----------

static Page* CurPage() {
    if (g_cfg.pages.empty()) return nullptr;
    if (s_curPage < 0 || s_curPage >= (int)g_cfg.pages.size()) s_curPage = 0;
    return &g_cfg.pages[s_curPage];
}
static Slot* CurSlot() {
    Page* p = CurPage();
    if (!p || s_curSlot < 0 || s_curSlot >= (int)p->slots.size()) return nullptr;
    return &p->slots[s_curSlot];
}

static void SaveSoon() {
    if (s_hwnd) SetTimer(s_hwnd, 1, 250, nullptr);
}

// ---------- InputBox (in-memory dialog template, classic format) ----------

struct Tpl {
    std::vector<WORD> w;
    void A() { if (w.size() & 1) w.push_back(0); }
    void W(WORD v) { w.push_back(v); }
    void S32(DWORD v) { W(LOWORD(v)); W(HIWORD(v)); }
    void Str(const wchar_t* s) { if (!s) { W(0); return; } for (size_t i = 0; i <= wcslen(s); i++) w.push_back(s[i]); }
    void Item(DWORD style, DWORD ex, WORD id, int x, int y, int cx, int cy, WORD atom, const wchar_t* text) {
        A();
        S32(style); S32(ex);
        W((WORD)x); W((WORD)y); W((WORD)cx); W((WORD)cy); W(id);
        W(0xFFFF); W(atom);
        Str(text); W(0);
    }
};

static INT_PTR CALLBACK InputProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)lp);
        std::wstring* out = (std::wstring*)lp;
        if (out) SetDlgItemTextW(h, 100, out->c_str());
        SetFocus(GetDlgItem(h, 100));
        SendDlgItemMessageW(h, 100, EM_SETSEL, 0, -1);
        return FALSE;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == IDOK) {
            std::wstring* out = (std::wstring*)GetWindowLongPtrW(h, DWLP_USER);
            if (out) {
                int n = GetWindowTextLengthW(GetDlgItem(h, 100));
                out->resize(n + 1);
                GetDlgItemTextW(h, 100, &(*out)[0], n + 1);
                out->resize(n);
            }
            EndDialog(h, 1);
            return TRUE;
        } else if (id == IDCANCEL) {
            EndDialog(h, 0);
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(h, 0);
        return TRUE;
    }
    return FALSE;
}

static bool InputBox(HWND parent, const wchar_t* title, std::wstring& text) {
    Tpl t;
    t.S32(DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER);
    t.S32(WS_EX_DLGMODALFRAME);
    t.W(4);
    t.W(0); t.W(0); t.W(212); t.W(68);
    t.W(0); t.W(0);
    t.Str(title);
    t.W(9); t.Str(L"Microsoft YaHei UI");
    t.A();
    const DWORD stTxt  = WS_CHILD | WS_VISIBLE;
    const DWORD stEdit = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    const DWORD stDef  = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON;
    const DWORD stBtn  = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    t.Item(stTxt, 0, (WORD)-1, 10, 9, 120, 10, 0x82, L"名称：");
    t.Item(stEdit, WS_EX_CLIENTEDGE, 100, 10, 22, 192, 13, 0x81, L"");
    t.Item(stDef, 0, IDOK, 102, 44, 48, 14, 0x80, L"确定");
    t.Item(stBtn, 0, IDCANCEL, 154, 44, 48, 14, 0x80, L"取消");
    std::wstring out = text;
    INT_PTR res = DialogBoxIndirectParamW(g_hInst, (LPCDLGTEMPLATE)t.w.data(), parent, InputProc, (LPARAM)&out);
    if (res == 1) {
        text = out;
        return true;
    }
    return false;
}

// ---------- pickers ----------

static bool PickColor(HWND parent, std::wstring& hex) {
    static COLORREF cust[16] = {};
    D2D1_COLOR_F c = ParseColor(hex);
    CHOOSECOLORW cc = { sizeof(cc) };
    cc.hwndOwner = parent;
    cc.rgbResult = RGB((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255));
    cc.lpCustColors = cust;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return false;
    hex = ColorToHex(D2D1::ColorF(GetRValue(cc.rgbResult) / 255.0f,
                                  GetGValue(cc.rgbResult) / 255.0f,
                                  GetBValue(cc.rgbResult) / 255.0f));
    return true;
}

static bool PickFont(HWND parent, std::wstring& font, float& sizePx) {
    LOGFONTW lf = {};
    wcsncpy_s(lf.lfFaceName, font.c_str(), _TRUNCATE);
    lf.lfHeight = -MulDiv((int)sizePx, (int)s_dpi, 96);
    lf.lfCharSet = DEFAULT_CHARSET;
    CHOOSEFONTW cf = { sizeof(cf) };
    cf.hwndOwner = parent;
    cf.lpLogFont = &lf;
    cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS;
    if (!ChooseFontW(&cf)) return false;
    font = lf.lfFaceName;
    int px = -lf.lfHeight;
    if (px < 6) px = 6;
    sizePx = (float)MulDiv(px, 96, (int)s_dpi);
    return true;
}

static std::wstring PickFileDialog(HWND parent, bool folder) {
    std::wstring result;
    IFileDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(folder ? CLSID_FileOpenDialog : CLSID_FileOpenDialog,
                                  nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return result;
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    opt |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (folder) opt |= FOS_PICKFOLDERS;
    dlg->SetOptions(opt);
    dlg->SetTitle(folder ? L"选择文件夹" : L"选择文件");
    if (SUCCEEDED(dlg->Show(parent))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

static std::wstring PickImageDialog(HWND parent) {
    wchar_t file[MAX_PATH * 2] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = L"图片/图标 (*.png;*.jpg;*.jpeg;*.bmp;*.ico;*.svg)\0*.png;*.jpg;*.jpeg;*.bmp;*.ico;*.svg\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return L"";
    return file;
}

// ---------- Lucide icon-library picker ----------
// Modal dialog with a search box and a scrollable icon grid over the
// embedded Lucide set (src/lucide_icons.cpp). Applies "builtin:lucide:<name>".

static const wchar_t* PICKER_GRID_CLASS = L"RightDialIconGrid";

struct LucidePicker {
    std::vector<int> idx;        // filtered indices into the Lucide table
    std::wstring     query;
    std::wstring*    out = nullptr;  // receives "builtin:lucide:<name>" on apply
    int              sel = -1;   // index into idx, -1 = none
    HWND             dlg = nullptr, grid = nullptr;
    int              cellW = 68, cellH = 84, iconPx = 44;
    int              cols = 8, rowsVis = 4, firstRow = 0, totalRows = 0;

    void Metrics(HWND h) {
        RECT rc;
        GetClientRect(h, &rc);
        cols = std::max(1, (((int)rc.right - (int)rc.left) - 6) / cellW);
        rowsVis = std::max(1, (((int)rc.bottom - (int)rc.top) - 6) / cellH);
    }
};

// rasterized-icon cache for the grid only; bounded so a full browse of the
// 1744-icon set never balloons memory (render's IconCache is untouched)
static std::unordered_map<std::string, HBITMAP> s_gridIcons;
static HFONT s_gridFont = nullptr;

static HBITMAP GridIcon(const char* name, int px) {
    std::string key = name;
    key += '|';
    key += std::to_string(px);
    auto it = s_gridIcons.find(key);
    if (it != s_gridIcons.end()) return it->second;
    if (s_gridIcons.size() > 600) {
        for (auto& kv : s_gridIcons) if (kv.second) DeleteObject(kv.second);
        s_gridIcons.clear();
    }
    HBITMAP hb = LucideIconDib(name, px, false);   // light ink: picker is on a light dialog
    s_gridIcons[key] = hb;
    return hb;
}

static void NormalizeQueryKey(const std::wstring& in, std::wstring& out) {
    out.clear();
    for (wchar_t c : in) {
        if (c == L'-' || c == L'_' || c == L' ') continue;
        out.push_back((wchar_t)towlower(c));
    }
}

static void PickerSyncScrollbar(LucidePicker* pk) {
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    si.nMin = 0;
    si.nMax = std::max(0, pk->totalRows - 1);
    si.nPage = (UINT)pk->rowsVis;
    si.nPos = pk->firstRow;
    SetScrollInfo(pk->grid, SB_VERT, &si, TRUE);
}

static void PickerUpdateTexts(LucidePicker* pk) {
    wchar_t buf[96];
    int n = LucideIconCount();
    if (pk->query.empty()) swprintf_s(buf, L"共 %d 个图标", n);
    else                   swprintf_s(buf, L"匹配 %d / %d", (int)pk->idx.size(), n);
    Static_SetText(GetDlgItem(pk->dlg, 1002), buf);
    if (pk->sel >= 0 && pk->sel < (int)pk->idx.size()) {
        const char* nm = LucideIconAt(pk->idx[pk->sel]).name;
        swprintf_s(buf, L"已选择: %S", nm);
    } else {
        wcscpy_s(buf, L"(未选择)");
    }
    Static_SetText(GetDlgItem(pk->dlg, 1003), buf);
    EnableWindow(GetDlgItem(pk->dlg, IDOK), pk->sel >= 0);
}

static void PickerApplyFilter(LucidePicker* pk) {
    std::wstring qk;
    NormalizeQueryKey(pk->query, qk);
    pk->idx.clear();
    int n = LucideIconCount();
    for (int i = 0; i < n; i++) {
        if (qk.empty()) { pk->idx.push_back(i); continue; }
        std::wstring w = Utf8ToUtf16(LucideIconAt(i).name), k;
        NormalizeQueryKey(w, k);
        if (k.find(qk) != std::wstring::npos) pk->idx.push_back(i);
    }
    if (pk->sel >= (int)pk->idx.size()) pk->sel = -1;
    pk->firstRow = 0;
    pk->totalRows = pk->idx.empty() ? 0 : (int)((pk->idx.size() + pk->cols - 1) / pk->cols);
    PickerSyncScrollbar(pk);
    PickerUpdateTexts(pk);
    InvalidateRect(pk->grid, nullptr, TRUE);
}

// keep the selected cell's row on screen
static void PickerEnsureVisible(LucidePicker* pk) {
    if (pk->sel < 0 || pk->sel >= (int)pk->idx.size() || pk->cols < 1) return;
    int row = pk->sel / pk->cols;
    if (row < pk->firstRow) pk->firstRow = row;
    if (row >= pk->firstRow + pk->rowsVis) pk->firstRow = row - pk->rowsVis + 1;
    PickerSyncScrollbar(pk);
    InvalidateRect(pk->grid, nullptr, TRUE);
}

static void PaintPickerGrid(HWND h, LucidePicker* pk, HDC hdc) {
    RECT rc;
    GetClientRect(h, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(252, 253, 255));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    if (pk->idx.empty()) {
        SetTextColor(hdc, RGB(130, 138, 150));
        SetBkMode(hdc, TRANSPARENT);
        HFONT of = (HFONT)SelectObject(hdc, s_font);
        DrawTextW(hdc, L"无匹配图标", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, of);
        return;
    }

    int rowEnd = std::min(pk->totalRows, pk->firstRow + pk->rowsVis + 1);
    for (int row = pk->firstRow; row < rowEnd; row++) {
        for (int col = 0; col < pk->cols; col++) {
            int i = row * pk->cols + col;
            if (i >= (int)pk->idx.size()) break;
            const LucideIcon& li = LucideIconAt(pk->idx[i]);
            RECT cell = { col * pk->cellW, (row - pk->firstRow) * pk->cellH,
                          col * pk->cellW + pk->cellW, (row - pk->firstRow) * pk->cellH + pk->cellH };
            bool selected = (i == pk->sel);
            if (selected) {
                HBRUSH hl = CreateSolidBrush(RGB(61, 111, 180));
                FillRect(hdc, &cell, hl);
                DeleteObject(hl);
            }
            int icon = std::min(pk->iconPx, pk->cellW - 8);
            int ix = cell.left + (pk->cellW - icon) / 2;
            int iy = cell.top + 8;
            if (HBITMAP hb = GridIcon(li.name, icon)) {
                HDC mem = CreateCompatibleDC(hdc);
                HGDIOBJ old = SelectObject(mem, hb);
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                AlphaBlend(hdc, ix, iy, icon, icon, mem, 0, 0, icon, icon, bf);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
            wchar_t label[64];
            MultiByteToWideChar(CP_UTF8, 0, li.name, -1, label, 64);
            RECT lr = { cell.left + 2, iy + icon, cell.right - 2, cell.bottom };
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, selected ? RGB(255, 255, 255) : RGB(96, 106, 120));
            HFONT of = (HFONT)SelectObject(hdc, s_gridFont);
            DrawTextW(hdc, label, -1, &lr, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(hdc, of);
        }
    }
}

static LRESULT CALLBACK PickerGridProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    LucidePicker* pk = (LucidePicker*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!pk) return DefSubclassProc(h, m, wp, lp);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintPickerGrid(h, pk, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        SetFocus(h);
        int col = (short)LOWORD(lp) / pk->cellW;
        int row = (short)HIWORD(lp) / pk->cellH + pk->firstRow;
        int i = row * pk->cols + col;
        pk->sel = (col < pk->cols && i >= 0 && i < (int)pk->idx.size()) ? i : -1;
        PickerUpdateTexts(pk);
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int col = (short)LOWORD(lp) / pk->cellW;
        int row = (short)HIWORD(lp) / pk->cellH + pk->firstRow;
        int i = row * pk->cols + col;
        if (col < pk->cols && i >= 0 && i < (int)pk->idx.size()) {
            pk->sel = i;
            PickerUpdateTexts(pk);
            InvalidateRect(h, nullptr, TRUE);
            SendMessageW(pk->dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)nullptr);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        int last = (int)pk->idx.size() - 1;
        int move = 0;
        switch (wp) {
        case VK_LEFT:  move = -1; break;
        case VK_RIGHT: move = 1; break;
        case VK_UP:    move = -pk->cols; break;
        case VK_DOWN:  move = pk->cols; break;
        case VK_HOME:  pk->sel = 0; break;
        case VK_END:   pk->sel = last; break;
        case VK_PRIOR: move = -pk->cols * pk->rowsVis; break;
        case VK_NEXT:  move = pk->cols * pk->rowsVis; break;
        default: return 0;
        }
        if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN || wp == VK_PRIOR || wp == VK_NEXT) {
            if (pk->sel < 0) pk->sel = (move > 0) ? 0 : last;
            else             pk->sel = std::min(std::max(0, pk->sel + move), last);
        }
        PickerEnsureVisible(pk);
        PickerUpdateTexts(pk);
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(h, SB_VERT, &si);
        int old = si.nPos;
        switch (LOWORD(wp)) {
        case SB_TOP:         si.nPos = si.nMin; break;
        case SB_BOTTOM:      si.nPos = si.nMax; break;
        case SB_LINEUP:      si.nPos--; break;
        case SB_LINEDOWN:    si.nPos++; break;
        case SB_PAGEUP:      si.nPos -= pk->rowsVis; break;
        case SB_PAGEDOWN:    si.nPos += pk->rowsVis; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: si.nPos = si.nTrackPos; break;
        }
        si.fMask = SIF_POS;
        SetScrollInfo(h, SB_VERT, &si, TRUE);
        GetScrollInfo(h, SB_VERT, &si);
        pk->firstRow = si.nPos;
        if (si.nPos != old)
            InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int lines = -(int)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 3;
        if (!lines) lines = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
        pk->firstRow = std::min(std::max(0, pk->firstRow + lines), std::max(0, pk->totalRows - pk->rowsVis));
        PickerSyncScrollbar(pk);
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    }
    return DefSubclassProc(h, m, wp, lp);
}

static INT_PTR CALLBACK LucidePickerProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_INITDIALOG: {
        static LucidePicker s_picker;   // modal dialog: one live instance
        LucidePicker* pk = &s_picker;
        *pk = LucidePicker{};           // reset state, keep defaults
        pk->dlg = h;
        pk->out = (std::wstring*)lp;
        SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)pk);
        WNDCLASSEXW tmp{};
        if (!GetClassInfoExW(g_hInst, PICKER_GRID_CLASS, &tmp)) {
            WNDCLASSEXW wc = { sizeof(wc) };
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = g_hInst;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = PICKER_GRID_CLASS;
            RegisterClassExW(&wc);
        }
        RECT r = { 10, 26, 370, 228 };
        MapDialogRect(h, &r);
        pk->grid = CreateWindowExW(WS_EX_CLIENTEDGE, PICKER_GRID_CLASS, L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL,
                                   r.left, r.top, r.right - r.left, r.bottom - r.top,
                                   h, (HMENU)(INT_PTR)1004, g_hInst, nullptr);
        SetWindowSubclass(pk->grid, PickerGridProc, 30, 0);
        SetWindowLongPtrW(pk->grid, GWLP_USERDATA, (LONG_PTR)pk);
        if (!s_gridFont) {
            LOGFONTW lf = {};
            lf.lfHeight = -11;
            lf.lfWeight = FW_NORMAL;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcsncpy_s(lf.lfFaceName, L"Microsoft YaHei UI", _TRUNCATE);
            s_gridFont = CreateFontIndirectW(&lf);
        }
        SendMessageW(pk->grid, WM_SETFONT, (WPARAM)s_font, TRUE);
        pk->Metrics(pk->grid);

        // preselect the current builtin:lucide:<name> if the slot has one
        std::wstring* out = (std::wstring*)lp;
        if (out && out->rfind(L"builtin:lucide:", 0) == 0) {
            std::string nm = Utf16ToUtf8(out->substr(15));
            const LucideIcon* li = LucideFindIcon(nm.c_str());
            if (li) pk->sel = (int)(li - &LucideIconAt(0));
        }
        PickerApplyFilter(pk);
        PickerEnsureVisible(pk);
        SetFocus(GetDlgItem(h, 1001));
        return FALSE;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        auto* pk = (LucidePicker*)GetWindowLongPtrW(h, DWLP_USER);
        if (id == 1001 && GET_WM_COMMAND_CMD(wp, lp) == EN_CHANGE && pk) {
            int n = GetWindowTextLengthW(GetDlgItem(h, 1001));
            pk->query.resize(n + 1);
            GetDlgItemTextW(h, 1001, &pk->query[0], n + 1);
            pk->query.resize(n);
            PickerApplyFilter(pk);
            return TRUE;
        }
        if (id == IDOK && pk) {
            if (pk->out && pk->sel >= 0 && pk->sel < (int)pk->idx.size())
                *pk->out = std::wstring(L"builtin:lucide:") + Utf8ToUtf16(LucideIconAt(pk->idx[pk->sel]).name);
            EndDialog(h, (pk->sel >= 0) ? 1 : 0);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(h, 0);
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(h, 0);
        return TRUE;
    }
    return FALSE;
}

// returns true and fills `source` with "builtin:lucide:<name>" on apply
static bool PickLucideIcon(HWND parent, std::wstring& source) {
    Tpl t;
    t.S32(DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER);
    t.S32(WS_EX_DLGMODALFRAME);
    t.W(6);
    t.W(0); t.W(0); t.W(380); t.W(252);
    t.W(0); t.W(0);
    t.Str(L"选择图标 - Lucide 图标库");
    t.W(9); t.Str(L"Microsoft YaHei UI");
    t.A();
    const DWORD stTxt  = WS_CHILD | WS_VISIBLE;
    const DWORD stEdit = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    const DWORD stDef  = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON;
    const DWORD stBtn  = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    t.Item(stTxt, 0, (WORD)-1, 10, 11, 26, 10, 0x82, L"搜索:");
    t.Item(stEdit, WS_EX_CLIENTEDGE, 1001, 38, 9, 250, 13, 0x81, L"");
    t.Item(stTxt, 0, 1002, 294, 11, 76, 10, 0x82, L"");
    t.Item(stTxt, 0, 1003, 10, 230, 230, 10, 0x82, L"(未选择)");
    t.Item(stDef, 0, IDOK, 252, 228, 58, 14, 0x80, L"使用");
    t.Item(stBtn, 0, IDCANCEL, 316, 228, 54, 14, 0x80, L"取消");
    INT_PTR res = DialogBoxIndirectParamW(g_hInst, (LPCDLGTEMPLATE)t.w.data(), parent,
                                          LucidePickerProc, (LPARAM)&source);
    return res == 1;
}


// ---------- painting helpers ----------

static void DrawIconPreview(HDC hdc, RECT rc) {
    HBRUSH bg = CreateSolidBrush(RGB(26, 32, 44));
    HPEN border = CreatePen(PS_SOLID, 1, RGB(74, 85, 104));
    HGDIOBJ oldBr = SelectObject(hdc, bg);
    HGDIOBJ oldPen = SelectObject(hdc, border);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(border);
    DeleteObject(bg);

    Slot* s = CurSlot();
    if (!s) return;
    int box = (rc.right - rc.left) - 16;
    if (box < 8) box = 8;
    HBITMAP hb = IconCache::I().GetHbitmap(SlotIconSource(*s), box);
    if (!hb) return;
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, hb);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    int ox = rc.left + ((rc.right - rc.left) - box) / 2;
    int oy = rc.top + ((rc.bottom - rc.top) - box) / 2;
    AlphaBlend(hdc, ox, oy, box, box, mem, 0, 0, box, box, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void RenderPreview(HWND h, HDC dst) {
    RECT rc;
    GetClientRect(h, &rc);
    int w = rc.right - rc.left, hgt = rc.bottom - rc.top;
    if (w < 40 || hgt < 40) return;

    // resolve the previewed theme: dark/light modes map directly; auto and
    // follow-system stand in with the system theme (the real wheel decides
    // auto by sampling the actual screen area it opens on)
    bool dark;
    switch (g_cfg.app.colorMode) {
    case 1:  dark = false; break;
    case 2:
    case 3:  dark = SystemAppTheme() == 0; break;
    default: dark = true; break;
    }

    // studio canvas background follows the previewed theme
    HBRUSH bg = CreateSolidBrush(dark ? RGB(22, 26, 35) : RGB(233, 237, 243));
    FillRect(dst, &rc, bg);
    DeleteObject(bg);

    // Subtle 1px inner frame border
    HPEN pen = CreatePen(PS_SOLID, 1, dark ? RGB(45, 55, 72) : RGB(184, 193, 206));
    HGDIOBJ oldPen = SelectObject(dst, pen);
    HGDIOBJ oldBr = SelectObject(dst, GetStockObject(NULL_BRUSH));
    Rectangle(dst, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dst, oldPen);
    SelectObject(dst, oldBr);
    DeleteObject(pen);

    static WheelSurface surf;
    float scale = (float)s_dpi / 96.0f;
    float d = g_cfg.app.diameter * scale;
    float avail = (float)std::min(w, hgt) - 20.0f;
    if (d > avail && d > 1) scale *= avail / d;   // shrink to fit the preview box
    d = g_cfg.app.diameter * scale;
    float pad = (std::min(w, hgt) - d) / 2;
    if (pad < 8) pad = 8;
    Page* p = CurPage();
    std::vector<Slot> empty;
    const std::vector<Slot>* slots = p ? &p->slots : &empty;
    int hover = (!slots->empty() && !(*slots)[0].Empty() && g_cfg.app.sectorCount > 0) ? 0 : -1;
    WheelFrameState st{ &g_cfg.app, slots, 1, 0, hover, 1.0f, scale };
    st.dark = dark;
    if (g_cfg.app.bgMode == 1 || g_cfg.app.bgMode == 2) {
        // simulated backdrop (a real wheel captures the actual screen behind
        // it; the preview uses a procedural stand-in). Liquid glass needs a
        // sharp, structured image so the refraction stays visible.
        static std::vector<uint8_t> fake;
        static int fw = 0, fh = 0, fmode = -1;
        int mode = g_cfg.app.bgMode;
        if (w != fw || hgt != fh || mode != fmode) {
            fw = w; fh = hgt; fmode = mode;
            fake.assign((size_t)w * hgt * 4, 0);
            struct Blob { float x, y, r; uint8_t r8, g8, b8; };
            Blob blobs[3] = {
                { 0.22f, 0.35f, 0.45f, 60, 110, 200 },
                { 0.68f, 0.28f, 0.40f, 160, 100, 190 },
                { 0.52f, 0.78f, 0.50f, 200, 140, 80 },
            };
            for (int y = 0; y < hgt; y++) {
                for (int x = 0; x < w; x++) {
                    float u = x / (float)w, v = y / (float)hgt;
                    float r = 24 + 30 * u, g = 28 + 20 * v, b = 42 + 25 * (1 - u);
                    for (auto& bl : blobs) {
                        float dx = u - bl.x, dy = v - bl.y;
                        float dd = sqrtf(dx * dx + dy * dy) / bl.r;
                        float glow = std::max(0.0f, 1.0f - dd);
                        r += bl.r8 * glow * 0.5f;
                        g += bl.g8 * glow * 0.5f;
                        b += bl.b8 * glow * 0.5f;
                    }
                    if (mode == 2) {
                        // diagonal waves + light bands give the lens something to bend
                        float wave = sinf((u * 5.0f + v * 3.5f) * kPiF);
                        r += 20 * wave; g += 20 * wave; b += 22 * wave;
                        float band = sinf((u * 14.0f - v * 2.0f) * kPiF);
                        if (band > 0.86f) { r += 70; g += 72; b += 74; }
                    }
                    size_t i = ((size_t)y * w + x) * 4;
                    fake[i] = (uint8_t)std::min(255.0f, b);
                    fake[i + 1] = (uint8_t)std::min(255.0f, g);
                    fake[i + 2] = (uint8_t)std::min(255.0f, r);
                    fake[i + 3] = 255;
                }
            }
            BlurBGRA(fake.data(), fw, fh, mode == 2 ? 3 : 26);
        }
        st.backdrop = fake.data();
        st.backdropW = fw;
        st.backdropH = fh;
        st.backdropDispW = (float)fw;
        st.backdropDispH = (float)fh;
        st.backdropX = 0;
        st.backdropY = 0;
        st.backdropSeq = (mode == 2) ? 2 : 1;
    }
    surf.Resize((float)w, (float)hgt);
    surf.Render(st, pad);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dst, 0, 0, w, hgt, surf.MemDC(), 0, 0, w, hgt, bf);

    // subtle watermark hint in top-left
    SetBkMode(dst, TRANSPARENT);
    SetTextColor(dst, dark ? RGB(110, 125, 145) : RGB(122, 136, 155));
    if (s_font) SelectObject(dst, s_font);
    TextOutW(dst, 14, 10, L"🎨 轮盘实时预览画布", 10);
}

static LRESULT CALLBACK PreviewProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RenderPreview(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefSubclassProc(h, m, wp, lp);
}

static LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT rc;
        GetClientRect(h, &rc);
        static HBRUSH s_bgBr = CreateSolidBrush(RGB(246, 248, 250));
        FillRect(dc, &rc, s_bgBr);
        return 1;
    }
    case WM_COMMAND:
    case WM_HSCROLL:
    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
        return SendMessageW(s_hwnd, m, wp, lp);
    }
    return DefSubclassProc(h, m, wp, lp);
}

// ---------- refresh ----------

static void RefreshPages() {
    HWND lb = P1Of(IDC_P1_PAGES);
    s_noSync = true;
    ListBox_ResetContent(lb);
    for (size_t i = 0; i < g_cfg.pages.size(); i++)
        ListBox_AddString(lb, g_cfg.pages[i].name.c_str());
    if (s_curPage < 0 || s_curPage >= (int)g_cfg.pages.size()) s_curPage = 0;
    if (!g_cfg.pages.empty()) ListBox_SetCurSel(lb, s_curPage);
    s_noSync = false;
}

// wheel view (defined below BuildPanel1's helpers); refreshed from everywhere
static HWND WheelViewOf();
static void InvalidateWheelView();

static void RefreshSlots() {
    HWND lb = P1Of(IDC_P1_SLOTS);    s_noSync = true;
    ListBox_ResetContent(lb);
    Page* p = CurPage();
    if (p) {
        for (size_t i = 0; i < p->slots.size(); i++) {
            std::wstring t = std::to_wstring(i + 1) + L". " +
                             (p->slots[i].name.empty() ? L"（空）" : p->slots[i].name);
            if ((int)i >= g_cfg.app.sectorCount) t += L"  (超出)";
            ListBox_AddString(lb, t.c_str());
        }
        if (s_curSlot >= (int)p->slots.size()) s_curSlot = p->slots.empty() ? -1 : (int)p->slots.size() - 1;
        if (s_curSlot < 0 && !p->slots.empty()) s_curSlot = 0;
        if (s_curSlot >= 0) ListBox_SetCurSel(lb, s_curSlot);
    }
    s_noSync = false;
    InvalidateWheelView();
}

// sync modifier checkboxes from the current slot's chord
static void SyncTogglesFromSlot() {
    Slot* s = CurSlot();
    bool win = false, ctrl = false, alt = false, shift = false;
    if (s && !s->keys.empty()) {
        ParsedChord c;
        if (ParseChord(s->keys, c)) {
            win = c.win; ctrl = c.ctrl; alt = c.alt; shift = c.shift;
        }
    }
    Button_SetCheck(P1Of(IDC_P1_MODWIN), win ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(P1Of(IDC_P1_MODCTRL), ctrl ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(P1Of(IDC_P1_MODALT), alt ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(P1Of(IDC_P1_MODSHIFT), shift ? BST_CHECKED : BST_UNCHECKED);
}

// rebuild slot.keys from checkbox state, keeping the existing regular keys
static void ApplyTogglesToSlot() {
    Slot* s = CurSlot();
    if (!s) return;
    ParsedChord c;
    ParseChord(s->keys, c);
    c.win = (Button_GetCheck(P1Of(IDC_P1_MODWIN)) == BST_CHECKED);
    c.ctrl = (Button_GetCheck(P1Of(IDC_P1_MODCTRL)) == BST_CHECKED);
    c.alt = (Button_GetCheck(P1Of(IDC_P1_MODALT)) == BST_CHECKED);
    c.shift = (Button_GetCheck(P1Of(IDC_P1_MODSHIFT)) == BST_CHECKED);

    std::wstring t;
    if (c.win)   t += L"Win+";
    if (c.ctrl)  t += L"Ctrl+";
    if (c.alt)   t += L"Alt+";
    if (c.shift) t += L"Shift+";
    for (size_t i = 0; i < c.vks.size(); i++) {
        if (i > 0) t += L"+";
        t += VkDisplayName(c.vks[i]);
    }
    if (c.vks.empty() && !t.empty() && t.back() == L'+') t.pop_back();

    s->keys = t;
    s_noSync = true;
    Edit_SetText(P1Of(IDC_P1_KEYS), s->keys.c_str());
    s_noSync = false;
    InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
    SaveSoon();
}

static void RefreshDetail() {
    Slot* s = CurSlot();
    s_noSync = true;
    bool has = !!s;
    HWND ctl[] = { P1Of(IDC_P1_TYPE), P1Of(IDC_P1_NAME), P1Of(IDC_P1_KEYS), P1Of(IDC_P1_KEYPRESET), P1Of(IDC_P1_TARGET),
                   P1Of(IDC_P1_BRFILE), P1Of(IDC_P1_BRFOLDER), P1Of(IDC_P1_ICONPRESET), P1Of(IDC_P1_ICONIMG), P1Of(IDC_P1_ICONCLEAR),
                   P1Of(IDC_P1_ICONLIB) };
    for (HWND c : ctl) EnableWindow(c, has);
    if (s) {
        bool hot = s->type == SlotType::Hotkey;
        bool isFile = s->type == SlotType::File;
        ComboBox_SetCurSel(P1Of(IDC_P1_TYPE), (int)s->type);
        Edit_SetText(P1Of(IDC_P1_NAME), s->name.c_str());
        Edit_SetText(P1Of(IDC_P1_KEYS), s->keys.c_str());
        Edit_SetText(P1Of(IDC_P1_TARGET), s->TargetText().c_str());
        ShowWindow(P1Of(IDC_P1_KEYS_L), hot ? SW_SHOW : SW_HIDE);
        ShowWindow(P1Of(IDC_P1_KEYS), hot ? SW_SHOW : SW_HIDE);
        ShowWindow(P1Of(IDC_P1_KEYPRESET), hot ? SW_SHOW : SW_HIDE);
        ShowWindow(P1Of(IDC_P1_KEYS_HINT), hot ? SW_SHOW : SW_HIDE);
        ShowWindow(P1Of(IDC_P1_MODWIN), hot ? SW_SHOW : SW_HIDE);
        ShowWindow(P1Of(IDC_P1_MODCTRL), hot ? SW_SHOW : SW_HIDE);
        ShowWindow(P1Of(IDC_P1_MODALT), hot ? SW_SHOW : SW_HIDE);
        ShowWindow(P1Of(IDC_P1_MODSHIFT), hot ? SW_SHOW : SW_HIDE);
        ShowWindow(P1Of(IDC_P1_TGT_L), hot ? SW_HIDE : SW_SHOW);
        ShowWindow(P1Of(IDC_P1_TARGET), hot ? SW_HIDE : SW_SHOW);
        Static_SetText(P1Of(IDC_P1_TGT_L), s->type == SlotType::Url ? L"网址" : L"目标");
        ShowWindow(P1Of(IDC_P1_BRFILE), isFile ? SW_SHOW : SW_HIDE);
        ShowWindow(P1Of(IDC_P1_BRFOLDER), isFile ? SW_SHOW : SW_HIDE);
        SyncTogglesFromSlot();
    }
    InvalidateRect(P1Of(IDC_P1_ICONPREV), nullptr, TRUE);
    InvalidateWheelView();
    s_noSync = false;
}

static void SetSlider(HWND sl, int val) { SendMessageW(sl, TBM_SETPOS, TRUE, val); }
static int SliderVal(HWND sl) { return (int)SendMessageW(sl, TBM_GETPOS, 0, 0); }

static float GetAppearanceVal(int sliderId) {
    const Appearance& a = g_cfg.app;
    switch (sliderId) {
    case IDC_P2_SECTORS:   return (float)a.sectorCount;
    case IDC_P2_ROTATION:  return a.rotationDeg;
    case IDC_P2_DIAMETER:  return a.diameter;
    case IDC_P2_ICONSIZE:  return a.iconSize;
    case IDC_P2_GAP:       return a.sectorGap;
    case IDC_P2_ANIMMS:    return a.animMs;
    case IDC_P2_BGOPACITY: return std::round(a.bgOpacity * 100.0f);
    case IDC_P2_BORDW:     return a.borderWidth;
    case IDC_P2_LABELSIZE: return a.labelSize;
    case IDC_P2_GLASSBLUR:   return a.glassBlur;
    case IDC_P2_GLASSHEIGHT: return a.glassHeight;
    case IDC_P2_GLASSAMT:    return a.glassAmount;
    case IDC_P2_CUTRES:      return a.backdropScale > 0.0f ? a.backdropScale * 100.0f : 100.0f;
    case IDC_P2_CUTFPS:      return (float)a.backdropFps;
    default: return 0.0f;
    }
}

static void SetAppearanceVal(int sliderId, float val) {
    Appearance& a = g_cfg.app;
    switch (sliderId) {
    case IDC_P2_SECTORS:
        a.sectorCount = std::clamp((int)std::lround(val), 1, 36);
        RefreshSlots();
        break;
    case IDC_P2_ROTATION: {
        float r = val;
        while (r < 0.0f) r += 360.0f;
        while (r >= 360.0f) r -= 360.0f;
        a.rotationDeg = r;
        break;
    }
    case IDC_P2_DIAMETER:
        a.diameter = std::lround(std::clamp(val, 120.0f, 800.0f));
        break;
    case IDC_P2_ICONSIZE:
        a.iconSize = std::lround(std::clamp(val, 12.0f, 120.0f));
        break;
    case IDC_P2_GAP:
        a.sectorGap = std::clamp(val, 0.0f, 30.0f);
        break;
    case IDC_P2_ANIMMS:
        a.animMs = std::lround(std::clamp(val, 0.0f, 1000.0f));
        break;
    case IDC_P2_BGOPACITY:
        a.bgOpacity = std::clamp(std::lround(val), 5L, 100L) / 100.0f;
        break;
    case IDC_P2_BORDW:
        a.borderWidth = std::clamp(std::round(val * 2.0f) / 2.0f, 0.0f, 12.0f);
        break;
    case IDC_P2_LABELSIZE:
        a.labelSize = std::lround(std::clamp(val, 6.0f, 36.0f));
        break;
    case IDC_P2_GLASSBLUR:
        a.glassBlur = std::clamp(val, 0.0f, 32.0f);
        break;
    case IDC_P2_GLASSHEIGHT:
        a.glassHeight = std::clamp(val, 0.0f, 96.0f);
        break;
    case IDC_P2_GLASSAMT:
        a.glassAmount = std::clamp(val, 0.0f, 200.0f);
        break;
    case IDC_P2_CUTRES:
        a.backdropScale = std::clamp(std::lround(val), 25L, 100L) / 100.0f;
        break;
    case IDC_P2_CUTFPS:
        a.backdropFps = (int)std::clamp(std::lround(val), 10L, 60L);
        break;
    }
}

static float GetSliderVal(int sliderId) {
    if (sliderId == IDC_P3_THRESHOLD) return g_cfg.beh.thresholdPx;
    if (sliderId == IDC_P3_WHEELSENS) return (float)g_cfg.beh.wheelSensitivity;
    return GetAppearanceVal(sliderId);
}

static void SetSliderVal(int sliderId, float val) {
    if (sliderId == IDC_P3_THRESHOLD) {
        g_cfg.beh.thresholdPx = (float)std::lround(std::clamp(val, 6.0f, 40.0f));
        return;
    }
    if (sliderId == IDC_P3_WHEELSENS) {
        g_cfg.beh.wheelSensitivity = (int)std::clamp((float)std::lround(val), 20.0f, 300.0f);
        return;
    }
    SetAppearanceVal(sliderId, val);
}

static void SyncSliderFromVal(int sliderId, float val) {
    HWND sl = (sliderId >= IDC_P3_ENABLE) ? P3Of(sliderId) : P2Of(sliderId);
    if (!sl) return;
    int pos = 0;
    if (sliderId == IDC_P2_BORDW) {
        pos = (int)std::lround(val * 2.0f);
    } else {
        pos = (int)std::lround(val);
    }
    SetSlider(sl, pos);
}

// sliders whose value is meaningfully fractional; their edit boxes accept and
// show one decimal place (all other sliders round typed decimals to ints)
static bool DecimalSlider(int id) {
    switch (id) {
    case IDC_P2_BORDW:
    case IDC_P2_GAP:
    case IDC_P2_ROTATION:
    case IDC_P2_GLASSBLUR:
    case IDC_P2_GLASSHEIGHT:
    case IDC_P2_GLASSAMT:
        return true;
    }
    return false;
}

static void FormatValForEdit(int sliderId, float val, wchar_t* buf, size_t sz) {
    if (DecimalSlider(sliderId)) {
        swprintf_s(buf, sz, L"%.1f", val);
    } else {
        swprintf_s(buf, sz, L"%d", (int)std::lround(val));
    }
}

static bool IsValueEdit(int id, int& sliderId) {
    static const int ids[] = {
        IDC_P2_SECTORS, IDC_P2_ROTATION, IDC_P2_DIAMETER,
        IDC_P2_ICONSIZE, IDC_P2_GAP, IDC_P2_ANIMMS,
        IDC_P2_BGOPACITY, IDC_P2_BORDW, IDC_P2_LABELSIZE,
        IDC_P2_GLASSBLUR, IDC_P2_GLASSHEIGHT, IDC_P2_GLASSAMT,
        IDC_P2_CUTRES, IDC_P2_CUTFPS,
        IDC_P3_THRESHOLD, IDC_P3_WHEELSENS
    };
    for (int sid : ids) {
        if (id == sid + 100) {
            sliderId = sid;
            return true;
        }
    }
    return false;
}

static LRESULT CALLBACK NumEditProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    int sliderId = (int)dwRefData;
    switch (m) {
    case WM_GETDLGCODE: {
        LRESULT res = DLGC_WANTARROWS | DLGC_WANTCHARS;
        if (lp) {
            MSG* pmsg = (MSG*)lp;
            if (pmsg->message == WM_KEYDOWN || pmsg->message == WM_KEYUP || pmsg->message == WM_CHAR) {
                if (pmsg->wParam == VK_RETURN) return DLGC_WANTALLKEYS;
            }
        }
        return res;
    }

    case WM_SETFOCUS: {
        LRESULT res = DefSubclassProc(h, m, wp, lp);
        PostMessageW(h, EM_SETSEL, 0, -1);
        return res;
    }

    case WM_KEYDOWN: {
        UINT vk = (UINT)wp;
        if (vk == VK_RETURN) {
            wchar_t text[64] = L"";
            GetWindowTextW(h, text, 64);
            wchar_t* end = nullptr;
            float val = wcstof(text, &end);
            if (end == text) val = GetSliderVal(sliderId);
            SetSliderVal(sliderId, val);
            float actual = GetSliderVal(sliderId);
            SyncSliderFromVal(sliderId, actual);
            wchar_t buf[32];
            FormatValForEdit(sliderId, actual, buf, 32);
            s_noSync = true;
            Edit_SetText(h, buf);
            s_noSync = false;
            SendMessageW(h, EM_SETSEL, 0, -1);
            if (sliderId < IDC_P3_ENABLE) InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
            SaveSoon();
            return 0;
        }
        if (vk == VK_ESCAPE) {
            float actual = GetSliderVal(sliderId);
            wchar_t buf[32];
            FormatValForEdit(sliderId, actual, buf, 32);
            s_noSync = true;
            Edit_SetText(h, buf);
            s_noSync = false;
            SendMessageW(h, EM_SETSEL, 0, -1);
            return 0;
        }
        if (vk == VK_UP || vk == VK_DOWN) {
            float step = 1.0f;
            if (sliderId == IDC_P2_BORDW) step = 0.5f;
            else if (DecimalSlider(sliderId)) step = 0.5f;
            else if (sliderId == IDC_P2_DIAMETER || sliderId == IDC_P2_ANIMMS || sliderId == IDC_P3_WHEELSENS) step = 10.0f;
            float cur = GetSliderVal(sliderId);
            float nxt = cur + (vk == VK_UP ? step : -step);
            SetSliderVal(sliderId, nxt);
            float actual = GetSliderVal(sliderId);
            SyncSliderFromVal(sliderId, actual);
            wchar_t buf[32];
            FormatValForEdit(sliderId, actual, buf, 32);
            s_noSync = true;
            Edit_SetText(h, buf);
            s_noSync = false;
            SendMessageW(h, EM_SETSEL, 0, -1);
            if (sliderId < IDC_P3_ENABLE) InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
            SaveSoon();
            return 0;
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (delta != 0) {
            float step = 1.0f;
            if (sliderId == IDC_P2_BORDW) step = 0.5f;
            else if (DecimalSlider(sliderId)) step = 0.5f;
            else if (sliderId == IDC_P2_DIAMETER || sliderId == IDC_P2_ANIMMS || sliderId == IDC_P3_WHEELSENS) step = 10.0f;
            float cur = GetSliderVal(sliderId);
            float nxt = cur + (delta > 0 ? step : -step);
            SetSliderVal(sliderId, nxt);
            float actual = GetSliderVal(sliderId);
            SyncSliderFromVal(sliderId, actual);
            wchar_t buf[32];
            FormatValForEdit(sliderId, actual, buf, 32);
            s_noSync = true;
            Edit_SetText(h, buf);
            s_noSync = false;
            SendMessageW(h, EM_SETSEL, 0, -1);
            if (sliderId < IDC_P3_ENABLE) InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
            SaveSoon();
            return 0;
        }
        break;
    }

    case WM_CHAR: {
        wchar_t ch = (wchar_t)wp;
        if (ch < 32) break;
        if (ch >= L'0' && ch <= L'9') break;
        if (ch == L'.') {
            wchar_t text[32] = L"";
            GetWindowTextW(h, text, 32);
            if (wcschr(text, L'.') == nullptr) break;
        }
        return 0;
    }
    }
    return DefSubclassProc(h, m, wp, lp);
}

static void UpdateGlassUIEnabled() {
    bool glass = (g_cfg.app.bgMode == 2);
    bool live  = (g_cfg.app.bgMode == 1 || g_cfg.app.bgMode == 2);
    const int ids[] = { IDC_P2_GLASSBLUR, IDC_P2_GLASSHEIGHT, IDC_P2_GLASSAMT,
                        IDC_P2_GLASSCHROMA, IDC_P2_GLASSHL, IDC_P2_GLASSVIB,
                        IDC_P2_CUTRES };
    for (int id : ids) {
        if (HWND c = P2Of(id)) EnableWindow(c, glass);
        if (HWND e = P2Of(id + 100)) EnableWindow(e, glass);
    }
    if (HWND c = P2Of(IDC_P2_CUTFPS)) EnableWindow(c, live);
    if (HWND e = P2Of(IDC_P2_CUTFPS + 100)) EnableWindow(e, live);
}

static void UpdateHoverAutoUI() {
    bool manual = !g_cfg.app.hoverAccent;
    EnableWindow(P2Of(IDC_P2_HOVERCOLOR), manual);
    EnableWindow(P2Of(IDC_P2_HOVERCOLOR_L), manual);
}

static void RefreshAppearance() {
    s_noSync = true;
    const Appearance& a = g_cfg.app;
    SetSlider(P2Of(IDC_P2_SECTORS), a.sectorCount);
    SetSlider(P2Of(IDC_P2_ROTATION), (int)std::lround(a.rotationDeg));
    SetSlider(P2Of(IDC_P2_DIAMETER), (int)std::lround(a.diameter));
    SetSlider(P2Of(IDC_P2_ICONSIZE), (int)std::lround(a.iconSize));
    SetSlider(P2Of(IDC_P2_GAP), (int)std::lround(a.sectorGap));
    SetSlider(P2Of(IDC_P2_ANIMMS), (int)std::lround(a.animMs));
    Button_SetCheck(P2Of(IDC_P2_ANIMCHK), a.animation ? BST_CHECKED : BST_UNCHECKED);
    ComboBox_SetCurSel(P2Of(IDC_P2_BGMODE), std::clamp(a.bgMode, 0, 2));
    ComboBox_SetCurSel(P2Of(IDC_P2_COLORMODE), std::clamp(a.colorMode, 0, 3));
    SetSlider(P2Of(IDC_P2_BGOPACITY), (int)std::lround(a.bgOpacity * 100));
    SetSlider(P2Of(IDC_P2_BORDW), (int)std::lround(a.borderWidth * 2));
    SetSlider(P2Of(IDC_P2_GLASSBLUR), (int)std::lround(a.glassBlur));
    SetSlider(P2Of(IDC_P2_GLASSHEIGHT), (int)std::lround(a.glassHeight));
    SetSlider(P2Of(IDC_P2_GLASSAMT), (int)std::lround(a.glassAmount));
    SetSlider(P2Of(IDC_P2_CUTRES), (int)std::lround(a.backdropScale > 0.0f ? a.backdropScale * 100.0f : 100.0f));
    SetSlider(P2Of(IDC_P2_CUTFPS), a.backdropFps);
    Button_SetCheck(P2Of(IDC_P2_HOVERAUTO), a.hoverAccent ? BST_CHECKED : BST_UNCHECKED);
    UpdateHoverAutoUI();
    Button_SetCheck(P2Of(IDC_P2_GLASSCHROMA), a.glassChroma ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(P2Of(IDC_P2_GLASSHL), a.glassHighlight ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(P2Of(IDC_P2_GLASSVIB), a.glassVibrancy ? BST_CHECKED : BST_UNCHECKED);
    UpdateGlassUIEnabled();
    Button_SetCheck(P2Of(IDC_P2_LABELS), a.showLabels ? BST_CHECKED : BST_UNCHECKED);
    SetSlider(P2Of(IDC_P2_LABELSIZE), (int)std::lround(a.labelSize));
    ComboBox_SetCurSel(P2Of(IDC_P2_CENTER), a.centerMode);
    Edit_SetText(P2Of(IDC_P2_CENTERTEXT), a.centerText.c_str());
    EnableWindow(P2Of(IDC_P2_CENTERTEXT), a.centerMode == 1);
    EnableWindow(P2Of(IDC_P2_ANIMMS), a.animation);
    EnableWindow(P2Of(IDC_P2_ANIMMS + 100), a.animation);
    EnableWindow(P2Of(IDC_P2_LABELSIZE), a.showLabels);
    EnableWindow(P2Of(IDC_P2_LABELSIZE + 100), a.showLabels);
    s_noSync = false;
    InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
    InvalidateWheelView();
}

static void RefreshBehavior() {
    s_noSync = true;
    const Behavior& b = g_cfg.beh;
    Button_SetCheck(P3Of(IDC_P3_ENABLE), b.enabled ? BST_CHECKED : BST_UNCHECKED);
    ComboBox_SetCurSel(P3Of(IDC_P3_BUTTON), b.triggerButton == 2 ? 1 : 0);
    SetSlider(P3Of(IDC_P3_THRESHOLD), (int)b.thresholdPx);
    wchar_t buf[32];
    swprintf_s(buf, L"%d", (int)b.thresholdPx);
    Edit_SetText(GetDlgItem(s_panel3, IDC_P3_THRESHOLD + 100), buf);

    SetSlider(P3Of(IDC_P3_WHEELSENS), b.wheelSensitivity);
    swprintf_s(buf, L"%d", b.wheelSensitivity);
    Edit_SetText(GetDlgItem(s_panel3, IDC_P3_WHEELSENS + 100), buf);

    Button_SetCheck(P3Of(IDC_P3_TRAY), b.showTray ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(P3Of(IDC_P3_AUTOSTART), IsAutostartEnabled() ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(P3Of(IDC_P3_EXCLFULL), b.excludeFullscreen ? BST_CHECKED : BST_UNCHECKED);
    HWND lb = P3Of(IDC_P3_EXCL);
    ListBox_ResetContent(lb);
    for (auto& e : b.exclusions) ListBox_AddString(lb, e.c_str());
    s_noSync = false;
}

static void RefreshValueStatics() {
    static const int ids[] = {
        IDC_P2_SECTORS, IDC_P2_ROTATION, IDC_P2_DIAMETER,
        IDC_P2_ICONSIZE, IDC_P2_GAP, IDC_P2_ANIMMS,
        IDC_P2_BGOPACITY, IDC_P2_BORDW, IDC_P2_LABELSIZE,
        IDC_P2_GLASSBLUR, IDC_P2_GLASSHEIGHT, IDC_P2_GLASSAMT,
        IDC_P2_CUTRES, IDC_P2_CUTFPS
    };
    HWND focused = GetFocus();
    bool oldSync = s_noSync;
    s_noSync = true;
    wchar_t buf[32];
    for (int id : ids) {
        HWND ed = P2Of(id + 100);
        if (!ed || !IsWindow(ed)) continue;
        if (focused == ed) continue;
        float v = GetAppearanceVal(id);
        FormatValForEdit(id, v, buf, 32);
        Edit_SetText(ed, buf);
    }
    s_noSync = oldSync;
}

static void RefreshAll() {
    RefreshPages(); RefreshSlots(); RefreshDetail();
    RefreshAppearance(); RefreshValueStatics(); RefreshBehavior();
}

// ---------- control building ----------

static HWND Ctl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex,
                int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
                             S(x), S(y), S(w), S(h), parent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    if (s_font) SendMessageW(c, WM_SETFONT, (WPARAM)s_font, TRUE);
    return c;
}

static HWND AddSlider(HWND p, int id, int x, int y, int w, int mn, int mx, int val) {
    HWND t = Ctl(p, TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 0, x, y, w, 26, id);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(mn, mx));
    SendMessageW(t, TBM_SETPAGESIZE, 0, 1);
    SendMessageW(t, TBM_SETPOS, TRUE, val);
    return t;
}

static void AddSliderWithEdit(HWND p, int id, int titleX, int titleY, int titleW, const wchar_t* titleText,
                              int sliderX, int sliderY, int sliderW, int minVal, int maxVal, int defaultVal,
                              int editX, int editY, int editW, int editH,
                              int unitX, int unitY, int unitW, const wchar_t* unitText) {
    Ctl(p, L"Static", titleText, 0, 0, titleX, titleY, titleW, 18, -1);
    AddSlider(p, id, sliderX, sliderY, sliderW, minVal, maxVal, defaultVal);
    HWND ed = Ctl(p, WC_EDITW, L"", ES_AUTOHSCROLL | ES_CENTER | WS_TABSTOP, WS_EX_CLIENTEDGE, editX, editY, editW, editH, id + 100);
    SetWindowSubclass(ed, NumEditProc, 10, (DWORD_PTR)id);
    if (unitText && *unitText) {
        Ctl(p, L"Static", unitText, 0, 0, unitX, unitY, unitW, 18, -1);
    }
}

// ---------- wheel view: visual slot order editor ----------
// Renders the current page's slots in their real sector layout next to the
// list. Drag an icon to another sector to reorder; the drop commits on
// button release (nothing is mutated mid-drag), the target sector lights up
// while hovering. Clicking a sector selects the slot.

static int s_wvDrag = -1;    // dragged slot index, -1 = idle
static int s_wvHover = -1;   // target sector under the cursor while dragging

static HWND WheelViewOf() { return P1Of(IDC_P1_WHEELVIEW); }
static void InvalidateWheelView() { InvalidateRect(WheelViewOf(), nullptr, TRUE); }

static bool PreviewDark() {
    switch (g_cfg.app.colorMode) {
    case 1:  return false;
    case 2:
    case 3:  return SystemAppTheme() == 0;
    default: return true;
    }
}

// fit the configured wheel into the view box
static void WheelViewFit(HWND h, float& scale, float& pad, int& w, int& hgt) {
    RECT rc;
    GetClientRect(h, &rc);
    w = rc.right - rc.left; hgt = rc.bottom - rc.top;
    scale = (float)s_dpi / 96.0f;
    float d = g_cfg.app.diameter * scale;
    float avail = (float)std::min(w, hgt) - 8.0f;
    if (d > avail && d > 1) scale *= avail / d;
    d = g_cfg.app.diameter * scale;
    pad = ((float)std::min(w, hgt) - d) / 2;
    if (pad < 4) pad = 4;
}

static void RenderWheelView(HWND h, HDC dst) {
    RECT rc;
    GetClientRect(h, &rc);
    int w = 0, hgt = 0;
    float scale = 0, pad = 0;
    WheelViewFit(h, scale, pad, w, hgt);
    if (w < 60 || hgt < 60) return;

    // panel-matching backdrop so the round wheel reads as an inset preview
    HBRUSH br = (HBRUSH)SendMessageW(GetParent(h), WM_CTLCOLORSTATIC, (WPARAM)dst, (LPARAM)h);
    if (br) { FillRect(dst, &rc, br); }

    static WheelSurface surf;
    surf.Resize((float)w, (float)hgt);
    if (!surf.MemDC()) return;
    Page* p = CurPage();
    std::vector<Slot> empty;
    const std::vector<Slot>* slots = p ? &p->slots : &empty;
    int hover = -1;
    if (s_wvDrag >= 0)          hover = s_wvHover;   // drop target lights up
    else if (s_curSlot >= 0 && s_curSlot < g_cfg.app.sectorCount) hover = s_curSlot;
    WheelFrameState st{ &g_cfg.app, slots, 1, 0, hover, 1.0f, scale };
    st.dark = PreviewDark();
    surf.Render(st, pad);

    // blend straight from the surface's own MemDC — the DIB stays selected
    // there (same pattern as the appearance preview)
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dst, 0, 0, w, hgt, surf.MemDC(), 0, 0, w, hgt, bf);
}

static int WheelViewHit(HWND h, short x, short y) {
    int w = 0, hgt = 0;
    float scale = 0, pad = 0;
    WheelViewFit(h, scale, pad, w, hgt);
    WheelLayout L = ComputeLayout(g_cfg.app, scale, pad, (float)w, (float)hgt);
    return WheelHitTest(L, (float)x, (float)y);
}

static LRESULT CALLBACK WheelViewProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RenderWheelView(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINTS pts = MAKEPOINTS(lp);
        int idx = WheelViewHit(h, pts.x, pts.y);
        Page* p = CurPage();
        if (idx >= 0 && p && idx < g_cfg.app.sectorCount && idx < (int)p->slots.size()) {
            s_wvDrag = idx;
            s_wvHover = idx;
            SetCapture(h);
            if (s_curSlot != idx) {
                s_curSlot = idx;
                RefreshSlots();
                RefreshDetail();
            }
            InvalidateWheelView();
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (s_wvDrag < 0) break;
        POINTS pts = MAKEPOINTS(lp);
        int idx = WheelViewHit(h, pts.x, pts.y);
        if (idx == -2 || idx >= g_cfg.app.sectorCount) idx = -1;   // deadzone / outside
        if (idx != s_wvHover) {
            s_wvHover = idx;
            InvalidateWheelView();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (s_wvDrag < 0) break;
        int from = s_wvDrag, to = s_wvHover;
        s_wvDrag = -1; s_wvHover = -1;
        if (GetCapture() == h) ReleaseCapture();
        Page* p = CurPage();
        bool moved = false;
        if (p && to >= 0 && to < g_cfg.app.sectorCount && to != from) {
            // pad with empty slots so the drop target exists
            while ((int)p->slots.size() <= to) p->slots.push_back(Slot{});
            Slot s = p->slots[from];
            p->slots.erase(p->slots.begin() + from);
            p->slots.insert(p->slots.begin() + to, s);
            s_curSlot = to;
            moved = true;
        }
        RefreshSlots(); RefreshDetail(); InvalidateWheelView();
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        if (moved) SaveSoon();
        return 0;
    }
    case WM_CAPTURECHANGED:
        s_wvDrag = -1; s_wvHover = -1;
        InvalidateWheelView();
        return 0;
    }
    return DefSubclassProc(h, m, wp, lp);
}

// widen push buttons / checkboxes whose label would be clipped, up to the
// nearest right-hand sibling so nothing overlaps
static void AutofitButtons(HWND panel) {
    struct Item { HWND h; RECT r; };
    std::vector<Item> items;
    RECT pr;
    GetWindowRect(panel, &pr);
    for (HWND c = GetWindow(panel, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        RECT r;
        GetWindowRect(c, &r);
        OffsetRect(&r, -pr.left, -pr.top);
        items.push_back({ c, r });
    }
    // top-to-bottom, left-to-right so a widened button constrains its neighbors
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.r.top != b.r.top) return a.r.top < b.r.top;
        return a.r.left < b.r.left;
    });
    int panelW = pr.right - pr.left;
    for (auto& it : items) {
        HWND c = it.h;
        wchar_t cls[32] = L"";
        GetClassNameW(c, cls, 32);
        if (_wcsicmp(cls, L"Button") != 0) continue;
        DWORD bs = (DWORD)GetWindowLongW(c, GWL_STYLE) & 0xF;
        if (bs == BS_GROUPBOX) continue;
        wchar_t text[192] = L"";
        int len = GetWindowTextW(c, text, 192);
        if (!len) continue;
        HFONT f = (HFONT)SendMessageW(c, WM_GETFONT, 0, 0);
        if (!f) f = s_font;
        if (!f) continue;
        HDC dc = GetDC(panel);
        HGDIOBJ of = SelectObject(dc, f);
        SIZE sz{};
        GetTextExtentPoint32W(dc, text, len, &sz);
        SelectObject(dc, of);
        ReleaseDC(panel, dc);
        int need = sz.cx + GetSystemMetrics(SM_CXEDGE) * 4 + 14;
        int cur = it.r.right - it.r.left;
        if (need <= cur) continue;
        int limit = panelW - 10 - it.r.left;
        for (auto& o : items) {
            if (o.h == c) continue;
            const RECT& orr = o.r;
            bool sameRow = orr.top < it.r.bottom - 2 && orr.bottom > it.r.top + 2;
            bool toMyRight = orr.left >= it.r.right - 1;
            // sibling left edge is a position; convert to a width cap
            if (sameRow && toMyRight) limit = std::min(limit, (int)(orr.left - 6 - it.r.left));
        }
        int newW = std::min(need, limit);
        if (newW > cur) {
            SetWindowPos(c, nullptr, 0, 0, newW, it.r.bottom - it.r.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            it.r.right = it.r.left + newW;
        }
    }
}

static void BuildPanel1(HWND p) {
    Ctl(p, L"Static", L"页面列表", 0, 0, 10, 8, 80, 18, -1);
    Ctl(p, WC_LISTBOXW, L"", LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 10, 28, 140, 240, IDC_P1_PAGES);
    Ctl(p, L"Button", L"+ 添加页", BS_PUSHBUTTON | WS_TABSTOP, 0, 10, 274, 68, 26, IDC_P1_ADDPAGE);
    Ctl(p, L"Button", L"重命名", BS_PUSHBUTTON | WS_TABSTOP, 0, 82, 274, 68, 26, IDC_P1_RENPAGE);
    Ctl(p, L"Button", L"删除页", BS_PUSHBUTTON | WS_TABSTOP, 0, 10, 304, 140, 26, IDC_P1_DELPAGE);

    Ctl(p, L"Static", L"当前页槽位", 0, 0, 164, 8, 120, 18, -1);
    Ctl(p, WC_LISTBOXW, L"", LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 164, 28, 150, 300, IDC_P1_SLOTS);

    Ctl(p, L"Static", L"轮盘位置 (拖动图标换位)", 0, 0, 164, 336, 150, 18, -1);
    Ctl(p, L"Static", L"", SS_NOTIFY, WS_EX_CLIENTEDGE, 164, 356, 150, 130, IDC_P1_WHEELVIEW);
    SetWindowSubclass(GetDlgItem(p, IDC_P1_WHEELVIEW), WheelViewProc, 21, 0);

    Ctl(p, L"Button", L"+ 插入槽位", BS_PUSHBUTTON | WS_TABSTOP, 0, 164, 492, 72, 26, IDC_P1_ADDSLOT);
    Ctl(p, L"Button", L"删除",     BS_PUSHBUTTON | WS_TABSTOP, 0, 242, 492, 72, 26, IDC_P1_DELSLOT);
    Ctl(p, L"Button", L"↑ 上移",     BS_PUSHBUTTON | WS_TABSTOP, 0, 164, 522, 72, 26, IDC_P1_UPSLOT);
    Ctl(p, L"Button", L"↓ 下移",     BS_PUSHBUTTON | WS_TABSTOP, 0, 242, 522, 72, 26, IDC_P1_DOWNSLOT);

    Ctl(p, L"Button", L"槽位属性详情设置", BS_GROUPBOX, 0, 330, 8, 560, 502, -1);

    Ctl(p, L"Static", L"动作类型", 0, 0, 350, 36, 70, 18, -1);
    HWND type = Ctl(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 430, 32, 200, 200, IDC_P1_TYPE);
    ComboBox_AddString(type, L"发送快捷键");
    ComboBox_AddString(type, L"打开文件 / 文件夹");
    ComboBox_AddString(type, L"打开网址");

    Ctl(p, L"Static", L"显示名称", 0, 0, 350, 74, 70, 18, -1);
    Ctl(p, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 430, 70, 320, 26, IDC_P1_NAME);

    Ctl(p, L"Static", L"快捷键", 0, 0, 350, 112, 70, 18, IDC_P1_KEYS_L);
    Ctl(p, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 430, 108, 128, 26, IDC_P1_KEYS);
    Ctl(p, L"Button", L"⚡ 常用预设…", BS_PUSHBUTTON | WS_TABSTOP, 0, 562, 108, 102, 26, IDC_P1_KEYPRESET);
    Ctl(p, L"Button", L"Win",   BS_AUTOCHECKBOX | WS_TABSTOP, 0, 672, 111, 48, 20, IDC_P1_MODWIN);
    Ctl(p, L"Button", L"Ctrl",  BS_AUTOCHECKBOX | WS_TABSTOP, 0, 722, 111, 52, 20, IDC_P1_MODCTRL);
    Ctl(p, L"Button", L"Alt",   BS_AUTOCHECKBOX | WS_TABSTOP, 0, 776, 111, 48, 20, IDC_P1_MODALT);
    Ctl(p, L"Button", L"Shift", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 826, 111, 56, 20, IDC_P1_MODSHIFT);
    Ctl(p, L"Static", L"💡 录入方式：可在输入框中直接打字输入（如 Ctrl+Shift+Esc、Win+Shift+S、Ctrl+Alt+A、F5），或勾选修饰键/点击“⚡ 常用预设…”。", 0, 0, 350, 140, 530, 18, IDC_P1_KEYS_HINT);

    Ctl(p, L"Static", L"目标路径", 0, 0, 350, 172, 70, 18, IDC_P1_TGT_L);
    Ctl(p, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 430, 168, 300, 26, IDC_P1_TARGET);
    Ctl(p, L"Button", L"选文件…", BS_PUSHBUTTON | WS_TABSTOP, 0, 736, 168, 68, 26, IDC_P1_BRFILE);
    Ctl(p, L"Button", L"选目录…", BS_PUSHBUTTON | WS_TABSTOP, 0, 808, 168, 68, 26, IDC_P1_BRFOLDER);

    Ctl(p, L"Static", L"槽位图标", 0, 0, 350, 212, 70, 18, -1);
    Ctl(p, L"Static", L"", SS_OWNERDRAW, 0, 350, 236, 72, 72, IDC_P1_ICONPREV);
    Ctl(p, L"Button", L"🎨 常用图标…", BS_PUSHBUTTON | WS_TABSTOP, 0, 436, 238, 120, 28, IDC_P1_ICONPRESET);
    Ctl(p, L"Button", L"🖼 本地图片…", BS_PUSHBUTTON | WS_TABSTOP, 0, 564, 238, 120, 28, IDC_P1_ICONIMG);
    Ctl(p, L"Button", L"↺ 恢复默认",   BS_PUSHBUTTON | WS_TABSTOP, 0, 692, 238, 100, 28, IDC_P1_ICONCLEAR);
    Ctl(p, L"Button", L"📚 图标库…",   BS_PUSHBUTTON | WS_TABSTOP, 0, 436, 272, 120, 28, IDC_P1_ICONLIB);
    Ctl(p, L"Static", L"留空自动提取目标文件/程序高清图标；支持内置矢量图标，或自定义 png / jpg / ico / svg。", 0, 0, 436, 308, 430, 36, -1);
}

static void BuildPanel2(HWND p) {
    Ctl(p, L"Static", L"", SS_NOTIFY, WS_EX_CLIENTEDGE, 10, 8, 880, 220, IDC_P2_PREVIEW);
    SetWindowSubclass(GetDlgItem(p, IDC_P2_PREVIEW), PreviewProc, 1, 0);

    // Col 1: 尺寸与布局
    Ctl(p, L"Button", L"布局与动画", BS_GROUPBOX, 0, 10, 236, 280, 246, -1);
    AddSliderWithEdit(p, IDC_P2_SECTORS,  22, 262, 36, L"扇区", 60, 258, 125, 1, 36, 8,
                      190, 259, 52, 22, 246, 262, 34, L"个");
    AddSliderWithEdit(p, IDC_P2_ROTATION, 22, 294, 36, L"旋转", 60, 290, 125, 0, 360, 0,
                      190, 291, 52, 22, 246, 294, 34, L"°");
    AddSliderWithEdit(p, IDC_P2_DIAMETER, 22, 326, 36, L"直径", 60, 322, 125, 150, 600, 300,
                      190, 323, 52, 22, 246, 326, 34, L"px");
    AddSliderWithEdit(p, IDC_P2_ICONSIZE, 22, 358, 36, L"图标", 60, 354, 125, 16, 96, 46,
                      190, 355, 52, 22, 246, 358, 34, L"px");
    AddSliderWithEdit(p, IDC_P2_GAP,      22, 390, 36, L"间隙", 60, 386, 125, 0, 15, 2,
                      190, 387, 52, 22, 246, 390, 34, L"°");
    AddSliderWithEdit(p, IDC_P2_ANIMMS,   22, 422, 36, L"动画", 60, 418, 125, 0, 500, 130,
                      190, 419, 52, 22, 246, 422, 34, L"ms");

    Ctl(p, L"Button", L"启用出现动画", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 22, 452, 120, 20, IDC_P2_ANIMCHK);

    // Col 2: 材质与色彩
    Ctl(p, L"Button", L"材质与色彩", BS_GROUPBOX, 0, 300, 236, 290, 246, -1);
    Ctl(p, L"Static", L"背景模式", 0, 0, 314, 264, 60, 18, -1);
    HWND bg = Ctl(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 380, 260, 194, 200, IDC_P2_BGMODE);
    ComboBox_AddString(bg, L"纯色半透明");
    ComboBox_AddString(bg, L"亚克力模糊 (毛玻璃)");
    ComboBox_AddString(bg, L"液态玻璃 (折射)");

    Ctl(p, L"Static", L"色彩模式", 0, 0, 314, 296, 60, 18, -1);
    HWND cmode = Ctl(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 380, 292, 194, 200, IDC_P2_COLORMODE);
    ComboBox_AddString(cmode, L"深色 (深底白字)");
    ComboBox_AddString(cmode, L"浅色 (浅底黑字)");
    ComboBox_AddString(cmode, L"自动 (按轮盘所在背景明暗)");
    ComboBox_AddString(cmode, L"跟随系统深浅模式");

    AddSliderWithEdit(p, IDC_P2_BGOPACITY, 314, 328, 50, L"透明度", 366, 324, 125, 10, 100, 86,
                      496, 325, 52, 22, 552, 328, 28, L"%");
    AddSliderWithEdit(p, IDC_P2_BORDW,     314, 364, 50, L"边框粗细", 366, 360, 125, 0, 20, 3,
                      496, 361, 52, 22, 552, 364, 28, L"px");

    Ctl(p, L"Static", L"深色", 0, 0, 314, 402, 34, 18, -1);
    Ctl(p, L"Button", L"背景色", BS_PUSHBUTTON | WS_TABSTOP, 0, 352, 398, 54, 26, IDC_P2_BGCOLOR);
    Ctl(p, L"Button", L"高亮色", BS_PUSHBUTTON | WS_TABSTOP, 0, 410, 398, 54, 26, IDC_P2_HOVERCOLOR);
    Ctl(p, L"Button", L"边框色", BS_PUSHBUTTON | WS_TABSTOP, 0, 468, 398, 54, 26, IDC_P2_BORDCOLOR);
    Ctl(p, L"Button", L"文字色", BS_PUSHBUTTON | WS_TABSTOP, 0, 526, 398, 54, 26, IDC_P2_TEXTCOLOR);
    Ctl(p, L"Static", L"浅色", 0, 0, 314, 434, 34, 18, -1);
    Ctl(p, L"Button", L"背景色", BS_PUSHBUTTON | WS_TABSTOP, 0, 352, 430, 54, 26, IDC_P2_BGCOLOR_L);
    Ctl(p, L"Button", L"高亮色", BS_PUSHBUTTON | WS_TABSTOP, 0, 410, 430, 54, 26, IDC_P2_HOVERCOLOR_L);
    Ctl(p, L"Button", L"边框色", BS_PUSHBUTTON | WS_TABSTOP, 0, 468, 430, 54, 26, IDC_P2_BORDCOLOR_L);
    Ctl(p, L"Button", L"文字色", BS_PUSHBUTTON | WS_TABSTOP, 0, 526, 430, 54, 26, IDC_P2_TEXTCOLOR_L);
    Ctl(p, L"Button", L"高亮颜色跟随系统主题色", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 314, 460, 268, 20, IDC_P2_HOVERAUTO);

    // Col 3: 文字与中心
    Ctl(p, L"Button", L"文字与中心区", BS_GROUPBOX, 0, 600, 236, 290, 246, -1);
    Ctl(p, L"Button", L"显示扇区文字标签", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 616, 264, 150, 20, IDC_P2_LABELS);

    AddSliderWithEdit(p, IDC_P2_LABELSIZE, 616, 300, 36, L"字号", 654, 296, 130, 8, 28, 13,
                      790, 297, 52, 22, 846, 300, 30, L"px");

    Ctl(p, L"Button", L"🔤 标签字体…", BS_PUSHBUTTON | WS_TABSTOP, 0, 616, 336, 120, 28, IDC_P2_LABELFONT);

    Ctl(p, L"Static", L"中心显示", 0, 0, 616, 384, 60, 18, -1);
    HWND cm = Ctl(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 684, 380, 192, 200, IDC_P2_CENTER);
    ComboBox_AddString(cm, L"页码 (如 1/2)");
    ComboBox_AddString(cm, L"自定义文字");
    ComboBox_AddString(cm, L"不显示 (留空)");

    Ctl(p, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 616, 420, 260, 26, IDC_P2_CENTERTEXT);

    // 液态玻璃参数条（背景模式选“液态玻璃”时可用，改动即时反映在上方预览）
    Ctl(p, L"Button", L"液态玻璃折射 (背景模式选“液态玻璃”时生效)", BS_GROUPBOX, 0, 10, 484, 880, 94, -1);
    AddSliderWithEdit(p, IDC_P2_GLASSBLUR,   24, 506, 32, L"模糊",     58, 502, 86, 0, 16, (int)g_cfg.app.glassBlur,
                      148, 503, 44, 22, 196, 506, 20, L"px");
    AddSliderWithEdit(p, IDC_P2_GLASSHEIGHT, 220, 506, 60, L"折射深度", 282, 502, 86, 0, 48, (int)g_cfg.app.glassHeight,
                      372, 503, 44, 22, 420, 506, 20, L"px");
    AddSliderWithEdit(p, IDC_P2_GLASSAMT,    444, 506, 60, L"折射强度", 506, 502, 86, 0, 100, (int)g_cfg.app.glassAmount,
                      596, 503, 44, 22, 0, 0, 0, nullptr);
    Ctl(p, L"Button", L"色散",     BS_AUTOCHECKBOX | WS_TABSTOP, 0, 648, 506, 58, 20, IDC_P2_GLASSCHROMA);
    Ctl(p, L"Button", L"边缘高光", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 710, 506, 80, 20, IDC_P2_GLASSHL);
    Ctl(p, L"Button", L"色彩鲜活", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 794, 506, 80, 20, IDC_P2_GLASSVIB);

    // 性能条：实时折射背景的截取分辨率与频率（液态玻璃/亚克力）
    AddSliderWithEdit(p, IDC_P2_CUTRES,  24, 538, 44, L"清晰度", 70, 530, 86, 25, 100,
                      (int)(g_cfg.app.backdropScale > 0.0f ? g_cfg.app.backdropScale * 100.0f : 100.0f),
                      160, 535, 44, 22, 208, 538, 22, L"%");
    AddSliderWithEdit(p, IDC_P2_CUTFPS, 236, 538, 56, L"截取频率", 294, 530, 86, 10, 60, g_cfg.app.backdropFps,
                      384, 535, 44, 22, 432, 538, 26, L"Hz");
    Ctl(p, L"Static", L"降低清晰度或截取频率可缓解卡顿（实时背景更新率）", 0, 0, 466, 542, 410, 18, -1);
}

static void BuildPanel3(HWND p) {
    Ctl(p, L"Button", L"手势触发与系统设置", BS_GROUPBOX, 0, 10, 8, 880, 165, -1);

    Ctl(p, L"Button", L"全局启用 RightDial 轮盘功能（关闭后手势完全失效）", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24, 32, 420, 20, IDC_P3_ENABLE);

    Ctl(p, L"Static", L"触发操作", 0, 0, 24, 66, 60, 18, -1);
    HWND btn = Ctl(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 90, 62, 175, 200, IDC_P3_BUTTON);
    ComboBox_AddString(btn, L"鼠标右键拖动 (推荐)");
    ComboBox_AddString(btn, L"鼠标中键拖动");

    AddSliderWithEdit(p, IDC_P3_THRESHOLD, 290, 66, 70, L"唤出灵敏度",
                      366, 62, 115, 6, 40, (int)g_cfg.beh.thresholdPx,
                      490, 62, 42, 26, 536, 66, 25, L"px");

    AddSliderWithEdit(p, IDC_P3_WHEELSENS, 580, 66, 70, L"翻页灵敏度",
                      656, 62, 115, 20, 300, g_cfg.beh.wheelSensitivity,
                      780, 62, 46, 26, 830, 66, 25, L"%");

    Ctl(p, L"Button", L"在任务栏系统托盘显示图标（隐藏后再次双击运行程序即可打开本设置）", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24, 100, 560, 20, IDC_P3_TRAY);
    Ctl(p, L"Button", L"开机自启动", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24, 130, 120, 20, IDC_P3_AUTOSTART);
    Ctl(p, L"Static", L"提示：翻页灵敏度默认 100%（滚轮 1 格翻 1 页），调高更敏锐，调低防误触。", 0, 0, 160, 132, 580, 18, -1);

    Ctl(p, L"Button", L"程序排除名单（游戏与专业软件防手势冲突）", BS_GROUPBOX, 0, 10, 182, 880, 335, -1);
    Ctl(p, L"Button", L"自动排除所有无边框全屏程序（玩独占/全屏游戏时自动禁用手势）", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 24, 206, 480, 20, IDC_P3_EXCLFULL);

    Ctl(p, WC_LISTBOXW, L"", LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL, WS_EX_CLIENTEDGE, 24, 234, 640, 210, IDC_P3_EXCL);

    Ctl(p, WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 24, 456, 320, 26, IDC_P3_EXCLEDIT);
    Ctl(p, L"Button", L"+ 添加进程", BS_PUSHBUTTON | WS_TABSTOP, 0, 352, 455, 90, 28, IDC_P3_EXCLADD);
    Ctl(p, L"Button", L"🔍 从运行中选择…", BS_PUSHBUTTON | WS_TABSTOP, 0, 448, 455, 140, 28, IDC_P3_EXCLRUN);
    Ctl(p, L"Button", L"✕ 删除选中", BS_PUSHBUTTON | WS_TABSTOP, 0, 594, 455, 90, 28, IDC_P3_EXCLDEL);

    Ctl(p, L"Static", L"提示：在以上指定进程的前台窗口中长按右键不会唤出轮盘，避免干扰游戏移动或视线操作。", 0, 0, 24, 490, 650, 18, -1);
}

// ---------- tab / panels ----------

static void SwitchTab(int idx) {
    ShowWindow(s_panel1, idx == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(s_panel2, idx == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(s_panel3, idx == 2 ? SW_SHOW : SW_HIDE);
}

static void LayoutPanels() {
    RECT rc;
    GetClientRect(s_tab, &rc);
    SendMessageW(s_tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&rc);
    MapWindowPoints(s_tab, s_hwnd, (POINT*)&rc, 2);
    SetWindowPos(s_panel1, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
    SetWindowPos(s_panel2, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
    SetWindowPos(s_panel3, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
}

// ---------- message handling ----------

static void ApplyAppearanceChange(int id) {
    Appearance& a = g_cfg.app;
    switch (id) {
    case IDC_P2_SECTORS:    a.sectorCount = SliderVal(P2Of(id)); RefreshSlots(); break;
    case IDC_P2_ROTATION:   a.rotationDeg = (float)SliderVal(P2Of(id)); break;
    case IDC_P2_DIAMETER:   a.diameter = (float)SliderVal(P2Of(id)); break;
    case IDC_P2_ICONSIZE:   a.iconSize = (float)SliderVal(P2Of(id)); break;
    case IDC_P2_GAP:        a.sectorGap = (float)SliderVal(P2Of(id)); break;
    case IDC_P2_ANIMMS:     a.animMs = (float)SliderVal(P2Of(id)); break;
    case IDC_P2_BGOPACITY:  a.bgOpacity = SliderVal(P2Of(id)) / 100.0f; break;
    case IDC_P2_BORDW:      a.borderWidth = SliderVal(P2Of(id)) / 2.0f; break;
    case IDC_P2_LABELSIZE:  a.labelSize = (float)SliderVal(P2Of(id)); break;
    case IDC_P2_GLASSBLUR:   a.glassBlur = (float)SliderVal(P2Of(id)); break;
    case IDC_P2_GLASSHEIGHT: a.glassHeight = (float)SliderVal(P2Of(id)); break;
    case IDC_P2_GLASSAMT:    a.glassAmount = (float)SliderVal(P2Of(id)); break;
    case IDC_P2_CUTRES:      a.backdropScale = SliderVal(P2Of(id)) / 100.0f; break;
    case IDC_P2_CUTFPS:      a.backdropFps = SliderVal(P2Of(id)); break;
    }
    RefreshValueStatics();
    InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
    SaveSoon();
}

static void OnCommand(HWND h, int id, int code, HWND ctl) {
    if (s_noSync) return;

    switch (id) {
    // ---- page / slot list selection ----
    case IDC_P1_PAGES: {
        if (code != LBN_SELCHANGE) return;
        int sel = ListBox_GetCurSel(ctl);
        if (sel >= 0 && sel < (int)g_cfg.pages.size() && sel != s_curPage) {
            s_curPage = sel;
            s_curSlot = 0;
            RefreshSlots();
            RefreshDetail();
            InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        }
        return;
    }
    case IDC_P1_SLOTS: {
        if (code != LBN_SELCHANGE) return;
        int sel = ListBox_GetCurSel(ctl);
        Page* p = CurPage();
        if (p && sel >= 0 && sel < (int)p->slots.size() && sel != s_curSlot) {
            s_curSlot = sel;
            RefreshDetail();
            InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        }
        return;
    }

    // ---- page / slot list ops ----
    case IDC_P1_ADDPAGE: {
        std::wstring name = L"页面" + std::to_wstring(g_cfg.pages.size() + 1);
        if (!InputBox(h, L"添加页面", name)) return;
        while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
        while (!name.empty() && iswspace(name.back())) name.pop_back();
        if (name.empty()) name = L"页面" + std::to_wstring(g_cfg.pages.size() + 1);
        Page p; p.name = name;
        g_cfg.pages.push_back(p);
        s_curPage = (int)g_cfg.pages.size() - 1;
        s_curSlot = 0;
        SaveSoon(); RefreshPages(); RefreshSlots(); RefreshDetail(); RefreshAppearance();
        return;
    }
    case IDC_P1_RENPAGE: {
        Page* p = CurPage();
        if (!p) return;
        std::wstring name = p->name;
        if (!InputBox(h, L"重命名页面", name)) return;
        while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
        while (!name.empty() && iswspace(name.back())) name.pop_back();
        if (!name.empty()) {
            p->name = name;
            SaveSoon();
            RefreshPages();
        }
        return;
    }
    case IDC_P1_DELPAGE:
        if (g_cfg.pages.size() <= 1) { MessageBeep(MB_ICONWARNING); return; }
        if (s_curPage >= 0 && s_curPage < (int)g_cfg.pages.size())
            g_cfg.pages.erase(g_cfg.pages.begin() + s_curPage);
        s_curPage = std::max(0, s_curPage - 1);
        s_curSlot = 0;
        SaveSoon(); RefreshPages(); RefreshSlots(); RefreshDetail(); RefreshAppearance();
        return;
    case IDC_P1_ADDSLOT: {
        Page* p = CurPage();
        if (!p || (int)p->slots.size() >= 36) return;
        Slot s; s.name = L"新槽位";
        p->slots.push_back(s);
        s_curSlot = (int)p->slots.size() - 1;
        SaveSoon(); RefreshSlots(); RefreshDetail();
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        return;
    }
    case IDC_P1_DELSLOT: {
        Page* p = CurPage();
        if (!p || s_curSlot < 0 || s_curSlot >= (int)p->slots.size()) return;
        p->slots.erase(p->slots.begin() + s_curSlot);
        if (s_curSlot >= (int)p->slots.size()) s_curSlot = (int)p->slots.size() - 1;
        SaveSoon(); RefreshSlots(); RefreshDetail();
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        return;
    }
    case IDC_P1_UPSLOT:
    case IDC_P1_DOWNSLOT: {
        Page* p = CurPage();
        if (!p || s_curSlot < 0) return;
        int i = s_curSlot, j = (id == IDC_P1_UPSLOT) ? i - 1 : i + 1;
        if (j < 0 || j >= (int)p->slots.size()) return;
        std::swap(p->slots[i], p->slots[j]);
        s_curSlot = j;
        SaveSoon(); RefreshSlots(); RefreshDetail();
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        return;
    }

    // ---- slot detail ----
    case IDC_P1_TYPE: {
        if (code != CBN_SELCHANGE) return;
        Slot* s = CurSlot();
        if (!s) return;
        s->type = (SlotType)ComboBox_GetCurSel(ctl);
        RefreshDetail(); SaveSoon();
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        return;
    }
    case IDC_P1_NAME: {
        if (code != EN_CHANGE) return;
        Slot* s = CurSlot();
        if (!s) return;
        int n = GetWindowTextLengthW(ctl);
        std::wstring t(n + 1, L'\0');
        GetWindowTextW(ctl, &t[0], n + 1);
        t.resize(n);
        s->name = t;
        // live-update slot list text safely without triggering selection change
        if (s_curSlot >= 0) {
            std::wstring item = std::to_wstring(s_curSlot + 1) + L". " + (t.empty() ? L"（空）" : t);
            if (s_curSlot >= g_cfg.app.sectorCount) item += L"  (超出)";
            s_noSync = true;
            ListBox_DeleteString(P1Of(IDC_P1_SLOTS), s_curSlot);
            ListBox_InsertString(P1Of(IDC_P1_SLOTS), s_curSlot, item.c_str());
            ListBox_SetCurSel(P1Of(IDC_P1_SLOTS), s_curSlot);
            s_noSync = false;
        }
        SaveSoon();
        return;
    }
    case IDC_P1_KEYS: {
        Slot* s = CurSlot();
        if (!s) return;
        if (code == EN_CHANGE) {
            if (s_noSync) return;
            wchar_t text[128] = L"";
            GetWindowTextW(ctl, text, 128);
            std::wstring norm = NormalizeChord(text);
            if (!norm.empty()) {
                s->keys = norm;
                SyncTogglesFromSlot();
                InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
                SaveSoon();
            } else if (text[0] == L'\0') {
                s->keys.clear();
                SyncTogglesFromSlot();
                InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
                SaveSoon();
            }
            return;
        }
        if (code == EN_KILLFOCUS) {
            wchar_t text[128] = L"";
            GetWindowTextW(ctl, text, 128);
            std::wstring norm = NormalizeChord(text);
            if (!norm.empty()) {
                s->keys = norm;
            } else if (text[0] == L'\0') {
                s->keys.clear();
            }
            s_noSync = true;
            Edit_SetText(ctl, s->keys.c_str());
            SyncTogglesFromSlot();
            s_noSync = false;
            InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
            SaveSoon();
            return;
        }
        return;
    }
    case IDC_P1_KEYPRESET: {
        Slot* s = CurSlot();
        if (!s) return;
        struct ShortcutPreset {
            const wchar_t* name;
            const wchar_t* chord;
            const wchar_t* icon;
            const wchar_t* menuLabel;
        };

        static const ShortcutPreset presets[] = {
            // --- 0..8: 常设高频快捷操作 ---
            { L"最大化", L"Win+Up", L"builtin:maximize", L"🗖 最大化窗口 (Win+Up)" },
            { L"最小化", L"Win+Down", L"builtin:minimize", L"🗕 最小化窗口 (Win+Down)" },
            { L"显示桌面", L"Win+D", L"builtin:minimize", L"🖥 显示桌面 (Win+D)" },
            { L"区域截图", L"Win+Shift+S", L"builtin:screenshot", L"📸 区域截图 (Win+Shift+S)" },
            { L"粘贴", L"Ctrl+V", L"builtin:paste", L"📋 粘贴 (Ctrl+V)" },
            { L"复制", L"Ctrl+C", L"builtin:copy", L"📄 复制 (Ctrl+C)" },
            { L"剪切", L"Ctrl+X", L"builtin:cut", L"✂️ 剪切 (Ctrl+X)" },
            { L"撤销", L"Ctrl+Z", L"builtin:undo", L"↺ 撤销 (Ctrl+Z)" },
            { L"保存", L"Ctrl+S", L"builtin:save", L"💾 保存 (Ctrl+S)" },

            // --- 9..16: 窗口与多任务控制 ---
            { L"关闭窗口", L"Alt+F4", L"builtin:trash", L"✕ 关闭当前窗口 (Alt+F4)" },
            { L"关闭标签", L"Ctrl+W", L"builtin:trash", L"✕ 关闭当前标签页 (Ctrl+W)" },
            { L"左半屏贴靠", L"Win+Left", L"builtin:keys", L"🗔 贴靠左半屏 (Win+Left)" },
            { L"右半屏贴靠", L"Win+Right", L"builtin:keys", L"🗔 贴靠右半屏 (Win+Right)" },
            { L"任务视图", L"Win+Tab", L"builtin:keys", L"📑 任务视图 (Win+Tab)" },
            { L"切换窗口", L"Alt+Tab", L"builtin:keys", L"⮂ 切换任务窗口 (Alt+Tab)" },
            { L"新建虚拟桌面", L"Win+Ctrl+D", L"builtin:keys", L"➕ 新建虚拟桌面 (Win+Ctrl+D)" },
            { L"关闭虚拟桌面", L"Win+Ctrl+F4", L"builtin:keys", L"✕ 关闭虚拟桌面 (Win+Ctrl+F4)" },

            // --- 17..21: 编辑与剪贴板 ---
            { L"全选", L"Ctrl+A", L"builtin:keys", L"🔍 全选内容 (Ctrl+A)" },
            { L"查找", L"Ctrl+F", L"builtin:search", L"🔎 查找内容 (Ctrl+F)" },
            { L"重做", L"Ctrl+Y", L"builtin:undo", L"↷ 重做操作 (Ctrl+Y)" },
            { L"剪贴板历史", L"Win+V", L"builtin:paste", L"📋 剪贴板历史 (Win+V)" },
            { L"删除", L"Delete", L"builtin:trash", L"🗑️ 删除 (Delete)" },

            // --- 22..28: 系统与实用工具 ---
            { L"全屏截图", L"PrtSc", L"builtin:screenshot", L"📸 全屏截图 (PrtSc)" },
            { L"锁定电脑", L"Win+L", L"builtin:lock", L"🔒 锁定电脑 (Win+L)" },
            { L"Windows设置", L"Win+I", L"builtin:settings", L"⚙️ 打开设置 (Win+I)" },
            { L"资源管理器", L"Win+E", L"builtin:folder", L"📁 资源管理器 (Win+E)" },
            { L"Windows搜索", L"Win+S", L"builtin:search", L"🔍 快速搜索 (Win+S)" },
            { L"运行对话框", L"Win+R", L"builtin:terminal", L"💻 运行对话框 (Win+R)" },
            { L"任务管理器", L"Ctrl+Shift+Escape", L"builtin:settings", L"📊 任务管理器 (Ctrl+Shift+Esc)" },

            // --- 29..35: 网页与浏览器 ---
            { L"新标签页", L"Ctrl+T", L"builtin:browser", L"🌐 新建标签页 (Ctrl+T)" },
            { L"恢复关闭标签", L"Ctrl+Shift+T", L"builtin:browser", L"↺ 恢复关闭标签 (Ctrl+Shift+T)" },
            { L"刷新页面", L"F5", L"builtin:keys", L"🔄 刷新 (F5)" },
            { L"强制刷新", L"Ctrl+F5", L"builtin:keys", L"⚡ 强制刷新 (Ctrl+F5)" },
            { L"网页全屏", L"F11", L"builtin:fullscreen", L"🗖 全屏切换 (F11)" },
            { L"网页放大", L"Ctrl+=", L"builtin:search", L"➕ 放大页面 (Ctrl+=)" },
            { L"网页缩小", L"Ctrl+-", L"builtin:search", L"➖ 缩小页面 (Ctrl+-)" },
        };

        HMENU root = CreatePopupMenu();
        HMENU subWin = CreatePopupMenu();
        HMENU subEdit = CreatePopupMenu();
        HMENU subSys = CreatePopupMenu();
        HMENU subWeb = CreatePopupMenu();

        for (int i = 0; i < 9; i++) {
            AppendMenuW(root, MF_STRING, i + 1, presets[i].menuLabel);
        }
        AppendMenuW(root, MF_SEPARATOR, 0, nullptr);

        for (int i = 9; i < 17; i++)
            AppendMenuW(subWin, MF_STRING, i + 1, presets[i].menuLabel);
        AppendMenuW(root, MF_POPUP, (UINT_PTR)subWin, L"🪟 更多窗口与多任务…");

        for (int i = 17; i < 22; i++)
            AppendMenuW(subEdit, MF_STRING, i + 1, presets[i].menuLabel);
        AppendMenuW(root, MF_POPUP, (UINT_PTR)subEdit, L"📋 更多编辑剪贴板…");

        for (int i = 22; i < 29; i++)
            AppendMenuW(subSys, MF_STRING, i + 1, presets[i].menuLabel);
        AppendMenuW(root, MF_POPUP, (UINT_PTR)subSys, L"🛠 更多系统与工具…");

        for (int i = 29; i < 36; i++)
            AppendMenuW(subWeb, MF_STRING, i + 1, presets[i].menuLabel);
        AppendMenuW(root, MF_POPUP, (UINT_PTR)subWeb, L"🌐 更多网页与浏览…");

        RECT rcBtn; GetWindowRect(ctl, &rcBtn);
        int cmd = TrackPopupMenu(root, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                 rcBtn.left, rcBtn.bottom, 0, h, nullptr);
        DestroyMenu(root);

        if (cmd >= 1 && cmd <= 36) {
            const auto& p = presets[cmd - 1];
            s->keys = p.chord;
            if (s->name.empty() || s->name == L"新槽位" || s->name == L"（空）") {
                s->name = p.name;
                s_noSync = true;
                Edit_SetText(P1Of(IDC_P1_NAME), s->name.c_str());
                if (s_curSlot >= 0) {
                    std::wstring item = std::to_wstring(s_curSlot + 1) + L". " + s->name;
                    if (s_curSlot >= g_cfg.app.sectorCount) item += L"  (超出)";
                    ListBox_DeleteString(P1Of(IDC_P1_SLOTS), s_curSlot);
                    ListBox_InsertString(P1Of(IDC_P1_SLOTS), s_curSlot, item.c_str());
                    ListBox_SetCurSel(P1Of(IDC_P1_SLOTS), s_curSlot);
                }
                s_noSync = false;
            }
            if (s->iconPath.empty() || s->iconPath.rfind(L"builtin:", 0) == 0) {
                s->iconPath = p.icon;
                IconCache::I().Clear();
            }
            s_noSync = true;
            Edit_SetText(P1Of(IDC_P1_KEYS), s->keys.c_str());
            SyncTogglesFromSlot();
            s_noSync = false;
            InvalidateRect(P1Of(IDC_P1_ICONPREV), nullptr, TRUE);
            InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
            SaveSoon();
        }
        return;
    }
    case IDC_P1_TARGET: {
        if (code != EN_CHANGE) return;
        Slot* s = CurSlot();
        if (!s) return;
        int n = GetWindowTextLengthW(ctl);
        std::wstring t(n + 1, L'\0');
        GetWindowTextW(ctl, &t[0], n + 1);
        t.resize(n);
        if (s->type == SlotType::File) s->path = t; else s->url = t;
        SaveSoon();
        return;
    }
    case IDC_P1_BRFILE:
    case IDC_P1_BRFOLDER: {
        Slot* s = CurSlot();
        if (!s) return;
        std::wstring v = PickFileDialog(h, id == IDC_P1_BRFOLDER);
        if (v.empty()) return;
        s->path = v;
        s_noSync = true;
        Edit_SetText(P1Of(IDC_P1_TARGET), v.c_str());
        s_noSync = false;
        IconCache::I().Clear();
        SaveSoon();
        return;
    }
    case IDC_P1_ICONPRESET: {
        Slot* s = CurSlot();
        if (!s) return;
        HMENU menu = CreatePopupMenu();
        struct PresetItem { UINT id; const wchar_t* label; const wchar_t* path; };
        static const PresetItem items[] = {
            { 1, L"📋 粘贴 (Ctrl+V)",      L"builtin:paste" },
            { 2, L"📄 复制 (Ctrl+C)",      L"builtin:copy" },
            { 3, L"✂️ 剪切 (Ctrl+X)",      L"builtin:cut" },
            { 4, L"↺ 撤销 (Ctrl+Z)",      L"builtin:undo" },
            { 5, L"💾 保存 (Ctrl+S)",      L"builtin:save" },
            { 6, L"📸 截图 (PrtSc)",       L"builtin:screenshot" },
            { 7, L"💻 命令行 / 终端",      L"builtin:terminal" },
            { 8, L"🔍 搜索 (Search)",     L"builtin:search" },
            { 9, L"🌐 浏览器 (Web)",      L"builtin:browser" },
            { 10, L"📁 文件夹 (Folder)",  L"builtin:folder" },
            { 11, L"📑 文件 (File)",      L"builtin:file" },
            { 12, L"⚙️ 设置 (Settings)",  L"builtin:settings" },
            { 13, L"🗑️ 删除 (Delete)",    L"builtin:trash" },
            { 14, L"🗖 最大化 (Maximize)", L"builtin:maximize" },
            { 15, L"🗕 最小化 (Minimize)", L"builtin:minimize" },
            { 16, L"🗖 全屏 (Fullscreen)", L"builtin:fullscreen" },
            { 17, L"🔒 锁定 (Lock)",      L"builtin:lock" },
            { 18, L"🔊 音量 (Volume)",    L"builtin:volume" },
            { 19, L"🧮 计算器 (Calc)",    L"builtin:calc" },
            { 20, L"⌨️ 快捷键默认",       L"builtin:keys" },
        };
        for (const auto& item : items) AppendMenuW(menu, MF_STRING, item.id, item.label);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 21, L"📚 图标库… (Lucide 全集)");
        RECT rcBtn; GetWindowRect(ctl, &rcBtn);
        int cmd = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                 rcBtn.left, rcBtn.bottom, 0, h, nullptr);
        DestroyMenu(menu);
        if (cmd == 21) {   // open the searchable Lucide library picker
            SendMessageW(h, WM_COMMAND, MAKEWPARAM(IDC_P1_ICONLIB, BN_CLICKED), (LPARAM)nullptr);
            return;
        }
        if (cmd >= 1 && cmd <= (int)(sizeof(items)/sizeof(items[0]))) {
            s->iconPath = items[cmd - 1].path;
            IconCache::I().Clear();
            InvalidateRect(P1Of(IDC_P1_ICONPREV), nullptr, TRUE);
            InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
            SaveSoon();
        }
        return;
    }
    case IDC_P1_ICONIMG: {
        Slot* s = CurSlot();
        if (!s) return;
        std::wstring v = PickImageDialog(h);
        if (v.empty()) return;
        s->iconPath = v;
        IconCache::I().Clear();
        InvalidateRect(P1Of(IDC_P1_ICONPREV), nullptr, TRUE);
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    }
    case IDC_P1_ICONCLEAR: {
        Slot* s = CurSlot();
        if (!s) return;
        s->iconPath.clear();
        IconCache::I().Clear();
        InvalidateRect(P1Of(IDC_P1_ICONPREV), nullptr, TRUE);
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    }
    case IDC_P1_ICONLIB: {
        Slot* s = CurSlot();
        if (!s) return;
        std::wstring cur = s->iconPath;
        if (PickLucideIcon(h, cur)) {
            s->iconPath = cur;
            IconCache::I().Clear();
            InvalidateRect(P1Of(IDC_P1_ICONPREV), nullptr, TRUE);
            InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
            SaveSoon();
        }
        return;
    }

    {
        int sliderId = 0;
        if (IsValueEdit(id, sliderId)) {
            if (code == EN_CHANGE) {
                if (s_noSync) return;
                wchar_t text[64] = L"";
                GetWindowTextW(ctl, text, 64);
                wchar_t* end = nullptr;
                float val = wcstof(text, &end);
                if (end != text) {
                    SetSliderVal(sliderId, val);
                    SyncSliderFromVal(sliderId, GetSliderVal(sliderId));
                    if (sliderId < IDC_P3_ENABLE) InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
                    SaveSoon();
                }
                return;
            }
            if (code == EN_KILLFOCUS) {
                wchar_t text[64] = L"";
                GetWindowTextW(ctl, text, 64);
                wchar_t* end = nullptr;
                float val = wcstof(text, &end);
                if (end == text) val = GetSliderVal(sliderId);
                SetSliderVal(sliderId, val);
                float actual = GetSliderVal(sliderId);
                SyncSliderFromVal(sliderId, actual);
                wchar_t buf[32];
                FormatValForEdit(sliderId, actual, buf, 32);
                s_noSync = true;
                Edit_SetText(ctl, buf);
                s_noSync = false;
                if (sliderId < IDC_P3_ENABLE) InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
                SaveSoon();
                return;
            }
            return;
        }
    }

    case IDC_P2_BGMODE: {
        if (code != CBN_SELCHANGE) return;
        int sel = ComboBox_GetCurSel(ctl);
        g_cfg.app.bgMode = std::clamp(sel, 0, 2);
        UpdateGlassUIEnabled();
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    }
    case IDC_P2_COLORMODE: {
        if (code != CBN_SELCHANGE) return;
        g_cfg.app.colorMode = std::clamp(ComboBox_GetCurSel(ctl), 0, 3);
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    }
    case IDC_P2_GLASSCHROMA:
        g_cfg.app.glassChroma = Button_GetCheck(ctl) == BST_CHECKED;
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    case IDC_P2_GLASSHL:
        g_cfg.app.glassHighlight = Button_GetCheck(ctl) == BST_CHECKED;
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    case IDC_P2_GLASSVIB:
        g_cfg.app.glassVibrancy = Button_GetCheck(ctl) == BST_CHECKED;
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    case IDC_P2_HOVERAUTO:
        g_cfg.app.hoverAccent = Button_GetCheck(ctl) == BST_CHECKED;
        UpdateHoverAutoUI();
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        InvalidateWheelView();
        SaveSoon();
        return;
    case IDC_P2_CENTER: {
        if (code != CBN_SELCHANGE) return;
        g_cfg.app.centerMode = ComboBox_GetCurSel(ctl);
        EnableWindow(P2Of(IDC_P2_CENTERTEXT), g_cfg.app.centerMode == 1);
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    }
    case IDC_P2_CENTERTEXT: {
        if (code != EN_CHANGE) return;
        int n = GetWindowTextLengthW(ctl);
        std::wstring t(n + 1, L'\0');
        GetWindowTextW(ctl, &t[0], n + 1);
        t.resize(n);
        g_cfg.app.centerText = t;
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    }
    case IDC_P2_ANIMCHK: {
        g_cfg.app.animation = Button_GetCheck(ctl) == BST_CHECKED;
        EnableWindow(P2Of(IDC_P2_ANIMMS), g_cfg.app.animation);
        EnableWindow(P2Of(IDC_P2_ANIMMS + 100), g_cfg.app.animation);
        SaveSoon();
        return;
    }
    case IDC_P2_LABELS: {
        g_cfg.app.showLabels = Button_GetCheck(ctl) == BST_CHECKED;
        EnableWindow(P2Of(IDC_P2_LABELSIZE), g_cfg.app.showLabels);
        EnableWindow(P2Of(IDC_P2_LABELSIZE + 100), g_cfg.app.showLabels);
        InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
        SaveSoon();
        return;
    }
    case IDC_P2_LABELFONT: {
        std::wstring f = g_cfg.app.labelFont;
        float sz = g_cfg.app.labelSize;
        if (PickFont(h, f, sz)) {
            g_cfg.app.labelFont = f;
            g_cfg.app.labelSize = sz;
            SetSlider(P2Of(IDC_P2_LABELSIZE), (int)std::lround(sz));
            RefreshValueStatics();
            InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
            SaveSoon();
        }
        return;
    }
    case IDC_P2_BGCOLOR:
        if (PickColor(h, g_cfg.app.bgColor)) { InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE); SaveSoon(); }
        return;
    case IDC_P2_HOVERCOLOR:
        if (PickColor(h, g_cfg.app.hoverColor)) { InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE); SaveSoon(); }
        return;
    case IDC_P2_BORDCOLOR:
        if (PickColor(h, g_cfg.app.borderColor)) { InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE); SaveSoon(); }
        return;
    case IDC_P2_TEXTCOLOR:
        if (PickColor(h, g_cfg.app.textColor)) { InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE); SaveSoon(); }
        return;
    case IDC_P2_BGCOLOR_L:
        if (PickColor(h, g_cfg.app.bgColorLight)) { InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE); SaveSoon(); }
        return;
    case IDC_P2_HOVERCOLOR_L:
        if (PickColor(h, g_cfg.app.hoverColorLight)) { InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE); SaveSoon(); }
        return;
    case IDC_P2_BORDCOLOR_L:
        if (PickColor(h, g_cfg.app.borderColorLight)) { InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE); SaveSoon(); }
        return;
    case IDC_P2_TEXTCOLOR_L:
        if (PickColor(h, g_cfg.app.textColorLight)) { InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE); SaveSoon(); }
        return;

    // ---- behavior ----
    case IDC_P3_ENABLE:
        g_cfg.beh.enabled = Button_GetCheck(ctl) == BST_CHECKED;
        SaveSoon();
        return;
    case IDC_P3_BUTTON: {
        if (code != CBN_SELCHANGE) return;
        g_cfg.beh.triggerButton = ComboBox_GetCurSel(ctl) + 1;
        SaveSoon();
        return;
    }
    case IDC_P3_TRAY: {
        g_cfg.beh.showTray = Button_GetCheck(ctl) == BST_CHECKED;
        TrayUpdate(g_cfg.beh.showTray);
        SaveSoon();
        return;
    }
    case IDC_P3_AUTOSTART: {
        bool on = Button_GetCheck(ctl) == BST_CHECKED;
        SetAutostart(on);
        g_cfg.beh.autostart = on;
        SaveSoon();
        return;
    }
    case IDC_P3_EXCLADD: {
        wchar_t buf[256] = L"";
        GetDlgItemTextW(s_panel3, IDC_P3_EXCLEDIT, buf, 256);
        std::wstring v = buf;
        // trim
        while (!v.empty() && iswspace(v.front())) v.erase(v.begin());
        while (!v.empty() && iswspace(v.back())) v.pop_back();
        if (v.empty()) return;
        g_cfg.beh.exclusions.push_back(v);
        Edit_SetText(ctl, L"");
        RefreshBehavior();
        SaveSoon();
        return;
    }
    case IDC_P3_EXCLDEL: {
        int sel = ListBox_GetCurSel(P3Of(IDC_P3_EXCL));
        if (sel < 0 || sel >= (int)g_cfg.beh.exclusions.size()) return;
        g_cfg.beh.exclusions.erase(g_cfg.beh.exclusions.begin() + sel);
        RefreshBehavior();
        SaveSoon();
        return;
    }
    case IDC_P3_EXCLFULL:
        g_cfg.beh.excludeFullscreen = Button_GetCheck(ctl) == BST_CHECKED;
        SaveSoon();
        return;
    case IDC_P3_EXCLRUN: {
        // popup menu listing processes of visible top-level windows
        std::vector<std::wstring> names;
        EnumWindows([](HWND hwnd, LPARAM l) -> BOOL {
            if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) == 0) return TRUE;
            if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
            BOOL cloaked = FALSE;
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (!pid) return TRUE;
            HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!pr) return TRUE;
            wchar_t nm[MAX_PATH] = L"";
            DWORD szn = MAX_PATH;
            std::wstring base;
            if (QueryFullProcessImageNameW(pr, 0, nm, &szn)) base = PathFindFileNameW(nm);
            CloseHandle(pr);
            if (base.empty()) return TRUE;
            auto* v = (std::vector<std::wstring>*)l;
            if (std::find(v->begin(), v->end(), base) == v->end()) v->push_back(base);
            return TRUE;
        }, (LPARAM)&names);
        std::sort(names.begin(), names.end());
        if (names.empty()) { MessageBoxW(h, L"未找到可见窗口的进程。", L"RightDial", MB_OK | MB_ICONINFORMATION); return; }
        POINT p = GetCursorPosSafe();
        HMENU m = CreatePopupMenu();
        int id = 3000;
        for (size_t i = 0; i < names.size() && i < 60; i++)
            AppendMenuW(m, MF_STRING, id++, names[i].c_str());
        int cmd = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, h, nullptr);
        DestroyMenu(m);
        if (cmd >= 3000 && cmd < 3000 + 60 && (size_t)(cmd - 3000) < names.size()) {
            g_cfg.beh.exclusions.push_back(names[cmd - 3000]);
            RefreshBehavior();
            SaveSoon();
        }
        return;
    }

    // ---- hotkey modifier toggles ----
    case IDC_P1_MODWIN:
    case IDC_P1_MODCTRL:
    case IDC_P1_MODALT:
    case IDC_P1_MODSHIFT:
        ApplyTogglesToSlot();
        SaveSoon();
        return;

    case IDC_CLOSE:
        SaveConfig(g_cfg);
        DestroyWindow(h);
        EnsureMouseHook();
        return;
    }
}

static LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_TIMER:
        if (wp == 1) {
            KillTimer(h, 1);
            SaveConfig(g_cfg);
            IconCache::I().Clear();
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE)
            SwitchTab((int)SendMessageW(s_tab, TCM_GETCURSEL, 0, 0));
        break;
    }
    case WM_COMMAND:
        OnCommand(h, LOWORD(wp), HIWORD(wp), (HWND)lp);
        return 0;
    case WM_HSCROLL: {
        HWND ctl = (HWND)lp;
        int id = GetDlgCtrlID(ctl);
        if ((id >= IDC_P2_SECTORS && id <= IDC_P2_LABELSIZE) ||
            (id >= IDC_P2_GLASSBLUR && id <= IDC_P2_CUTFPS)) {
            ApplyAppearanceChange(id);
        } else if (id == IDC_P3_THRESHOLD) {
            g_cfg.beh.thresholdPx = (float)SliderVal(ctl);
            wchar_t buf[32];
            swprintf_s(buf, L"%d", (int)g_cfg.beh.thresholdPx);
            s_noSync = true;
            Edit_SetText(GetDlgItem(s_panel3, IDC_P3_THRESHOLD + 100), buf);
            s_noSync = false;
            SaveSoon();
        } else if (id == IDC_P3_WHEELSENS) {
            g_cfg.beh.wheelSensitivity = SliderVal(ctl);
            wchar_t buf[32];
            swprintf_s(buf, L"%d", g_cfg.beh.wheelSensitivity);
            s_noSync = true;
            Edit_SetText(GetDlgItem(s_panel3, IDC_P3_WHEELSENS + 100), buf);
            s_noSync = false;
            SaveSoon();
        }
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlID == IDC_P1_ICONPREV) {
            DrawIconPreview(di->hDC, di->rcItem);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(30, 41, 59));
        static HBRUSH s_bgBr = CreateSolidBrush(RGB(246, 248, 250));
        return (LRESULT)s_bgBr;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, RGB(30, 41, 59));
        static HBRUSH s_editBr = CreateSolidBrush(RGB(255, 255, 255));
        return (LRESULT)s_editBr;
    }
    case WM_CTLCOLORDLG: {
        static HBRUSH s_bgBr = CreateSolidBrush(RGB(246, 248, 250));
        return (LRESULT)s_bgBr;
    }
    case WM_CLOSE:
        SaveConfig(g_cfg);
        DestroyWindow(h);
        EnsureMouseHook();
        return 0;
    case WM_DESTROY:
        s_hwnd = nullptr;
        IconCache::I().Clear();   // free icon bitmaps; wheel reloads them on demand
        EnsureMouseHook();
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool ToggleChecked(int id) { return Button_GetCheck(P1Of(id)) == BST_CHECKED; }

static LRESULT CALLBACK KeysProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_GETDLGCODE: {
        LRESULT res = DLGC_WANTARROWS | DLGC_WANTCHARS;
        if (lp) {
            MSG* pmsg = (MSG*)lp;
            if (pmsg->message == WM_KEYDOWN || pmsg->message == WM_KEYUP || pmsg->message == WM_CHAR) {
                if (pmsg->wParam == VK_RETURN) return DLGC_WANTALLKEYS;
            }
        }
        return res;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        UINT vk = (UINT)wp;
        if (vk == VK_RETURN) {
            wchar_t text[128] = L"";
            GetWindowTextW(h, text, 128);
            std::wstring norm = NormalizeChord(text);
            if (Slot* s = CurSlot()) {
                if (!norm.empty()) {
                    s->keys = norm;
                } else if (text[0] == L'\0') {
                    s->keys.clear();
                }
                s_noSync = true;
                Edit_SetText(h, s->keys.c_str());
                SyncTogglesFromSlot();
                s_noSync = false;
                SendMessageW(h, EM_SETSEL, 0, -1);
                InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
                SaveSoon();
            }
            return 0; // Prevent dialog default button from triggering!
        }
        if (vk == VK_ESCAPE) {
            if (Slot* s = CurSlot()) {
                s_noSync = true;
                Edit_SetText(h, s->keys.c_str());
                SyncTogglesFromSlot();
                s_noSync = false;
                SendMessageW(h, EM_SETSEL, 0, -1);
            }
            return 0;
        }
        // Direct support for pressing physical Function keys (F1-F24) or PrtSc
        if ((vk >= VK_F1 && vk <= VK_F24) || vk == VK_SNAPSHOT) {
            bool win   = ToggleChecked(IDC_P1_MODWIN)   || (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000);
            bool ctrl  = ToggleChecked(IDC_P1_MODCTRL)  || (GetKeyState(VK_CONTROL) & 0x8000);
            bool alt   = ToggleChecked(IDC_P1_MODALT)   || (GetKeyState(VK_MENU) & 0x8000);
            bool shift = ToggleChecked(IDC_P1_MODSHIFT) || (GetKeyState(VK_SHIFT) & 0x8000);
            std::wstring chord;
            if (win)   chord += L"Win+";
            if (ctrl)  chord += L"Ctrl+";
            if (alt)   chord += L"Alt+";
            if (shift) chord += L"Shift+";
            chord += VkDisplayName(vk);
            if (Slot* s = CurSlot()) {
                std::wstring norm = NormalizeChord(chord);
                s->keys = norm.empty() ? chord : norm;
                s_noSync = true;
                Edit_SetText(h, s->keys.c_str());
                SyncTogglesFromSlot();
                s_noSync = false;
                SendMessageW(h, EM_SETSEL, 0, -1);
                InvalidateRect(P2Of(IDC_P2_PREVIEW), nullptr, TRUE);
                SaveSoon();
            }
            return 0;
        }
        break;
    }
    }
    return DefSubclassProc(h, m, wp, lp);
}

// ---------- window creation ----------

static void CreateMainWindow(HWND h) {
    s_dpi = GetDpiForWindow(h);

    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(10, (int)s_dpi, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, L"Microsoft YaHei UI", _TRUNCATE);
    s_font = CreateFontIndirectW(&lf);

    int W = S(950), H = S(675);
    RECT rw = { 0, 0, W, H };
    AdjustWindowRect(&rw, WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME), FALSE);
    RECT wa = WorkAreaForPoint(GetCursorPosSafe());
    int x = wa.left + ((wa.right - wa.left) - (rw.right - rw.left)) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - (rw.bottom - rw.top)) / 2;
    SetWindowPos(h, nullptr, x, y, rw.right - rw.left, rw.bottom - rw.top, SWP_NOZORDER);

    s_tab = Ctl(h, WC_TABCONTROL, L"", WS_TABSTOP | TCS_TABS, 0, 10, 10, 924, 605, IDC_TAB);
    TCITEMW ti = {};
    ti.mask = TCIF_TEXT;
    const wchar_t* tabs[3] = { L"  ⚙️ 槽位与动作  ", L"  🎨 轮盘外观定制  ", L"  🛡️ 行为与规则  " };
    for (int i = 0; i < 3; i++) {
        ti.pszText = (LPWSTR)tabs[i];
        TabCtrl_InsertItem(s_tab, i, &ti);
    }
    s_panel1 = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, h, nullptr, g_hInst, nullptr);
    s_panel2 = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, h, nullptr, g_hInst, nullptr);
    s_panel3 = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, h, nullptr, g_hInst, nullptr);
    BuildPanel1(s_panel1);
    BuildPanel2(s_panel2);
    BuildPanel3(s_panel3);
    AutofitButtons(s_panel1);
    AutofitButtons(s_panel2);
    AutofitButtons(s_panel3);
    SetWindowSubclass(s_panel1, PanelProc, 3, 0);
    SetWindowSubclass(s_panel2, PanelProc, 3, 0);
    SetWindowSubclass(s_panel3, PanelProc, 3, 0);
    SetWindowSubclass(P1Of(IDC_P1_KEYS), KeysProc, 2, 0);
    LayoutPanels();

    Ctl(h, L"Static", L"✨ 修改即时生效并自动保存；呼出轮盘即应用新配置。", 0, 0, 14, 624, 550, 20, -1);
    Ctl(h, L"Button", L"完成并关闭", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 814, 620, 120, 30, IDC_CLOSE);

    SwitchTab(0);
    RefreshAll();
    SendMessageW(s_tab, TCM_SETCURSEL, 0, 0);
}

static LRESULT CALLBACK SettingsWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_CREATE) {
        s_hwnd = h;
        CreateMainWindow(h);
        return 0;
    }
    return SettingsProc(h, m, wp, lp);
}

bool IsSettingsWindow(HWND h) { return h == s_hwnd; }

// force a window above everything (even fullscreen windows), then settle back
static void RaiseAndActivate(HWND h) {
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(h);
}

void OpenSettings() {
    if (s_hwnd && IsWindow(s_hwnd)) {
        ShowWindow(s_hwnd, SW_RESTORE);
        RaiseAndActivate(s_hwnd);
        return;
    }
    WNDCLASSEXW wc = { sizeof(wc) };
    if (!GetClassInfoExW(g_hInst, MAIN_CLASS, &wc)) {
        wc = { sizeof(wc) };
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(246, 248, 250));
        wc.lpszClassName = MAIN_CLASS;
        wc.hIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
        RegisterClassExW(&wc);
    }
    CreateWindowExW(0, MAIN_CLASS, L"RightDial 设置",
                    WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME),
                    CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, g_hInst, nullptr);
    if (s_hwnd) {
        ShowWindow(s_hwnd, SW_SHOWNORMAL);
        RaiseAndActivate(s_hwnd);
    }
}

void SettingsFlushAndClose() {
    if (s_hwnd && IsWindow(s_hwnd)) {
        SaveConfig(g_cfg);
        DestroyWindow(s_hwnd);
    }
}
