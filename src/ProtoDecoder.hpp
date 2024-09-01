#include <cstring>
#include <mutex>
#include <string>

#include "../deps/HkThreadPool/src/ThreadPool.hpp"
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

    std::vector<FieldMap> parseProtobuffs(const XMLDecoder::XmlResult& firstXML,
        const XMLDecoder::XmlResult& secondXML,
        const std::vector<std::string>& objectClassName,
        const std::vector<std::vector<uint8_t>>& buffer);

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
        STRING_OR_BYTES,
        PACKED_DOUBLE,
        PACKED_ENUM,
    };

    struct DecodeResult
    {
        std::string name;
        bool isRepeated{false};
        std::pair<std::string, FieldValue> field;
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
#define META_VERSION_TOP_XML 1
#define META_VERSION_TOP_NO_XML 0
    uint8_t metaVersion{META_VERSION_TOP_XML};

    ThreadPool tp{8};
    std::mutex objectsMapLock;
    std::vector<std::future<FieldMap>> futures;
    std::unordered_map<std::string, XMLDecoder::NodeSPtr> objectsMap;
};
} // namespace hk
