#include "Utility.hpp"
#include <cstdint>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include "../deps/HkXML/src/HkXml.hpp"

namespace hk
{
class ProtobufDecoder
{
public:
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

    struct Field;
    using FieldVec = std::vector<Field>;
    using FieldValue = std::variant<uint64_t, std::string, FieldVec>;

    struct Field
    {
        std::string name;
        FieldValue value;
    };

    ProtobufDecoder()
    {
        println("Loading meta XML in..");
        std::ifstream beMeta{"metaTmp/bm/meta.xml"};
        std::ifstream elMeta{"metaTmp/lte/meta.xml"};

        if (beMeta.fail() || elMeta.fail())
        {
            printlne("One of the meta files failed to load for xml parsing");
            return;
        }

        beXmlResult = XMLDecoder().decodeFromStream(beMeta);
        if (!beXmlResult.second.empty())
        {
            printlne("Error while parsing XML: %s", beXmlResult.second.c_str());
            beXmlResult = XMLDecoder::XmlResult{}; // reset it to nothing
            return;
        }

        // only need BM for now
        // elXmlResult = XMLDecoder().decodeFromStream(elMeta);
        // if (!elXmlResult.second.empty())
        // {
        //     printlne("Error while parsing XML: %s", elXmlResult.second.c_str());
        //     elXmlResult = XMLDecoder::XmlResult{}; // reset it to nothing
        //     return;
        // }
        println("Loading meta XML done");
    }

    void parseProtobufFromBuffer(const std::string& objectName, const std::vector<uint8_t>& buffer)
    {
        std::string result{"{"};
        uint64_t currentIndex{0};

        XMLDecoder::NodeSPtr objectNode = beXmlResult.first[1]->getTagNamedWithAttrib("managedObject",
            {"class", objectName});
        if (!objectNode)
        {
            printlne("Couldn't find object named %s", objectName.c_str());
            return;
        }

        FieldVec fv;
        while (buffer[currentIndex] != '\0')
        {
            // objectNode - managed object 'objectName' in this case
            Field f = decode(objectNode, buffer, currentIndex);
            fv.emplace_back(f);
        }
        printFields(fv);
    }

    void printFields(const FieldVec& fv, uint64_t depth = 0)
    {
        std::string sp;
        sp.reserve(depth * 4 + 4);
        for (uint64_t i = 0; i < depth; i++)
        {
            sp += "    ";
        }

        for (const auto& field : fv)
        {
            if (std::holds_alternative<uint64_t>(field.value))
            {
                println("%s%s: %lu", sp.c_str(), field.name.c_str(), std::get<std::uint64_t>(field.value));
            }
            else if (std::holds_alternative<std::string>(field.value))
            {
                println("%s%s: %s", sp.c_str(), field.name.c_str(), std::get<std::string>(field.value).c_str());
            }
            else
            {
                println("%s%s []:", sp.c_str(), field.name.c_str());
                printFields(std::get<FieldVec>(field.value), depth + 1);
            }
            // if (std::holds_alternative<uint64_t>(field.value))
            // {
            //     println("%sFieldName: %s FieldValue: %lu", sp.c_str(), field.name.c_str(),
            //         std::get<std::uint64_t>(field.value));
            // }
            // else if (std::holds_alternative<std::string>(field.value))
            // {
            //     println("%sFieldName: %s FieldValue: %s", sp.c_str(), field.name.c_str(),
            //         std::get<std::string>(field.value).c_str());
            // }
            // else
            // {
            //     println("%sFieldName: %s FieldValue[]:", sp.c_str(), field.name.c_str());
            //     printFields(std::get<FieldVec>(field.value), depth + 1);
            // }
        }
    }

private:
    uint64_t decodeVarInt(const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
    {
        uint64_t result{0};
        uint8_t byteCount{0};
        uint8_t varintPart{0};
        while (true)
        {
            varintPart = buffer[currentIndex++];
            /* Construct the number (left to right)
               The last 7 bits are part of the final number. The first bit tells us
               if there's another byte coming to complete the number or not
               First 7 bits we read here will be the last 7 bits of the final number (LSB)
               Last 7 bits we read while forming the number will be the first bits of the final number (LSB) */
            result |= (uint64_t)(varintPart & 0b01111111) << (7 * byteCount);

            bool hasNextByte = varintPart & 0b10000000;

            ++byteCount;

            if (!hasNextByte)
            {
                break;
            }
        }

        return result;
    }

    uint64_t decodeNumber64(const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
    {
        /* Similar to decodeVarint but this is fixed 64bit number. No need for guessing if there's another byte. */
        uint64_t result{0};
        uint8_t byteCount{0};
        uint8_t numberPart{0};

        const int8_t BYTES_8 = 8;
        while (byteCount < BYTES_8)
        {
            numberPart = buffer[currentIndex++];

            result |= (uint64_t)(numberPart) << (8 * byteCount);
            ++byteCount;
        }
        return result;
    }

    std::string getTagString(const TagDecodeResult& tag)
    {
        switch (tag.type)
        {
            case WireType::I32:
                return "I32";
            case WireType::I64:
                return "I64";
            case WireType::VARINT:
                return "VARINT";
            case WireType::LEN:
                return "LEN";
            case WireType::UNKNOWN:
                return "UNKNOWN";
            default:
                return "UNKNOWN";
        }
        return "UNKNOWN";
    }

    Field decode(const XMLDecoder::NodeSPtr& objectNode, const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
    {
        Field field;

        TagDecodeResult tagResult = decodeTag(buffer, currentIndex);

        /* Get also pIndecies which are indecies of the "p" nodes relative to the "objectNode"'s children */
        const auto& [pNodes, pIndices] = objectNode->getTagsNamedIndexed("p");

        /* Get also protoParentIndex which is the index relative to the "pNodes" vector of the found protoNodeParent */
        const auto& [protoNodeParent, protoParentIndex] = XMLDecoder::selfGetDirectChildWithTagAndAttribFromVec(pNodes,
            "proto", {"index", std::to_string(tagResult.fieldNumber)});
        if (!protoNodeParent)
        {
            printlne("ProtoNode not found for field id: %ld", tagResult.fieldNumber);
            return field;
        }

        XMLDecoder::NodeSPtr newObjectNode{nullptr};
        if (protoNodeParent->getAttribValue("type") == "StateInfo")
        {
            printlne("its a struct!");
            // newObjectNode = objectNode->getTagNamedWithAttrib("struct", {"name", "StateInfo"});

            /* "p" nodes of type 'StateInfo' are always guaranteed to have the structure itself one position before
             * them */
            uint32_t oneAboveObjectNode = pIndices[protoParentIndex] - 1;
            newObjectNode = objectNode->children[oneAboveObjectNode];
            if (newObjectNode)
            {
                println("Found state info");
            }
        }

        field.name = protoNodeParent->getAttribValue("name").value_or("UNKNOWN");
        FieldValue decodedPayload = decodePayload(newObjectNode, tagResult, buffer, currentIndex);

        /* Did we decode a proto of an enum field? */
        if (protoNodeParent->children[0]->getAttribValue("type") == "enum")
        {
            uint32_t oneAboveObjectNode = pIndices[protoParentIndex] - 1;
            field.value = objectNode
                              ->children[oneAboveObjectNode]
                              /* tagField-1 because enums are 0 based and protobuf is 1 based */
                              ->getTagNamedWithAttrib("enum", {"value", std::to_string(tagResult.fieldNumber - 1)})
                              ->getAttribValue("name")
                              .value_or("VALUE_NOT_FOUND");
        }
        /* Plain value such as string/number */
        else
        {
            field.value = decodedPayload;
        }
        return field;
    }

    TagDecodeResult decodeTag(const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
    {
        uint8_t tag = buffer[currentIndex++];

        // 0 0 0 0 0 0 0 0
        //           -----  -> wire type
        //   -------        -> field number part
        // -                -> continuation bit
        uint8_t wireTypeMask = 0b00000111;
        uint8_t continuationBitMask = 0b10000000;
        uint8_t fieldNumberMask = 0b01111000;

        WireType type = static_cast<WireType>(tag & wireTypeMask);

        /* Store LSB bits of the field number & check 1st bit of tag to see if we have more bits to form the final
         * number*/
        uint64_t fieldNumber = (tag & fieldNumberMask) >> 3;
        bool moreBitsForFieldNumber = (tag & continuationBitMask) >> 7;

        if (moreBitsForFieldNumber)
        {
            uint64_t restOfFieldBits = decodeVarInt(buffer, currentIndex);

            /* We aleady have the last 4 bits for the decoded vInt will go 4 places higher to form the final number.*/
            fieldNumber |= restOfFieldBits << 4;
        }

        /* Return found data and how much we read. */
        return {.type = type, .fieldNumber = fieldNumber};
    }

    std::string decodeStringPayload(const uint64_t len, const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
    {
        /* Construct string from the next LEN bytes. No need to return bytesRead as it is already known. */
        std::string result(len, '\0');
        for (uint64_t i = 0; i < len; ++i)
        {
            result[i] = buffer[currentIndex++];
        }
        return result;
    }

    FieldValue decodePayload(const XMLDecoder::NodeSPtr& objectNode,
        const TagDecodeResult& decodedTag,
        const std::vector<uint8_t>& buffer,
        uint64_t& currentIndex)
    {
        switch (decodedTag.type)
        {
            case WireType::I32:
                printlne("error32?");
                break;
            case WireType::I64: {
                uint64_t decodedVarint = decodeNumber64(buffer, currentIndex);
                return decodedVarint;
            }
            case WireType::VARINT: {
                uint64_t decodedVarint = decodeVarInt(buffer, currentIndex);
                return decodedVarint;
            }
            case WireType::LEN: {
                /* Decoded length of the LEN payload in bytes*/
                uint64_t payloadLen = decodeVarInt(buffer, currentIndex);

                const bool isSimpleStringAhead = std::isalnum(buffer[currentIndex + 1]);
                if (isSimpleStringAhead)
                {
                    return decodeStringPayload(payloadLen, buffer, currentIndex);
                }
                else
                {
                    FieldVec fv;

                    const uint64_t maxToRead{currentIndex + payloadLen};
                    while (currentIndex < maxToRead)
                    {
                        Field field = decode(objectNode, buffer, currentIndex);
                        fv.emplace_back(field);
                    }
                    return fv;
                }
            }
            break;
            case WireType::UNKNOWN:
                printlne("unknown?");
                break;
        }

        printlne("ERROR WIRE: %d", (uint8_t)decodedTag.type);
        return {};
    }

    void showTag(const TagDecodeResult& tag)
    {
        std::string wireStr;
        switch (tag.type)
        {
            case WireType::I32:
                wireStr = "I32";
                break;
            case WireType::I64:
                wireStr = "I64";
                break;
            case WireType::VARINT:
                wireStr = "VARINT";
                break;
            case WireType::LEN:
                wireStr = "LEN";
                break;
            case WireType::UNKNOWN:
                wireStr = "UNKNOWN";
                break;
            default:
                wireStr = "UNKNOWN";
        }
        printlne("%s:%lu", wireStr.c_str(), tag.fieldNumber);
    }

private:
    XMLDecoder::XmlResult beXmlResult;
    XMLDecoder::XmlResult elXmlResult;
};
} // namespace hk

int main()
{
    // Unfortunatelly lists cannot be trivially decoded without knowing the schema
    // Later edit: we now have means to parse the XML META and figure out if the LEN is bytes/string/repeated

    std::vector<uint8_t> buffer = {10, 6, 8, 0, 16, 0, 24, 0, 16, 221, 32};
    // std::vector<uint8_t> buffer = {10, 8, 8, 0, 16, 1, 24, 0, 32, 1, 18, 5, 8, 221, 32, 24, 0};
    // std::vector<uint8_t> buffer = {10, 8, 8, 1, 16, 1, 24, 0, 32, 1, 18, 15, 18, 13, 65, 115, 105, 97, 47, 83, 104,
    // 97,
    //     110, 103, 104, 97, 105, 24, 1, 80, 2, 104, 18, 121, 0, 0, 0, 0, 0, 0, 0, 0, 144, 1, 1, 160, 1, 0, 192, 1, 0};
    hk::ProtobufDecoder pbDecoder;
    pbDecoder.parseProtobufFromBuffer("CPU", buffer);

    // printlne("EOF %d", protoBin.peek() == EOF);

    return 0;
}