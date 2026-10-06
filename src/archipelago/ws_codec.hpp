#pragma once

// A small WebSocket (RFC 6455) client codec over a plain byte stream.
//
// Dusklight's WebSocket service only allows ws:// to localhost, but self-hosted Archipelago
// servers on a LAN or the internet are often plain ws://. This codec runs the WebSocket protocol
// over a raw TCP stream (Dusklight's net service in game, POSIX sockets in the host tests).
// It has no I/O of its own: feed it received bytes, send what it hands back.

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace randomizer::archi {

std::string Base64Encode(const uint8_t* data, size_t size);
std::array<uint8_t, 20> Sha1(std::string_view data);

struct WsUrl {
    bool secure = false;
    std::string host;
    uint16_t port = 0;
    std::string path = "/";
};

/// Parses ws://host:port/path or wss://... (IPv6 hosts in brackets). Returns false if invalid.
bool ParseWsUrl(std::string_view url, WsUrl& out);

class WsClientCodec {
public:
    enum class EventType { Open, Text, Binary, Closed };

    struct Event {
        EventType type;
        std::string data;  // message payload, or the close/failure reason
        uint16_t closeCode = 0;
    };

    explicit WsClientCodec(size_t maxMessageBytes = 32u << 20);

    /// The HTTP upgrade request to send right after the TCP connection opens.
    std::string Handshake(const WsUrl& url);

    /// Feeds bytes received from the server. Returns bytes that must be sent back (pongs,
    /// close replies); events are appended to `events`.
    std::string Feed(const uint8_t* data, size_t size, std::vector<Event>& events);

    std::string EncodeText(std::string_view text);
    std::string EncodeClose(uint16_t code, std::string_view reason);

    bool IsOpen() const { return mState == State::Open; }
    bool IsClosed() const { return mState == State::Closed; }

private:
    enum class State { Handshake, Open, Closed };

    std::string EncodeFrame(uint8_t opcode, std::string_view payload);
    void Fail(std::string reason, std::vector<Event>& events);

    State mState = State::Handshake;
    std::string mKey;
    std::string mBuffer;
    std::string mFragments;
    uint8_t mFragmentOpcode = 0;
    size_t mMaxMessageBytes;
    std::mt19937 mRandom;
};

}  // namespace randomizer::archi
