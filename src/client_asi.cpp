#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>
#include "RakHook/rakhook.hpp"
#include "RakHook/wrappers/minhook.hpp"

namespace
{
    constexpr unsigned char RpcId = 221;
    constexpr std::uint32_t Magic = 0x44475244;
    constexpr std::uint16_t Protocol = 1;
    constexpr unsigned char MessageHello = 1;
    constexpr unsigned char MessageChallenge = 2;
    constexpr unsigned char MessageReport = 3;
    constexpr unsigned char ProfileR1 = 1;
    constexpr unsigned char ProfileDL = 2;
    constexpr std::size_t MaxModules = 16;
    constexpr std::size_t MaxName = 63;
    using Hook = rakhook::hooker<rakhook::wrappers::minhook>;
    HMODULE selfModule = nullptr;
    std::atomic<bool> running{true};
    std::atomic<unsigned char> clientProfile{0};

    std::wstring lower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        return value;
    }

    bool getClientProfile(unsigned char& profile)
    {
        HMODULE samp = GetModuleHandleW(L"samp.dll");
        if (!samp) return false;
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(samp);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const unsigned char*>(samp) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
        const DWORD entry = nt->OptionalHeader.AddressOfEntryPoint;
        if (entry == 0x31DF13) { profile = ProfileR1; return true; }
        if (entry == 0x0FDB60) { profile = ProfileDL; return true; }
        return false;
    }

    std::string narrow(const wchar_t* value)
    {
        char buffer[MaxName + 1]{};
        const int count = WideCharToMultiByte(CP_UTF8, 0, value, -1, buffer, static_cast<int>(sizeof(buffer)), nullptr, nullptr);
        return count > 1 ? std::string(buffer, static_cast<std::size_t>(count - 1)) : std::string();
    }

    std::vector<std::string> scanAsiModules(bool& complete)
    {
        std::vector<std::string> names;
        complete = false;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
        if (snapshot == INVALID_HANDLE_VALUE) return names;
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot, &entry))
        {
            do
            {
                if (entry.hModule == selfModule) continue;
                const std::wstring file = lower(entry.szModule);
                if (file.size() < 4 || file.substr(file.size() - 4) != L".asi") continue;
                if (file == L"irxvoice.asi") continue;
                std::string name = narrow(entry.szModule);
                if (name.empty()) name = "unknown.asi";
                if (names.size() < MaxModules) names.push_back(std::move(name));
                else names.push_back("module-list-overflow");
            } while (Module32NextW(snapshot, &entry));
            complete = true;
        }
        CloseHandle(snapshot);
        return names;
    }

    void sendHello()
    {
        if (!Hook::initialized) return;
        RakNet::BitStream stream;
        stream.Write(Magic);
        stream.Write(Protocol);
        stream.Write(MessageHello);
        stream.Write(clientProfile.load());
        Hook::send_rpc(RpcId, &stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0x7F, false);
    }

    void sendReport(std::uint64_t nonce)
    {
        bool complete = false;
        const auto modules = scanAsiModules(complete);
        RakNet::BitStream stream;
        stream.Write(Magic);
        stream.Write(Protocol);
        stream.Write(MessageReport);
        stream.Write(nonce);
        stream.Write(static_cast<unsigned char>(complete ? 1 : 0));
        stream.Write(static_cast<unsigned char>(std::min<std::size_t>(modules.size(), 255)));
        for (const auto& module : modules)
        {
            const std::size_t length = std::min(module.size(), MaxName);
            stream.Write(static_cast<unsigned char>(length));
            if (length) stream.Write(module.data(), static_cast<unsigned int>(length));
        }
        Hook::send_rpc(RpcId, &stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0x7F, false);
    }

    bool onReceiveRpc(unsigned char& id, RakNet::BitStream* stream)
    {
        if (id != RpcId || !stream) return true;
        std::uint32_t magic = 0;
        std::uint16_t protocol = 0;
        unsigned char message = 0;
        std::uint64_t nonce = 0;
        if (!stream->Read(magic) || !stream->Read(protocol) || !stream->Read(message)) return true;
        if (magic != Magic || protocol != Protocol || message != MessageChallenge) return true;
        if (!stream->Read(nonce) || nonce == 0) return true;
        sendReport(nonce);
        return true;
    }

    DWORD WINAPI worker(void*)
    {
        unsigned char profile = 0;
        for (int attempt = 0; attempt < 60 && running.load(); ++attempt)
        {
            if (getClientProfile(profile)) break;
            Sleep(500);
        }
        if (!running.load()) return 0;
        if (!profile)
        {
            MessageBoxA(nullptr, "Unsupported SA-MP/open.mp client build. Supported: SA-MP 0.3.7 R1, SA-MP 0.3.DL R1, and compatible open.mp Client builds.", "DrayvenGUARD", MB_OK | MB_ICONERROR | MB_TOPMOST);
            return 0;
        }
        clientProfile.store(profile);
        if (!Hook::initialize())
        {
            MessageBoxA(nullptr, "DrayvenGUARD could not safely initialize this client network profile.", "DrayvenGUARD", MB_OK | MB_ICONERROR | MB_TOPMOST);
            return 0;
        }
        Hook::on_receive_rpc += onReceiveRpc;
        while (running.load())
        {
            sendHello();
            Sleep(5000);
        }
        Hook::destroy();
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        selfModule = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH)
        running.store(false);
    return TRUE;
}
