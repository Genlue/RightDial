// probe: which API returns the LIVE accent under Win11 automatic accent mode?
#include "stdafx.h"
#include <windows.ui.viewmanagement.h>
#include <activation.h>
#include <winstring.h>
#include <cstdio>

using ABI::Windows::UI::ViewManagement::IUISettings3;

static void TryWinRT(bool mta, const char* tag) {
    using RoGetActivationFactory_t = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
    using WindowsCreateString_t = HRESULT(WINAPI*)(PCWSTR, UINT32, HSTRING*);
    using WindowsDeleteString_t = HRESULT(WINAPI*)(HSTRING);
    HMODULE combase = LoadLibraryW(L"combase.dll");
    if (!combase) { printf("  %s: no combase\n", tag); return; }
    auto pRoGet = (RoGetActivationFactory_t)GetProcAddress(combase, "RoGetActivationFactory");
    auto pCreate = (WindowsCreateString_t)GetProcAddress(combase, "WindowsCreateString");
    auto pDelete = (WindowsDeleteString_t)GetProcAddress(combase, "WindowsDeleteString");
    if (!pRoGet || !pCreate || !pDelete) { printf("  %s: no exports\n", tag); return; }

    Microsoft::WRL::ComPtr<IActivationFactory> factory;
    HSTRING hcls = nullptr;
    HRESULT hrC = pCreate(RuntimeClass_Windows_UI_ViewManagement_UISettings,
                          (UINT32)wcslen(RuntimeClass_Windows_UI_ViewManagement_UISettings), &hcls);
    HRESULT hrF = E_FAIL, hrA = E_FAIL, hrQ = E_FAIL;
    if (SUCCEEDED(hrC)) {
        hrF = pRoGet(hcls, __uuidof(IActivationFactory), (void**)factory.ReleaseAndGetAddressOf());
        pDelete(hcls);
    }
    printf("  %s: create=0x%08X factory=0x%08X\n", tag, (unsigned)hrC, (unsigned)hrF);
    if (SUCCEEDED(hrF) && factory) {
        Microsoft::WRL::ComPtr<IInspectable> obj;
        hrA = factory->ActivateInstance(&obj);
        Microsoft::WRL::ComPtr<IUISettings3> settings;
        if (SUCCEEDED(hrA) && obj) hrQ = obj.As(&settings);
        printf("  %s: activate=0x%08X qi=0x%08X\n", tag, (unsigned)hrA, (unsigned)hrQ);
        if (SUCCEEDED(hrQ) && settings) {
            ABI::Windows::UI::Color c{};
            HRESULT hrG = settings->GetColorValue(ABI::Windows::UI::ViewManagement::UIColorType_Accent, &c);
            printf("  %s: GetColorValue=0x%08X -> #%02x%02x%02x\n", tag, (unsigned)hrG, c.R, c.G, c.B);
        }
    }
}

struct MtaArg { HANDLE done; };
static DWORD WINAPI MtaThread(LPVOID) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) { TryWinRT(true, "MTA "); CoUninitialize(); }
    return 0;
}

int main() {
    printf("registry AccentColor: ");
    DWORD v = 0, type = 0, cb = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor",
                     RRF_RT_REG_DWORD, &type, &v, &cb) == ERROR_SUCCESS)
        printf("0x%08X -> #%02x%02x%02x\n", v, GetRValue(v & 0xFFFFFF), GetGValue(v & 0xFFFFFF), GetBValue(v & 0xFFFFFF));
    else
        printf("not present\n");

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    TryWinRT(false, "STA ");
    HANDLE t = CreateThread(nullptr, 0, MtaThread, nullptr, 0, nullptr);
    WaitForSingleObject(t, 10000);
    CloseHandle(t);
    CoUninitialize();
    return 0;
}
