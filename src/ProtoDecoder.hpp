#include <cstring>
#include <string>

#include "../deps/HkXML/src/HkXml.hpp"
#include "CommonTypes.hpp"

namespace hk
{
class ProtobufDecoder
{
public:
    FieldMap parseProtobufFromBuffer(const XMLDecoder::XmlResult& firstXML,
        const XMLDecoder::XmlResult& secondXML,
        const std::string& objectClassName,
        const std::vector<uint8_t>& buffer);

    static void printFields(const FieldMap& fm, uint64_t depth = 0);

private:
    enum class WireType : uint8_t
    {
        VARINT = 0,
        I64 = 1,
        LEN = 2,
        I32 = 5,
        UNKNOWN = 7
    };

    struct TagDecodeResult
    {
        WireType type{WireType::UNKNOWN};
        uint64_t fieldNumber{0};
    };

    enum class DecodeHint
    {
        NONE,
        STRING_OR_PACKED,
    };

    struct DecodeResult
    {
        std::string name;
        bool isRepeated{false};
        Field field;
    };

    DecodeResult
    decode(const XMLDecoder::NodeSPtr& objectNode, const std::vector<uint8_t>& buffer, uint64_t& currentIndex);

    void resolveTopLevelDecodeResult(FieldMap& fieldMap, const DecodeResult& decodeResult);

    uint64_t decodeVarInt(const std::vector<uint8_t>& buffer, uint64_t& currentIndex);

    uint64_t decodeNumber64(const std::vector<uint8_t>& buffer, uint64_t& currentIndex);

    std::string getTagString(const TagDecodeResult& tag);

    TagDecodeResult decodeTag(const std::vector<uint8_t>& buffer, uint64_t& currentIndex);

    std::string decodePackedPayload(const uint64_t len, const std::vector<uint8_t>& buffer, uint64_t& currentIndex);

    FieldValue decodePayload(const XMLDecoder::NodeSPtr& objectNode,
        const TagDecodeResult& decodedTag,
        const std::vector<uint8_t>& buffer,
        const DecodeHint hint,
        uint64_t& currentIndex);

private:
    std::unordered_map<std::string, XMLDecoder::NodeSPtr> objectsMap;
};
} // namespace hk
