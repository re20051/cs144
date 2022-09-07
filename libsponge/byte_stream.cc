#include "byte_stream.hh"

#include <iostream>
// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) { _capacity = capacity; }

size_t ByteStream::write(const string &data) {
    if (remaining_capacity() == 0)
        return 0;
    //输入字符串长度
    size_t len = data.length();
    //将要写入缓冲区的长度，为输入字符串的长度和剩余空间的较小值
    size_t cutlen = min(len, _capacity - _length);
    //将字符串写入缓冲区
    _buffer.insert(_buffer.end(), data.begin(), data.begin() + cutlen);
    //更新缓冲区缓冲字符数量
    _length += cutlen;
    //更新累计写入字符数
    _bytesWritten += cutlen;
    //返回
    return cutlen;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    //确保长度不会超过缓冲区长度
    size_t strlen = min(len, _length);
    return string(_buffer.begin(), _buffer.begin() + strlen);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    //确保长度不会超过缓冲区长度
    size_t strlen = min(len, _length);
    _buffer.erase(_buffer.begin(), _buffer.begin() + strlen);
    //更新缓冲区长度
    _length -= strlen;
    //更新已经读取的字节数
    _bytesRead += strlen;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    //先提取字符串，再pop
    //确保长度不会超过缓冲区长度
    size_t strlen = min(len, _length);
    string result = peek_output(strlen);
    pop_output(strlen);
    return result;
}

void ByteStream::end_input() { _isEnd = true; }

bool ByteStream::input_ended() const { return _isEnd; }

size_t ByteStream::buffer_size() const { return _length; }

bool ByteStream::buffer_empty() const { return _length == 0; }

//必须已经输入完毕且缓冲区为空
bool ByteStream::eof() const { return _isEnd && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _bytesWritten; }

size_t ByteStream::bytes_read() const { return _bytesRead; }

size_t ByteStream::remaining_capacity() const { return _capacity - _length; }
