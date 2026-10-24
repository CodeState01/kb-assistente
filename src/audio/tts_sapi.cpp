#include "audio/tts_sapi.h"
#include "core/text.h"
#include <windows.h>
#include <sapi.h>
#include <chrono>

namespace {
double NowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Abertura aproximada da boca para cada visema SAPI (SP_VISEME_0..21).
const float kVisemeOpen[22] = {
    0.0f,  // 0 silêncio
    0.9f,  // 1 ae ax ah
    1.0f,  // 2 aa
    0.8f,  // 3 ao
    0.7f,  // 4 ey eh uh
    0.6f,  // 5 er
    0.5f,  // 6 y iy ih ix
    0.55f, // 7 w uw
    0.75f, // 8 ow
    0.8f,  // 9 aw
    0.7f,  // 10 oy
    0.8f,  // 11 ay
    0.4f,  // 12 h
    0.35f, // 13 r
    0.4f,  // 14 l
    0.3f,  // 15 s z
    0.35f, // 16 sh ch jh zh
    0.3f,  // 17 th dh
    0.25f, // 18 f v
    0.35f, // 19 d t n
    0.4f,  // 20 k g ng
    0.1f,  // 21 p b m
};

struct TokenEntry {
    ISpObjectToken* token;
    VoiceInfo info;
};

std::string LangName(const std::wstring& hex) {
    // Atributo "Language" pode ser "416" ou "416;816"
    unsigned long id = wcstoul(hex.c_str(), nullptr, 16);
    switch (id) {
    case 0x416: return "pt-BR";
    case 0x816: return "pt-PT";
    case 0x409: return "en-US";
    case 0x809: return "en-GB";
    case 0xC0A: case 0x40A: return "es-ES";
    case 0x80A: return "es-MX";
    default: {
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
        if (LCIDToLocaleName(id, name, LOCALE_NAME_MAX_LENGTH, 0)) return text::Narrow(name);
        return "?";
    }
    }
}

void Enumerate(const wchar_t* category, std::vector<TokenEntry>& out) {
    ISpObjectTokenCategory* cat = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory, (void**)&cat)))
        return;
    if (SUCCEEDED(cat->SetId(category, FALSE))) {
        IEnumSpObjectTokens* en = nullptr;
        if (SUCCEEDED(cat->EnumTokens(nullptr, nullptr, &en)) && en) {
            ISpObjectToken* tok = nullptr;
            while (en->Next(1, &tok, nullptr) == S_OK) {
                TokenEntry e{tok, {}};
                LPWSTR desc = nullptr;
                if (SUCCEEDED(tok->GetStringValue(nullptr, &desc)) && desc) {
                    e.info.name = text::Narrow(desc);
                    CoTaskMemFree(desc);
                }
                ISpDataKey* attr = nullptr;
                if (SUCCEEDED(tok->OpenKey(L"Attributes", &attr)) && attr) {
                    LPWSTR lang = nullptr;
                    if (SUCCEEDED(attr->GetStringValue(L"Language", &lang)) && lang) {
                        e.info.language = LangName(lang);
                        CoTaskMemFree(lang);
                    }
                    attr->Release();
                }
                e.info.portuguese = e.info.language.rfind("pt", 0) == 0;
                bool dup = false;
                for (auto& o : out) if (o.info.name == e.info.name) dup = true;
                if (dup || e.info.name.empty()) tok->Release();
                else out.push_back(e);
            }
            en->Release();
        }
    }
    cat->Release();
}
} // namespace

TextToSpeech::TextToSpeech() { th_ = std::thread([this] { Run(); }); }

TextToSpeech::~TextToSpeech() {
    quit_ = true;
    stop_ = true;
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

void TextToSpeech::Speak(const std::string& utf8) {
    std::string t = text::Trim(utf8);
    if (t.empty()) return;
    {
        std::lock_guard<std::mutex> l(mtx_);
        queue_.push_back(text::Widen(t));
        speaking_ = true;
    }
    cv_.notify_one();
}

void TextToSpeech::StopAll() {
    {
        std::lock_guard<std::mutex> l(mtx_);
        queue_.clear();
    }
    stop_ = true;
    cv_.notify_one();
}

void TextToSpeech::SetVoice(const std::string& n) { std::lock_guard<std::mutex> l(mtx_); wantVoice_ = n; dirty_ = true; }
void TextToSpeech::SetRate(int r) { std::lock_guard<std::mutex> l(mtx_); wantRate_ = r; dirty_ = true; }
void TextToSpeech::SetVolume(int v) { std::lock_guard<std::mutex> l(mtx_); wantVolume_ = v; dirty_ = true; }
std::vector<VoiceInfo> TextToSpeech::Voices() { std::lock_guard<std::mutex> l(mtx_); return voices_; }
std::string TextToSpeech::CurrentVoice() { std::lock_guard<std::mutex> l(mtx_); return current_; }

void TextToSpeech::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ISpVoice* voice = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**)&voice)) || !voice) {
        CoUninitialize();
        return;
    }
    ULONGLONG interest = SPFEI(SPEI_VISEME) | SPFEI(SPEI_WORD_BOUNDARY);
    voice->SetInterest(interest, interest);

    std::vector<TokenEntry> tokens;
    Enumerate(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices", tokens);
    Enumerate(L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices", tokens);
    {
        std::lock_guard<std::mutex> l(mtx_);
        voices_.clear();
        for (auto& t : tokens) voices_.push_back(t.info);
    }

    auto pumpEvents = [&] {
        SPEVENT ev;
        ULONG fetched = 0;
        while (SUCCEEDED(voice->GetEvents(1, &ev, &fetched)) && fetched == 1) {
            if (ev.eEventId == SPEI_VISEME) {
                int vis = LOWORD(ev.lParam);
                mouth_ = (vis >= 0 && vis < 22) ? kVisemeOpen[vis] : 0.5f;
                lastEvent_ = NowSec();
            } else if (ev.eEventId == SPEI_WORD_BOUNDARY) {
                mouth_ = 0.85f;
                lastEvent_ = NowSec();
            }
            fetched = 0;
        }
    };

    while (!quit_) {
        std::wstring item;
        {
            std::unique_lock<std::mutex> l(mtx_);
            cv_.wait_for(l, std::chrono::milliseconds(100), [&] { return quit_ || !queue_.empty() || dirty_; });
            if (dirty_) {
                dirty_ = false;
                // Escolhe a voz: a pedida, ou a primeira pt-BR, ou a primeira pt.
                TokenEntry* pick = nullptr;
                for (auto& t : tokens) if (!wantVoice_.empty() && t.info.name == wantVoice_) pick = &t;
                if (!pick) for (auto& t : tokens) if (t.info.language == "pt-BR") { pick = &t; break; }
                if (!pick) for (auto& t : tokens) if (t.info.portuguese) { pick = &t; break; }
                if (pick && pick->info.name != current_) {
                    if (SUCCEEDED(voice->SetVoice(pick->token))) current_ = pick->info.name;
                }
                if (current_.empty()) {
                    // sem voz em português: mostra o nome da voz padrão do sistema
                    ISpObjectToken* curTok = nullptr;
                    if (SUCCEEDED(voice->GetVoice(&curTok)) && curTok) {
                        LPWSTR desc = nullptr;
                        if (SUCCEEDED(curTok->GetStringValue(nullptr, &desc)) && desc) { current_ = text::Narrow(desc); CoTaskMemFree(desc); }
                        curTok->Release();
                    }
                }
                voice->SetRate(wantRate_);
                voice->SetVolume((USHORT)wantVolume_);
            }
            if (!queue_.empty()) {
                item = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (quit_) break;
        if (item.empty()) {
            std::lock_guard<std::mutex> l(mtx_);
            speaking_ = !queue_.empty();
            mouth_ = 0.f;
            continue;
        }
        stop_ = false;
        speaking_ = true;
        voice->Speak(item.c_str(), SPF_ASYNC | SPF_IS_NOT_XML, nullptr);
        for (;;) {
            HRESULT hr = voice->WaitUntilDone(15);
            pumpEvents();
            if (stop_) {
                voice->Speak(nullptr, SPF_PURGEBEFORESPEAK | SPF_ASYNC, nullptr);
                break;
            }
            if (hr == S_OK) break;
        }
        mouth_ = 0.f;
        std::lock_guard<std::mutex> l(mtx_);
        speaking_ = !queue_.empty();
    }

    for (auto& t : tokens) t.token->Release();
    voice->Release();
    CoUninitialize();
}
