#include "net/tunnel.h"
#include "core/config.h"
#include "core/platform.h"
#include "core/text.h"
#include "net/http.h"
#include <windows.h>
#include <vector>

static const char* kCloudflaredUrl = "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-windows-amd64.exe";

Tunnel::~Tunnel() { Stop(); }

void Tunnel::Start(int port) {
    if (th_.joinable()) {
        if (state_ != State::Error && state_ != State::Off) return;
        Stop();
    }
    quit_ = false;
    th_ = std::thread([this, port] { Run(port); });
}

void Tunnel::Stop() {
    quit_ = true;
    if (process_) TerminateProcess((HANDLE)process_, 0);
    if (th_.joinable()) th_.join();
    if (process_) { CloseHandle((HANDLE)process_); process_ = nullptr; }
    if (job_) { CloseHandle((HANDLE)job_); job_ = nullptr; }
    state_ = State::Off;
    std::lock_guard<std::mutex> l(mtx_);
    url_.clear();
}

std::string Tunnel::Url() { std::lock_guard<std::mutex> l(mtx_); return url_; }
std::string Tunnel::Error() { std::lock_guard<std::mutex> l(mtx_); return error_; }

void Tunnel::Run(int port) {
    auto fail = [&](const std::string& e) {
        std::lock_guard<std::mutex> l(mtx_);
        error_ = e;
        state_ = State::Error;
    };
    // 1) cloudflared.exe (programa oficial da Cloudflare), baixado uma única vez
    std::string dir = config::AppDataDir() + "\\bin";
    CreateDirectoryW(text::Widen(dir).c_str(), nullptr);
    std::string exe = dir + "\\cloudflared.exe";
    if (!platform::FileExists(exe) || platform::FileSize(exe) < 10 * 1024 * 1024) {
        state_ = State::Downloading;
        progress_ = 0;
        std::atomic<bool> cancel{false};
        std::thread watch([&] { while (!quit_ && !cancel) Sleep(100); cancel = true; });
        auto r = http::Download(kCloudflaredUrl, exe, [&](uint64_t d, uint64_t t) { if (t) progress_ = (float)((double)d / t); }, &cancel);
        cancel = true;
        watch.join();
        if (quit_) return;
        if (!r.ok() || !r.error.empty()) { fail("Não consegui baixar o túnel: " + r.error); return; }
    }
    if (quit_) return;
    state_ = State::Starting;

    // 2) inicia escondido, lendo a saída para descobrir o endereço https
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { fail("Falha ao iniciar o túnel"); return; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    std::wstring cmd = L"\"" + text::Widen(exe) + L"\" tunnel --url http://127.0.0.1:" + std::to_wstring(port) + L" --no-autoupdate";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; // morre junto com o KB
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); CloseHandle(job); fail("Falha ao iniciar o túnel"); return; }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    job_ = job;

    std::string out;
    char chunk[4096];
    DWORD n = 0;
    while (!quit_ && ReadFile(rd, chunk, sizeof(chunk), &n, nullptr) && n > 0) {
        out.append(chunk, n);
        if (state_ != State::Ready) {
            size_t p = out.find("https://");
            while (p != std::string::npos) {
                size_t e = out.find_first_of(" \r\n|", p);
                std::string u = out.substr(p, e == std::string::npos ? std::string::npos : e - p);
                if (u.find(".trycloudflare.com") != std::string::npos && e != std::string::npos) {
                    std::lock_guard<std::mutex> l(mtx_);
                    url_ = u;
                    state_ = State::Ready;
                    break;
                }
                p = out.find("https://", p + 8);
            }
        }
        if (out.size() > 64 * 1024) out.erase(0, out.size() - 8 * 1024);
    }
    CloseHandle(rd);
    if (!quit_) fail("O túnel foi encerrado. Tente ligar de novo.");
}
