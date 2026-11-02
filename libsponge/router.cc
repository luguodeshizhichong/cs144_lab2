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

    // 保存这条转发规则;路由表按路由添加的先后顺序存放
    _routes.push_back(RouteEntry{route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // 生存时间已耗尽(或即将耗尽)的数据报不得转发
    if (dgram.header().ttl <= 1) {
        return;
    }

    // 最长前缀匹配:在所有匹配的路由中保留前缀最长(最具体)的一条
    const uint32_t dst = dgram.header().dst;
    const RouteEntry *best_route = nullptr;
    for (const RouteEntry &route : _routes) {
        if (route.prefix_length > 32) {
            continue;  // 非法的路由:它永远无法匹配任何地址
        }

        // 前缀长度为 0 表示匹配任意目的地址(默认路由);
        // 否则要求高 prefix_length 位完全相同
        const bool matches = route.prefix_length == 0 or
                             (dst >> (32 - route.prefix_length)) == (route.route_prefix >> (32 - route.prefix_length));

        if (matches and (best_route == nullptr or route.prefix_length > best_route->prefix_length)) {
            best_route = &route;
        }
    }

    if (best_route == nullptr) {
        return;  // 没有通往该目的地的路由:丢弃这个数据报
    }

    // 转发该数据报,剩余跳数减一
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
