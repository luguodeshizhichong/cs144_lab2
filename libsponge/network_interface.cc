#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"

#include <iostream>
#include <utility>

using namespace std;

// Implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

void NetworkInterface::_push_frame(const EthernetAddress &dst, const uint16_t type, const BufferList &payload) {
    EthernetFrame frame;
    frame.header().src = _ethernet_address;
    frame.header().dst = dst;
    frame.header().type = type;
    frame.payload() = payload;
    _frames_out.push(std::move(frame));
}

void NetworkInterface::_send_arp_request(const uint32_t target_ip) {
    ARPMessage arp_request;
    arp_request.opcode = ARPMessage::OPCODE_REQUEST;
    arp_request.sender_ethernet_address = _ethernet_address;
    arp_request.sender_ip_address = _ip_address.ipv4_numeric();
    arp_request.target_ethernet_address = {};  // 未知:请求时目标以太网地址填全 0
    arp_request.target_ip_address = target_ip;

    _push_frame(ETHERNET_BROADCAST, EthernetHeader::TYPE_ARP, BufferList{arp_request.serialize()});
}

void NetworkInterface::_flush_pending(const uint32_t next_hop_ip) {
    const auto pending_it = _pending_datagrams.find(next_hop_ip);
    if (pending_it == _pending_datagrams.end()) {
        return;
    }

    const auto cache_it = _arp_cache.find(next_hop_ip);
    if (cache_it == _arp_cache.end()) {
        return;
    }

    for (const InternetDatagram &dgram : pending_it->second) {
        _push_frame(cache_it->second.ethernet_address, EthernetHeader::TYPE_IPv4, dgram.serialize());
    }

    _pending_datagrams.erase(pending_it);
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // 已缓存该映射:直接把数据报封装成帧发出
    const auto cache_it = _arp_cache.find(next_hop_ip);
    if (cache_it != _arp_cache.end()) {
        _push_frame(cache_it->second.ethernet_address, EthernetHeader::TYPE_IPv4, dgram.serialize());
        return;
    }

    // 映射未知:先把数据报挂起,等 ARP 应答到达后再发出
    _pending_datagrams[next_hop_ip].push_back(dgram);

    // 但不要淹没网络:对同一 IP 地址最多每 5 秒发一次 ARP 请求
    if (_arp_requests.count(next_hop_ip) == 0) {
        _send_arp_request(next_hop_ip);
        _arp_requests[next_hop_ip] = ARP_REQUEST_INTERVAL_MS;
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // 丢弃既不是发给本机、也不是广播地址的帧
    if (frame.header().dst != _ethernet_address and frame.header().dst != ETHERNET_BROADCAST) {
        return {};
    }

    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload().concatenate()) != ParseResult::NoError) {
            return {};
        }
        return dgram;
    }

    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp;
        if (arp.parse(frame.payload().concatenate()) != ParseResult::NoError) {
            return {};
        }

        // 学习(或刷新)发送方的映射,有效期 30 秒
        _arp_cache[arp.sender_ip_address] = ARPCacheEntry{arp.sender_ethernet_address, ARP_CACHE_TTL_MS};
        _arp_requests.erase(arp.sender_ip_address);

        // 若请求问的是本机的 IP 地址,则予以应答
        if (arp.opcode == ARPMessage::OPCODE_REQUEST and arp.target_ip_address == _ip_address.ipv4_numeric()) {
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ethernet_address = arp.sender_ethernet_address;
            arp_reply.target_ip_address = arp.sender_ip_address;

            _push_frame(arp.sender_ethernet_address, EthernetHeader::TYPE_ARP, BufferList{arp_reply.serialize()});
        }

        // 现在已知发送方的地址:把等待该地址的数据报全部发出
        _flush_pending(arp.sender_ip_address);
    }

    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    for (auto it = _arp_cache.begin(); it != _arp_cache.end();) {
        if (ms_since_last_tick >= it->second.ttl_ms) {
            it = _arp_cache.erase(it);  // 映射已过期:下一个数据报将触发新的 ARP 查询
        } else {
            it->second.ttl_ms -= ms_since_last_tick;
            ++it;
        }
    }

    for (auto it = _arp_requests.begin(); it != _arp_requests.end();) {
        if (ms_since_last_tick >= it->second) {
            it = _arp_requests.erase(it);  // 抑制期已过:可以再次发送 ARP 请求
        } else {
            it->second -= ms_since_last_tick;
            ++it;
        }
    }
}
