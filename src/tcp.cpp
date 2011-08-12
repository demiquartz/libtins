/*
 * libtins is a net packet wrapper library for crafting and 
 * interpreting sniffed packets.
 * 
 * Copyright (C) 2011 Nasel
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <cstring>
#include <cassert>
#include <iostream> //borrame
#ifndef WIN32
    #include <netinet/in.h>
#endif
#include "tcp.h"
#include "ip.h"
#include "utils.h"


const uint16_t Tins::TCP::DEFAULT_WINDOW = 32678;

Tins::TCP::TCP(uint16_t dport, uint16_t sport) : PDU(IPPROTO_TCP), _payload(0), _payload_size(0), 
                                                 _options_size(0), _total_options_size(0) {
    std::memset(&_tcp, 0, sizeof(tcphdr));
    _tcp.dport = Utils::net_to_host_s(dport);
    _tcp.sport = Utils::net_to_host_s(sport);
    _tcp.doff = sizeof(tcphdr) / sizeof(uint32_t);
    _tcp.window = Utils::net_to_host_s(DEFAULT_WINDOW);
    _tcp.check = 0;
}

Tins::TCP::~TCP() {
    for(unsigned i(0); i < _options.size(); ++i)
        delete[] _options[i].data;
}

void Tins::TCP::dport(uint16_t new_dport) {
    _tcp.dport = Utils::net_to_host_s(new_dport);
}

void Tins::TCP::sport(uint16_t new_sport) {
    _tcp.sport = Utils::net_to_host_s(new_sport);
}

void Tins::TCP::seq(uint32_t new_seq) {
    _tcp.seq = new_seq;
}

void Tins::TCP::ack_seq(uint32_t new_ack_seq) {
    _tcp.ack_seq = new_ack_seq;
}

void Tins::TCP::window(uint16_t new_window) {
    _tcp.window = new_window;
}

void Tins::TCP::check(uint16_t new_check) {
    _tcp.check = new_check;
}

void Tins::TCP::urg_ptr(uint16_t new_urg_ptr) {
    _tcp.urg_ptr = new_urg_ptr;
}

void Tins::TCP::payload(uint8_t *new_payload, uint32_t new_payload_size) {
    _payload = new_payload;
    _payload_size = new_payload_size;
}

void Tins::TCP::set_mss(uint16_t value) {
    value = Utils::net_to_host_s(value);
    add_option(MSS, 2, (uint8_t*)&value);
}

void Tins::TCP::set_timestamp(uint32_t value, uint32_t reply) {
    uint64_t buffer = ((uint64_t)Utils::net_to_host_l(reply) << 32) | Utils::net_to_host_l(value);
    add_option(TSOPT, 8, (uint8_t*)&buffer);
}

void Tins::TCP::set_flag(Flags tcp_flag, uint8_t value) {
    switch(tcp_flag) {
        case FIN:
            _tcp.fin = value;
            break;
        case SYN:
            _tcp.syn = value;
            break;
        case RST:
            _tcp.rst = value;
            break;
        case PSH:
            _tcp.psh = value;
            break;
        case ACK:
            _tcp.ack = value;
            break;
        case URG:
            _tcp.urg = value;
            break;
        case ECE:
            _tcp.ece = value;
            break;
        case CWR:
            _tcp.cwr = value;
            break;
    };
}

void Tins::TCP::add_option(Options tcp_option, uint8_t length, uint8_t *data) {
    uint8_t *new_data = new uint8_t[length], padding;
    memcpy(new_data, data, length);
    _options.push_back(TCPOption(tcp_option, length, new_data));
    _options_size += length + (sizeof(uint8_t) << 1);
    padding = _options_size & 3;
    _total_options_size = (padding) ? _options_size - padding + 4 : _options_size;
}

uint32_t Tins::TCP::do_checksum(uint8_t *start, uint8_t *end) const {
    uint32_t checksum(0);
    uint16_t *ptr = (uint16_t*)start, *last = (uint16_t*)end, padding(0);
    if(((end - start) & 1) == 1) {
        last = (uint16_t*)end - 1;
        padding = *(end - 1) << 8;
    }
    while(ptr < last)
        checksum += Utils::net_to_host_s(*(ptr++));
    return checksum + padding;
}

uint32_t Tins::TCP::pseudoheader_checksum(uint32_t source_ip, uint32_t dest_ip) const {
    uint32_t checksum(0), len(header_size());
    source_ip = Utils::net_to_host_l(source_ip);
    dest_ip = Utils::net_to_host_l(dest_ip);
    uint16_t *ptr = (uint16_t*)&source_ip;
    
    checksum += *ptr + ptr[1];
    ptr = (uint16_t*)&dest_ip;
    checksum += *ptr + ptr[1];
    checksum += IPPROTO_TCP + len;
    return checksum;
}

uint32_t Tins::TCP::header_size() const {
    return sizeof(tcphdr) + _payload_size + _total_options_size;
}

void Tins::TCP::write_serialization(uint8_t *buffer, uint32_t total_sz, PDU *parent) {
    assert(total_sz >= header_size());
    uint8_t *tcp_start = buffer;
    buffer += sizeof(tcphdr);
    _tcp.doff = (sizeof(tcphdr) + _total_options_size) / sizeof(uint32_t);
    for(unsigned i(0); i < _options.size(); ++i)
        buffer = _options[i].write(buffer);

    if(_options_size < _total_options_size) {    
        uint8_t padding = _total_options_size;
        while(padding < _options_size) {
            *(buffer++) = 1;
            padding++;
        }
    }
        
    memcpy(buffer, _payload, _payload_size);
    buffer += _payload_size;
    IP *ip_packet = dynamic_cast<IP*>(parent);
    if(ip_packet) {
        _tcp.check = 0;
        uint32_t checksum = pseudoheader_checksum(ip_packet->source_address(), ip_packet->dest_address()) + 
                            do_checksum(tcp_start + sizeof(tcphdr), buffer) + do_checksum((uint8_t*)&_tcp, ((uint8_t*)&_tcp) + sizeof(tcphdr));
        while (checksum >> 16)
            checksum = (checksum & 0xffff)+(checksum >> 16);
        _tcp.check = Utils::net_to_host_s(~checksum);
    }
    memcpy(tcp_start, &_tcp, sizeof(tcphdr));
}


/* TCPOptions */

uint8_t *Tins::TCP::TCPOption::write(uint8_t *buffer) {
    if(kind == 1) {
        *buffer = kind;
        return buffer + 1;
    }
    else {
        buffer[0] = kind;
        buffer[1] = length + (sizeof(uint8_t) << 1);
        memcpy(buffer + 2, data, length);
        return buffer + length + (sizeof(uint8_t) << 1);
    }
}