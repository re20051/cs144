#include "tcp_sender.hh"

#include "iostream"
#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {
    _current_retransmission_timeout = retx_timeout;
}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _has_ack; }

void TCPSender::fill_window() {
    uint64_t temp_unacceptable = _first_unacceptable + _is_window_zero;
    // close状态，发送syn为true的段
    if (_next_seqno == 0) {
        TCPSegment segment;
        segment.header().syn = true;
        send_segment(segment);
    }

    // syn_sent状态
    if (_next_seqno > 0 && _next_seqno == bytes_in_flight()) {
        return;
    }

    // syn_ack状态
    if (_next_seqno > bytes_in_flight() && !stream_in().eof()) {
        //尽可能的发送段
        while (_next_seqno < temp_unacceptable && !stream_in().buffer_empty()) {
            TCPSegment segment;
            //限制段的长度
            size_t length = min(temp_unacceptable - _next_seqno, _max_package);
            //从字节流中读取最多长度为length的字段
            segment.payload() = stream_in().read(length);
            //如果读取结束，且窗口还有空间，带上fin标记
            if (stream_in().eof() && (_next_seqno + segment.length_in_sequence_space()) < temp_unacceptable)
                segment.header().fin = true;
            //发送tcp段
            send_segment(segment);
        }
    }

    //输入结束，但fin依旧未被发送，且拥有空间发送
    if (stream_in().eof() && _next_seqno < stream_in().bytes_written() + 2 && _next_seqno < temp_unacceptable) {
        TCPSegment segment;
        segment.header().fin = true;
        send_segment(segment);
    }

    // fyn_sent状态
    if (stream_in().eof() && _next_seqno == stream_in().bytes_written() + 2 && bytes_in_flight() > 0) {
        return;
    }

    // fyn_ack状态
    return;
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abso_seq;
    // close状态，直接忽略
    if (_next_seqno == 0)
        return;

    //将32相对序列号转为64位绝对序列号，并判断合法性
    if (convert_to_64(ackno.raw_value(), abso_seq) == false)
        return;

    //如果64位序列号合法，那么要更新window_size
    _first_unacceptable = abso_seq + window_size;
    _is_window_zero = window_size == 0 ? true : false;

    //更新未确认段队列
    while (!_sent_but_not_ack.empty()) {
        //取出第一个未确认(最早的)TCP段
        TCPSegment frontSeg = _sent_but_not_ack.front();
        //判断整段是否被确认
        if (_has_ack + frontSeg.length_in_sequence_space() <= abso_seq) {
            //从队列中删除
            _sent_but_not_ack.pop();
            _has_ack += frontSeg.length_in_sequence_space();

            //重置RTO，累计ticks以及重传次数
            _current_retransmission_timeout = _initial_retransmission_timeout;
            _accumulate_seconds = 0;
            _consecutive_retransmissions = 0;

        } else
            break;
    }

    //没有outstanding段了，计时器停止
    if (_sent_but_not_ack.empty())
        _timer_start = false;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    //如果计时器未启动，直接忽略
    if (!_timer_start)
        return;

    _accumulate_seconds += ms_since_last_tick;

    //重传计时器超时
    if (_accumulate_seconds >= _current_retransmission_timeout) {
        //首先重传第一个字段
        TCPSegment firstSegment = _sent_but_not_ack.front();
        _segments_out.push(firstSegment);
        //计时清零
        _accumulate_seconds = 0;
        //如果窗口大小不为0，RTO加倍，累计重传加一
        if (!_is_window_zero) {
            _current_retransmission_timeout *= 2;
            _consecutive_retransmissions++;
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    //传一个空段，不需要计时
    TCPSegment segment;
    _segments_out.push(segment);
}

/*用于包装TCP数据包并发送*/
void TCPSender::send_segment(TCPSegment tcpSegment) {
    //如果计时器未开始计时，则启动
    if (!_timer_start) {
        _timer_start = true;
    }

    tcpSegment.header().seqno = wrap(_next_seqno, _isn);
    //将tcp段放入准备传送的队列里
    _segments_out.push(tcpSegment);
    //放入已发送的队列里
    _sent_but_not_ack.push(tcpSegment);
    //更新_next_seqno
    _next_seqno += tcpSegment.length_in_sequence_space();
}

/*32位序列号转为64位*/
bool TCPSender::convert_to_64(uint32_t ackno, uint64_t &abso_64_seq) {
    //转为32位绝对序列号
    uint32_t abso_32_seq = ackno - _isn.raw_value();

    abso_64_seq = (_next_seqno & (0xffffffff00000000)) | abso_32_seq;
    //保证序列号在_next_seqno的左边
    if (abso_64_seq > _next_seqno)
        abso_64_seq -= 0xffffffff;

    //左溢出
    if (abso_64_seq > _next_seqno)
        return false;

    //检验是否是过时的序列号
    if (abso_64_seq < _has_ack)
        return false;

    return true;
}