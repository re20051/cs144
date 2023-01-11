#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    // 判断下一帧ip对应的物理地址是否已被记录
    if (_ip2mac.count(next_hop_ip) == 0) {
        // 如果从未对该ip发送过arp请求或距离上次发送超过5秒，则发送arp广播
        auto it = _arp_retransmit.find(next_hop_ip);
        if (it == _arp_retransmit.end() || it->second + _arp_retransmission_timeout < _accumulate_ticks) {
            // 封装arp广播请求帧
            ARPMessage request;
            request.sender_ethernet_address = _ethernet_address;
            request.sender_ip_address = _ip_address.ipv4_numeric();
            request.target_ip_address = next_hop_ip;
            request.opcode = ARPMessage::OPCODE_REQUEST;
            EthernetFrame frame =
                wrap_frame(EthernetHeader::TYPE_ARP, ETHERNET_BROADCAST, BufferList(request.serialize()));

            // 发送arp广播
            _frames_out.push(frame);

            // 开启重传计时器
            _arp_retransmit[next_hop_ip] = _accumulate_ticks;
        }

        // 将ip数据报放入等待队列
        _wait_for_reply.push_back(pair<const uint32_t, InternetDatagram>(next_hop_ip, dgram));

        return;
    }

    // 已知目标mac地址
    EthernetFrame frame =
        wrap_frame(EthernetHeader::TYPE_IPv4, _ip2mac[next_hop_ip].ethernet_address, BufferList(dgram.serialize()));
    _frames_out.push(frame);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // 只放行广播帧和目的地址是本地mac地址的帧
    if (frame.header().dst == ETHERNET_BROADCAST || frame.header().dst == _ethernet_address) {
        // arp帧
        if (frame.header().type == EthernetHeader::TYPE_ARP) {
            // 将载荷解析为ARPMessage
            ARPMessage msg;
            msg.parse(frame.payload().buffers().at(0));

            // 更新ip - MAC映射表
            update_arp_cache(msg.sender_ip_address, msg.sender_ethernet_address);

            // 发送等待队列中的数据报
            send_datagrams_by_ip(msg.sender_ip_address);

            // arp为请求类型且目的ip为本地ip
            if (msg.opcode == ARPMessage::OPCODE_REQUEST && msg.target_ip_address == _ip_address.ipv4_numeric()) {
                // 封装响应arp
                ARPMessage arp_reply;
                arp_reply.sender_ethernet_address = _ethernet_address;
                arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
                arp_reply.target_ethernet_address = msg.sender_ethernet_address;
                arp_reply.target_ip_address = msg.sender_ip_address;
                arp_reply.opcode = ARPMessage::OPCODE_REPLY;

                // 发送
                EthernetFrame reply_frame = wrap_frame(
                    EthernetHeader::TYPE_ARP, msg.sender_ethernet_address, BufferList(arp_reply.serialize()));
                _frames_out.push(reply_frame);
            }

            return {};
        }

        // ip数据报
        InternetDatagram datagram;
        datagram.parse(frame.payload().buffers().at(0));
        return datagram;
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _accumulate_ticks += ms_since_last_tick;

    // 销毁过期 ip-mac映射条目
    for (auto it = _ip2mac.begin(); it != _ip2mac.end();) {
        arp_life_cycle alc = it->second;
        uint32_t ip_address = it->first;
        it++;

        if (_accumulate_ticks - alc.ticks > _arp_expire) {
            _ip2mac.erase(ip_address);
        }
    }
}

EthernetFrame NetworkInterface::wrap_frame(uint16_t type, EthernetAddress target_address, BufferList payload) {
    EthernetFrame frame;
    frame.header().src = _ethernet_address;
    frame.header().dst = target_address;
    frame.header().type = type;
    frame.payload() = payload;
    return frame;
}

void NetworkInterface::update_arp_cache(uint32_t ip_address, EthernetAddress ethernet_address) {
    arp_life_cycle alc;
    alc.ethernet_address = ethernet_address;
    alc.ticks = _accumulate_ticks;

    _ip2mac[ip_address] = alc;
}

void NetworkInterface::send_datagrams_by_ip(uint32_t ip) {
    EthernetAddress ethernet_address = _ip2mac[ip].ethernet_address;
    for (auto it = _wait_for_reply.begin(); it != _wait_for_reply.end();) {
        if (ip == it->first) {
            EthernetFrame reply_frame =
                wrap_frame(EthernetHeader::TYPE_IPv4, ethernet_address, BufferList(it->second.serialize()));
            _frames_out.push(reply_frame);
            it = _wait_for_reply.erase(it);
        } else {
            it++;
        }
    }
}