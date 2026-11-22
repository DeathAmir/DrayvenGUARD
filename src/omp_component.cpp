#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <sdk.hpp>
#include <network.hpp>
#include <player.hpp>
#include <bitstream.hpp>

namespace
{
    constexpr int ReportRpc = 221;
    constexpr std::uint32_t Magic = 0x44475244;
    constexpr std::uint16_t Protocol = 1;
    constexpr unsigned char MessageHello = 1;
    constexpr unsigned char MessageChallenge = 2;
    constexpr unsigned char MessageReport = 3;
    constexpr unsigned char ProfileR1 = 1;
    constexpr unsigned char ProfileDL = 2;
    constexpr unsigned char ProfileOpenMP = 3;
    constexpr unsigned char MaxModules = 16;
    constexpr unsigned char MaxName = 63;
    constexpr auto ReportTimeout = std::chrono::seconds(20);

    struct PlayerState
    {
        std::uint64_t nonce = 0;
        std::chrono::steady_clock::time_point connectedAt{};
        std::chrono::steady_clock::time_point lastReportAt{};
        bool verified = false;
        bool kicked = false;
        unsigned char profile = 0;
    };

    class DrayvenGuardComponent final : public IComponent, public PlayerConnectEventHandler, public CoreEventHandler, public SingleNetworkInEventHandler
    {
    public:
        PROVIDE_UID(0x6472617976656e31);
        StringView componentName() const override { return "DrayvenGUARD"; }
        SemanticVersion componentVersion() const override { return SemanticVersion(1, 1, 0, 0); }

        void onLoad(ICore* core) override
        {
            core_ = core;
            core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);
            core_->getEventDispatcher().addEventHandler(this);
            core_->addPerRPCInEventHandler<ReportRpc>(this);
            core_->printLn("[DrayvenGUARD] Native open.mp RPC component loaded.");
        }

        void onInit(IComponentList*) override {}
        void onReady() override {}
        void onFree(IComponent*) override {}
        void free() override { delete this; }
        void reset() override { for (auto& state : states_) state = PlayerState{}; }

        void onPlayerConnect(IPlayer& player) override
        {
            auto& state = states_[static_cast<std::size_t>(player.getID())];
            state = PlayerState{};
            state.connectedAt = std::chrono::steady_clock::now();
        }

        void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason) override
        {
            states_[static_cast<std::size_t>(player.getID())] = PlayerState{};
        }

        void onTick(Microseconds, TimePoint) override
        {
            const auto now = std::chrono::steady_clock::now();
            for (IPlayer* player : core_->getPlayers().players())
            {
                if (!player) continue;
                auto& state = states_[static_cast<std::size_t>(player->getID())];
                if (state.kicked)
                    continue;
                if (state.connectedAt.time_since_epoch().count() == 0)
                    continue;
                if (!state.verified && now - state.connectedAt > ReportTimeout)
                    reject(*player, "Client version unsupported or DrayvenGUARD ASI is missing.");
                else if (state.verified && state.lastReportAt.time_since_epoch().count() != 0 && now - state.lastReportAt > std::chrono::seconds(15))
                    reject(*player, "DrayvenGUARD client scan timed out.");
            }
        }

        bool onReceive(IPlayer& player, NetworkBitStream& stream) override
        {
            std::uint32_t magic = 0;
            std::uint16_t protocol = 0;
            unsigned char message = 0;
            if (!stream.readUINT32(magic) || !stream.readUINT16(protocol) || !stream.readUINT8(message)) return false;
            if (magic != Magic || protocol != Protocol) return false;

            auto& state = states_[static_cast<std::size_t>(player.getID())];
            if (message == MessageHello)
            {
                unsigned char profile = 0;
                if (!stream.readUINT8(profile) || (profile != ProfileR1 && profile != ProfileDL && profile != ProfileOpenMP)) return false;
                const ClientVersion version = player.getClientVersion();
                if (version != ClientVersion::ClientVersion_SAMP_037 && version != ClientVersion::ClientVersion_SAMP_03DL && version != ClientVersion::ClientVersion_openmp)
                {
                    reject(player, "Client version unsupported.");
                    return false;
                }
                if (version == ClientVersion::ClientVersion_SAMP_03DL && profile != ProfileDL)
                {
                    reject(player, "Client version unsupported: expected SA-MP 0.3.DL R1.");
                    return false;
                }
                if (version == ClientVersion::ClientVersion_SAMP_037 && profile != ProfileR1)
                {
                    reject(player, "Client version unsupported: expected SA-MP 0.3.7 R1.");
                    return false;
                }
                state.profile = profile;
                state.nonce = makeNonce();
                NetworkBitStream response;
                response.writeUINT32(Magic);
                response.writeUINT16(Protocol);
                response.writeUINT8(MessageChallenge);
                response.writeUINT64(state.nonce);
                player.sendRPC(ReportRpc, Span<uint8_t>(response.GetData(), response.GetNumberOfBitsUsed()), OrderingChannel_SyncRPC);
                return true;
            }

            if (message != MessageReport) return false;
            std::uint64_t nonce = 0;
            unsigned char complete = 0;
            unsigned char count = 0;
            if (!stream.readUINT64(nonce) || !stream.readUINT8(complete) || !stream.readUINT8(count)) return false;
            if (!nonce || nonce != state.nonce || complete != 1 || count > MaxModules)
            {
                reject(player, "Invalid DrayvenGUARD client report.");
                return false;
            }

            std::vector<std::string> unauthorized;
            for (unsigned char i = 0; i < count; ++i)
            {
                HybridString<MaxName> module;
                if (!stream.readDynStr8(module) || module.length() == 0 || module.length() > MaxName)
                {
                    reject(player, "Malformed DrayvenGUARD module report.");
                    return false;
                }
                if (!equalsIgnoreCase(module, "iRxvoice.asi"))
                    unauthorized.emplace_back(module.data(), module.length());
            }

            if (stream.GetNumberOfUnreadBits() != 0)
            {
                reject(player, "Malformed DrayvenGUARD module report.");
                return false;
            }

            if (!unauthorized.empty())
            {
                std::string list;
                for (const auto& module : unauthorized) { if (!list.empty()) list += ", "; list += module; }
                core_->printLn("[DrayvenGUARD] Player %d unauthorized ASI: %s", player.getID(), list.c_str());
                reject(player, "Lotfan File Haye Ghir Mojhaz Inject Ya Load Nakonid");
                return false;
            }

            state.verified = true;
            state.lastReportAt = std::chrono::steady_clock::now();
            state.nonce = 0;
            core_->printLn("[DrayvenGUARD] Player %d passed ASI scan (profile %u).", player.getID(), state.profile);
            return true;
        }

    private:
        ICore* core_ = nullptr;
        std::array<PlayerState, PLAYER_POOL_SIZE> states_{};
        std::mt19937_64 random_{std::random_device{}()};

        std::uint64_t makeNonce()
        {
            const std::uint64_t value = random_();
            return value ? value : 1;
        }

        static bool equalsIgnoreCase(StringView left, const char* right)
        {
            if (left.length() != std::char_traits<char>::length(right)) return false;
            for (std::size_t i = 0; i < left.length(); ++i)
            {
                char a = left[i], b = right[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                if (a != b) return false;
            }
            return true;
        }

        void reject(IPlayer& player, const char* reason)
        {
            auto& state = states_[static_cast<std::size_t>(player.getID())];
            if (state.kicked) return;
            state.kicked = true;
            player.sendClientMessage(Colour(255, 64, 64, 255), reason);
            core_->printLn("[DrayvenGUARD] Kicking player %d: %s", player.getID(), reason);
            player.kick();
        }
    };

    DrayvenGuardComponent* instance = nullptr;
}

COMPONENT_ENTRY_POINT()
{
    if (!instance) instance = new DrayvenGuardComponent();
    return instance;
}
