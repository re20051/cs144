#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

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

    // DUMMY_CODE(route_prefix, prefix_length, next_hop, interface_num);
    // Your code here.
    _route_table.push_back(Route{route_prefix, prefix_length, next_hop, interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // DUMMY_CODE(dgram);
    // Your code here.
    IPv4Header header = dgram.header();
    uint32_t target_ip = header.dst;
    size_t interface_num = 0;
    int8_t max_length = -1;
    optional<Address> next_hop;

    // 超时，则丢弃
    if (header.ttl == 0 || header.ttl == 1) {
        return;
    }

    for (auto it = _route_table.begin(); it != _route_table.end(); it++) {
        uint8_t length = it->prefix_length;

        // 首先判断是否匹配
        if (length == 0 || (target_ip >> (32 - length)) == (it->route_prefix >> (32 - length))) {
            // 如果匹配，满足最长匹配原则
            if (length >= max_length) {
                max_length = length;
                interface_num = it->interface_num;
                next_hop = it->next_hop;
            }
        }
    }

    // 没有匹配的路由，直接丢弃
    if (max_length == -1) {
        return;
    }

    // TTL减一
    dgram.header().ttl--;

    // 转发报文
    if (next_hop.has_value()) {
        _interfaces[interface_num].send_datagram(dgram, next_hop.value());
    } else {
        _interfaces[interface_num].send_datagram(dgram, Address::from_ipv4_numeric(target_ip));
    }
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
