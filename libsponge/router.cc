#include "router.hh"

#include "ipv4_datagram.hh"

#include <iostream>

using namespace std;

// Implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // store the forwarding rule; the routing table keeps the order in which routes were added
    _routes.push_back(RouteEntry{route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // a datagram whose time-to-live has run out (or is about to) must not be forwarded
    if (dgram.header().ttl <= 1) {
        return;
    }

    // longest prefix match: among all matching routes, keep the most specific one
    const uint32_t dst = dgram.header().dst;
    const RouteEntry *best_route = nullptr;
    for (const RouteEntry &route : _routes) {
        if (route.prefix_length > 32) {
            continue;  // malformed route: it can never match
        }

        // a prefix length of zero matches every destination address (default route);
        // otherwise the high-order `prefix_length` bits must be identical
        const bool matches = route.prefix_length == 0 or
                             (dst >> (32 - route.prefix_length)) == (route.route_prefix >> (32 - route.prefix_length));

        if (matches and (best_route == nullptr or route.prefix_length > best_route->prefix_length)) {
            best_route = &route;
        }
    }

    if (best_route == nullptr) {
        return;  // no route to this destination: drop the datagram
    }

    // forward the datagram, with one less hop left
    dgram.header().ttl -= 1;

    const Address next_hop =
        best_route->next_hop.has_value() ? best_route->next_hop.value() : Address::from_ipv4_numeric(dst);

    _interfaces.at(best_route->interface_num).send_datagram(dgram, next_hop);
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
