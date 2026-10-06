#include "ap_transport.hpp"

#include "ws_codec.hpp"

#include <mods/svc/log.hpp>
#include <mods/svc/net.hpp>
#include <mods/svc/websocket.hpp>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace randomizer::archi::transport {
namespace {

constexpr size_t kMaxMessageBytes = 16u << 20;  // WebSocket service maximum

class ServiceWsTransport;
class NetWsTransport;

std::unordered_map<WebSocketHandle, ServiceWsTransport*> s_wsTransports;
std::unordered_map<NetHandle, NetWsTransport*> s_netTransports;

std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

bool IsLocalHost(const std::string& host) {
    const std::string lower = Lower(host);
    return lower == "localhost" || lower == "127.0.0.1" || lower == "::1";
}

// wss://, and ws:// to localhost, through the WebSocket service
class ServiceWsTransport final : public Transport {
public:
    explicit ServiceWsTransport(const std::string& url) {
        mods::ws::Options options;
        options.url = url;
        options.connectTimeoutMs = 10000;
        options.closeTimeoutMs = 2000;
        options.keepaliveIntervalMs = 15000;
        options.maxMessageBytes = kMaxMessageBytes;
        mConnection = mods::ws::connect(options);
        if (mConnection) {
            s_wsTransports[mConnection.handle()] = this;
        }
    }

    ~ServiceWsTransport() override { Close(); }

    bool Valid() const { return static_cast<bool>(mConnection); }

    void Send(const std::string& text) override {
        if (mOpen) {
            mConnection.send_text(text);
        }
    }

    void Close() override {
        if (mConnection.handle() != 0) {
            s_wsTransports.erase(mConnection.handle());
            mConnection.close(1000, "closing");
        }
        mOpen = false;
    }

    void Poll(std::vector<TransportEvent>& out) override {
        out.insert(out.end(), std::make_move_iterator(mEvents.begin()), std::make_move_iterator(mEvents.end()));
        mEvents.clear();
    }

    void OnEvent(const mods::ws::Event& event) {
        switch (event.type) {
        case WEBSOCKET_EVENT_OPEN:
            mOpen = true;
            mEvents.push_back({TransportEvent::Type::Open, {}});
            break;
        case WEBSOCKET_EVENT_MESSAGE:
            if (event.messageKind == WEBSOCKET_MESSAGE_TEXT) {
                mEvents.push_back({TransportEvent::Type::Message,
                    std::string{reinterpret_cast<const char*>(event.data.data()), event.data.size()}});
            }
            break;
        case WEBSOCKET_EVENT_CLOSED: {
            std::string reason{event.message};
            if (reason.empty()) {
                reason = std::string{event.closeReason};
            }
            if (reason.empty()) {
                reason = event.handshakeStatus != 0 ?
                             "the server answered " + std::to_string(event.handshakeStatus) :
                             "the connection closed";
            }
            s_wsTransports.erase(mConnection.handle());
            mConnection.detach();
            mOpen = false;
            mEvents.push_back({TransportEvent::Type::Closed, std::move(reason)});
            break;
        }
        default:
            break;
        }
    }

private:
    mods::ws::Connection mConnection;
    std::vector<TransportEvent> mEvents;
    bool mOpen = false;
};

// ws:// to other hosts: raw TCP from the net service + our WebSocket codec
class NetWsTransport final : public Transport {
public:
    explicit NetWsTransport(const WsUrl& url) : mUrl{url} {
        if (svc_net == nullptr) {
            return;
        }
        const bool ipv6 = url.host.find(':') != std::string::npos;
        const std::string endpoint = "tcp://" + (ipv6 ? "[" + url.host + "]" : url.host) + ":" +
                                     std::to_string(url.port);
        NetConnectDesc desc = NET_CONNECT_DESC_INIT;
        desc.endpoint = endpoint.c_str();
        desc.connect_timeout_ms = 10000;
        desc.close_timeout_ms = 2000;
        desc.max_send_queue_bytes = 4u << 20;
        desc.no_delay = true;
        if (svc_net->connect(mod_ctx, &desc, &mHandle) == MOD_OK && mHandle != 0) {
            s_netTransports[mHandle] = this;
        } else {
            mHandle = 0;
        }
    }

    ~NetWsTransport() override { Close(); }

    bool Valid() const { return mHandle != 0; }

    void Send(const std::string& text) override { Write(mCodec.EncodeText(text)); }

    void Close() override {
        if (mHandle != 0) {
            Write(mCodec.EncodeClose(1000, "closing"));
            s_netTransports.erase(mHandle);
            svc_net->close(mod_ctx, mHandle);
            mHandle = 0;
        }
    }

    void Poll(std::vector<TransportEvent>& out) override {
        out.insert(out.end(), std::make_move_iterator(mEvents.begin()), std::make_move_iterator(mEvents.end()));
        mEvents.clear();
    }

    void OnEvent(const NetEvent& event) {
        switch (event.type) {
        case NET_EVENT_CONNECTED:
            Write(mCodec.Handshake(mUrl));
            break;
        case NET_EVENT_STREAM_DATA: {
            std::vector<WsClientCodec::Event> events;
            Write(mCodec.Feed(static_cast<const uint8_t*>(event.data), event.size, events));
            for (auto& ws : events) {
                switch (ws.type) {
                case WsClientCodec::EventType::Open:
                    mEvents.push_back({TransportEvent::Type::Open, {}});
                    break;
                case WsClientCodec::EventType::Text:
                    mEvents.push_back({TransportEvent::Type::Message, std::move(ws.data)});
                    break;
                case WsClientCodec::EventType::Binary:
                    break;
                case WsClientCodec::EventType::Closed:
                    Finish(ws.data);
                    return;
                }
            }
            break;
        }
        case NET_EVENT_CLOSED: {
            std::string reason = event.error_message != nullptr ? event.error_message : "";
            if (reason.empty()) {
                reason = "the server closed the connection";
            }
            s_netTransports.erase(mHandle);
            mHandle = 0;  // the handle is already invalid
            Finish(reason);
            break;
        }
        default:
            break;
        }
    }

private:
    void Write(const std::string& bytes) {
        if (mHandle != 0 && !bytes.empty()) {
            svc_net->send(mod_ctx, mHandle, bytes.data(), bytes.size());
        }
    }

    void Finish(const std::string& reason) {
        if (!mFinished) {
            mFinished = true;
            mEvents.push_back({TransportEvent::Type::Closed, reason});
        }
        if (mHandle != 0) {
            s_netTransports.erase(mHandle);
            svc_net->close(mod_ctx, mHandle);
            mHandle = 0;
        }
    }

    WsUrl mUrl;
    NetHandle mHandle = 0;
    WsClientCodec mCodec{kMaxMessageBytes * 2};
    std::vector<TransportEvent> mEvents;
    bool mFinished = false;
};

}  // namespace

std::unique_ptr<Transport> Create(const std::string& url) {
    WsUrl parsed;
    if (!ParseWsUrl(url, parsed)) {
        return nullptr;
    }
    if (parsed.secure || IsLocalHost(parsed.host)) {
        if (svc_websocket == nullptr) {
            return nullptr;
        }
        auto transport = std::make_unique<ServiceWsTransport>(url);
        if (!transport->Valid()) {
            mods::log::warn("Archipelago: the WebSocket service could not open {}", url);
            return nullptr;
        }
        return transport;
    }
    auto transport = std::make_unique<NetWsTransport>(parsed);
    if (!transport->Valid()) {
        mods::log::warn("Archipelago: the net service could not open {}", url);
        return nullptr;
    }
    return transport;
}

void Pump() {
    if (svc_websocket != nullptr) {
        mods::ws::Event event;
        while (mods::ws::poll(event)) {
            if (const auto it = s_wsTransports.find(event.handle); it != s_wsTransports.end()) {
                it->second->OnEvent(event);
            }
        }
    }
    if (svc_net != nullptr) {
        while (true) {
            NetEvent event = NET_EVENT_INIT;
            if (svc_net->poll_event(mod_ctx, &event) != MOD_OK || event.type == NET_EVENT_NONE) {
                break;
            }
            if (const auto it = s_netTransports.find(event.handle); it != s_netTransports.end()) {
                it->second->OnEvent(event);
            }
        }
    }
}

}  // namespace randomizer::archi::transport
