// Host-side driver for the mod's Archipelago client (src/archipelago/ap_client.cpp), used by
// client_test.py to run the exact client code against a real MultiServer without the game.
//
// Plain ws:// over POSIX sockets with the mod's own WebSocket codec (the same code path the game
// uses for ws:// servers that aren't on localhost). Commands come in on stdin, one per line:
//   connect <address> <slot> [password]     disconnect
//   check <location id>...                  goal
//   deathlink on|off                        die <cause>
//   say <text>                              quit
// Events go out on stdout as JSON lines.

#include "../../../src/archipelago/ap_client.hpp"
#include "../../../src/archipelago/ws_codec.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

using namespace randomizer::archi;
using nlohmann::json;

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Emit(json event) {
    std::cout << event.dump(-1, ' ', false, json::error_handler_t::replace) << std::endl;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

class PosixWsTransport final : public Transport {
public:
    static std::unique_ptr<Transport> Create(const std::string& url) {
        WsUrl parsed;
        if (!ParseWsUrl(url, parsed) || parsed.secure) {
            return nullptr;  // no TLS in the test driver
        }
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(parsed.host.c_str(), std::to_string(parsed.port).c_str(), &hints, &result) != 0) {
            return nullptr;
        }
        int fd = -1;
        for (auto* ai = result; ai != nullptr; ai = ai->ai_next) {
            fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0) {
                continue;
            }
            if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
                break;
            }
            close(fd);
            fd = -1;
        }
        freeaddrinfo(result);
        auto transport = std::make_unique<PosixWsTransport>();
        if (fd < 0) {
            transport->mPending.push_back({TransportEvent::Type::Closed, "connection refused"});
            transport->mDone = true;
            return transport;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        transport->mFd = fd;
        transport->Write(transport->mCodec.Handshake(parsed));
        return transport;
    }

    ~PosixWsTransport() override { Close(); }

    void Send(const std::string& text) override { Write(mCodec.EncodeText(text)); }

    void Close() override {
        if (mFd >= 0) {
            Write(mCodec.EncodeClose(1000, "bye"));
            close(mFd);
            mFd = -1;
        }
    }

    void Poll(std::vector<TransportEvent>& out) override {
        out.insert(out.end(), mPending.begin(), mPending.end());
        mPending.clear();
        if (mFd < 0 || mDone) {
            return;
        }
        uint8_t buffer[65536];
        while (true) {
            const ssize_t got = recv(mFd, buffer, sizeof(buffer), 0);
            if (got > 0) {
                std::vector<WsClientCodec::Event> events;
                Write(mCodec.Feed(buffer, static_cast<size_t>(got), events));
                for (auto& event : events) {
                    switch (event.type) {
                    case WsClientCodec::EventType::Open:
                        out.push_back({TransportEvent::Type::Open, {}});
                        break;
                    case WsClientCodec::EventType::Text:
                    case WsClientCodec::EventType::Binary:
                        out.push_back({TransportEvent::Type::Message, std::move(event.data)});
                        break;
                    case WsClientCodec::EventType::Closed:
                        out.push_back({TransportEvent::Type::Closed, event.data});
                        mDone = true;
                        return;
                    }
                }
                continue;
            }
            if (got == 0) {
                out.push_back({TransportEvent::Type::Closed, "the server closed the connection"});
                mDone = true;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                out.push_back({TransportEvent::Type::Closed, std::strerror(errno)});
                mDone = true;
            }
            return;
        }
    }

private:
    void Write(const std::string& bytes) {
        size_t sent = 0;
        while (mFd >= 0 && sent < bytes.size()) {
            const ssize_t n = send(mFd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
            if (n > 0) {
                sent += static_cast<size_t>(n);
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                pollfd pfd{mFd, POLLOUT, 0};
                ::poll(&pfd, 1, 100);
            } else {
                return;
            }
        }
    }

    int mFd = -1;
    bool mDone = false;
    WsClientCodec mCodec;
    std::vector<TransportEvent> mPending;
};

class Listener final : public ApListener {
public:
    ApClient* client = nullptr;

    void OnStateChanged(ApState state, const std::string& detail) override {
        Emit({{"event", "state"}, {"state", ApStateName(state)}, {"detail", detail}});
    }
    void OnRoomInfo(const std::string& seed) override { Emit({{"event", "room"}, {"seed_name", seed}}); }
    void OnConnected(const json& slotData) override {
        Emit({{"event", "connected"}, {"slot", client->Slot()}, {"team", client->Team()},
            {"slot_data_keys", [&] {
                json keys = json::array();
                for (const auto& [key, value] : slotData.items()) {
                    keys.push_back(key);
                }
                return keys;
            }()},
            {"slot_data_seed", slotData.value("seed", "")},
            {"slot_data", slotData},
            {"locations", slotData.contains("locations") ? slotData["locations"].size() : 0},
            {"checked", client->ServerCheckedLocations().size()},
            {"missing", client->MissingLocations().size()}});
    }
    void OnItems(const std::vector<NetworkItem>& items) override {
        json list = json::array();
        for (const auto& item : items) {
            list.push_back({item.item, item.location, item.player, item.flags});
        }
        Emit({{"event", "items"}, {"items", list}});
    }
    void OnLocationsChecked() override {
        Emit({{"event", "checked"}, {"count", client->ServerCheckedLocations().size()}});
    }
    void OnMessage(const ApMessage& message) override {
        Emit({{"event", "message"}, {"type", message.type}, {"text", message.Plain()}});
    }
    void OnDeathLink(const std::string& source, const std::string& cause) override {
        Emit({{"event", "deathlink"}, {"source", source}, {"cause", cause}});
    }
};

}  // namespace

int main() {
    Listener listener;
    ApClient client{PosixWsTransport::Create, &listener};
    listener.client = &client;

    // stdin reader thread -> command queue
    std::mutex mutex;
    std::vector<std::string> commands;
    bool eof = false;
    std::thread reader([&] {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::lock_guard lock{mutex};
            commands.push_back(line);
        }
        std::lock_guard lock{mutex};
        eof = true;
    });
    reader.detach();

    while (true) {
        std::vector<std::string> batch;
        bool done = false;
        {
            std::lock_guard lock{mutex};
            batch.swap(commands);
            done = eof;
        }
        for (const auto& line : batch) {
            std::istringstream in{line};
            std::string cmd;
            in >> cmd;
            if (cmd == "connect") {
                ApClientConfig config;
                in >> config.address >> config.slotName;
                in >> config.password;
                config.game = "Twilight Princess Dusklight";
                config.uuid = "client-test";
                config.autoReconnect = true;
                client.Connect(config, NowMs());
            } else if (cmd == "connect-once") {
                ApClientConfig config;
                in >> config.address >> config.slotName;
                in >> config.password;
                config.game = "Twilight Princess Dusklight";
                config.uuid = "client-test";
                config.autoReconnect = false;
                client.Connect(config, NowMs());
            } else if (cmd == "disconnect") {
                client.Disconnect();
            } else if (cmd == "check") {
                std::vector<int64_t> ids;
                int64_t id;
                while (in >> id) {
                    ids.push_back(id);
                }
                client.CheckLocations(ids);
            } else if (cmd == "goal") {
                client.SetGoalReached();
            } else if (cmd == "deathlink") {
                std::string value;
                in >> value;
                client.SetDeathLink(value == "on");
            } else if (cmd == "die") {
                std::string cause;
                std::getline(in, cause);
                if (!cause.empty() && cause.front() == ' ') {
                    cause.erase(0, 1);
                }
                client.SendDeathLink(cause, std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            } else if (cmd == "say") {
                std::string text;
                std::getline(in, text);
                client.Say(text.size() > 1 ? text.substr(1) : text);
            } else if (cmd == "names") {
                int64_t item;
                int slot;
                in >> item >> slot;
                Emit({{"event", "names"}, {"item", client.ItemName(item, slot)},
                    {"player", client.PlayerName(slot)}});
            } else if (cmd == "count") {
                Emit({{"event", "count"}, {"items", client.Items().size()},
                    {"checked", client.ServerCheckedLocations().size()}});
            } else if (cmd == "quit") {
                return 0;
            }
        }
        client.Tick(NowMs());
        if (done && batch.empty()) {
            // keep running until quit; stdin closing alone does not stop the driver
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
