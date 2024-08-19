#include "Utility.hpp"
#include <cstdint>
#include <cstdio>

namespace utils
{

uint8_t peek1(std::ifstream& stream)
{
    return stream.peek();
}

uint8_t read1(std::ifstream& stream)
{
    uint8_t tmp[1];
    stream.read((char*)tmp, 1);

    return tmp[0];
}

uint16_t read2(std::ifstream& stream)
{
    uint8_t tmp[2];
    stream.read((char*)tmp, 2);

    return (uint16_t)tmp[1] | (uint16_t)tmp[0] << 8;
}

uint32_t read4(std::ifstream& stream)
{
    uint8_t tmp[4];
    stream.read((char*)tmp, 4);

    return tmp[3] | tmp[2] << 8 | tmp[1] << 16 | tmp[0] << 24;
}

uint64_t read8(std::ifstream& stream)
{
    // promote to 64 directly as we will hold in it final result
    uint64_t high = read4(stream);
    uint32_t low = read4(stream);

    return high << 32 | low;
}

std::vector<uint8_t> readBytes(std::ifstream& stream, uint32_t n)
{
    std::vector<uint8_t> result(n, '\0');

    stream.read((char*)result.data(), n);
    return result;
}

std::string readStringBytes(std::ifstream& stream, uint32_t n)
{
    std::string result(n, '\0');

    stream.read(result.data(), n);
    return result;
}

bool isMagicNumberNext(std::ifstream& stream)
{
    // Magic hex: e91100a843a0412d94b306da
    uint64_t highMagic = utils::read8(stream);
    uint32_t lowMagic = utils::read4(stream);

    // printlne("%08lx %04x", highMagic, lowMagic);
    return highMagic == 0xe91100a843a0412d && lowMagic == 0x94b306da;
}

} // namespace utils
