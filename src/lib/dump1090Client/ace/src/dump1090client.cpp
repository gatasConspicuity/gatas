#include <cctype>

#include "../dump1090client.hpp"
#include "ace/coreutils.hpp"

GATAS::PostConstruct Dump1090Client::postConstruct()
{
    return tcpClient.postConstruct();
}

void Dump1090Client::start()
{
    getBus().subscribe(*this);
};

void Dump1090Client::processNewSentence(etl::span<uint8_t> data)
{
    constexpr uint8_t frameLength = GATAS::ADSBMessageBinMsg::MAX_BINARY_LENGTH;
    constexpr uint8_t hexLength = frameLength * 2;
    // The line parser appends a NUL after the dump1090 line's semicolon.
    if (data.size() != hexLength + 3 || data.front() != '*' || data[hexLength + 1] != ';' || data.back() != 0)
    {
        return;
    }
    const auto firstByteLowNibble = std::toupper(data[2]);
    if (data[1] != '8' || (firstByteLowNibble != 'D' && firstByteLowNibble != 'A' && firstByteLowNibble != '0'))
    {
        return;
    }
    const auto hex = data.subspan(1, hexLength);
    if (!etl::all_of(hex.begin(), hex.end(), [](uint8_t c) { return std::isxdigit(c) != 0; }))
    {
        return;
    }

    auto &pool = getGlobalPool();
    auto *frame = static_cast<uint8_t *>(pool.alloc(frameLength));
    if (frame == nullptr)
    {
        ++statistics.poolDrops;
        return;
    }
    CoreUtils::hexStrToByteArray(reinterpret_cast<const char *>(hex.data()), hexLength, frame);
    getBus().receive(GATAS::ADSBMessageBinMsg{pool, frame, frameLength, GATAS::TrafficIngress::Dump1090});
    statistics.totalReceived += 1;
}

void Dump1090Client::getData(etl::string_stream &stream, const etl::string_view path) const
{
    (void)path;
    stream << "{";
    stream << "\"totalReceived:k\":" << statistics.totalReceived;
    stream << ",\"poolDrops:err\":" << statistics.poolDrops;
    stream << ",\"reConnects\":" << statistics.reConnects;
    stream << "}";
}

void Dump1090Client::on_receive_unknown(const etl::imessage &msg)
{
    (void)msg;
}

void Dump1090Client::on_receive(const GATAS::WifiConnectionStateMsg &wcs)
{
    wifiConnected = wcs.wifiMode != GATAS::WifiMode::NC;
    networkAddress = wcs.gatasIp & 0xFFFFFF;
}
