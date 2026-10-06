#include "ws_codec.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstring>

namespace randomizer::archi {

std::string Base64Encode(const uint8_t* data, size_t size) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
        const uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out += kAlphabet[(triple >> 18) & 0x3F];
        out += kAlphabet[(triple >> 12) & 0x3F];
        out += i + 1 < size ? kAlphabet[(triple >> 6) & 0x3F] : '=';
        out += i + 2 < size ? kAlphabet[triple & 0x3F] : '=';
    }
    return out;
}

std::array<uint8_t, 20> Sha1(std::string_view input) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string message{input};
    const uint64_t bitLength = static_cast<uint64_t>(input.size()) * 8;
    message += static_cast<char>(0x80);
    while (message.size() % 64 != 56) {
        message += '\0';
    }
    for (int i = 7; i >= 0; --i) {
        message += static_cast<char>((bitLength >> (i * 8)) & 0xFF);
    }
    auto rotl = [](uint32_t value, int bits) { return (value << bits) | (value >> (32 - bits)); };
    for (size_t chunk = 0; chunk < message.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const uint8_t*>(message.data() + chunk + i * 4);
            w[i] = (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<uint8_t, 20> digest{};
    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
    return digest;
}

bool ParseWsUrl(std::string_view url, WsUrl& out) {
    out = WsUrl{};
    if (url.starts_with("wss://")) {
        out.secure = true;
        url.remove_prefix(6);
    } else if (url.starts_with("ws://")) {
        url.remove_prefix(5);
    } else {
        return false;
    }
    const size_t pathStart = url.find('/');
    std::string_view authority = url.substr(0, pathStart);
    if (pathStart != std::string_view::npos) {
        out.path = std::string{url.substr(pathStart)};
    }
    std::string_view portText;
    if (authority.starts_with('[')) {
        const size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return false;
        }
        out.host = std::string{authority.substr(1, close - 1)};
        authority.remove_prefix(close + 1);
        if (authority.starts_with(':')) {
            portText = authority.substr(1);
        } else if (!authority.empty()) {
            return false;
        }
    } else {
        const size_t colon = authority.rfind(':');
        out.host = std::string{authority.substr(0, colon)};
        if (colon != std::string_view::npos) {
            portText = authority.substr(colon + 1);
        }
    }
    if (out.host.empty()) {
        return false;
    }
    if (portText.empty()) {
        out.port = out.secure ? 443 : 80;
        return true;
    }
    unsigned port = 0;
    const auto [end, error] = std::from_chars(portText.data(), portText.data() + portText.size(), port);
    if (error != std::errc{} || end != portText.data() + portText.size() || port == 0 || port > 65535) {
        return false;
    }
    out.port = static_cast<uint16_t>(port);
    return true;
}

WsClientCodec::WsClientCodec(size_t maxMessageBytes)
    : mMaxMessageBytes{maxMessageBytes}, mRandom{std::random_device{}()} {}

std::string WsClientCodec::Handshake(const WsUrl& url) {
    std::array<uint8_t, 16> nonce{};
    for (auto& byte : nonce) {
        byte = static_cast<uint8_t>(mRandom());
    }
    mKey = Base64Encode(nonce.data(), nonce.size());
    const bool ipv6 = url.host.find(':') != std::string::npos;
    std::string host = ipv6 ? "[" + url.host + "]" : url.host;
    if (url.port != (url.secure ? 443 : 80)) {
        host += ":" + std::to_string(url.port);
    }
    return "GET " + url.path + " HTTP/1.1\r\n"
           "Host: " + host + "\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: " + mKey + "\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "User-Agent: Dusklight-Archipelago\r\n"
           "\r\n";
}

void WsClientCodec::Fail(std::string reason, std::vector<Event>& events) {
    if (mState != State::Closed) {
        mState = State::Closed;
        events.push_back({EventType::Closed, std::move(reason), 1002});
    }
}

static std::string Lowercase(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

std::string WsClientCodec::Feed(const uint8_t* data, size_t size, std::vector<Event>& events) {
    std::string reply;
    if (mState == State::Closed) {
        return reply;
    }
    mBuffer.append(reinterpret_cast<const char*>(data), size);

    if (mState == State::Handshake) {
        const size_t end = mBuffer.find("\r\n\r\n");
        if (end == std::string::npos) {
            if (mBuffer.size() > 16384) {
                Fail("The server's handshake response is too large", events);
            }
            return reply;
        }
        const std::string head = mBuffer.substr(0, end);
        mBuffer.erase(0, end + 4);
        const size_t lineEnd = head.find("\r\n");
        const std::string status = head.substr(0, lineEnd);
        if (status.size() < 12 || !status.starts_with("HTTP/1.") || status.substr(9, 3) != "101") {
            Fail("The server refused the WebSocket connection (" + status + ")", events);
            return reply;
        }
        const auto digest = Sha1(mKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        const std::string expected = Base64Encode(digest.data(), digest.size());
        bool acceptOk = false;
        size_t pos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
        while (pos < head.size()) {
            size_t next = head.find("\r\n", pos);
            if (next == std::string::npos) {
                next = head.size();
            }
            const std::string_view line{head.data() + pos, next - pos};
            const size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                const std::string name = Lowercase(line.substr(0, colon));
                std::string_view value = line.substr(colon + 1);
                while (!value.empty() && value.front() == ' ') {
                    value.remove_prefix(1);
                }
                while (!value.empty() && value.back() == ' ') {
                    value.remove_suffix(1);
                }
                if (name == "sec-websocket-accept") {
                    acceptOk = value == expected;
                }
            }
            pos = next + 2;
        }
        if (!acceptOk) {
            Fail("The server's WebSocket handshake was invalid", events);
            return reply;
        }
        mState = State::Open;
        events.push_back({EventType::Open, {}, 0});
    }

    while (mState == State::Open) {
        if (mBuffer.size() < 2) {
            break;
        }
        const auto* bytes = reinterpret_cast<const uint8_t*>(mBuffer.data());
        const bool fin = (bytes[0] & 0x80) != 0;
        const uint8_t opcode = bytes[0] & 0x0F;
        const bool masked = (bytes[1] & 0x80) != 0;
        uint64_t length = bytes[1] & 0x7F;
        size_t header = 2;
        if (length == 126) {
            if (mBuffer.size() < 4) {
                break;
            }
            length = (uint64_t{bytes[2]} << 8) | bytes[3];
            header = 4;
        } else if (length == 127) {
            if (mBuffer.size() < 10) {
                break;
            }
            length = 0;
            for (int i = 0; i < 8; ++i) {
                length = (length << 8) | bytes[2 + i];
            }
            header = 10;
        }
        if (length > mMaxMessageBytes) {
            Fail("A message from the server is too large", events);
            break;
        }
        const size_t maskOffset = header;
        if (masked) {
            header += 4;
        }
        if (mBuffer.size() < header + length) {
            break;
        }
        std::string payload = mBuffer.substr(header, length);
        if (masked) {
            for (size_t i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(payload[i] ^ bytes[maskOffset + (i % 4)]);
            }
        }
        mBuffer.erase(0, header + length);

        switch (opcode) {
        case 0x0:  // continuation
        case 0x1:  // text
        case 0x2:  // binary
            if (opcode != 0) {
                if (!mFragments.empty() || mFragmentOpcode != 0) {
                    Fail("The server interleaved WebSocket messages", events);
                    return reply;
                }
                mFragmentOpcode = opcode;
            } else if (mFragmentOpcode == 0) {
                Fail("Unexpected WebSocket continuation frame", events);
                return reply;
            }
            mFragments += payload;
            if (mFragments.size() > mMaxMessageBytes) {
                Fail("A message from the server is too large", events);
                return reply;
            }
            if (fin) {
                events.push_back({mFragmentOpcode == 0x1 ? EventType::Text : EventType::Binary,
                    std::move(mFragments), 0});
                mFragments.clear();
                mFragmentOpcode = 0;
            }
            break;
        case 0x8: {  // close
            uint16_t code = 1005;
            std::string reason;
            if (payload.size() >= 2) {
                code = static_cast<uint16_t>((static_cast<uint8_t>(payload[0]) << 8) |
                                             static_cast<uint8_t>(payload[1]));
                reason = payload.substr(2);
            }
            reply += EncodeFrame(0x8, payload.substr(0, std::min<size_t>(payload.size(), 2)));
            mState = State::Closed;
            events.push_back({EventType::Closed,
                reason.empty() ? "The server closed the connection" : reason, code});
            return reply;
        }
        case 0x9:  // ping
            reply += EncodeFrame(0xA, payload);
            break;
        case 0xA:  // pong
            break;
        default:
            Fail("Unknown WebSocket frame type", events);
            return reply;
        }
    }
    return reply;
}

std::string WsClientCodec::EncodeFrame(uint8_t opcode, std::string_view payload) {
    std::string frame;
    frame.reserve(payload.size() + 14);
    frame += static_cast<char>(0x80 | opcode);
    if (payload.size() < 126) {
        frame += static_cast<char>(0x80 | payload.size());
    } else if (payload.size() <= 0xFFFF) {
        frame += static_cast<char>(0x80 | 126);
        frame += static_cast<char>((payload.size() >> 8) & 0xFF);
        frame += static_cast<char>(payload.size() & 0xFF);
    } else {
        frame += static_cast<char>(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            frame += static_cast<char>((static_cast<uint64_t>(payload.size()) >> (i * 8)) & 0xFF);
        }
    }
    std::array<uint8_t, 4> mask{};
    for (auto& byte : mask) {
        byte = static_cast<uint8_t>(mRandom());
    }
    frame.append(reinterpret_cast<const char*>(mask.data()), mask.size());
    const size_t start = frame.size();
    frame.append(payload);
    for (size_t i = 0; i < payload.size(); ++i) {
        frame[start + i] = static_cast<char>(frame[start + i] ^ mask[i % 4]);
    }
    return frame;
}

std::string WsClientCodec::EncodeText(std::string_view text) {
    return mState == State::Open ? EncodeFrame(0x1, text) : std::string{};
}

std::string WsClientCodec::EncodeClose(uint16_t code, std::string_view reason) {
    if (mState != State::Open) {
        return {};
    }
    std::string payload;
    payload += static_cast<char>(code >> 8);
    payload += static_cast<char>(code & 0xFF);
    payload += reason.substr(0, 120);
    mState = State::Closed;
    return EncodeFrame(0x8, payload);
}

}  // namespace randomizer::archi
