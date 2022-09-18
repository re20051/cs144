#include "wrapping_integers.hh"

#include "iostream"

using namespace std;
// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    return WrappingInt32((n + static_cast<uint64_t>(isn.raw_value())) & (0x00000000ffffffff));
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint64_t result;
    WrappingInt32 cp_seq = wrap(checkpoint, isn);
    uint32_t difference = cp_seq - n;

    if (difference > 0x7fffffff) {
        result = checkpoint + (0xffffffff - difference + 1);
    } else {
        result = checkpoint - difference;

        //考虑溢出情况
        if (checkpoint - difference > checkpoint)
            result += 0x100000000;
    }
    return result;
}
