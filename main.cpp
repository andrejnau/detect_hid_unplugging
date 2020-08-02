#define UNICODE
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>
#include <dbt.h>
#include <Ntddmou.h>
#include <Ntddkbd.h>
#include <setupapi.h>
#include <map>
#include <string>
#include <iostream>
#include <combaseapi.h>
#include <algorithm>

GUID device_guid;
std::string cmd_exe = "\"C:/Program Files (x86)/Dell/Dell Display Manager/ddm.exe\"";
std::string cmd_args = "SetActiveInput HDMI2";

std::wstring str_tolower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](auto c) { return std::tolower(c); });
    return s;
}

class Handler
{
public:
    Handler()
    {
        DWORD devindex, needed, retbytes;
        PSP_INTERFACE_DEVICE_DETAIL_DATA detail;
        SP_INTERFACE_DEVICE_DATA ifdata = { sizeof(SP_INTERFACE_DEVICE_DATA) };
        SP_DEVINFO_DATA devdata = { sizeof(SP_DEVINFO_DATA) };

        HDEVINFO info = SetupDiGetClassDevs(&device_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (info == INVALID_HANDLE_VALUE)
        {
            std::wcout << "SetupDiGetClassDevs failed" << std::endl;
            return;
        }

        DWORD dwPropertyTypes[17] = { SPDRP_CHARACTERISTICS, SPDRP_CLASS, SPDRP_CLASSGUID, SPDRP_DEVICEDESC, SPDRP_DRIVER,
                                 SPDRP_ENUMERATOR_NAME,  SPDRP_FRIENDLYNAME, SPDRP_HARDWAREID,
                                 SPDRP_INSTALL_STATE, SPDRP_LOCATION_INFORMATION, SPDRP_LOCATION_PATHS, SPDRP_LOWERFILTERS,
                                 SPDRP_MFG, SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, SPDRP_SECURITY_SDS, SPDRP_UI_NUMBER_DESC_FORMAT, SPDRP_UPPERFILTERS };


        for (devindex = 0; SetupDiEnumDeviceInterfaces(info, NULL, &device_guid, devindex, &ifdata); ++devindex)
        {
            SetupDiGetDeviceInterfaceDetail(info, &ifdata, NULL, 0, &needed, NULL);
            detail = (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(needed);
            detail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
            SetupDiGetDeviceInterfaceDetail(info, &ifdata, detail, needed, NULL, &devdata);
            std::wcout << detail->DevicePath << std::endl;
            ++m_ref_count[str_tolower(detail->DevicePath)];

            DWORD dwX = ERROR_SUCCESS, dwProperty = ERROR_SUCCESS, dwSize = ERROR_SUCCESS;
            WCHAR szDeviceInstance[4096], szPropertyData[4096];

            int dwNext = 0;
            for (; dwNext < 17; dwNext++)
            {
                if (SetupDiGetDeviceRegistryPropertyW(info, &devdata, dwPropertyTypes[dwNext], &dwProperty, (PBYTE)szPropertyData, sizeof(szPropertyData), &dwSize))
                    _putws(szPropertyData);
            }
        }
        SetupDiDestroyDeviceInfoList(info);
    }

    void OnDeviceChange(WPARAM wParam, LPARAM lParam)
    {
        if (DBT_DEVICEARRIVAL != wParam && DBT_DEVICEREMOVECOMPLETE != wParam)
            return;

        PDEV_BROADCAST_HDR pHdr = reinterpret_cast<PDEV_BROADCAST_HDR>(lParam);
        if (pHdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
            return;
        PDEV_BROADCAST_DEVICEINTERFACE pDevInf = reinterpret_cast<PDEV_BROADCAST_DEVICEINTERFACE>(pHdr);
        std::wstring name = reinterpret_cast<const wchar_t*>(pDevInf->dbcc_name);
        name = str_tolower(name);
        if (DBT_DEVICEARRIVAL == wParam)
        {
            std::wcout << L"attach " << name << std::endl;
            ++m_ref_count[name];
        }
        else
        {
            std::wcout << L"dettach " << name << std::endl;
            --m_ref_count[name];
            if (m_ref_count[name] == 0)
            {
                if (m_handled_device.empty())
                    m_handled_device = name;
                if (name == m_handled_device)
                    SwitchMonitor();
            }
        }
    }

    void SwitchMonitor()
    {
        OutputDebugStringA("SwitchMonitor");
        std::string command = "/c " + cmd_exe + " " + "Rescan";
        ShellExecuteA(0, "open", "cmd.exe", command.c_str(), 0, SW_HIDE);
        Sleep(1000);
        command = "/c " + cmd_exe + " " + cmd_args;
        ShellExecuteA(0, "open", "cmd.exe", command.c_str(), 0, SW_HIDE);
    }

private:
    std::map<std::wstring, int> m_ref_count;
    std::wstring m_handled_device;
};

INT_PTR WINAPI WinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static HDEVNOTIFY hDeviceNotify;
    static Handler handler;
    LRESULT ret = 1;
    switch (message)
    {
    case WM_CREATE:
    {
        DEV_BROADCAST_DEVICEINTERFACE NotificationFilter = {};
        NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
        NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        NotificationFilter.dbcc_classguid = device_guid;
        hDeviceNotify = RegisterDeviceNotification(
            hWnd,
            &NotificationFilter,
            DEVICE_NOTIFY_WINDOW_HANDLE
        );
        break;
    }
    case WM_DEVICECHANGE:
    {
        handler.OnDeviceChange(wParam, lParam);
    }
    break;
    case WM_CLOSE:
        UnregisterDeviceNotification(hDeviceNotify);
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        ret = DefWindowProc(hWnd, message, wParam, lParam);
        break;
    }
    return ret;
}

int WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR     lpCmdLine,
    int       nShowCmd)
{
    ::ShowWindow(::GetConsoleWindow(), SW_HIDE);
    device_guid = GUID_DEVINTERFACE_MOUSE;
    auto class_name = L"detect_hid_unplugging";
    WNDCLASSEXW wnd_class = {};
    wnd_class.cbSize = sizeof(WNDCLASSEXW);
    wnd_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wnd_class.hInstance = hInstance;
    wnd_class.lpfnWndProc = reinterpret_cast<WNDPROC>(WinProcCallback);
    wnd_class.hIcon = LoadIcon(0, IDI_APPLICATION);
    wnd_class.hbrBackground = CreateSolidBrush(RGB(192, 192, 192));
    wnd_class.hCursor = LoadCursor(0, IDC_ARROW);
    wnd_class.lpszClassName = class_name;
    wnd_class.hIconSm = wnd_class.hIcon;
    RegisterClassExW(&wnd_class);

    HWND hWnd = CreateWindowExW(
        WS_EX_CLIENTEDGE | WS_EX_APPWINDOW,
        class_name,
        L"detect_hid_unplugging",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0,
        640, 480,
        nullptr, nullptr,
        wnd_class.hInstance,
        nullptr);

    ShowWindow(hWnd, SW_HIDE);
    UpdateWindow(hWnd);

    while (true)
    {
        MSG msg = {};
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            continue;
        if (msg.message == WM_QUIT)
            return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
