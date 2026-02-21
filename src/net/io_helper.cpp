#include "net/io_helper.hpp"
#include "net/fd_helper.hpp"
#include "protocol/line_parser.hpp"
#include <cerrno>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <array>
#include <iostream>

bool offset_buffer::clear_if_done(){
    if(buf.size() != offset) return false;
    buf.clear();
    offset = 0;
    return true;
}

bool offset_buffer::compact_if_needed(){
    if(offset < 8192) return false;
    if(offset * 2 < buf.size()) return false;
    buf.erase(0, offset);
    offset = 0;
    return true;
}

void offset_buffer::reset_offset(){
    offset = 0;
}

bool offset_buffer::has_pending() const{
    return offset < buf.size();
}

const char* offset_buffer::current_data() const{
    return buf.data() + offset;
}

std::size_t offset_buffer::remaining() const{
    return buf.size() - offset;
}

void offset_buffer::advance(std::size_t n){
    offset += n;
}

std::size_t offset_buffer::get_offset() const{
    return offset;
}

void offset_buffer::set_offset(std::size_t new_offset){
    offset = new_offset;
}

std::string& offset_buffer::raw(){
    return buf;
}

const std::string& offset_buffer::raw() const{
    return buf;
}

bool send_buffer::append(const command_codec::command& cmd){
    return append(command_codec::encode(cmd));
}

bool send_buffer::append(std::string_view sv){
    bool was_pending = has_pending();
    buf += sv;
    return !was_pending && has_pending();
}

bool send_buffer::append(const char* p, std::size_t n){
    bool was_pending = has_pending();
    buf.append(p, n);
    return !was_pending && has_pending();
}

void recv_buffer::append(const char* p, std::size_t n){
    buf.append(p, n);
}

std::string recv_buffer::take_all(){
    std::string out = std::move(buf);
    buf.clear();
    reset_offset();
    return out;
}

std::expected <std::size_t, error_code> flush_send(socket_info& si){
    if(si.tls.get() == nullptr) return std::unexpected(error_code::from_errno(EINVAL));

    std::size_t send_byte = 0;
    while(si.send.has_pending()){
        auto wr_exp = si.tls.write(si.send.current_data(), si.send.remaining());
        if(!wr_exp) return std::unexpected(wr_exp.error());

        const tls_io_result& wr = *wr_exp;
        if(wr.byte > 0){
            si.send.advance(wr.byte);
            send_byte += wr.byte;
        }

        if(wr.closed) return std::unexpected(error_code::from_errno(EPIPE));
        if(wr.want_read || wr.want_write){
            si.send.compact_if_needed();
            return send_byte;
        }

        if(wr.byte == 0) return std::unexpected(error_code::from_errno(EPROTO));
    }

    si.send.clear_if_done();
    return send_byte;
}

std::expected <recv_info, error_code> drain_recv(socket_info& si){
    if(si.tls.get() == nullptr) return std::unexpected(error_code::from_errno(EINVAL));

    recv_info ret;
    std::array <char, BUF_SIZE> tmp{};
    while(true){
        auto rd_exp = si.tls.read(tmp.data(), tmp.size());
        if(!rd_exp) return std::unexpected(rd_exp.error());

        const tls_io_result& rd = *rd_exp;
        if(rd.byte > 0){
            si.recv.append(tmp.data(), rd.byte);
            ret.byte += rd.byte;
            if(rd.want_read || rd.want_write) return ret;
            continue;
        }

        if(rd.closed){
            ret.closed = true;
            return ret;
        }

        if(rd.want_read || rd.want_write) return ret;
        return ret;
    }
}
