#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <urlmon.h>
#include <gdiplus.h>
#include <objidl.h>
#include <fstream>
#include <string>
#include <vector>

#include "Resource.h"

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")

using namespace std;
using namespace Gdiplus;

Image* gBackground = nullptr;
ULONG_PTR gGdiplusToken;

vector<wstring> gLogs;

RECT gLogRect = { 20, 85, 760, 540 };
RECT gDownloadButtonRect = { 20, 20, 190, 65 };
RECT gSelectButtonRect = { 210, 20, 380, 65 };

bool gDownloading = false;

int gHoverButton = 0; // 0 无，1 下载，2 选择
int gDownButton = 0;

wstring gJsonPath = L"modrinth.index.json";

#define WM_LOG_MESSAGE (WM_APP + 1)
#define WM_DOWNLOAD_DONE (WM_APP + 2)

struct ModFile
{
    string fileName;
    vector<string> urls;
};

Image* LoadPngFromResource(HINSTANCE hInstance, int resourceId)
{
    HRSRC hResource = FindResourceW(
        hInstance,
        MAKEINTRESOURCEW(resourceId),
        L"PNG"
    );

    if (!hResource)
    {
        return nullptr;
    }

    DWORD imageSize = SizeofResource(hInstance, hResource);

    if (imageSize == 0)
    {
        return nullptr;
    }

    HGLOBAL hLoadedResource = LoadResource(hInstance, hResource);

    if (!hLoadedResource)
    {
        return nullptr;
    }

    void* pResourceData = LockResource(hLoadedResource);

    if (!pResourceData)
    {
        return nullptr;
    }

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);

    if (!hBuffer)
    {
        return nullptr;
    }

    void* pBuffer = GlobalLock(hBuffer);

    if (!pBuffer)
    {
        GlobalFree(hBuffer);
        return nullptr;
    }

    memcpy(pBuffer, pResourceData, imageSize);

    GlobalUnlock(hBuffer);

    IStream* pStream = nullptr;

    HRESULT hr = CreateStreamOnHGlobal(
        hBuffer,
        TRUE,
        &pStream
    );

    if (FAILED(hr))
    {
        GlobalFree(hBuffer);
        return nullptr;
    }

    Image* image = Image::FromStream(pStream);

    pStream->Release();

    if (!image || image->GetLastStatus() != Ok)
    {
        delete image;
        return nullptr;
    }

    return image;
}

wstring Utf8ToWide(const string& str)
{
    if (str.empty())
    {
        return L"";
    }

    int sizeNeeded = MultiByteToWideChar(
        CP_UTF8,
        0,
        str.c_str(),
        -1,
        NULL,
        0
    );

    if (sizeNeeded <= 0)
    {
        return wstring(str.begin(), str.end());
    }

    wstring result(sizeNeeded - 1, L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        str.c_str(),
        -1,
        &result[0],
        sizeNeeded
    );

    return result;
}

void AppendLog(HWND hwnd, const wstring& text)
{
    gLogs.push_back(text);

    if (gLogs.size() > 300)
    {
        gLogs.erase(gLogs.begin());
    }

    InvalidateRect(hwnd, NULL, TRUE);
}

void Log(HWND hwnd, const wstring& text)
{
    wstring* msg = new wstring(text);

    PostMessageW(
        hwnd,
        WM_LOG_MESSAGE,
        0,
        (LPARAM)msg
    );
}

string ReadFileWString(const wstring& path)
{
    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE)
    {
        return "";
    }

    DWORD fileSize = GetFileSize(hFile, NULL);

    if (fileSize == INVALID_FILE_SIZE || fileSize == 0)
    {
        CloseHandle(hFile);
        return "";
    }

    string content;
    content.resize(fileSize);

    DWORD bytesRead = 0;

    BOOL ok = ReadFile(
        hFile,
        &content[0],
        fileSize,
        &bytesRead,
        NULL
    );

    CloseHandle(hFile);

    if (!ok || bytesRead == 0)
    {
        return "";
    }

    content.resize(bytesRead);

    return content;
}

bool SelectJsonFile(HWND hwnd, wstring& selectedPath)
{
    wchar_t fileName[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"JSON 文件 (*.json)\0*.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"请选择 JSON 文件";
    ofn.Flags =
        OFN_FILEMUSTEXIST |
        OFN_PATHMUSTEXIST |
        OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn))
    {
        selectedPath = fileName;
        return true;
    }

    return false;
}

string GetFileName(const string& path)
{
    size_t pos = path.find_last_of("/\\");

    if (pos == string::npos)
    {
        return path.empty() ? "mod.jar" : path;
    }

    string name = path.substr(pos + 1);

    size_t qpos = name.find('?');

    if (qpos != string::npos)
    {
        name = name.substr(0, qpos);
    }

    if (name.empty())
    {
        name = "mod.jar";
    }

    return name;
}

vector<ModFile> ExtractMods(const string& json)
{
    vector<ModFile> mods;

    size_t pos = 0;

    while (true)
    {
        size_t pathPos = json.find("\"path\"", pos);

        if (pathPos == string::npos)
        {
            break;
        }

        size_t pathColon = json.find(":", pathPos);
        size_t pathStart = json.find("\"", pathColon + 1);
        size_t pathEnd = json.find("\"", pathStart + 1);

        if (pathColon == string::npos ||
            pathStart == string::npos ||
            pathEnd == string::npos)
        {
            break;
        }

        string path = json.substr(
            pathStart + 1,
            pathEnd - pathStart - 1
        );

        string fileName = GetFileName(path);

        size_t downloadsPos = json.find("\"downloads\"", pathEnd);

        if (downloadsPos == string::npos)
        {
            break;
        }

        size_t arrayStart = json.find("[", downloadsPos);
        size_t arrayEnd = json.find("]", arrayStart);

        if (arrayStart == string::npos ||
            arrayEnd == string::npos)
        {
            break;
        }

        vector<string> urls;

        size_t urlPos = arrayStart;

        while (true)
        {
            urlPos = json.find("https://", urlPos);

            if (urlPos == string::npos || urlPos > arrayEnd)
            {
                break;
            }

            size_t urlEnd = json.find("\"", urlPos);

            if (urlEnd == string::npos || urlEnd > arrayEnd)
            {
                break;
            }

            string url = json.substr(urlPos, urlEnd - urlPos);

            urls.push_back(url);

            urlPos = urlEnd + 1;
        }

        if (!urls.empty())
        {
            ModFile mod;
            mod.fileName = fileName;
            mod.urls = urls;

            mods.push_back(mod);
        }

        pos = arrayEnd + 1;
    }

    return mods;
}

DWORD WINAPI DownloadThread(LPVOID lpParam)
{
    HWND hwnd = (HWND)lpParam;

    vector<wstring> failedFiles;

    Log(hwnd, L"正在读取 JSON 文件...");
    Log(hwnd, L"当前 JSON 文件：" + gJsonPath);

    string json = ReadFileWString(gJsonPath);

    if (json.empty())
    {
        Log(hwnd, L"无法读取 JSON 文件");

        failedFiles.push_back(L"JSON 文件读取失败");

        Log(hwnd, L"");
        Log(hwnd, L"下载失败文件：");

        for (size_t i = 0; i < failedFiles.size(); i++)
        {
            Log(hwnd, L"- " + failedFiles[i]);
        }

        PostMessageW(hwnd, WM_DOWNLOAD_DONE, 0, 0);
        return 0;
    }

    vector<ModFile> mods = ExtractMods(json);

    if (mods.empty())
    {
        Log(hwnd, L"未找到可下载的 Mod 文件");

        Log(hwnd, L"");
        Log(hwnd, L"下载失败文件：");
        Log(hwnd, L"无失败文件");

        PostMessageW(hwnd, WM_DOWNLOAD_DONE, 0, 0);
        return 0;
    }

    Log(hwnd, L"找到 Mod 文件数量：" + to_wstring(mods.size()));

    CreateDirectoryW(L"mods", NULL);

    for (size_t i = 0; i < mods.size(); i++)
    {
        wstring wFileName = Utf8ToWide(mods[i].fileName);

        wstring savePath = L"mods\\" + wFileName;

        Log(hwnd, L"正在下载：" + wFileName);

        bool success = false;

        for (size_t j = 0; j < mods[i].urls.size(); j++)
        {
            string url = mods[i].urls[j];
            wstring wUrl = Utf8ToWide(url);

            Log(
                hwnd,
                L"正在尝试链接 " +
                to_wstring(j + 1) +
                L"/" +
                to_wstring(mods[i].urls.size())
            );

            HRESULT hr = URLDownloadToFileW(
                NULL,
                wUrl.c_str(),
                savePath.c_str(),
                0,
                NULL
            );

            if (SUCCEEDED(hr))
            {
                Log(hwnd, L"下载成功");
                success = true;
                break;
            }
            else
            {
                Log(hwnd, L"当前链接下载失败，尝试下一个链接");
            }
        }

        if (!success)
        {
            Log(hwnd, L"下载失败：" + wFileName);
            failedFiles.push_back(wFileName);
        }
    }

    Log(hwnd, L"全部下载任务已完成");

    Log(hwnd, L"");
    Log(hwnd, L"下载失败文件：");

    if (failedFiles.empty())
    {
        Log(hwnd, L"无失败文件");
    }
    else
    {
        for (size_t i = 0; i < failedFiles.size(); i++)
        {
            Log(hwnd, L"- " + failedFiles[i]);
        }
    }

    PostMessageW(hwnd, WM_DOWNLOAD_DONE, 0, 0);

    return 0;
}

void UpdateLayout(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    int margin = 20;
    int buttonWidth = 170;
    int buttonHeight = 45;
    int gap = 20;

    gDownloadButtonRect.left = margin;
    gDownloadButtonRect.top = 20;
    gDownloadButtonRect.right = margin + buttonWidth;
    gDownloadButtonRect.bottom = 20 + buttonHeight;

    gSelectButtonRect.left = gDownloadButtonRect.right + gap;
    gSelectButtonRect.top = 20;
    gSelectButtonRect.right = gSelectButtonRect.left + buttonWidth;
    gSelectButtonRect.bottom = 20 + buttonHeight;

    gLogRect.left = margin;
    gLogRect.top = 85;
    gLogRect.right = rc.right - margin;
    gLogRect.bottom = rc.bottom - margin;
}

void DrawButton(
    Graphics& graphics,
    const RECT& rect,
    const wchar_t* text,
    bool hover,
    bool down,
    bool disabled)
{
    BYTE alpha = 150;

    if (disabled)
    {
        alpha = 90;
    }
    else if (down)
    {
        alpha = 210;
    }
    else if (hover)
    {
        alpha = 180;
    }

    Gdiplus::RectF buttonBox(
        (REAL)rect.left,
        (REAL)rect.top,
        (REAL)(rect.right - rect.left),
        (REAL)(rect.bottom - rect.top)
    );

    SolidBrush buttonBrush(
        Color(alpha, 255, 255, 255)
    );

    graphics.FillRectangle(
        &buttonBrush,
        buttonBox
    );

    Pen borderPen(
        Color(190, 255, 255, 255),
        2
    );

    graphics.DrawRectangle(
        &borderPen,
        buttonBox
    );

    FontFamily fontFamily(L"Microsoft YaHei");

    Font font(
        &fontFamily,
        17,
        FontStyleRegular,
        UnitPixel
    );

    SolidBrush textBrush(
        disabled ?
        Color(200, 80, 80, 80) :
        Color(255, 0, 0, 0)
    );

    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    graphics.DrawString(
        text,
        -1,
        &font,
        buttonBox,
        &format,
        &textBrush
    );
}

void DrawInterface(HDC memDC, HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    Graphics graphics(memDC);

    if (gBackground && gBackground->GetLastStatus() == Ok)
    {
        graphics.DrawImage(
            gBackground,
            0,
            0,
            width,
            height
        );
    }
    else
    {
        SolidBrush bgBrush(
            Color(255, 40, 40, 40)
        );

        Gdiplus::Rect bgRect(
            0,
            0,
            width,
            height
        );

        graphics.FillRectangle(
            &bgBrush,
            bgRect
        );
    }

    DrawButton(
        graphics,
        gDownloadButtonRect,
        gDownloading ? L"正在下载..." : L"下载 Mod",
        gHoverButton == 1,
        gDownButton == 1,
        gDownloading
    );

    DrawButton(
        graphics,
        gSelectButtonRect,
        L"选择 JSON",
        gHoverButton == 2,
        gDownButton == 2,
        gDownloading
    );

    SolidBrush logBrush(
        Color(150, 255, 255, 255)
    );

    Gdiplus::Rect logBox(
        gLogRect.left,
        gLogRect.top,
        gLogRect.right - gLogRect.left,
        gLogRect.bottom - gLogRect.top
    );

    graphics.FillRectangle(
        &logBrush,
        logBox
    );

    Pen borderPen(
        Color(180, 255, 255, 255),
        2
    );

    graphics.DrawRectangle(
        &borderPen,
        logBox
    );

    FontFamily fontFamily(
        L"Microsoft YaHei"
    );

    Font font(
        &fontFamily,
        16,
        FontStyleRegular,
        UnitPixel
    );

    SolidBrush textBrush(
        Color(255, 0, 0, 0)
    );

    int lineHeight = 24;

    int maxLines =
        (gLogRect.bottom - gLogRect.top - 20) / lineHeight;

    int start = 0;

    if ((int)gLogs.size() > maxLines)
    {
        start = (int)gLogs.size() - maxLines;
    }

    int y = gLogRect.top + 10;

    for (int i = start; i < (int)gLogs.size(); i++)
    {
        PointF point(
            (REAL)(gLogRect.left + 10),
            (REAL)y
        );

        graphics.DrawString(
            gLogs[i].c_str(),
            -1,
            &font,
            point,
            &textBrush
        );

        y += lineHeight;
    }
}

int HitTestButton(POINT pt)
{
    if (!gDownloading && PtInRect(&gDownloadButtonRect, pt))
    {
        return 1;
    }

    if (!gDownloading && PtInRect(&gSelectButtonRect, pt))
    {
        return 2;
    }

    return 0;
}

void ChooseJson(HWND hwnd)
{
    wstring selectedPath;

    if (SelectJsonFile(hwnd, selectedPath))
    {
        gJsonPath = selectedPath;
        AppendLog(hwnd, L"已选择 JSON 文件：" + gJsonPath);
    }
    else
    {
        AppendLog(hwnd, L"未选择新文件，当前仍使用：" + gJsonPath);
    }

    InvalidateRect(hwnd, NULL, TRUE);
}

void StartDownload(HWND hwnd)
{
    if (gDownloading)
    {
        return;
    }

    gDownloading = true;
    gDownButton = 0;

    AppendLog(hwnd, L"开始下载，请稍候...");

    HANDLE hThread = CreateThread(
        NULL,
        0,
        DownloadThread,
        hwnd,
        0,
        NULL
    );

    if (hThread)
    {
        CloseHandle(hThread);
    }
    else
    {
        AppendLog(hwnd, L"创建下载线程失败");
        gDownloading = false;
    }

    InvalidateRect(hwnd, NULL, TRUE);
}

LRESULT CALLBACK WndProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        HINSTANCE hInstance = (HINSTANCE)cs->hInstance;

        gBackground = LoadPngFromResource(
            hInstance,
            IDB_PNG1
        );

        UpdateLayout(hwnd);

        break;
    }

    case WM_SIZE:
    {
        UpdateLayout(hwnd);

        InvalidateRect(
            hwnd,
            NULL,
            TRUE
        );

        break;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        int hoverNow = HitTestButton(pt);

        if (hoverNow != gHoverButton)
        {
            gHoverButton = hoverNow;

            InvalidateRect(
                hwnd,
                NULL,
                TRUE
            );
        }

        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        break;
    }

    case WM_MOUSELEAVE:
    {
        gHoverButton = 0;
        gDownButton = 0;

        InvalidateRect(
            hwnd,
            NULL,
            TRUE
        );

        break;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        int hit = HitTestButton(pt);

        if (hit != 0)
        {
            gDownButton = hit;
            SetCapture(hwnd);

            InvalidateRect(
                hwnd,
                NULL,
                TRUE
            );
        }

        break;
    }

    case WM_LBUTTONUP:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        int hit = HitTestButton(pt);

        if (gDownButton != 0)
        {
            int clicked = gDownButton;

            gDownButton = 0;
            ReleaseCapture();

            InvalidateRect(
                hwnd,
                NULL,
                TRUE
            );

            if (clicked == hit)
            {
                if (clicked == 1)
                {
                    StartDownload(hwnd);
                }
                else if (clicked == 2)
                {
                    ChooseJson(hwnd);
                }
            }
        }

        break;
    }

    case WM_ERASEBKGND:
    {
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;

        HDC hdc = BeginPaint(
            hwnd,
            &ps
        );

        RECT rc;
        GetClientRect(hwnd, &rc);

        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        HDC memDC = CreateCompatibleDC(hdc);

        HBITMAP memBitmap = CreateCompatibleBitmap(
            hdc,
            width,
            height
        );

        HBITMAP oldBitmap = (HBITMAP)SelectObject(
            memDC,
            memBitmap
        );

        DrawInterface(
            memDC,
            hwnd
        );

        BitBlt(
            hdc,
            0,
            0,
            width,
            height,
            memDC,
            0,
            0,
            SRCCOPY
        );

        SelectObject(
            memDC,
            oldBitmap
        );

        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(
            hwnd,
            &ps
        );

        return 0;
    }

    case WM_LOG_MESSAGE:
    {
        wstring* text = (wstring*)lParam;

        if (text)
        {
            AppendLog(
                hwnd,
                *text
            );

            delete text;
        }

        break;
    }

    case WM_DOWNLOAD_DONE:
    {
        gDownloading = false;
        gDownButton = 0;

        AppendLog(
            hwnd,
            L"下载线程已结束"
        );

        InvalidateRect(
            hwnd,
            NULL,
            TRUE
        );

        break;
    }

    case WM_DESTROY:
    {
        if (gBackground)
        {
            delete gBackground;
            gBackground = nullptr;
        }

        PostQuitMessage(0);

        break;
    }

    default:
    {
        return DefWindowProcW(
            hwnd,
            msg,
            wParam,
            lParam
        );
    }
    }

    return 0;
}

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    GdiplusStartupInput gdiplusStartupInput;

    GdiplusStartup(
        &gGdiplusToken,
        &gdiplusStartupInput,
        NULL
    );

    WNDCLASSW wc = {};

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ModDownloader";
    wc.hbrBackground = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(
        L"ModDownloader",
        L"Minecraft Mod 下载器",
        WS_OVERLAPPEDWINDOW,
        100,
        100,
        900,
        600,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    ShowWindow(
        hwnd,
        nCmdShow
    );

    UpdateWindow(hwnd);

    MSG msg;

    while (GetMessageW(
        &msg,
        NULL,
        0,
        0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(gGdiplusToken);

    return 0;
}