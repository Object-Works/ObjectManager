#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#if __has_include("version.h")
#include "version.h"
#endif
#ifndef OM_VERSION_WSTRING
#define OM_VERSION_WSTRING L"0.1.0"
#endif

#include "resource.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kPages = 3;
constexpr int kIdTab = 100;
constexpr int kIdSave = 110;
constexpr int kIdOpen = 111;
constexpr COLORREF kBg = RGB(18, 18, 18);
constexpr COLORREF kEdit = RGB(32, 32, 32);
constexpr COLORREF kText = RGB(232, 232, 232);

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

enum class Kind { Check, Edit, Combo };

struct Field {
    std::wstring key;
    HWND hwnd = nullptr;
    Kind kind = Kind::Edit;
    int page = 0;
    bool color = false;
    bool accent = false;
};

struct Widget {
    HWND hwnd = nullptr;
    int page = 0;  // -1 stays visible on every page
};

std::wstring Trim(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

std::wstring ToLower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

// '#' starts a comment only at the beginning of a line or after whitespace, so values can contain '#'.
std::wstring StripComment(const std::wstring& line) {
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == L'#' && (i == 0 || iswspace(line[i - 1]))) return line.substr(0, i);
    }
    return line;
}

std::wstring LineKey(const std::wstring& line) {
    const std::wstring body = StripComment(line);
    const size_t eq = body.find(L'=');
    return eq == std::wstring::npos ? std::wstring() : ToLower(Trim(body.substr(0, eq)));
}

std::wstring Home() {
    wchar_t home[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? home : L".";
}

std::wstring ConfigPath(int page) {
    const std::wstring base = Home();
    if (page == 0) return base + L"\\.config\\objectpulse\\pulse.conf";
    if (page == 1) return base + L"\\.config\\ofwm\\ofwm.conf";
    return base + L"\\.config\\objectarc\\arc.conf";
}

const wchar_t* PageName(int page) {
    if (page == 0) return L"ObjectPulse";
    if (page == 1) return L"ObjectFrame";
    return L"ObjectArc";
}

std::map<std::wstring, std::wstring> ReadValues(const std::wstring& path) {
    std::map<std::wstring, std::wstring> out;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f) return out;
    wchar_t buf[2048];
    while (fgetws(buf, 2048, f)) {
        std::wstring line = buf;
        while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
        const std::wstring body = StripComment(line);
        const size_t eq = body.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring key = ToLower(Trim(body.substr(0, eq)));
        if (!key.empty()) out[key] = Trim(body.substr(eq + 1));
    }
    fclose(f);
    return out;
}

bool WriteLines(const std::wstring& path, const std::vector<std::wstring>& lines) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") != 0 || !f) return false;
    for (const std::wstring& line : lines) {
        fputws(line.c_str(), f);
        fputws(L"\n", f);
    }
    fclose(f);
    return true;
}

// Replaces every existing line of each key and keeps the comment on that line. Other lines stay.
bool Upsert(const std::wstring& path, const std::vector<std::pair<std::wstring, std::wstring>>& values) {
    std::vector<std::wstring> lines;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") == 0 && f) {
        wchar_t buf[2048];
        while (fgetws(buf, 2048, f)) {
            std::wstring line = buf;
            while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
            lines.push_back(std::move(line));
        }
        fclose(f);
    }
    for (const auto& [key, value] : values) {
        const std::wstring entry = key + L" = " + value;
        bool found = false;
        for (std::wstring& line : lines) {
            if (LineKey(line) != key) continue;
            std::wstring next = entry;
            const std::wstring body = StripComment(line);
            if (body.size() < line.size()) {
                std::wstring comment = line.substr(body.size());
                if (comment.empty() || !iswspace(comment.front())) comment.insert(comment.begin(), L' ');
                next += comment;
            }
            line = std::move(next);
            found = true;
        }
        if (!found) lines.push_back(entry);
    }
    return WriteLines(path, lines);
}

std::wstring NormalizeColor(std::wstring value, bool accent) {
    value = Trim(value);
    const std::wstring lower = ToLower(value);
    if (accent && (lower.empty() || lower == L"auto")) return L"auto";
    if (!accent && lower.empty()) return L"0x000000";
    std::wstring hex = lower;
    if (hex.rfind(L"0x", 0) == 0) hex = hex.substr(2);
    else if (!hex.empty() && hex[0] == L'#') hex = hex.substr(1);
    if (hex.size() == 6 && hex.find_first_not_of(L"0123456789abcdef") == std::wstring::npos) return L"0x" + hex;
    return value;
}

bool PostReload(const wchar_t* cls, UINT msg) {
    HWND hwnd = FindWindowW(cls, nullptr);
    if (!hwnd) return false;
    PostMessageW(hwnd, msg, 0, 0);
    return true;
}

bool ReloadFrame() {
    HWND hwnd = FindWindowW(L"ObjectFrameWM", nullptr);
    if (!hwnd) return false;
    std::wstring cmd = L"reload";
    COPYDATASTRUCT cds{};
    cds.dwData = 0x4F46574D;
    cds.cbData = (DWORD)((cmd.size() + 1) * sizeof(wchar_t));
    cds.lpData = cmd.data();
    DWORD_PTR result = 0;
    SendMessageTimeoutW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 2000, &result);
    return true;
}

class Manager {
public:
    int Run(HINSTANCE instance) {
        instance_ = instance;
        dpi_ = GetDpiForSystem();
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"ObjectManagerWnd";
        wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_MANAGER), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
        wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_MANAGER), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

        RECT rc{0, 0, Px(700), Px(640)};
        AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        const int x = work.left + (work.right - work.left - w) / 2;
        const int y = work.top + (work.bottom - work.top - h) / 2;
        hwnd_ = CreateWindowExW(0, L"ObjectManagerWnd", L"ObjectManager " OM_VERSION_WSTRING, WS_OVERLAPPEDWINDOW,
                                x, y, w, h, nullptr, nullptr, instance, this);
        if (!hwnd_) return 1;
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        return (int)msg.wParam;
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto* self = reinterpret_cast<Manager*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return self ? self->Handle(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    }

    int Px(int dip) const { return MulDiv(dip, dpi_, 96); }

    HWND Make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int page, HMENU id) {
        HWND hwnd = CreateWindowExW(0, cls, text, style, Px(x), Px(y), Px(w), Px(h), hwnd_, id, instance_, nullptr);
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        widgets_.push_back({hwnd, page});
        return hwnd;
    }

    void AddCheck(int page, int column, const wchar_t* key, const wchar_t* label, bool on) {
        int& y = columnY_[page][column];
        const int x = column == 0 ? 20 : 360;
        HWND hwnd = Make(L"BUTTON", label, WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX, x, y, 320, 22, page, nullptr);
        if (on) SendMessageW(hwnd, BM_SETCHECK, BST_CHECKED, 0);
        fields_.push_back({key, hwnd, Kind::Check, page, false, false});
        y += 28;
    }

    void AddEdit(int page, int column, const wchar_t* key, const wchar_t* label, const wchar_t* value, bool color,
                 bool accent) {
        int& y = columnY_[page][column];
        const int x = column == 0 ? 20 : 360;
        Make(L"STATIC", label, WS_CHILD | SS_CENTERIMAGE, x, y, 140, 22, page, nullptr);
        HWND hwnd = Make(L"EDIT", value, WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x + 144, y, 150, 22, page,
                         nullptr);
        SendMessageW(hwnd, EM_SETLIMITTEXT, 400, 0);
        fields_.push_back({key, hwnd, Kind::Edit, page, color, accent});
        y += 30;
    }

    void AddCombo(int page, int column, const wchar_t* key, const wchar_t* label, const wchar_t* value,
                  std::initializer_list<const wchar_t*> items) {
        int& y = columnY_[page][column];
        const int x = column == 0 ? 20 : 360;
        Make(L"STATIC", label, WS_CHILD | SS_CENTERIMAGE, x, y, 140, 22, page, nullptr);
        HWND hwnd = Make(L"COMBOBOX", L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, x + 144,
                         y, 150, 160, page, nullptr);
        for (const wchar_t* item : items) SendMessageW(hwnd, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
        SetCombo(hwnd, value);
        fields_.push_back({key, hwnd, Kind::Combo, page, false, false});
        y += 30;
    }

    void AddWide(int page, const wchar_t* key, const wchar_t* label, const wchar_t* value) {
        const int y = std::max(columnY_[page][0], columnY_[page][1]) + 6;
        columnY_[page][0] = columnY_[page][1] = y + 30;
        Make(L"STATIC", label, WS_CHILD | SS_CENTERIMAGE, 20, y, 140, 22, page, nullptr);
        HWND hwnd = Make(L"EDIT", value, WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 164, y, 516, 22, page,
                         nullptr);
        SendMessageW(hwnd, EM_SETLIMITTEXT, 400, 0);
        fields_.push_back({key, hwnd, Kind::Edit, page, false, false});
    }

    void AddNote(int page, const wchar_t* text) {
        const int y = std::max(columnY_[page][0], columnY_[page][1]) + 8;
        Make(L"STATIC", text, WS_CHILD | SS_LEFT, 20, y, 660, 64, page, nullptr);
    }

    void Build(HWND hwnd) {
        hwnd_ = hwnd;
        dpi_ = GetDpiForWindow(hwnd);
        font_ = CreateFontW(-MulDiv(15, dpi_, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            L"Segoe UI");
        bg_ = CreateSolidBrush(kBg);
        editBg_ = CreateSolidBrush(kEdit);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        const wchar_t* tabs[] = {L"ObjectPulse", L"ObjectFrame", L"ObjectArc"};
        for (int i = 0; i < kPages; ++i) {
            tab_[i] = Make(L"BUTTON", tabs[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 20 + i * 160, 16, 148,
                           32, -1, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTab + i)));
        }
        for (int page = 0; page < kPages; ++page) columnY_[page][0] = columnY_[page][1] = 64;

        AddCheck(0, 0, L"corners", L"四隅を角丸にする", false);
        AddCheck(0, 0, L"auto_hide", L"何もないときは隠す", true);
        AddCheck(0, 0, L"always_small", L"常に小さく表示する", false);
        AddCheck(0, 0, L"reserve_space", L"上部に領域を確保する", true);
        AddCheck(0, 0, L"hide_taskbar", L"タスクバーを隠す", true);
        AddCheck(0, 0, L"expand_on_hover", L"ホバーで展開する", true);
        AddCheck(0, 0, L"notify", L"変化を通知する", true);
        AddCheck(0, 0, L"notify_focus", L"フォーカスも通知する", false);
        AddCheck(0, 0, L"clock_seconds", L"時計に秒を表示する", false);
        AddCheck(0, 0, L"hide_on_fullscreen", L"全画面では隠す", true);
        AddCombo(0, 1, L"monitor", L"モニター", L"all", {L"all", L"primary", L"1", L"2", L"3", L"4"});
        AddEdit(0, 1, L"scale", L"大きさ (%)", L"100", false, false);
        AddEdit(0, 1, L"hover_delay", L"展開までの時間", L"150", false, false);
        AddEdit(0, 1, L"notify_duration", L"通知の時間", L"1400", false, false);
        AddEdit(0, 1, L"opacity", L"不透明度 (%)", L"100", false, false);
        AddEdit(0, 1, L"background", L"背景色", L"0x000000", true, false);
        AddEdit(0, 1, L"accent", L"アクセント", L"auto", true, true);
        AddWide(0, L"compact_items", L"通常時の項目", L"battery, attention, paused, workspaces, clock");
        AddWide(0, L"expanded_items", L"展開時の項目",
                L"pins, tasks, battery, clock, workspaces, layouts, pause");
        AddNote(0, L"項目はカンマ区切りです。常に小さく表示すると上部は小さいノッチのまま出て、ホバーすると展開します。時刻は左、バッテリーは右です。日付と時刻は横に並びます。明るさと音量・マイクは左右の中央に、常に小さくしないときも、普段は隠れて端へホバーしたときだけ出ます。四隅の半径はノッチと同じです。ピン留めは pins.txt です。");

        AddCheck(1, 0, L"border", L"枠線", true);
        AddCheck(1, 0, L"glow", L"フォーカスの光彩", false);
        AddCheck(1, 0, L"animate_move", L"移動をアニメーション", true);
        AddCheck(1, 0, L"animate_workspace", L"ワークスペースの切替をアニメーション", true);
        AddCheck(1, 0, L"focus_follows_mouse", L"マウスの位置にフォーカス", false);
        AddCheck(1, 0, L"cursor_follows_focus", L"フォーカスへカーソルを移動", false);
        AddEdit(1, 1, L"gaps_in", L"内側の余白", L"8", false, false);
        AddEdit(1, 1, L"gaps_out", L"外側の余白", L"12", false, false);
        AddEdit(1, 1, L"border_width", L"枠線の太さ", L"2", false, false);
        AddEdit(1, 1, L"border_radius", L"角の半径", L"8", false, false);
        AddEdit(1, 1, L"glow_size", L"光彩の大きさ", L"12", false, false);
        AddEdit(1, 1, L"glow_intensity", L"光彩の強さ", L"60", false, false);
        AddEdit(1, 1, L"animation_duration", L"アニメーション (ms)", L"200", false, false);
        AddEdit(1, 1, L"workspaces", L"ワークスペース数", L"9", false, false);
        AddEdit(1, 1, L"border_active", L"アクティブの色", L"0x89b4fa", true, false);
        AddEdit(1, 1, L"border_inactive", L"非アクティブの色", L"0x313244", true, false);
        AddCombo(1, 0, L"default_layout", L"レイアウト", L"dwindle", {L"dwindle", L"master", L"monocle", L"scroll"});
        AddCombo(1, 0, L"workspace_mode", L"ワークスペース", L"independent", {L"independent", L"linked"});
        AddCombo(1, 0, L"workspace_slide", L"切替の方向", L"auto", {L"auto", L"up", L"down", L"left", L"right"});
        AddNote(1, L"キーバインド、ウィンドウルール、exec-once は設定ファイルに残ります。ワークスペース数は再起動後に反映されます。");

        AddCheck(2, 0, L"win_key", L"Win キーで開く", true);
        AddCheck(2, 0, L"compact", L"開いたときは検索バーだけ", false);
        AddCheck(2, 0, L"keep_query", L"入力を残す", false);
        AddCheck(2, 0, L"tray_icon", L"通知領域にアイコン", true);
        AddCheck(2, 0, L"show_windows", L"ウィンドウを候補に出す", true);
        AddCheck(2, 0, L"frame_commands", L"ObjectFrame の操作を候補に出す", true);
        AddEdit(2, 1, L"hotkey", L"ホットキー", L"alt+space", false, false);
        AddEdit(2, 1, L"rows", L"候補の数", L"8", false, false);
        AddEdit(2, 1, L"scale", L"大きさ (%)", L"100", false, false);
        AddEdit(2, 1, L"search_name", L"Web 検索の名前", L"Google", false, false);
        AddEdit(2, 1, L"accent", L"アクセント", L"auto", true, true);
        AddNote(2, L"folder と search_url は設定ファイルで編集します。Windows の設定を開く候補はそのまま残ります。");

        RECT client{};
        GetClientRect(hwnd, &client);
        const int by = client.bottom - Px(48);
        save_ = Make(L"BUTTON", L"保存して反映", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 20, 0, 160, 32, -1,
                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSave)));
        open_ = Make(L"BUTTON", L"設定ファイルを開く", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 192, 0, 180, 32,
                     -1, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOpen)));
        status_ = Make(L"STATIC", L"起動中のアプリには、保存と同時に反映します。", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 388,
                       0, 292, 32, -1, nullptr);
        // Make() stored DIP coordinates, so place the footer in pixels after the client size is known.
        SetWindowPos(save_, nullptr, Px(20), by, Px(160), Px(32), SWP_NOZORDER);
        SetWindowPos(open_, nullptr, Px(192), by, Px(180), Px(32), SWP_NOZORDER);
        SetWindowPos(status_, nullptr, Px(384), by, client.right - Px(404), Px(32), SWP_NOZORDER);

        LoadAll();
        ShowPage(0);
    }

    static void SetCombo(HWND hwnd, const std::wstring& value) {
        const int n = (int)SendMessageW(hwnd, CB_GETCOUNT, 0, 0);
        for (int i = 0; i < n; ++i) {
            wchar_t item[64] = {};
            SendMessageW(hwnd, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(item));
            if (_wcsicmp(item, value.c_str()) == 0) {
                SendMessageW(hwnd, CB_SETCURSEL, i, 0);
                return;
            }
        }
        SetWindowTextW(hwnd, value.c_str());
    }

    void LoadAll() {
        std::map<std::wstring, std::wstring> values[kPages];
        for (int page = 0; page < kPages; ++page) values[page] = ReadValues(ConfigPath(page));
        for (const Field& field : fields_) {
            auto it = values[field.page].find(field.key);
            if (it == values[field.page].end()) continue;
            if (field.kind == Kind::Check) {
                const std::wstring v = ToLower(it->second);
                const bool on = v == L"true" || v == L"yes" || v == L"on" || v == L"1";
                SendMessageW(field.hwnd, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            } else if (field.kind == Kind::Combo) {
                SetCombo(field.hwnd, it->second);
            } else {
                SetWindowTextW(field.hwnd, it->second.c_str());
            }
        }
    }

    std::wstring FieldValue(const Field& field) const {
        if (field.kind == Kind::Check) {
            return SendMessageW(field.hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"true" : L"false";
        }
        wchar_t buf[512] = {};
        GetWindowTextW(field.hwnd, buf, 512);
        std::wstring value = Trim(buf);
        if (field.color) value = NormalizeColor(value, field.accent);
        return value;
    }

    void Save() {
        std::vector<std::pair<std::wstring, std::wstring>> values[kPages];
        for (const Field& field : fields_) values[field.page].push_back({field.key, FieldValue(field)});
        bool saved[kPages] = {};
        for (int page = 0; page < kPages; ++page) saved[page] = Upsert(ConfigPath(page), values[page]);

        std::wstring applied;
        std::wstring pending;
        auto note = [&](int page, bool running) {
            if (!saved[page]) return;
            std::wstring& dest = running ? applied : pending;
            if (!dest.empty()) dest += L"、";
            dest += PageName(page);
        };
        note(0, PostReload(L"ObjectPulseWnd", WM_APP + 2));
        note(1, ReloadFrame());
        note(2, PostReload(L"ObjectArcWnd", WM_APP + 5));

        std::wstring text;
        if (!saved[0] || !saved[1] || !saved[2]) text = L"一部の設定ファイルを保存できませんでした。";
        else text = L"保存しました。";
        if (!applied.empty()) text += applied + L" に反映しました。";
        if (!pending.empty()) text += pending + L" は次回の起動時に反映されます。";
        SetWindowTextW(status_, text.c_str());
    }

    void OpenFile() {
        const std::wstring path = ConfigPath(page_);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            WriteLines(path, {L"# 未記載の項目はアプリの既定値です。ObjectManager で保存すると、画面の項目が追記されます。"});
        }
        const std::wstring arg = L"\"" + path + L"\"";
        ShellExecuteW(hwnd_, L"open", L"notepad.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    }

    void ShowPage(int page) {
        page_ = page;
        for (const Widget& widget : widgets_) {
            if (widget.page < 0) continue;
            ShowWindow(widget.hwnd, widget.page == page ? SW_SHOW : SW_HIDE);
        }
        for (int i = 0; i < kPages; ++i) {
            std::wstring title = (i == page ? L"● " : L"");
            title += PageName(i);
            SetWindowTextW(tab_[i], title.c_str());
        }
    }

    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        (void)lp;
        switch (msg) {
            case WM_CREATE:
                Build(hwnd);
                return 0;
            case WM_COMMAND:
                if (LOWORD(wp) >= kIdTab && LOWORD(wp) < kIdTab + kPages) ShowPage(LOWORD(wp) - kIdTab);
                else if (LOWORD(wp) == kIdSave) Save();
                else if (LOWORD(wp) == kIdOpen) OpenFile();
                return 0;
            case WM_ERASEBKGND: {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                FillRect(reinterpret_cast<HDC>(wp), &rc, bg_);
                return 1;
            }
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN: {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetTextColor(dc, kText);
                SetBkColor(dc, kBg);
                return reinterpret_cast<LRESULT>(bg_);
            }
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX: {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetTextColor(dc, kText);
                SetBkColor(dc, kEdit);
                return reinterpret_cast<LRESULT>(editBg_);
            }
            case WM_DESTROY:
                if (font_) DeleteObject(font_);
                if (bg_) DeleteObject(bg_);
                if (editBg_) DeleteObject(editBg_);
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND tab_[kPages] = {};
    HWND save_ = nullptr;
    HWND open_ = nullptr;
    HWND status_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH bg_ = nullptr;
    HBRUSH editBg_ = nullptr;
    int dpi_ = 96;
    int page_ = 0;
    int columnY_[kPages][2] = {};
    std::vector<Field> fields_;
    std::vector<Widget> widgets_;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"quit" || a == L"--quit") {
            if (HWND running = FindWindowW(L"ObjectManagerWnd", nullptr)) PostMessageW(running, WM_CLOSE, 0, 0);
            LocalFree(argv);
            return 0;
        }
    }
    if (argv) LocalFree(argv);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ObjectManager");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND running = FindWindowW(L"ObjectManagerWnd", nullptr)) {
            ShowWindow(running, SW_RESTORE);
            SetForegroundWindow(running);
        }
        return 0;
    }
    Manager manager;
    const int code = manager.Run(instance);
    if (mutex) CloseHandle(mutex);
    return code;
}
