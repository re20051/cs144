#include "tcp_receiver.hh"

#include "iostream"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // listen状态，进行握手
    if (!_syn && seg.header().syn) {
        _syn = true;
        _isn = seg.header().seqno.raw_value();
    }

    //挥手状态
    if (!_fin && seg.header().fin) {
        _fin = true;
    }

    //通信状态
    if (_syn) {
        //获得有效载荷
        std::string payload = seg.payload().copy();
        // syn占据了32位序列号的一位
        uint64_t absolute_seq =
            unwrap(seg.header().seqno + seg.header().syn, WrappingInt32(_isn), stream_out().bytes_written()) - _syn;
        _reassembler.push_substring(payload, absolute_seq, seg.header().fin);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    //未建立连接
    if (_syn == false)
        return {};
    //_syn也占据一个位
    uint64_t result = stream_out().bytes_written() + _syn;

    //最后一个字符被读取，输入结束，隐含了_fin = 1
    if (stream_out().input_ended())
        result++;
    return wrap(result, WrappingInt32(_isn));
}

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
