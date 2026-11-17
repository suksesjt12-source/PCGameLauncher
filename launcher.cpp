#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")

namespace fs = std::filesystem;

static HINSTANCE g_instance = nullptr;
static HWND g_window = nullptr;
static HWND g_pathEdit = nullptr;
static HWND g_argsEdit = nullptr;
static HWND g_fpsEdit = nullptr;
static HWND g_status = nullptr;
static HWND g_windowedCheck = nullptr;
static std::string g_baseDir;
static const char* APP_TITLE = "PC Game Launcher";

enum ControlId {
    ID_PATH = 1001, ID_BROWSE = 1002, ID_ARGS = 1003,
    ID_FPS = 1004, ID_WINDOWED = 1005, ID_LAUNCH = 1006,
    ID_CONFIG = 1007
};

static std::string Narrow(const std::wstring& value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), n, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

static std::wstring Widen(const std::string& value) {
    if (value.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

static std::string ReadControl(HWND control) {
    int length = GetWindowTextLengthA(control);
    std::string value(static_cast<size_t>(length) + 1, '\0');
    GetWindowTextA(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

static void SetStatus(const std::string& message) {
    if (g_status) SetWindowTextA(g_status, message.c_str());
}

static fs::path SettingsPath() {
    return fs::path(g_baseDir) / "launcher.ini";
}

static void SaveSettings() {
    std::ofstream out(SettingsPath(), std::ios::trunc);
    if (!out) return;
    out << "game=" << ReadControl(g_pathEdit) << "\n";
    out << "args=" << ReadControl(g_argsEdit) << "\n";
    out << "fps=" << ReadControl(g_fpsEdit) << "\n";
    out << "windowed=" << (IsDlgButtonChecked(g_window, ID_WINDOWED) == BST_CHECKED ? "1" : "0") << "\n";
}

static void LoadSettings() {
    std::ifstream in(SettingsPath());
    std::string line;
    while (std::getline(in, line)) {
        auto split = line.find('=');
        if (split == std::string::npos) continue;
        std::string key = line.substr(0, split);
        std::string value = line.substr(split + 1);
        if (key == "game") SetWindowTextA(g_pathEdit, value.c_str());
        else if (key == "args") SetWindowTextA(g_argsEdit, value.c_str());
        else if (key == "fps") SetWindowTextA(g_fpsEdit, value.c_str());
        else if (key == "windowed" && value == "1") CheckDlgButton(g_window, ID_WINDOWED, BST_CHECKED);
    }
}

static bool WriteDXVKConfig(const fs::path& gameDirectory, int fps, bool windowed) {
    fs::path configPath = gameDirectory / "dxvk.conf";
    std::vector<std::string> lines;
    {
        std::ifstream in(configPath);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }

    auto upsert = [&lines](const std::string& key, const std::string& value) {
        bool found = false;
        for (auto& line : lines) {
            std::string trimmed = line;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            if (trimmed.rfind(key, 0) == 0 &&
                (trimmed.size() == key.size() || trimmed[key.size()] == '=' || trimmed[key.size()] == ' ')) {
                line = key + " = " + value;
                found = true;
            }
        }
        if (!found) lines.push_back(key + " = " + value);
    };

    upsert("dxgi.maxFrameRate", std::to_string(fps));
    upsert("d3d9.maxFrameRate", std::to_string(fps));
    if (windowed) {
        upsert("dxgi.syncInterval", "0");
    }

    std::ofstream out(configPath, std::ios::trunc);
    if (!out) return false;
    for (const auto& line : lines) out << line << "\n";
    return static_cast<bool>(out);
}

static void BrowseGame() {
    char fileName[MAX_PATH] = {};
    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = "Windows executable (*.exe)\0*.exe\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    dialog.lpstrTitle = "Select a PC game executable";
    if (GetOpenFileNameA(&dialog)) {
        SetWindowTextA(g_pathEdit, fileName);
        SaveSettings();
        SetStatus("Executable selected.");
    }
}

static void LaunchGame() {
    std::string executable = ReadControl(g_pathEdit);
    std::string arguments = ReadControl(g_argsEdit);
    std::string fpsText = ReadControl(g_fpsEdit);

    if (executable.empty() || !fs::exists(fs::path(executable))) {
        MessageBoxA(g_window, "Select a valid game .exe file first.", APP_TITLE, MB_ICONWARNING);
        return;
    }

    int fps = 0;
    if (!fpsText.empty()) {
        try { fps = std::stoi(fpsText); }
        catch (...) { fps = -1; }
        if (fps < 0 || fps > 1000) {
            MessageBoxA(g_window, "FPS limit must be between 0 and 1000. Use 0 for unlimited.", APP_TITLE, MB_ICONWARNING);
            return;
        }
    }

    fs::path exePath = fs::absolute(fs::path(executable));
    fs::path gameDirectory = exePath.parent_path();
    if (!WriteDXVKConfig(gameDirectory, fps, IsDlgButtonChecked(g_window, ID_WINDOWED) == BST_CHECKED)) {
        MessageBoxA(g_window, "Could not write dxvk.conf. Check folder permissions.", APP_TITLE, MB_ICONWARNING);
        return;
    }

    std::string command = "\"" + exePath.string() + "\"";
    if (!arguments.empty()) command += " " + arguments;
    std::vector<char> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back('\0');

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessA(
        exePath.string().c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
        0, nullptr, gameDirectory.string().c_str(), &startup, &process
    );

    if (!started) {
        DWORD error = GetLastError();
        std::ostringstream message;
        message << "Windows could not start the game. Error code: " << error;
        MessageBoxA(g_window, message.str().c_str(), APP_TITLE, MB_ICONERROR);
        return;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    SaveSettings();
    SetStatus("Game process started. DXVK settings were written to the game folder.");
}

static void CreateControls(HWND window) {
    g_window = window;
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    auto addLabel = [&](const char* text, int x, int y, int width) {
        HWND h = CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE,
            x, y, width, 22, window, nullptr, g_instance, nullptr);
        SendMessageA(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    };
    auto addButton = [&](const char* text, int id, int x, int y, int width) {
        HWND h = CreateWindowExA(0, "BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            x, y, width, 28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
        SendMessageA(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    };

    addLabel("Game executable (.exe)", 20, 20, 220);
    g_pathEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        20, 45, 390, 27, window, reinterpret_cast<HMENU>(ID_PATH), g_instance, nullptr);
    SendMessageA(g_pathEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    addButton("Browse...", ID_BROWSE, 420, 45, 90);

    addLabel("Launch arguments (optional)", 20, 88, 250);
    g_argsEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        20, 113, 490, 27, window, reinterpret_cast<HMENU>(ID_ARGS), g_instance, nullptr);
    SendMessageA(g_argsEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    addLabel("DXVK FPS limit (0 = unlimited)", 20, 155, 260);
    g_fpsEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "0", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
        20, 180, 100, 27, window, reinterpret_cast<HMENU>(ID_FPS), g_instance, nullptr);
    SendMessageA(g_fpsEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    g_windowedCheck = CreateWindowExA(0, "BUTTON", "Disable DXVK VSync (syncInterval = 0)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        145, 180, 300, 27, window, reinterpret_cast<HMENU>(ID_WINDOWED), g_instance, nullptr);
    SendMessageA(g_windowedCheck, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    addButton("Launch Game", ID_LAUNCH, 20, 230, 150);
    addButton("Write DXVK Config Only", ID_CONFIG, 185, 230, 190);

    g_status = CreateWindowExA(0, "STATIC", "Ready.", WS_CHILD | WS_VISIBLE,
        20, 280, 490, 40, window, nullptr, g_instance, nullptr);
    SendMessageA(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

static void WriteConfigOnly() {
    std::string executable = ReadControl(g_pathEdit);
    if (executable.empty() || !fs::exists(fs::path(executable))) {
        MessageBoxA(g_window, "Select a valid game .exe file first.", APP_TITLE, MB_ICONWARNING);
        return;
    }
    int fps = 0;
    try { fps = std::stoi(ReadControl(g_fpsEdit)); }
    catch (...) { fps = -1; }
    if (fps < 0 || fps > 1000) {
        MessageBoxA(g_window, "FPS limit must be between 0 and 1000.", APP_TITLE, MB_ICONWARNING);
        return;
    }
    fs::path directory = fs::absolute(fs::path(executable)).parent_path();
    if (WriteDXVKConfig(directory, fps, IsDlgButtonChecked(g_window, ID_WINDOWED) == BST_CHECKED))
        SetStatus("dxvk.conf updated. Existing unrelated settings were preserved.");
    else
        MessageBoxA(g_window, "Could not write dxvk.conf. Check folder permissions.", APP_TITLE, MB_ICONERROR);
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        g_window = window;
        return DefWindowProcA(window, message, wParam, lParam);
    }
    switch (message) {
    case WM_CREATE:
        CreateControls(window);
        LoadSettings();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BROWSE: BrowseGame(); return 0;
        case ID_LAUNCH: LaunchGame(); return 0;
        case ID_CONFIG: WriteConfigOnly(); return 0;
        }
        break;
    case WM_CLOSE:
        SaveSettings();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    g_instance = instance;
    char modulePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    g_baseDir = fs::path(modulePath).parent_path().string();

    const char* className = "PCGameLauncherWindow";
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    if (!RegisterClassExA(&wc)) return 1;

    HWND window = CreateWindowExA(0, className, APP_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 550, 380,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return static_cast<int>(message.wParam);
}
