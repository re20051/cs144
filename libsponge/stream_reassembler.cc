#include "stream_reassembler.hh"

#include "iostream"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    cout << index << ' ' << data.length() << ' ' << _first_unassembled << ' ' << _output.bytes_read() + _capacity
         << endl;
    //空串需要特判
    if (data.length() == 0 && eof)
        _iseof = eof;

    //经过处理后的字符串和首字符索引
    string _parsed_data = data;
    size_t _parsed_index = index;

    //判断传入的tcp有效载荷是否存在一部分能存入流重组器中
    if (isValid(_parsed_data, _parsed_index, eof)) {
        //将处理后的字段假如碎片集合中
        add_fragment_set(_parsed_data, _parsed_index);

        //尝试将数据写入bytestream中
        write_into_buffer();
    }

    //判断是否该传送eof给字节流
    if (empty() && _iseof)
        _output.end_input();
}

//判断传入的tcp有效载荷是否存在一部分能存入流重组器中
bool StreamReassembler::isValid(string &data, size_t &index, bool eof) {
    //计算得到_first_unaccceptable
    size_t _first_unaccceptable = _output.bytes_read() + _capacity;

    //过时的载荷，即载荷最后一个字符小于_first_unassembled
    if (index + data.length() <= _first_unassembled)
        return false;
    //超前的载荷，即载荷的第一个字符大于等于_first_unaccceptable
    if (index >= _first_unaccceptable)
        return false;
    if (eof && data.length() + index <= _first_unaccceptable)
        _iseof = true;
    //部分过时
    if (index < _first_unassembled) {
        data = data.substr(_first_unassembled - index);
        index = _first_unassembled;
    }
    //部分超前
    if (index + data.length() > _first_unaccceptable) {
        data = data.substr(0, _first_unaccceptable - index);
    }
    return true;
}

//将新的字符串加入到碎片集合中
void StreamReassembler::add_fragment_set(string &data, size_t index) {
    segment _seg;
    _seg.payload = data;
    _seg.index = index;

    for (auto it = _fragments.begin(); it != _fragments.end();) {
        size_t new_end = _seg.index + _seg.payload.length() - 1;
        size_t old_end = it->index + it->payload.length() - 1;

        //判断是否存在重合
        if ((it->index <= new_end && old_end >= new_end) || (_seg.index <= old_end && new_end >= old_end)) {
            _seg = merge_fragment(_seg, *it);
            it = _fragments.erase(it);
        } else
            it++;
    }

    //空载荷不需要添加至碎片集合中
    if (_seg.payload.length() > 0)
        _fragments.insert(_seg);
}

//新字符串和迭代的segment寻求合并
StreamReassembler::segment StreamReassembler::merge_fragment(const segment new_seg, const segment old_seg) {
    size_t new_index = new_seg.index;
    size_t new_end = new_seg.index + new_seg.payload.length() - 1;
    size_t old_index = old_seg.index;
    size_t old_end = old_seg.index + old_seg.payload.length() - 1;
    segment result;

    //新段完全包含旧段 [1, 5], [1, 4]
    if (new_index <= old_index && new_end >= old_end) {
        result = new_seg;
    }
    //新段完全被旧段包含 [1, 5] [1, 6]
    else if (old_index <= new_index && old_end >= new_end) {
        result = old_seg;
    }
    //新段右边和旧段左边重合 [1, 6] [4, 9]
    else if (old_index <= new_end && old_index >= new_index) {
        result.index = new_index;
        result.payload = new_seg.payload + old_seg.payload.substr(new_end - old_index + 1);
    }
    //新段左边和旧段右边重合 [4, 6] [1, 5]
    else if (new_index <= old_end && new_index >= old_index) {
        result.index = old_index;
        result.payload = old_seg.payload + new_seg.payload.substr(old_end - new_index + 1);
    }
    return result;
}

void StreamReassembler::write_into_buffer() {
    //判断碎片集合的第一个元素是否能写入字节流中
    if (!empty() && _fragments.begin()->index == _first_unassembled) {
        segment temp = *_fragments.begin();
        _output.write(temp.payload);
        _first_unassembled += temp.payload.length();
        _fragments.erase(temp);
        write_into_buffer();
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t result = 0;

    for (auto it = _fragments.begin(); it != _fragments.end(); it++)
        result += it->payload.length();
    return result;
}

bool StreamReassembler::empty() const { return _fragments.empty(); }
