#include "agent/computer.h"
#include "core/text.h"
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <UIAutomation.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <thread>

namespace computer {
namespace {

std::atomic<bool> g_stop{false};

template <class T> void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

bool IsOurs(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

bool Candidate(HWND h) {
    if (!IsWindowVisible(h) || IsIconic(h) || IsOurs(h) || GetWindow(h, GW_OWNER)) return false;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked || GetWindowTextLengthW(h) == 0) return false;
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return false;
    RECT r;
    GetWindowRect(h, &r);
    return r.right - r.left > 200 && r.bottom - r.top > 150;
}

HWND Target() {
    HWND fg = GetForegroundWindow();
    if (fg && Candidate(fg)) return fg;
    for (HWND h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT))
        if (Candidate(h)) return h;
    return nullptr;
}

std::string WinText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring t(n + 1, L'\0');
    GetWindowTextW(h, t.data(), n + 1);
    t.resize(wcslen(t.c_str()));
    return text::Narrow(t);
}

std::string ExeOf(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return {};
    wchar_t buf[MAX_PATH];
    DWORD n = MAX_PATH;
    std::wstring s;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) s.assign(buf, n);
    CloseHandle(p);
    s = s.substr(s.find_last_of(L'\\') + 1);
    return text::Narrow(s);
}

const char* TypeName(CONTROLTYPEID t) {
    switch (t) {
    case UIA_ButtonControlTypeId: return "botão";
    case UIA_EditControlTypeId: return "campo de texto";
    case UIA_HyperlinkControlTypeId: return "link";
    case UIA_ListItemControlTypeId: return "item";
    case UIA_MenuItemControlTypeId: return "menu";
    case UIA_TabItemControlTypeId: return "aba";
    case UIA_CheckBoxControlTypeId: return "caixa de seleção";
    case UIA_RadioButtonControlTypeId: return "opção";
    case UIA_ComboBoxControlTypeId: return "lista";
    case UIA_SplitButtonControlTypeId: return "botão";
    case UIA_TreeItemControlTypeId: return "item";
    case UIA_DataItemControlTypeId: return "item";
    case UIA_DocumentControlTypeId: return "documento";
    default: return nullptr;
    }
}

std::string BstrText(BSTR b) { return b ? text::Narrow(std::wstring(b, SysStringLen(b))) : std::string(); }

void SendKey(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR ||
        vk == VK_NEXT || vk == VK_DELETE || vk == VK_INSERT)
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in, sizeof(in));
}

void Sleep_(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
} // namespace

std::atomic<bool>& StopFlag() { return g_stop; }

bool FocusTarget() {
    HWND h = Target();
    if (!h) return false;
    if (GetForegroundWindow() == h) return true;
    // truque do Alt: o Windows só deixa trocar o foco se houver "entrada" recente
    SendKey(VK_MENU, false);
    SendKey(VK_MENU, true);
    SetForegroundWindow(h);
    Sleep_(120);
    return GetForegroundWindow() == h;
}

Snapshot Inspect(bool withShot, int maxEls) {
    Snapshot s;
    HWND h = Target();
    if (!h) return s;
    s.window = WinText(h);
    s.app = ExeOf(h);
    RECT wr;
    GetWindowRect(h, &wr);
    s.wx = wr.left; s.wy = wr.top; s.ww = wr.right - wr.left; s.wh = wr.bottom - wr.top;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* uia = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia))) {
        IUIAutomationElement* root = nullptr;
        IUIAutomationCacheRequest* cache = nullptr;
        IUIAutomationCondition *c1 = nullptr, *c2 = nullptr, *cond = nullptr;
        IUIAutomationElementArray* arr = nullptr;
        VARIANT vt, vf;
        VariantInit(&vt); vt.vt = VT_BOOL; vt.boolVal = VARIANT_TRUE;
        VariantInit(&vf); vf.vt = VT_BOOL; vf.boolVal = VARIANT_FALSE;
        if (SUCCEEDED(uia->ElementFromHandle(h, &root)) && SUCCEEDED(uia->CreateCacheRequest(&cache))) {
            for (PROPERTYID p : {UIA_NamePropertyId, UIA_ControlTypePropertyId, UIA_BoundingRectanglePropertyId, UIA_IsEnabledPropertyId,
                                 UIA_IsKeyboardFocusablePropertyId, UIA_ValueValuePropertyId, UIA_HelpTextPropertyId})
                cache->AddProperty(p);
            uia->CreatePropertyCondition(UIA_IsControlElementPropertyId, vt, &c1);
            uia->CreatePropertyCondition(UIA_IsOffscreenPropertyId, vf, &c2);
            uia->CreateAndCondition(c1, c2, &cond);
            if (cond) root->FindAllBuildCache(TreeScope_Descendants, cond, cache, &arr);
        }
        int n = 0;
        if (arr) arr->get_Length(&n);
        std::map<std::string, int> seen;
        for (int i = 0; i < n && (int)s.elements.size() < maxEls * 3; ++i) {
            IUIAutomationElement* e = nullptr;
            if (FAILED(arr->GetElement(i, &e)) || !e) continue;
            CONTROLTYPEID ct = 0;
            e->get_CachedControlType(&ct);
            const char* tn = TypeName(ct);
            RECT r{};
            e->get_CachedBoundingRectangle(&r);
            BOOL enabled = TRUE, focusable = FALSE;
            e->get_CachedIsEnabled(&enabled);
            e->get_CachedIsKeyboardFocusable(&focusable);
            BSTR nb = nullptr;
            e->get_CachedName(&nb);
            std::string name = BstrText(nb);
            SysFreeString(nb);
            if (name.empty()) {
                BSTR hb = nullptr;
                e->get_CachedHelpText(&hb);
                name = BstrText(hb);
                SysFreeString(hb);
            }
            std::string value;
            VARIANT vv;
            VariantInit(&vv);
            if (SUCCEEDED(e->GetCachedPropertyValue(UIA_ValueValuePropertyId, &vv)) && vv.vt == VT_BSTR) value = BstrText(vv.bstrVal);
            VariantClear(&vv);
            e->Release();
            if (!tn || !enabled) continue;
            if (ct == UIA_DocumentControlTypeId) continue;
            if (name.empty() && ct != UIA_EditControlTypeId && ct != UIA_ComboBoxControlTypeId) continue;
            int w = r.right - r.left, hh = r.bottom - r.top;
            if (w < 4 || hh < 4) continue;
            // dentro da janela e da tela
            if (r.right < wr.left || r.left > wr.right || r.bottom < wr.top || r.top > wr.bottom) continue;
            if (name.size() > 90) name = name.substr(0, 87) + "...";
            std::string key = name + "|" + std::to_string(r.left / 8) + "|" + std::to_string(r.top / 8);
            if (seen[key]++) continue;
            Element el;
            el.name = name;
            el.type = tn;
            el.value = value.size() > 60 ? value.substr(0, 57) + "..." : value;
            el.x = r.left; el.y = r.top; el.w = w; el.h = hh;
            el.focusable = focusable != FALSE;
            s.elements.push_back(el);
        }
        // ordem de leitura: de cima para baixo, da esquerda para a direita
        std::stable_sort(s.elements.begin(), s.elements.end(), [](const Element& a, const Element& b) {
            int ra = a.y / 24, rb = b.y / 24;
            return ra != rb ? ra < rb : a.x < b.x;
        });
        if ((int)s.elements.size() > maxEls) {
            // prioriza campos de texto e botões quando há elementos demais
            std::stable_partition(s.elements.begin(), s.elements.end(), [](const Element& e) { return e.type != "item" && e.type != "link"; });
            s.elements.resize(maxEls);
            std::stable_sort(s.elements.begin(), s.elements.end(), [](const Element& a, const Element& b) {
                int ra = a.y / 24, rb = b.y / 24;
                return ra != rb ? ra < rb : a.x < b.x;
            });
        }
        for (size_t i = 0; i < s.elements.size(); ++i) s.elements[i].id = (int)i + 1;
        Rel(arr); Rel(cond); Rel(c1); Rel(c2); Rel(cache); Rel(root); Rel(uia);
    }

    if (withShot) {
        // captura a área da janela (o KB fica fora das capturas)
        int x = std::max(wr.left, (LONG)GetSystemMetrics(SM_XVIRTUALSCREEN)), y = std::max(wr.top, (LONG)GetSystemMetrics(SM_YVIRTUALSCREEN));
        int w = std::min(wr.right, (LONG)(GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN))) - x;
        int hh = std::min(wr.bottom, (LONG)(GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN))) - y;
        if (w > 0 && hh > 0) {
            HDC scr = GetDC(nullptr);
            HDC mem = CreateCompatibleDC(scr);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -hh;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            void* bits = nullptr;
            HBITMAP bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (bmp) {
                HGDIOBJ old = SelectObject(mem, bmp);
                if (BitBlt(mem, 0, 0, w, hh, scr, x, y, SRCCOPY | CAPTUREBLT)) {
                    auto* px = (uint8_t*)bits;
                    for (size_t i = 3; i < (size_t)w * hh * 4; i += 4) px[i] = 255;
                    s.screenshot = img::FromPixels(px, w, hh, w * 4, true, "Tela", 1280);
                    float maxSide = 1280.f;
                    s.shotScale = std::max(w, hh) > maxSide ? std::max(w, hh) / maxSide : 1.f;
                    s.shotX = x;
                    s.shotY = y;
                }
                SelectObject(mem, old);
                DeleteObject(bmp);
            }
            DeleteDC(mem);
            ReleaseDC(nullptr, scr);
        }
    }
    return s;
}

std::string Describe(const Snapshot& s) {
    if (s.window.empty()) return "Nenhuma janela aberta além do KB.";
    std::string out = "Janela ativa: \"" + s.window + "\" (" + s.app + ")\n";
    if (s.elements.empty()) {
        out += "Não consegui ler os controles desta janela";
        out += s.screenshot ? "; use a imagem e click_xy." : "; tente atalhos de teclado (press_keys) ou digitar (type_text).";
        return out + "\n";
    }
    out += "Controles visíveis — use o número com click / type_text:\n";
    for (auto& e : s.elements) {
        out += "[" + std::to_string(e.id) + "] " + e.type;
        if (!e.name.empty()) out += " \"" + e.name + "\"";
        if (!e.value.empty()) out += " = \"" + e.value + "\"";
        out += "\n";
    }
    if (s.screenshot)
        out += "(A imagem anexada mostra a janela. Para click_xy use coordenadas da imagem, 0,0 no canto superior esquerdo.)\n";
    return out;
}

void MoveSmooth(int x, int y, int ms) {
    POINT p;
    GetCursorPos(&p);
    double dist = std::hypot(x - p.x, y - p.y);
    int steps = std::clamp((int)(ms / 8), 6, 80);
    if (dist < 8) steps = 2;
    for (int i = 1; i <= steps; ++i) {
        if (g_stop) return;
        double t = (double)i / steps;
        double e = t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; // ease in-out
        SetCursorPos((int)std::lround(p.x + (x - p.x) * e), (int)std::lround(p.y + (y - p.y) * e));
        Sleep_(8);
    }
}

bool ClickAt(int x, int y, bool right, bool dbl) {
    if (g_stop) return false;
    MoveSmooth(x, y);
    if (g_stop) return false;
    Sleep_(60);
    for (int k = 0; k < (dbl ? 2 : 1); ++k) {
        INPUT in[2]{};
        in[0].type = in[1].type = INPUT_MOUSE;
        in[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
        in[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
        SendInput(2, in, sizeof(INPUT));
        Sleep_(70);
    }
    return true;
}

bool ClickElement(const Snapshot& s, int id, bool right, bool dbl, std::string* err) {
    for (auto& e : s.elements)
        if (e.id == id) return ClickAt(e.x + e.w / 2, e.y + e.h / 2, right, dbl);
    if (err) *err = "Não existe o controle [" + std::to_string(id) + "]. Use look_at_screen de novo.";
    return false;
}

bool TypeText(const std::string& utf8) {
    std::wstring w = text::Widen(utf8);
    for (wchar_t ch : w) {
        if (g_stop) return false;
        if (ch == L'\n') { SendKey(VK_RETURN, false); SendKey(VK_RETURN, true); }
        else if (ch == L'\t') { SendKey(VK_TAB, false); SendKey(VK_TAB, true); }
        else if (ch != L'\r') {
            INPUT in[2]{};
            in[0].type = in[1].type = INPUT_KEYBOARD;
            in[0].ki.wScan = in[1].ki.wScan = ch;
            in[0].ki.dwFlags = KEYEVENTF_UNICODE;
            in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(2, in, sizeof(INPUT));
        }
        Sleep_(6);
    }
    return true;
}

bool PressKeys(const std::string& combo, std::string* err) {
    static const std::map<std::string, WORD> names = {
        {"ctrl", VK_CONTROL}, {"control", VK_CONTROL}, {"shift", VK_SHIFT}, {"alt", VK_MENU}, {"win", VK_LWIN}, {"windows", VK_LWIN},
        {"enter", VK_RETURN}, {"return", VK_RETURN}, {"tab", VK_TAB}, {"esc", VK_ESCAPE}, {"escape", VK_ESCAPE}, {"space", VK_SPACE},
        {"espaco", VK_SPACE}, {"backspace", VK_BACK}, {"delete", VK_DELETE}, {"del", VK_DELETE}, {"up", VK_UP}, {"down", VK_DOWN},
        {"left", VK_LEFT}, {"right", VK_RIGHT}, {"home", VK_HOME}, {"end", VK_END}, {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT},
        {"cima", VK_UP}, {"baixo", VK_DOWN}, {"esquerda", VK_LEFT}, {"direita", VK_RIGHT}};
    std::vector<WORD> keys;
    for (auto& raw : text::Split(text::Fold(combo), '+')) {
        std::string k = text::Trim(raw);
        if (k.empty()) continue;
        auto it = names.find(k);
        if (it != names.end()) { keys.push_back(it->second); continue; }
        if (k.size() >= 2 && k[0] == 'f' && isdigit((unsigned char)k[1])) { int n = atoi(k.c_str() + 1); if (n >= 1 && n <= 12) { keys.push_back((WORD)(VK_F1 + n - 1)); continue; } }
        if (k.size() == 1) {
            SHORT v = VkKeyScanW((wchar_t)k[0]);
            if (v != -1) { keys.push_back(LOBYTE(v)); continue; }
        }
        if (err) *err = "Tecla desconhecida: " + k;
        return false;
    }
    if (keys.empty()) return false;
    for (auto k : keys) { SendKey(k, false); Sleep_(25); }
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) { SendKey(*it, true); Sleep_(15); }
    return true;
}

void Scroll(int notches) {
    if (HWND h = Target()) {
        RECT r;
        GetWindowRect(h, &r);
        POINT p;
        GetCursorPos(&p);
        if (!PtInRect(&r, p)) MoveSmooth((r.left + r.right) / 2, (r.top + r.bottom) / 2, 250);
    }
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = (DWORD)(notches * WHEEL_DELTA);
    SendInput(1, &in, sizeof(in));
}

namespace {
// procura um atalho no menu Iniciar com o nome parecido
std::wstring FindShortcut(const std::string& name) {
    std::string want = text::Fold(name);
    std::wstring best;
    size_t bestLen = SIZE_MAX;
    for (int csidl : {CSIDL_COMMON_PROGRAMS, CSIDL_PROGRAMS, CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY}) {
        wchar_t dir[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, dir))) continue;
        std::vector<std::wstring> stack{dir};
        while (!stack.empty()) {
            std::wstring d = stack.back();
            stack.pop_back();
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                if (fd.cFileName[0] == L'.') continue;
                std::wstring full = d + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { stack.push_back(full); continue; }
                std::wstring fn = fd.cFileName;
                if (fn.size() < 5 || _wcsicmp(fn.c_str() + fn.size() - 4, L".lnk")) continue;
                std::string base = text::Fold(text::Narrow(fn.substr(0, fn.size() - 4)));
                if (base.find("uninstall") != std::string::npos || base.find("desinstalar") != std::string::npos) continue;
                if (base.find(want) != std::string::npos && base.size() < bestLen) { best = full; bestLen = base.size(); }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    return best;
}
} // namespace

bool OpenApp(const std::string& name, std::string* err) {
    std::string f = text::Fold(text::Trim(name));
    static const std::map<std::string, const wchar_t*> alias = {
        {"chrome", L"chrome"}, {"google chrome", L"chrome"}, {"edge", L"msedge"}, {"microsoft edge", L"msedge"}, {"firefox", L"firefox"},
        {"bloco de notas", L"notepad"}, {"notepad", L"notepad"}, {"calculadora", L"calc"}, {"paint", L"mspaint"},
        {"explorador de arquivos", L"explorer"}, {"explorer", L"explorer"}, {"arquivos", L"explorer"}, {"cmd", L"cmd"},
        {"prompt de comando", L"cmd"}, {"terminal", L"wt"}, {"powershell", L"powershell"}, {"configuracoes", L"ms-settings:"},
        {"whatsapp", L"whatsapp:"}, {"spotify", L"spotify:"}, {"word", L"winword"}, {"excel", L"excel"}, {"powerpoint", L"powerpnt"},
        {"outlook", L"outlook"}, {"teams", L"msteams:"}, {"discord", L"discord:"}, {"camera", L"microsoft.windows.camera:"},
        {"loja", L"ms-windows-store:"}, {"microsoft store", L"ms-windows-store:"}};
    auto it = alias.find(f);
    if (it != alias.end()) {
        if ((INT_PTR)ShellExecuteW(nullptr, L"open", it->second, nullptr, nullptr, SW_SHOWNORMAL) > 32) return true;
    }
    std::wstring lnk = FindShortcut(f);
    if (!lnk.empty() && (INT_PTR)ShellExecuteW(nullptr, L"open", lnk.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32) return true;
    // último recurso: o próprio nome (ex.: "notepad++" registrado em App Paths)
    if ((INT_PTR)ShellExecuteW(nullptr, L"open", text::Widen(name).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32) return true;
    if (err) *err = "Não encontrei o aplicativo \"" + name + "\" instalado.";
    return false;
}

} // namespace computer
