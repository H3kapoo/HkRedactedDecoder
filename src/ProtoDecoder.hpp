#include "Utility.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
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

    void referenceXMLs(const XMLDecoder::XmlResult& firstXML, const XMLDecoder::XmlResult& secondXML)
    {
        beXmlResult = firstXML;
        elXmlResult = secondXML;
    }

    FieldVec parseProtobufFromBuffer(const std::string& objectName, const std::vector<uint8_t>& buffer)
    {
        uint64_t currentIndex{0};
        uint64_t bufferSize = buffer.size();

        XMLDecoder::NodeSPtr objectNode{nullptr};
        /* Check the cache first */
        if (objectsMap.contains(objectName))
        {
            objectNode = objectsMap[objectName];
        }
        /* Else do the hard work of finding it */
        else
        {
            objectNode = beXmlResult.first[1]->getTagNamedWithAttrib("managedObject", {"class", objectName});
            if (!objectNode)
            {
                objectNode = elXmlResult.first[1]->getTagNamedWithAttrib("managedObject", {"class", objectName});
                if (!objectNode)
                {
                    printlne("Couldn't find object named %s nowhere", objectName.c_str());
                    return {};
                }
            }
            objectsMap[objectName] = objectNode;
        }

        FieldVec fv;

        // printlne("Object name %s %ld", objectName.c_str(), bufferSize);
        while (currentIndex < bufferSize)
        {
            // objectNode - managed object 'objectName' in this case
            Field f = decode(objectNode, buffer, currentIndex);
            fv.emplace_back(f);
        }
        // printFields(fv);
        return fv;
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
                println("%sFieldName: %s FieldValue: %lu", sp.c_str(), field.name.c_str(),
                    std::get<std::uint64_t>(field.value));
            }
            else if (std::holds_alternative<double>(field.value))
            {
                println("%sFieldName: %s: FieldValue: %lf", sp.c_str(), field.name.c_str(),
                    std::get<double>(field.value));
            }
            else if (std::holds_alternative<std::string>(field.value))
            {
                println("%sFieldName: %s FieldValue: %s", sp.c_str(), field.name.c_str(),
                    std::get<std::string>(field.value).c_str());
            }
            else
            {
                println("%sFieldName: %s FieldValue[]:", sp.c_str(), field.name.c_str());
                printFields(std::get<FieldVec>(field.value), depth + 1);
            }
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
        /* Decoded field to be returned. Since it's a variant, it can have int/double/string/[] forms */
        Field field;

        TagDecodeResult tagResult = decodeTag(buffer, currentIndex);

        // println("TAG FIELD NR IS: %ld %s %s", tagResult.fieldNumber, objectNode->nodeName.c_str(),
        //     objectNode->getAttribValue("name").value_or("idk").c_str());

        /* Find "p" for which child "proto"'s "index" value is tagResult.fieldNumber */
        XMLDecoder::NodeSPtr pNode{nullptr};
        int64_t pNodeIndex{0};
        for (const auto& objectNodeChild : objectNode->children)
        {
            if (objectNodeChild->nodeName == "p" || objectNodeChild->nodeName == "action")
            {
                const uint64_t childrenCount = objectNodeChild->children.size();
                const XMLDecoder::NodeSPtr protoNode = objectNodeChild->children[childrenCount - 1]; // get last element

                const std::string indexValue = protoNode->getAttribValue("index").value_or("0");
                if (std::atoi(indexValue.c_str()) == (int)tagResult.fieldNumber)
                {
                    pNode = objectNodeChild;
                    const std::string fieldName = pNode->getAttribValue("name").value_or("??");
                    // field.name = std::to_string(tagResult.fieldNumber) + "-" + fieldName;
                    field.name = fieldName;
                    // printlne("Name of p: %s", fieldName.c_str());
                    break;
                }
            }
            pNodeIndex++;
        }

        if (!pNode) // pNode or actionNode :)
        {
            // it will enter here for CLOCK, don't care for now
            // printlne("WHERE P FIELD WE NEED %ld", tagResult.fieldNumber);
            // exit(1);
            return field;
        }

        /* If it's a simple value, easily decode it. (non LEN proto type)*/
        const std::string pNodeType = pNode->getAttribValue("type").value_or("UNKNOWN");
        const std::string pNodeRecurrence = pNode->getAttribValue("recurrence").value_or("UNKNOWN");
        if (pNodeRecurrence != "repeated" &&
            (pNodeType == "integer" || pNodeType == "double" || pNodeType == "boolean"))
        {
            // printlne("entered here");
            FieldValue decodedPayload = decodePayload(nullptr, tagResult, buffer, false, currentIndex);

            if (pNodeType == "double")
            {
                double d;
                std::memcpy(&d, &std::get<0>(decodedPayload), sizeof(d));
                field.value = d;
            }
            else
            {
                field.value = decodedPayload;
            }
            // printlne("decoded load: %ld", std::get<0>(field.value));
        }
        /* Otherwise it will be encoded as a LEN somehow (enums/strings/lists)*/
        else
        {
            // printlne("entered wew %s %s %s", field.name.c_str(), pNodeType.c_str(), pNodeRecurrence.c_str());
            if (pNodeType == "string")
            {
                FieldValue decodedPayload = decodePayload(objectNode, tagResult, buffer, true, currentIndex);
                field.value = decodedPayload;
            }
            else
            {
                const auto protoNodePacked =
                    pNode->children[pNode->children.size() - 1]->getAttribValue("packed").value_or("?") == "true";
                // needs to be treated like a string, they are packed, not tag/val pair anymore
                if (protoNodePacked && pNodeRecurrence == "repeated")
                {
                    // printlne("just decode as rep int");
                    FieldValue decodedPayload = decodePayload(objectNode, tagResult, buffer, true, currentIndex);
                    field.value = decodedPayload;
                }
                else
                {
                    // enums/structs are encoded as LEN
                    if (pNodeIndex - 1 < 0)
                    {
                        // some will end here (such as GNSS) because meta layout is inconsistent and the enum values are
                        // not directly above "p" like they should be. It can be specially handled, but not sure if
                        // worth it
                        // printlne("One above index is less than zero!");
                        // exit(1);
                        return field;
                    }
                    XMLDecoder::NodeSPtr nodeAbovePNode = objectNode->children[pNodeIndex - 1];
                    FieldValue decodedPayload = decodePayload(nodeAbovePNode, tagResult, buffer, false, currentIndex);
                    field.value = decodedPayload;

                    // check if it was ENUM to decode
                    if (pNode->children[0]->getAttribValue("type") == "enum")
                    {
                        uint64_t enumVal = std::get<0>(field.value);
                        // printlne("value %ld node %s", enumVal, nodeAbovePNode->getAttribValue("name")->c_str());
                        const auto node = nodeAbovePNode->getTagNamedWithAttrib("enum",
                            {"value", std::to_string(enumVal)});

                        // if this is the case, means one avobe isn't directly an enumeration. We shall find the
                        // referenced enum, but it's slow and not many meta objects do this, so idk
                        if (!node)
                        {
                            return field;
                        }
                        field.value = node->getAttribValue("name").value_or("VALUE_NOT_FOUND");
                    }
                }
            }
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
        const bool stringOrPacked,
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

                if (stringOrPacked)
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
    std::unordered_map<std::string, XMLDecoder::NodeSPtr> objectsMap;
};
} // namespace hk
