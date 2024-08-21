#include "Utility.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
    using FieldValue = std::variant<uint64_t, std::string, double, FieldVec>;

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
        elXmlResult = XMLDecoder().decodeFromStream(elMeta);
        if (!elXmlResult.second.empty())
        {
            printlne("Error while parsing XML: %s", elXmlResult.second.c_str());
            elXmlResult = XMLDecoder::XmlResult{}; // reset it to nothing
            return;
        }
        println("Loading meta XML done");
    }

    void parseProtobufFromBuffer(const std::string& objectName, const std::vector<uint8_t>& buffer)
    {
        uint64_t currentIndex{0};
        uint64_t bufferSize = buffer.size();

        XMLDecoder::NodeSPtr objectNode = elXmlResult.first[1]->getTagNamedWithAttrib("managedObject",
            {"class", objectName});
        if (!objectNode)
        {
            printlne("Couldn't find object named %s", objectName.c_str());
            return;
        }

        FieldVec fv;

        while (currentIndex < bufferSize)
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
            // if (std::holds_alternative<uint64_t>(field.value))
            // {
            //     println("%s%s: %lu", sp.c_str(), field.name.c_str(), std::get<uint64_t>(field.value));
            // }
            // else if (std::holds_alternative<double>(field.value))
            // {
            //     println("%s%s: %lf", sp.c_str(), field.name.c_str(), std::get<double>(field.value));
            // }
            // else if (std::holds_alternative<std::string>(field.value))
            // {
            //     println("%s%s: %s", sp.c_str(), field.name.c_str(), std::get<std::string>(field.value).c_str());
            // }
            // else
            // {
            //     println("%s%s []:", sp.c_str(), field.name.c_str());
            //     printFields(std::get<FieldVec>(field.value), depth + 1);
            // }
            if (std::holds_alternative<uint64_t>(field.value))
            {
                println("%sFieldName: %s FieldValue: %lu", sp.c_str(), field.name.c_str(),
                    std::get<std::uint64_t>(field.value));
            }
            else if (std::holds_alternative<double>(field.value))
            {
                println("%sFieldName%s: FieldValue:%lf", sp.c_str(), field.name.c_str(), std::get<double>(field.value));
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

        println("TAG FIELD NR IS: %ld %s %s", tagResult.fieldNumber, objectNode->nodeName.c_str(),
            objectNode->getAttribValue("name").value_or("idk").c_str());

        // find "p" for which child "proto"'s "index" value is tagResult.fieldNumber
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
                    field.name = std::to_string(tagResult.fieldNumber) + "-" + fieldName;
                    printlne("Name of p: %s", fieldName.c_str());
                    break;
                }
            }
            pNodeIndex++;
        }

        if (!pNode) // pNode or actionNode :)
        {
            // we either as deep as we can go in LEN or "p" is really missing in xml
            FieldValue decodedPayload = decodePayload(nullptr, tagResult, buffer, currentIndex);
            field.value = decodedPayload;
            exit(1);
            return field;
        }

        // decode the value now
        const std::string pNodeType = pNode->getAttribValue("type").value_or("UNKNOWN");
        /* If it's a simple value, easily decode it. (non LEN proto type)*/
        if (pNodeType == "integer" || pNodeType == "double" || pNodeType == "boolean")
        {
            /* In this case we can decode stuff directly. We will not need an object to "dig" deeper into. So nullptr
             * can be passed. */
            FieldValue decodedPayload = decodePayload(nullptr, tagResult, buffer, currentIndex);
            field.value = decodedPayload;
        }
        /* Otherwise it will be encoded as a LEN somehow (enums/strings/lists)*/
        else
        {
            // enums/structs are encoded as LEN
            if (pNodeIndex - 1 < 0)
            {
                printlne("One above index is less than zero!");
                return field;
            }

            XMLDecoder::NodeSPtr nodeAbovePNode = objectNode->children[pNodeIndex - 1];
            FieldValue decodedPayload = decodePayload(nodeAbovePNode, tagResult, buffer, currentIndex);
            field.value = decodedPayload;
        }

        return field;
    }
    // Field decode(const XMLDecoder::NodeSPtr& objectNode, const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
    // {
    //     /* Decoded field to be returned. Since it's a variant, it can have int/double/string/[] forms */
    //     Field field;

    //     TagDecodeResult tagResult = decodeTag(buffer, currentIndex);

    //     println("TAG FIELD NR IS: %ld %s", tagResult.fieldNumber, objectNode->nodeName.c_str());

    //     /* Get also pIndecies which are indecies of the "p" nodes relative to the "objectNode"'s children */
    //     const auto& [pNodes, pIndices] = objectNode->getTagsNamedIndexed("p");

    //     /* Get also protoParentIndex which is the index relative to the "pNodes" vector of the found protoNodeParent
    //     */ const auto& [protoNodeParent, protoParentIndex] =
    //     XMLDecoder::selfGetDirectChildWithTagAndAttribFromVec(pNodes,
    //         "proto", {"index", std::to_string(tagResult.fieldNumber)});
    //     if (!protoNodeParent)
    //     {
    //         printlne("ProtoNode not found for field id: %ld", tagResult.fieldNumber);
    //         return field;
    //     }

    //     XMLDecoder::NodeSPtr newObjectNode{objectNode};
    //     std::string protoParentType = protoNodeParent->getAttribValue("type").value_or(""); // todo check for empty
    //     if (protoParentType.empty())
    //     {
    //         printlne("its fking empty");
    //     }

    //     printlne("What is it: %s %s", protoParentType.c_str(), objectNode->nodeName.c_str());
    //     if (protoParentType != "integer" && protoParentType != "double" && protoParentType != "boolean")
    //     {
    //         printlne("entered %s", protoParentType.c_str());
    //         /* "p" nodes of type != integer & != string are always guaranteed to have the structure itself one
    //          * position before them. Except for some cases in the BM xml (bm/CLOCK) */
    //         uint32_t oneAboveObjectNode = pIndices[protoParentIndex] - 1;

    //         // printlne("Index is %d %ld", oneAboveObjectNode, objectNode->children.size());
    //         newObjectNode = objectNode->children[oneAboveObjectNode];
    //         if (newObjectNode)
    //         {
    //             println("Found state info");
    //         }
    //         else
    //         {
    //             printlne("not dound");
    //         }
    //     }
    //     // else
    //     // {
    //     //     printlne("Something else: %s %s", protoParentType.c_str(), objectNode->nodeName.c_str());
    //     // }

    //     field.name = std::to_string(tagResult.fieldNumber) +
    //                  protoNodeParent->getAttribValue("name").value_or("UNKNOWN");
    //     printlne("protoNodeParent name %s", field.name.c_str());

    //     FieldValue decodedPayload = decodePayload(newObjectNode, tagResult, buffer, currentIndex);

    //     /* Did we decode a proto of an enum field? */
    //     if (protoNodeParent->children[0]->getAttribValue("type") == "enum")
    //     {
    //         uint32_t oneAboveObjectNode = pIndices[protoParentIndex] - 1;
    //         // field.value = decodedPayload;
    //         uint64_t enumVal = std::get<0>(decodedPayload);
    //         field.value = objectNode->children[oneAboveObjectNode]
    //                           ->getTagNamedWithAttrib("enum", {"value", std::to_string(enumVal)})
    //                           ->getAttribValue("name")
    //                           .value_or("VALUE_NOT_FOUND");
    //     }
    //     /* Plain value such as string/number */
    //     else
    //     {
    //         field.value = decodedPayload;
    //     }

    //     return field;
    // }

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

                bool isSimpleStringAhead = std::isalnum(buffer[currentIndex]);
                if (isSimpleStringAhead)
                {
                    // for (int i = 0; i < payloadLen; i++)
                    printlne("CREDEM CA E STRING: %c", buffer[currentIndex]);
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

    // std::vector<uint8_t> buffer = {10, 6, 8, 0, 16, 0, 24, 0, 16, 221, 32};
    // std::vector<uint8_t> buffer = {10, 8, 8, 0, 16, 1, 24, 0, 32, 1, 18, 5, 8, 221, 32, 24, 0};
    // std::vector<uint8_t> buffer = {10, 8, 8, 1, 16, 1, 24, 0, 32, 1, 18, 15, 18, 13, 65, 115, 105, 97, 47, 83, 104,
    // 97,
    //     110, 103, 104, 97, 105, 24, 1, 80, 2, 104, 18, 121, 0, 0, 0, 0, 0, 0, 0, 0, 144, 1, 1, 160, 1, 0, 192, 1, 0};
    // std::vector<uint8_t> buffer = {0, 74, 47, 77, 82, 66, 84, 83, 45, 49, 47, 82, 65, 84, 45, 49, 47, 69, 81, 77, 95,
    //     76, 45, 49, 47, 82, 85, 95, 76, 45, 49, 47, 86, 85, 66, 85, 83, 95, 76, 45, 56, 47, 86, 85, 95, 76, 45, 49,
    //     47, 72, 87, 80, 79, 82, 84, 95, 76, 45, 49, 0, 0, 0, 0, 28, 10, 8, 8, 1, 16, 1, 24, 0, 32, 2, 16, 0, 24, 0,
    //     32, 7, 40, 181, 201, 1, 48, 1, 56, 1, 64, 1, 80, 1, 0, 66, 47, 77, 82, 66, 84, 83, 45, 49, 47, 82, 65, 84,
    //     45, 49, 47, 69, 81, 77, 95, 76, 45, 49, 47, 82, 77, 79, 68, 95, 76, 45, 50, 47, 67, 79, 78, 78, 69, 67, 84,
    //     79, 82, 95, 76, 45, 50, 48, 55, 47, 72, 87, 80, 79, 82, 84, 95, 76, 45, 49, 0, 0, 0, 0, 28, 10, 8, 8, 1, 16,
    //     1, 24, 0, 32, 2, 16, 0, 24, 0, 32, 7, 40, 181, 201, 1, 48, 1, 56, 1, 64, 1, 80, 1, 0, 78, 47, 77, 82, 66, 84,
    //     83, 45, 49, 47, 82, 65, 84, 45, 49, 47, 66, 84, 83, 95, 76, 45, 49, 47, 69, 81, 77, 95, 76, 45, 49, 47, 82,
    //     77, 79, 68, 95, 76, 45, 49, 47, 82, 85, 95, 76, 45, 49, 47, 72, 68, 76, 67, 66, 85, 83, 95, 76, 45, 49, 47,
    //     65, 76, 68, 85, 95, 76, 45, 49, 47, 72, 87, 80, 79, 82, 84, 95, 76, 45, 49, 0, 0, 0, 0, 28, 10, 8, 8, 1, 16,
    //     1, 24, 0, 32, 2, 16, 0, 24, 0, 32, 7, 40, 181, 201, 1, 48, 1, 56, 1, 64, 1, 80, 1, 0, 56, 47, 77, 82, 66, 84,
    //     83, 45, 49, 47, 82, 65, 84, 45, 49, 47, 66, 84, 83, 95, 76, 45, 49, 47, 69, 81, 77, 95, 76, 45, 49, 47, 82,
    //     77, 79, 68, 95, 76, 45, 49, 47, 82, 85, 95, 76, 45, 49, 47, 86, 85, 66, 85, 83, 95, 76, 45, 49, 0, 0, 0, 0,
    //     10, 10, 8, 8, 0, 16, 0, 24, 0, 32, 2, 0, 72, 47, 77, 82, 66, 84, 83, 45, 49, 47, 82, 65, 84, 45, 49, 47, 66,
    //     84, 83, 95, 76};
    // std::vector<uint8_t> buffer = {10, 10, 8, 1, 16, 1, 24, 0, 32, 1, 40, 0, 18, 6, 8, 0, 16, 1, 24, 1, 18, 6, 8, 6,
    // 16,
    //     1, 24, 1, 18, 6, 8, 7, 16, 1, 24, 1, 26, 4, 8, 0, 16, 2, 26, 4, 8, 6, 16, 0, 26, 4, 8, 7, 16, 0, 32, 0, 80,
    //     0, 130, 1, 43, 47, 77, 82, 66, 84, 83, 45, 49, 47, 69, 81, 77, 45, 49, 47, 83, 77, 79, 68, 45, 52, 49, 49,
    //     51, 47, 67, 67, 85, 45, 49, 47, 79, 83, 67, 73, 76, 76, 65, 84, 79, 82, 45, 49, 168, 1, 0};
    std::vector<uint8_t> buffer = {8, 0, 16, 0, 25, 0, 0, 0, 0, 0, 0, 0, 0, 33, 0, 0, 0, 0, 0, 0, 0, 0, 42, 4, 84, 120,
        71, 56, 48, 2, 65, 0, 0, 0, 0, 216, 248, 21, 65, 73, 0, 0, 0, 0, 128, 192, 20, 65, 82, 33, 49, 48, 44, 49, 53,
        44, 50, 48, 44, 51, 48, 44, 52, 48, 44, 53, 48, 44, 54, 48, 44, 55, 48, 44, 56, 48, 44, 57, 48, 44, 49, 48, 48,
        89, 0, 0, 0, 0, 0, 0, 89, 64, 89, 0, 0, 0, 0, 0, 0, 105, 64, 89, 0, 0, 0, 0, 0, 192, 114, 64, 89, 0, 0, 0, 0, 0,
        0, 121, 64, 89, 0, 0, 0, 0, 0, 64, 127, 64, 89, 0, 0, 0, 0, 0, 192, 130, 64, 89, 0, 0, 0, 0, 0, 224, 133, 64,
        89, 0, 0, 0, 0, 0, 0, 137, 64, 89, 0, 0, 0, 0, 0, 32, 140, 64, 89, 0, 0, 0, 0, 0, 64, 143, 64, 114, 96, 8, 5,
        18, 92, 8, 78, 17, 0, 0, 0, 0, 0, 0, 89, 64, 17, 0, 0, 0, 0, 0, 0, 105, 64, 17, 0, 0, 0, 0, 192, 114, 64, 17, 0,
        0, 0, 0, 0, 0, 121, 64, 17, 0, 0, 0, 0, 64, 127, 64, 17, 0, 0, 0, 0, 192, 130, 64, 17, 0, 0, 0, 0, 224, 133, 64,
        17, 0, 0, 0, 0, 0, 0, 137, 64, 17, 0, 0, 0, 0, 32, 140, 64, 17, 0, 0, 0, 0, 64, 143, 64, 114, 33, 8, 0, 18, 29,
        8, 42, 17, 0, 0, 0, 0, 0, 0, 89, 64, 17, 0, 0, 0, 0, 192, 98, 64, 17, 0, 0, 0, 0, 0, 0, 105, 64, 122, 41, 8,
        144, 78, 8, 160, 156, 1, 8, 176, 234, 1, 8, 192, 184, 2, 8, 208, 134, 3, 8, 224, 212, 3, 8, 240, 162, 4, 8, 128,
        241, 4, 8, 144, 191, 5, 8, 160, 141, 6, 16, 5, 122, 12, 8, 144, 78, 8, 152, 117, 8, 160, 156, 1, 16, 0, 128, 1,
        4, 144, 1, 3, 152, 1, 0, 169, 1, 0, 0, 0, 0, 0, 136, 227, 64, 185, 1, 0, 0, 0, 0, 0, 249, 21, 65, 185, 1, 0, 0,
        0, 0, 128, 19, 28, 65, 201, 1, 0, 0, 0, 0, 0, 0, 240, 63, 209, 1, 0, 0, 0, 0, 136, 211, 64, 217, 1, 0, 0, 0, 0,
        136, 211, 64, 226, 1, 15, 8, 4, 18, 11, 8, 78, 17, 0, 0, 0, 0, 0, 64, 159, 64, 226, 1, 15, 8, 0, 18, 11, 8, 42,
        17, 0, 0, 0, 0, 0, 64, 159, 64, 234, 1, 24, 8, 3, 18, 20, 8, 78, 17, 0, 0, 0, 0, 0, 154, 224, 64, 25, 0, 0, 0,
        0, 224, 147, 225, 64, 234, 1, 24, 8, 0, 18, 20, 8, 42, 17, 0, 0, 0, 0, 0, 154, 224, 64, 25, 0, 0, 0, 0, 224,
        147, 225, 64, 128, 2, 1, 176, 2, 0, 185, 2, 0, 0, 0, 0, 0, 0, 0, 0, 192, 2, 0, 217, 2, 0, 0, 0, 0, 0, 0, 0, 0};
    hk::ProtobufDecoder pbDecoder;
    // pbDecoder.parseProtobufFromBuffer("TIME", buffer);
    pbDecoder.parseProtobufFromBuffer("TX_PU_BP_L", buffer);

    // printlne("EOF %d", protoBin.peek() == EOF);

    return 0;
}