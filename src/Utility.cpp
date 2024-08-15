#include "Utility.hpp"
#include <cstdint>
#include <cstdio>

namespace utils
{

/**
    @brief Look ahead 1 byte from _stream_ and return int8_t
*/
uint8_t peek1(std::ifstream& stream)
{
    return stream.peek();
}

/**
    @brief Read 1 byte from _stream_ and return uint8_t
*/
uint8_t read1(std::ifstream& stream)
{
    uint8_t tmp[1];
    stream.read((char*)tmp, 1);

    return tmp[0];
}

/**
    @brief Read 2 big endian bytes from _stream_ and return uint16_t
*/
uint16_t read2(std::ifstream& stream)
{
    uint8_t tmp[2];
    stream.read((char*)tmp, 2);

    return (uint16_t)tmp[1] | (uint16_t)tmp[0] << 8;
}

/**
    @brief Read 4 big endian bytes from _stream_ and return uint32_t
*/
uint32_t read4(std::ifstream& stream)
{
    uint8_t tmp[4];
    stream.read((char*)tmp, 4);

    return tmp[3] | tmp[2] << 8 | tmp[1] << 16 | tmp[0] << 24;
}

/**
    @brief Read 8 big endian bytes from _stream_ and return int64_t
*/
uint64_t read8(std::ifstream& stream)
{
    // promote to 64 directly as we will hold in it final result
    uint64_t high = read4(stream);
    uint32_t low = read4(stream);

    return high << 32 | low;
}

/**
    @brief Read N bytes, supposedly ASCII, and return the string it forms
*/
std::string readStringBytes(std::ifstream& stream, uint32_t n)
{
    std::string result(n, '\0');

    stream.read(result.data(), n);
    return result;
}

/**
    @brief Determine if the next 12 bytes form the magic number
*/

bool isMagicNumberNext(std::ifstream& stream)
{
    // Magic hex: e91100a843a0412d94b306da
    uint64_t highMagic = utils::read8(stream);
    uint32_t lowMagic = utils::read4(stream);

    // printlne("%08lx %04x", highMagic, lowMagic);
    return highMagic == 0xe91100a843a0412d && lowMagic == 0x94b306da;
}

} // namespace utils
