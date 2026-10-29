#ifdef LP_CLIENT

// NarratorReadout's voice: SAPI's ISpVoice on its own COM thread. Only Windows headers here (the
// game headers and windows.h do not mix well).

#include "client/misctweaks/Speech.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <sapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")

namespace lp::client::misctweaks {

namespace {

std::mutex                  gMutex;
std::condition_variable     gWake;
std::optional<std::wstring> gPending;
bool                        gStarted = false;
unsigned                    gGeneration = 0; // a worker runs while the generation it started with is current
bool                        gBroken  = false; // the speech API could not be created

std::wstring widen(std::string const& s) {
    if (s.empty()) return {};
    int          n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void run(unsigned generation) {
    HRESULT  init  = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ISpVoice* voice = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL, __uuidof(ISpVoice), reinterpret_cast<void**>(&voice)))) {
        std::lock_guard lock(gMutex);
        gBroken = true;
    }
    for (;;) {
        std::wstring text;
        {
            std::unique_lock lock(gMutex);
            gWake.wait(lock, [&] { return gGeneration != generation || gPending.has_value(); });
            if (gGeneration != generation) break;
            text = std::move(*gPending);
            gPending.reset();
        }
        if (voice) voice->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
    }
    if (voice) {
        voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
        voice->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
}

} // namespace

bool say(std::string const& utf8) {
    std::lock_guard lock(gMutex);
    if (gBroken) return false;
    if (!gStarted) {
        gStarted = true;
        std::thread(run, ++gGeneration).detach(); // never joined at exit (static destruction must not see a joinable thread)
    }
    gPending = widen(utf8);
    gWake.notify_one();
    return true;
}

void stopSpeech() {
    {
        std::lock_guard lock(gMutex);
        if (!gStarted) return;
        gStarted = false;
        ++gGeneration;
        gPending.reset();
    }
    gWake.notify_all();
}

} // namespace lp::client::misctweaks

#endif
