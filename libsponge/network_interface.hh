#ifndef SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
#define SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH

#include "ethernet_frame.hh"
#include "tcp_over_ip.hh"
#include "tun.hh"

#include <cstdint>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

//! \brief A "network interface" that connects IP (the internet layer, or network layer)
//! with Ethernet (the network access layer, or link layer).

//! This module is the lowest layer of a TCP/IP stack
//! (connecting IP with the lower-layer network protocol,
//! e.g. Ethernet). But the same module is also used repeatedly
//! as part of a router: a router generally has many network
//! interfaces, and the router's job is to route Internet datagrams
//! between the different interfaces.

//! The network interface translates datagrams (coming from the
//! "customer," e.g. a TCP/IP stack or router) into Ethernet
//! frames. To fill in the Ethernet destination address, it looks up
//! the Ethernet address of the next IP hop of each datagram, making
//! requests with the [Address Resolution Protocol](\ref rfc::rfc826).
//! In the opposite direction, the network interface accepts Ethernet
//! frames, checks if they are intended for it, and if so, processes
//! the the payload depending on its type. If it's an IPv4 datagram,
//! the network interface passes it up the stack. If it's an ARP
//! request or reply, the network interface processes the frame
//! and learns or replies as necessary.
class NetworkInterface {
  private:
    //! ARP 映射的有效期(毫秒):超过 30 秒未被刷新则遗忘该条目
    static constexpr size_t ARP_CACHE_TTL_MS = 30000;

    //! 对同一个尚未解析的 IP 地址,两次 ARP 请求之间的最小间隔(5 秒)
    static constexpr size_t ARP_REQUEST_INTERVAL_MS = 5000;

    //! 一条学到的映射:下一跳 IP 地址 -> 以太网地址
    struct ARPCacheEntry {
        EthernetAddress ethernet_address{};  //!< 下一跳的以太网地址
        size_t ttl_ms{};                     //!< 该条目的剩余生存时间(毫秒)
    };

    //! Ethernet (known as hardware, network-access-layer, or link-layer) address of the interface
    EthernetAddress _ethernet_address;

    //! IP (known as internet-layer or network-layer) address of the interface
    Address _ip_address;

    //! outbound queue of Ethernet frames that the NetworkInterface wants sent
    std::queue<EthernetFrame> _frames_out{};

    //! ARP 缓存:下一跳 IP 地址 -> (以太网地址, 剩余生存时间)
    std::unordered_map<uint32_t, ARPCacheEntry> _arp_cache{};

    //! 尚未完成的 ARP 请求:目标 IP 地址 -> 距下次可以再发请求的剩余时间
    std::unordered_map<uint32_t, size_t> _arp_requests{};

    //! 等待 ARP 解析的数据报:下一跳 IP 地址 -> 按发送顺序排队的数据报
    std::unordered_map<uint32_t, std::vector<InternetDatagram>> _pending_datagrams{};

    //! \brief 把 payload 封装成一个以太网帧,并放入待发送队列
    void _push_frame(const EthernetAddress &dst, const uint16_t type, const BufferList &payload);

    //! \brief 广播一个 ARP 请求,询问 target_ip 对应的以太网地址
    void _send_arp_request(const uint32_t target_ip);

    //! \brief 把所有等待 next_hop_ip 以太网地址的数据报发出
    void _flush_pending(const uint32_t next_hop_ip);

  public:
    //! \brief Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer) addresses
    NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address);

    //! \brief Access queue of Ethernet frames awaiting transmission
    std::queue<EthernetFrame> &frames_out() { return _frames_out; }

    //! \brief Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination address).

    //! Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address for the next hop
    //! ("Sending" is accomplished by pushing the frame onto the frames_out queue.)
    void send_datagram(const InternetDatagram &dgram, const Address &next_hop);

    //! \brief Receives an Ethernet frame and responds appropriately.

    //! If type is IPv4, returns the datagram.
    //! If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
    //! If type is ARP reply, learn a mapping from the "sender" fields.
    std::optional<InternetDatagram> recv_frame(const EthernetFrame &frame);

    //! \brief Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);
};

#endif  // SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
