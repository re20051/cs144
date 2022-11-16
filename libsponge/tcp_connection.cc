#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

//发送出去但尚未被确认的字节数
size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

//接受了，但停留在流重组器中的字节数
size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    //不处于活跃状态，直接返回
    if (!active())
        return;
    //更新上一次接收到段的时间
    _time_since_last_segment_received = 0;

    // listen状态，接收到syn，进入syn_rec状态
    if (_sender.next_seqno_absolute() == 0 && !_receiver.ackno().has_value()) {
        //如果段头部rst被设置
        if (seg.header().rst) {
            _is_active = false;
            return;
        }
        //如果syn未被设置，代表着不是建立连接请求，直接返回
        if (!seg.header().syn)
            return;
        _receiver.segment_received(seg);
        //发起建立连接请求，发送syn + ack
        connect();
        return;
    }

    // syn_sent状态，1. 收到syn，进入syn_rec状态，2. 收到ack + syn，进入establish
    if (_sender.next_seqno_absolute() > 0 && !_receiver.ackno().has_value()) {
        // rst被设置，直接关闭，因为是接收方，则不需要回传rst
        if (seg.header().rst) {
            unclean_shutdown(false);
            return;
        }
        //如果syn未被设置，代表着不是建立连接请求，直接返回，这里默认进入了syn_rec状态
        if (!seg.header().syn)
            return;
        _receiver.segment_received(seg);

        if (seg.header().ack)
            _sender.ack_received(seg.header().ackno, seg.header().win);

        //发送，包含ack + seq
        _sender.send_empty_segment();
        segment_send();
        return;
    }

    //同时发出syn，还未接收到ack，接收到ack后不需要再次发送ack
    if (_receiver.ackno().has_value() && _sender.next_seqno_absolute() == _sender.bytes_in_flight()) {
        // rst被设置
        if (seg.header().rst) {
            unclean_shutdown(false);
            return;
        }
        _receiver.segment_received(seg);

        if (seg.header().ack)
            _sender.ack_received(seg.header().ackno, seg.header().win);
        //不用发送任何东西，直接返回
        return;
    }

    //正常通信状态
    // rst被设置
    if (seg.header().rst) {
        //由于是接收方，所以不需要回传rst
        unclean_shutdown(false);
        return;
    }

    //将段交给接收器
    _receiver.segment_received(seg);
    //告知sender ack和ws
    if (seg.header().ack)
        _sender.ack_received(seg.header().ackno, seg.header().win);

    // keep-alive
    if (_receiver.ackno().has_value() && (seg.length_in_sequence_space() == 0) &&
        seg.header().seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
    }
    //填充窗口
    _sender.fill_window();
    //填充窗口后，发送队列为空，但因为收到的段存在载荷，必须做出回应，所以在发送队列中加入空段
    if (_sender.segments_out().empty() && seg.length_in_sequence_space())
        _sender.send_empty_segment();
    //发送
    segment_send();
}

bool TCPConnection::active() const { return _is_active; }

//将数据写入字节流，然后尽可能填充窗口后发送
size_t TCPConnection::write(const string &data) {
    size_t result = _sender.stream_in().write(data);
    _sender.fill_window();
    segment_send();
    return result;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    _time_since_last_segment_received += ms_since_last_tick;
    //连续重传次数大于最大重传次数
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        //主动关闭，需要发送rst段
        unclean_shutdown(true);
        return;
    }
    segment_send();
}

//结束输入，填充窗口，然后发送
void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    segment_send();
}

//建立连接
void TCPConnection::connect() {
    //填充窗口，sender中实现，发送syn段
    _sender.fill_window();
    segment_send();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            //主动关闭，需要发送rst段
            unclean_shutdown(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

//将sender的队列全部发送
void TCPConnection::segment_send() {
    //准备发送，从sender的准备队列中拿出segment，添加到connection的发送队列里
    while (!_sender.segments_out().empty()) {
        TCPSegment segment = _sender.segments_out().front();

        //如果接收方可以获得ackno，捎带ackno和window_size
        auto data = _receiver.ackno();
        if (data.has_value()) {
            //设置确认号和窗口大小，此处存疑
            segment.header().ack = 1;
            segment.header().win = _receiver.window_size();
            segment.header().ackno = *data;
        }
        _sender.segments_out().pop();
        segments_out().push(segment);
    }

    clean_shutdown();
}

//如果主动发送rst，_send_rst设为true，需要发送，被动接收，_send_rst为false，不需要回传
void TCPConnection::unclean_shutdown(bool _send_rst) {
    clear();
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _is_active = false;
    _linger_after_streams_finish = false;

    if (_send_rst) {
        _sender.send_empty_segment();
        TCPSegment segment = _sender.segments_out().front();
        if (_receiver.ackno().has_value()) {
            segment.header().ack = 1;
            segment.header().ackno = _receiver.ackno().value();
        }
        segment.header().rst = 1;
        segment.header().win = _receiver.window_size();
        segments_out().push(segment);
    }
}

void TCPConnection::clean_shutdown() {
    //接受完了但还未发送完毕
    if (_receiver.stream_out().input_ended() && !_sender.stream_in().eof())
        _linger_after_streams_finish = false;

    //条件1-3均满足，即接收完了，同时得知对方得知己方已发送完
    if (_receiver.stream_out().input_ended() && _sender.stream_in().eof() && _sender.bytes_in_flight() == 0) {
        //情况1：_linger_after_streams_finish = false，即被动关闭
        if (!_linger_after_streams_finish)
            _is_active = false;
        //情况2：上一次段发送的时间大于10次timeout，主动关闭
        else if (time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
            _is_active = false;
        }
    }
}

// unclean_shutdown之前清空发送队列
void TCPConnection::clear() {
    while (!_sender.segments_out().empty()) {
        _sender.segments_out().pop();
    }
}