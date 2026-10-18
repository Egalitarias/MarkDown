#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl.h>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <winhttp.h>

#include "WebView2.h"
#include "markdown.h"
#include "resource.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

const wchar_t* kClassName = L"MarkdownViewerWindow";
const wchar_t* kAppName = L"Markdown Viewer";
const wchar_t* kHost = L"md.local";

struct Settings {
    bool dark = false, outline = true, wide = false, onTop = false;
    int zoom = 100;  // percent
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1100, h = 800;
    bool maximized = false;
    std::wstring lastFile;
} g_set;

HWND g_hwnd = nullptr;
HACCEL g_accel = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_view;
bool g_ready = false;  // page script has signalled it can receive messages

std::wstring g_path;       // current local document ("" = welcome screen or URL)
std::wstring g_url;        // current remote document
std::wstring g_urlInput;
FILETIME g_lastWrite = {};
std::wstring g_exportPath;

// ---------- helpers ----------

std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart < (256ll << 20);
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        ok = sz.QuadPart == 0 || (ReadFile(h, &out[0], (DWORD)sz.QuadPart, &got, nullptr) && got == sz.QuadPart);
    }
    CloseHandle(h);
    return ok;
}

bool WriteFileBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size();
    CloseHandle(h);
    return ok;
}

std::string LoadResourceText(int id) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return "";
    HGLOBAL g = LoadResource(nullptr, r);
    return std::string((const char*)LockResource(g), SizeofResource(nullptr, r));
}

std::wstring DirOf(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"" : p.substr(0, s);
}

std::wstring NameOf(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? p : p.substr(s + 1);
}

bool HasExt(const std::wstring& p, std::initializer_list<const wchar_t*> exts) {
    const wchar_t* e = PathFindExtensionW(p.c_str());
    for (auto x : exts) if (_wcsicmp(e, x) == 0) return true;
    return false;
}

bool IsMarkdownFile(const std::wstring& p) {
    return HasExt(p, {L".md", L".markdown", L".mdown", L".mkd", L".txt"});
}

std::wstring SettingsPath() {
    wchar_t buf[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) return L"";
    std::wstring dir = std::wstring(buf) + L"\\MarkdownViewer";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

std::wstring DataDir() {
    wchar_t buf[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf))) return L"";
    std::wstring dir = std::wstring(buf) + L"\\MarkdownViewer";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\WebView2";
}

bool SystemPrefersDark() {
    DWORD v = 1, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v == 0;
}

void LoadSettings() {
    std::wstring ini = SettingsPath();
    auto geti = [&](const wchar_t* k, int d) { return (int)GetPrivateProfileIntW(L"App", k, d, ini.c_str()); };
    g_set.dark = geti(L"dark", SystemPrefersDark() ? 1 : 0) != 0;
    g_set.outline = geti(L"outline", 1) != 0;
    g_set.wide = geti(L"wide", 0) != 0;
    g_set.onTop = geti(L"ontop", 0) != 0;
    g_set.zoom = std::max(25, std::min(500, geti(L"zoom", 100)));
    g_set.x = geti(L"x", CW_USEDEFAULT);
    g_set.y = geti(L"y", CW_USEDEFAULT);
    g_set.w = geti(L"w", 1100);
    g_set.h = geti(L"h", 800);
    g_set.maximized = geti(L"max", 0) != 0;
    wchar_t buf[MAX_PATH * 2] = L"";
    GetPrivateProfileStringW(L"App", L"last", L"", buf, MAX_PATH * 2, ini.c_str());
    g_set.lastFile = buf;
}

void SaveSettings() {
    std::wstring ini = SettingsPath();
    auto put = [&](const wchar_t* k, int v) { WritePrivateProfileStringW(L"App", k, std::to_wstring(v).c_str(), ini.c_str()); };
    put(L"dark", g_set.dark); put(L"outline", g_set.outline); put(L"wide", g_set.wide);
    put(L"ontop", g_set.onTop); put(L"zoom", g_set.zoom);
    WINDOWPLACEMENT wp = {sizeof(wp)};
    if (g_hwnd && GetWindowPlacement(g_hwnd, &wp)) {
        RECT r = wp.rcNormalPosition;
        put(L"x", r.left); put(L"y", r.top); put(L"w", r.right - r.left); put(L"h", r.bottom - r.top);
        put(L"max", wp.showCmd == SW_SHOWMAXIMIZED);
    }
    WritePrivateProfileStringW(L"App", L"last", (g_url.empty() ? g_path : g_url).c_str(), ini.c_str());
}

// ---------- talking to the page ----------

void Send(const std::wstring& msg) {
    if (g_view && g_ready) g_view->PostWebMessageAsString(msg.c_str());
}

void SyncMenu() {
    HMENU m = GetMenu(g_hwnd);
    CheckMenuItem(m, IDM_THEME, MF_BYCOMMAND | (g_set.dark ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_OUTLINE, MF_BYCOMMAND | (g_set.outline ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_WIDE, MF_BYCOMMAND | (g_set.wide ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_ONTOP, MF_BYCOMMAND | (g_set.onTop ? MF_CHECKED : MF_UNCHECKED));
}

void ApplyZoom() {
    if (g_controller) g_controller->put_ZoomFactor(g_set.zoom / 100.0);
}

void PushConfig() {
    Send(std::wstring(L"T:") + (g_set.dark ? L"dark" : L"light"));
    Send(std::wstring(L"O:") + (g_set.outline ? L"1" : L"0"));
    Send(std::wstring(L"W:") + (g_set.wide ? L"1" : L"0"));
    if (g_controller) {
        ComPtr<ICoreWebView2Controller2> c2;
        if (SUCCEEDED(g_controller.As(&c2))) {
            COREWEBVIEW2_COLOR col = g_set.dark ? COREWEBVIEW2_COLOR{255, 13, 17, 23} : COREWEBVIEW2_COLOR{255, 255, 255, 255};
            c2->put_DefaultBackgroundColor(col);
        }
    }
    ApplyZoom();
}

std::wstring DocName() {
    if (!g_url.empty()) {
        std::wstring u = g_url.substr(0, g_url.find_first_of(L"?#"));
        std::wstring n = NameOf(u);
        return n.empty() ? g_url : n;
    }
    return g_path.empty() ? std::wstring(L"Welcome") : NameOf(g_path);
}

void SetTitle() {
    std::wstring t = DocName() + L" - " + kAppName;
    SetWindowTextW(g_hwnd, t.c_str());
}

void ShowDocument(const std::string& markdown, bool resetScroll) {
    std::string base = "https://md.local/";
    if (!g_url.empty()) {
        std::wstring u = g_url.substr(0, g_url.find_first_of(L"?#"));
        base = Narrow(u.substr(0, u.find_last_of(L'/') + 1));
    }
    std::string html = RenderMarkdown(markdown, base);
    if (g_view) {
        ComPtr<ICoreWebView2_3> v3;
        if (SUCCEEDED(g_view.As(&v3))) {
            std::wstring dir = g_path.empty() ? L"" : DirOf(g_path);
            if (!dir.empty()) v3->SetVirtualHostNameToFolderMapping(kHost, dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }
    }
    Send(L"N:" + DocName());
    if (resetScroll) Send(L"R:");
    Send(L"D:" + Widen(html));
    SetTitle();
}

std::string DecodeText(std::string bytes) {
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE) {
        return Narrow(std::wstring((const wchar_t*)(bytes.data() + 2), (bytes.size() - 2) / 2));
    }
    return bytes;
}

bool LoadFile(const std::wstring& path, bool resetScroll) {
    std::string bytes;
    if (!ReadFileBytes(path, bytes)) return false;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) g_lastWrite = fad.ftLastWriteTime;
    g_path = path;
    g_url.clear();
    ShowDocument(DecodeText(std::move(bytes)), resetScroll);
    return true;
}

void ShowWelcome() {
    g_path.clear();
    g_url.clear();
    g_lastWrite = {};
    ShowDocument(LoadResourceText(IDR_WELCOME), true);
}

std::wstring FullPath(const std::wstring& p) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 2, buf, nullptr);
    return n ? std::wstring(buf) : p;
}

void OpenPath(const std::wstring& path) {
    std::wstring full = FullPath(path);
    if (!LoadFile(full, true)) {
        MessageBoxW(g_hwnd, (L"Could not open:\n" + full).c_str(), kAppName, MB_ICONWARNING);
    }
}

void DoOpen() {
    wchar_t file[MAX_PATH * 2] = L"";
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"Markdown (*.md;*.markdown;*.mdown;*.txt)\0*.md;*.markdown;*.mdown;*.mkd;*.txt\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    std::wstring dir = g_path.empty() ? L"" : DirOf(g_path);
    if (!dir.empty()) ofn.lpstrInitialDir = dir.c_str();
    if (GetOpenFileNameW(&ofn)) OpenPath(file);
}

void DoExport() {
    if (!g_ready) return;
    std::wstring base = DocName();
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base.resize(dot);
    wchar_t file[MAX_PATH * 2];
    wcsncpy_s(file, (base + L".html").c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"HTML (*.html)\0*.html\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.lpstrDefExt = L"html";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    g_exportPath = file;
    Send(L"X:");
}

struct UrlResult {
    bool ok;
    std::wstring url;
    std::string body;
    std::wstring error;
};

std::wstring NormalizeUrl(std::wstring u) {
    while (!u.empty() && iswspace(u.front())) u.erase(0, 1);
    while (!u.empty() && iswspace(u.back())) u.pop_back();
    if (u.empty()) return u;
    if (u.find(L"://") == std::wstring::npos) u = L"https://" + u;
    // github.com/<user>/<repo>/blob/<branch>/<path>  ->  raw.githubusercontent.com/<user>/<repo>/<branch>/<path>
    const std::wstring gh = L"https://github.com/";
    size_t b = u.find(L"/blob/");
    if (_wcsnicmp(u.c_str(), gh.c_str(), gh.size()) == 0 && b != std::wstring::npos)
        u = L"https://raw.githubusercontent.com/" + u.substr(gh.size(), b - gh.size()) + L"/" + u.substr(b + 6);
    return u;
}

UrlResult* FetchUrl(const std::wstring& url) {
    auto* r = new UrlResult{false, url, "", L""};
    URL_COMPONENTS uc = {sizeof(uc)};
    wchar_t host[256] = L"", path[4096] = L"";
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 4096;
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { r->error = L"Invalid URL."; return r; }
    std::wstring target = path;
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength) target.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    HINTERNET ses = WinHttpOpen(L"MarkdownViewer/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    HINTERNET con = ses ? WinHttpConnect(ses, host, uc.nPort, 0) : nullptr;
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    if (req && WinHttpSendRequest(req, L"Accept: text/markdown, text/plain, */*\r\n", (DWORD)-1, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
        if (status == 200) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail) {
                size_t old = r->body.size();
                if (old + avail > (64u << 20)) { r->error = L"The file is too large."; break; }
                r->body.resize(old + avail);
                DWORD got = 0;
                if (!WinHttpReadData(req, &r->body[old], avail, &got)) break;
                r->body.resize(old + got);
            }
            r->ok = r->error.empty();
        } else {
            r->error = L"The server returned HTTP " + std::to_wstring(status) + L".";
        }
    } else {
        r->error = L"Could not connect (error " + std::to_wstring(GetLastError()) + L").";
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return r;
}

void LoadUrl(const std::wstring& raw) {
    std::wstring url = NormalizeUrl(raw);
    if (url.empty()) return;
    SetWindowTextW(g_hwnd, (L"Loading " + url + L"... - " + kAppName).c_str());
    HWND h = g_hwnd;
    std::thread([h, url]() { PostMessageW(h, WM_APP + 1, 0, (LPARAM)FetchUrl(url)); }).detach();
}

INT_PTR CALLBACK UrlDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            std::wstring init = g_url;
            if (init.empty() && OpenClipboard(d)) {  // prefill with a URL from the clipboard
                if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                    if (auto* t = (const wchar_t*)GlobalLock(h)) {
                        if (_wcsnicmp(t, L"http://", 7) == 0 || _wcsnicmp(t, L"https://", 8) == 0) init = t;
                        GlobalUnlock(h);
                    }
                }
                CloseClipboard();
            }
            SetDlgItemTextW(d, IDC_URL, init.c_str());
            SendDlgItemMessageW(d, IDC_URL, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(d, IDC_URL));
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                wchar_t buf[4096];
                GetDlgItemTextW(d, IDC_URL, buf, 4096);
                g_urlInput = buf;
                EndDialog(d, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) { EndDialog(d, IDCANCEL); return TRUE; }
            break;
    }
    return FALSE;
}

void DoOpenUrl() {
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_URL), g_hwnd, UrlDlgProc, 0) == IDOK) LoadUrl(g_urlInput);
}

void OpenLink(const std::wstring& href) {
    if (_wcsnicmp(href.c_str(), L"http://", 7) == 0 || _wcsnicmp(href.c_str(), L"https://", 8) == 0 || _wcsnicmp(href.c_str(), L"mailto:", 7) == 0) {
        ShellExecuteW(g_hwnd, L"open", href.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (!g_url.empty()) {
        wchar_t out[4096];
        DWORD n = 4096;
        if (SUCCEEDED(UrlCombineW(g_url.c_str(), href.c_str(), out, &n, 0))) {
            std::wstring u = out, p = u.substr(0, u.find_first_of(L"?#"));
            if (IsMarkdownFile(p)) LoadUrl(u);
            else ShellExecuteW(g_hwnd, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }
    if (g_path.empty()) return;
    std::wstring rel = href;
    size_t cut = rel.find_first_of(L"#?");
    if (cut != std::wstring::npos) rel.resize(cut);
    wchar_t dec[MAX_PATH * 2];
    DWORD len = MAX_PATH * 2;
    if (SUCCEEDED(UrlUnescapeW(&rel[0], dec, &len, 0))) rel = dec;
    for (auto& c : rel) if (c == L'/') c = L'\\';
    std::wstring target = FullPath(DirOf(g_path) + L"\\" + rel);
    if (!PathFileExistsW(target.c_str())) return;
    if (IsMarkdownFile(target)) OpenPath(target);
    else ShellExecuteW(g_hwnd, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void SetZoom(int z) {
    g_set.zoom = std::max(25, std::min(500, z));
    ApplyZoom();
}

void Command(int id) {
    switch (id) {
        case IDM_OPEN: DoOpen(); break;
        case IDM_OPENURL: DoOpenUrl(); break;
        case IDM_RELOAD: if (!g_url.empty()) LoadUrl(g_url); else if (!g_path.empty()) LoadFile(g_path, false); break;
        case IDM_EXPORT: DoExport(); break;
        case IDM_PRINT: if (g_view) g_view->ExecuteScript(L"window.print()", nullptr); break;
        case IDM_EXIT: DestroyWindow(g_hwnd); break;
        case IDM_FIND: Send(L"F:"); if (g_controller) g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC); break;
        case IDM_THEME: g_set.dark = !g_set.dark; PushConfig(); SyncMenu(); break;
        case IDM_OUTLINE: g_set.outline = !g_set.outline; PushConfig(); SyncMenu(); break;
        case IDM_WIDE: g_set.wide = !g_set.wide; PushConfig(); SyncMenu(); break;
        case IDM_ONTOP:
            g_set.onTop = !g_set.onTop;
            SetWindowPos(g_hwnd, g_set.onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SyncMenu();
            break;
        case IDM_ZOOMIN: SetZoom(g_set.zoom + (g_set.zoom >= 100 ? 10 : 5)); break;
        case IDM_ZOOMOUT: SetZoom(g_set.zoom - (g_set.zoom > 100 ? 10 : 5)); break;
        case IDM_ZOOMRESET: SetZoom(100); break;
        case IDM_ABOUT:
            MessageBoxW(g_hwnd, L"Markdown Viewer\n\nNative Win32 app. Markdown is parsed in C++ and displayed with WebView2.", kAppName, MB_ICONINFORMATION);
            break;
    }
}

void OnWebMessage(const std::wstring& m) {
    if (m == L"ready") {
        g_ready = true;
        PushConfig();
        if (!g_url.empty()) LoadUrl(g_url);
        else if (!g_path.empty() && LoadFile(g_path, true)) {}
        else ShowWelcome();
        return;
    }
    if (m.rfind(L"K:", 0) == 0) {
        static const struct { const wchar_t* n; int id; } keys[] = {
            {L"open", IDM_OPEN}, {L"openurl", IDM_OPENURL}, {L"reload", IDM_RELOAD}, {L"theme", IDM_THEME}, {L"outline", IDM_OUTLINE},
            {L"wide", IDM_WIDE}, {L"export", IDM_EXPORT}, {L"print", IDM_PRINT}, {L"zoomin", IDM_ZOOMIN},
            {L"zoomout", IDM_ZOOMOUT}, {L"zoomreset", IDM_ZOOMRESET}};
        for (auto& k : keys) if (m.compare(2, std::wstring::npos, k.n) == 0) PostMessageW(g_hwnd, WM_COMMAND, k.id, 0);
        return;
    }
    if (m.rfind(L"L:", 0) == 0) { OpenLink(m.substr(2)); return; }
    if (m.rfind(L"X:", 0) == 0) {
        if (!g_exportPath.empty() && !WriteFileBytes(g_exportPath, Narrow(m.substr(2))))
            MessageBoxW(g_hwnd, L"Could not write the file.", kAppName, MB_ICONWARNING);
        g_exportPath.clear();
    }
}

void Layout() {
    if (!g_controller) return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    g_controller->put_Bounds(rc);
}

void InitWebView() {
    std::wstring data = DataDir();
    CreateCoreWebView2EnvironmentWithOptions(nullptr, data.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    MessageBoxW(g_hwnd, L"The Microsoft Edge WebView2 Runtime is required but could not be started.\n\nInstall it from https://developer.microsoft.com/microsoft-edge/webview2/", kAppName, MB_ICONERROR);
                    return hr;
                }
                env->CreateCoreWebView2Controller(g_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT hr2, ICoreWebView2Controller* ctl) -> HRESULT {
                            if (FAILED(hr2) || !ctl) return hr2;
                            g_controller = ctl;
                            g_controller->get_CoreWebView2(&g_view);

                            ComPtr<ICoreWebView2Settings> st;
                            g_view->get_Settings(&st);
                            st->put_IsZoomControlEnabled(FALSE);
                            st->put_AreDevToolsEnabled(FALSE);
                            ComPtr<ICoreWebView2Settings3> st3;
                            if (SUCCEEDED(st.As(&st3))) st3->put_AreBrowserAcceleratorKeysEnabled(FALSE);

                            PushConfig();  // background colour + zoom (messages are dropped until ready)
                            Layout();

                            EventRegistrationToken tok;
                            g_view->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                                        LPWSTR s = nullptr;
                                        if (SUCCEEDED(a->TryGetWebMessageAsString(&s)) && s) {
                                            std::wstring m(s);
                                            CoTaskMemFree(s);
                                            OnWebMessage(m);
                                        }
                                        return S_OK;
                                    }).Get(), &tok);

                            g_view->add_NavigationStarting(
                                Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
                                        LPWSTR u = nullptr;
                                        a->get_Uri(&u);
                                        std::wstring uri = u ? u : L"";
                                        CoTaskMemFree(u);
                                        if (uri.rfind(L"data:", 0) == 0 || uri.rfind(L"about:", 0) == 0) return S_OK;
                                        a->put_Cancel(TRUE);
                                        if (uri.rfind(L"file:", 0) == 0) {  // a file dropped onto the page
                                            wchar_t p[MAX_PATH * 2];
                                            DWORD n = MAX_PATH * 2;
                                            if (SUCCEEDED(PathCreateFromUrlW(uri.c_str(), p, &n, 0)) && IsMarkdownFile(p))
                                                PostMessageW(g_hwnd, WM_APP, 0, (LPARAM)new std::wstring(p));
                                        } else {
                                            OpenLink(uri);
                                        }
                                        return S_OK;
                                    }).Get(), &tok);

                            g_view->add_NewWindowRequested(
                                Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                                        a->put_Handled(TRUE);
                                        LPWSTR u = nullptr;
                                        a->get_Uri(&u);
                                        if (u) { OpenLink(u); CoTaskMemFree(u); }
                                        return S_OK;
                                    }).Get(), &tok);

                            // Load the page template from the embedded resource.
                            g_view->NavigateToString(Widen(LoadResourceText(IDR_SHELL)).c_str());
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g_hwnd = h;
            DragAcceptFiles(h, TRUE);
            SetTimer(h, 1, 600, nullptr);
            SyncMenu();
            InitWebView();
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_DPICHANGED: {
            RECT* r = (RECT*)lp;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SETFOCUS:
            if (g_controller) g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            return 0;
        case WM_COMMAND:
            Command(LOWORD(wp));
            return 0;
        case WM_DROPFILES: {
            wchar_t p[MAX_PATH * 2];
            if (DragQueryFileW((HDROP)wp, 0, p, MAX_PATH * 2)) OpenPath(p);
            DragFinish((HDROP)wp);
            return 0;
        }
        case WM_APP: {
            std::unique_ptr<std::wstring> p((std::wstring*)lp);
            OpenPath(*p);
            return 0;
        }
        case WM_APP + 1: {
            std::unique_ptr<UrlResult> r((UrlResult*)lp);
            if (r->ok) {
                g_path.clear();
                g_url = r->url;
                g_lastWrite = {};
                ShowDocument(DecodeText(std::move(r->body)), true);
            } else {
                SetTitle();
                MessageBoxW(g_hwnd, (L"Could not load:\n" + r->url + L"\n\n" + r->error).c_str(), kAppName, MB_ICONWARNING);
            }
            return 0;
        }
        case WM_TIMER: {
            if (g_path.empty()) return 0;
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &fad) &&
                CompareFileTime(&fad.ftLastWriteTime, &g_lastWrite) != 0)
                LoadFile(g_path, false);
            return 0;
        }
        case WM_CLOSE:
            SaveSettings();
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            KillTimer(h, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadSettings();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    auto isUrl = [](const std::wstring& s) { return _wcsnicmp(s.c_str(), L"http://", 7) == 0 || _wcsnicmp(s.c_str(), L"https://", 8) == 0; };
    if (argv && argc > 1) { if (isUrl(argv[1])) g_url = NormalizeUrl(argv[1]); else g_path = FullPath(argv[1]); }
    else if (isUrl(g_set.lastFile)) g_url = g_set.lastFile;
    else if (!g_set.lastFile.empty() && PathFileExistsW(g_set.lastFile.c_str())) g_path = g_set.lastFile;
    if (argv) LocalFree(argv);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(g_set.dark ? BLACK_BRUSH : WHITE_BRUSH);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    int x = g_set.x, y = g_set.y;
    RECT want = {x, y, x + g_set.w, y + g_set.h};
    if (x != CW_USEDEFAULT && !MonitorFromRect(&want, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;

    HWND hwnd = CreateWindowExW(g_set.onTop ? WS_EX_TOPMOST : 0, kClassName, kAppName, WS_OVERLAPPEDWINDOW, x, y, g_set.w, g_set.h,
                                nullptr, LoadMenuW(inst, MAKEINTRESOURCEW(IDM_MENU)), inst, nullptr);
    if (!hwnd) return 1;

    ACCEL acc[] = {
        {FCONTROL | FVIRTKEY, 'O', IDM_OPEN},          {FCONTROL | FVIRTKEY, 'U', IDM_OPENURL},          {FVIRTKEY, VK_F5, IDM_RELOAD},
        {FCONTROL | FVIRTKEY, 'R', IDM_RELOAD},        {FCONTROL | FVIRTKEY, 'S', IDM_EXPORT},
        {FCONTROL | FVIRTKEY, 'P', IDM_PRINT},         {FCONTROL | FVIRTKEY, 'F', IDM_FIND},
        {FCONTROL | FVIRTKEY, 'B', IDM_OUTLINE},       {FCONTROL | FVIRTKEY, 'D', IDM_THEME},
        {FCONTROL | FVIRTKEY, 'L', IDM_WIDE},          {FCONTROL | FVIRTKEY, '0', IDM_ZOOMRESET},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, IDM_ZOOMIN}, {FCONTROL | FVIRTKEY, VK_OEM_MINUS, IDM_ZOOMOUT}};
    g_accel = CreateAcceleratorTableW(acc, (int)(sizeof(acc) / sizeof(acc[0])));

    ShowWindow(hwnd, g_set.maximized ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!TranslateAcceleratorW(hwnd, g_accel, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    return (int)m.wParam;
}
