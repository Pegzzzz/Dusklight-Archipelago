#pragma once

// Archipelago connections through Dusklight's network services.
//
//  - wss:// (any host) and ws:// to this computer: Dusklight's WebSocket service.
//  - ws:// to other hosts (LAN or self-hosted servers without TLS), which the WebSocket service
//    refuses: a TCP stream from the net service with the WebSocket protocol on top (ws_codec).

#include "ap_client.hpp"

namespace randomizer::archi::transport {

/// Creates the transport for a URL, or nullptr when no service can reach it.
std::unique_ptr<Transport> Create(const std::string& url);

/// Reads the services' event queues and hands events to the open transports.
/// Call once per frame, before ApClient::Tick().
void Pump();

}  // namespace randomizer::archi::transport
