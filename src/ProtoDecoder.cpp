#include "ProtoDecoder.hpp"

#include "CommonTypes.hpp"
#include "Utility.hpp"
#include <cstdint>
#include <mutex>
#include <string>
#include <variant>

namespace hk
{
FieldMap ProtobufDecoder::parseProtobufFromBuffer(const XMLDecoder::XmlResult& firstXML,
    const XMLDecoder::XmlResult& secondXML,
    const std::string& objectClassName,
    const std::vector<uint8_t>& buffer)
{
    uint64_t currentIndex{0};
    uint64_t bufferSize = buffer.size();
    XMLDecoder::NodeSPtr objectNode{nullptr};

    /* Check the cache first */
    objectsMapLock.lock();
    metaVersion = secondXML.first[0]->nodeName == "?xml" ? 1 : 0;
    if (objectsMap.contains(objectClassName))
    {
        objectNode = objectsMap[objectClassName];
        objectsMapLock.unlock();
    }
    /* Else do the hard work of finding it */
    else
    {
        // printlne("Children %ld", firstXML.first[1]->children.size());
        objectsMapLock.unlock();
        const XMLDecoder::AttrPair searchAttr{"class", objectClassName};
        objectNode = firstXML.first[metaVersion]->getTagNamedWithAttrib("managedObject", searchAttr);
        if (!objectNode)
        {
            objectNode = secondXML.first[metaVersion]->getTagNamedWithAttrib("managedObject", searchAttr);
            if (!objectNode)
            {
                printlne("Couldn't find object named %s nowhere", objectClassName.c_str());
                return {};
            }
        }

        objectsMapLock.lock();
        objectsMap[objectClassName] = objectNode;
        objectsMapLock.unlock();
    }

    // printlne("Object name %s %ld", objectClassName.c_str(), bufferSize);
    FieldMap fieldsMap;
    while (currentIndex < bufferSize)
    {
        resolveTopLevelDecodeResult(fieldsMap, decode(objectNode, buffer, currentIndex));
    }
    // printFields(fieldsMap);
    return fieldsMap;
}

std::vector<FieldMap> ProtobufDecoder::parseProtobuffs(const XMLDecoder::XmlResult& firstXML,
    const XMLDecoder::XmlResult& secondXML,
    const std::vector<std::string>& objectClassNames,
    const std::vector<std::vector<uint8_t>>& buffers)
{
    std::vector<FieldMap> results;
    futures.reserve(buffers.size());

    // for (uint64_t index = 0; const auto& objCn : objectClassNames)
    // {
    //     results.emplace_back(parseProtobufFromBuffer(firstXML, secondXML, objCn, buffers[index++]));
    // }

    for (uint64_t index = 0; const auto& objCn : objectClassNames)
    {
        // printlne("changes: %ld", buffers.size());
        // clang-format off
        futures.emplace_back(
            tp.enqueue(
                std::bind(
                    &ProtobufDecoder::parseProtobufFromBuffer, this,
                    firstXML, secondXML, objCn, buffers[index++]
                    )));
        // clang-format on
    }

    for (auto& future : futures)
    {
        results.emplace_back(future.get());
    }

    futures.clear();

    return results;
}

void ProtobufDecoder::printFields(const FieldMap& fm, uint64_t depth)
{
    std::string sp;
    sp.reserve(depth * 4 + 4);
    for (uint64_t i = 0; i < depth; i++)
    {
        sp += "    ";
    }

    for (const auto& [fieldName, field] : fm)
    {
        if (std::holds_alternative<uint64_t>(field))
        {
            println("%sFieldName: %s FieldValue: %lu", sp.c_str(), fieldName.c_str(), std::get<std::uint64_t>(field));
        }
        else if (std::holds_alternative<double>(field))
        {
            println("%sFieldName: %s: FieldValue: %lf", sp.c_str(), fieldName.c_str(), std::get<double>(field));
        }
        else if (std::holds_alternative<std::string>(field))
        {
            println("%sFieldName: %s FieldValue: %s", sp.c_str(), fieldName.c_str(),
                std::get<std::string>(field).c_str());
        }
        else if (std::holds_alternative<StringVec>(field))
        {
            println("%sFieldName: %s FieldValue:", sp.c_str(), fieldName.c_str());
            for (const auto& x : std::get<StringVec>(field))
            {
                println("%s    %s", sp.c_str(), x.c_str());
            }
        }
        else if (std::holds_alternative<IntegerVec>(field))
        {
            println("%sFieldName: %s FieldValue:", sp.c_str(), fieldName.c_str());
            for (const auto& x : std::get<IntegerVec>(field))
            {
                println("%s    %ld", sp.c_str(), x);
            }
        }
        else if (std::holds_alternative<DoubleVec>(field))
        {
            println("%sFieldName: %s FieldValue:", sp.c_str(), fieldName.c_str());
            for (const auto& x : std::get<DoubleVec>(field))
            {
                println("%s    %lf", sp.c_str(), x);
            }
        }
        else if (std::holds_alternative<FieldMap>(field))
        {
            println("%sFieldName: %s FieldValue{}:", sp.c_str(), fieldName.c_str());
            printFields(std::get<FieldMap>(field), depth + 1);
        }
    }
}

// Protobuf decoding related //

ProtobufDecoder::DecodeResult ProtobufDecoder::decode(const XMLDecoder::NodeSPtr& objectNode,
    const std::vector<uint8_t>& buffer,
    uint64_t& currentIndex)
{
    /* Decoded result to be returned. Since it's a variant, it can have int/double/string/[] forms */
    DecodeResult decodeResult;

    TagDecodeResult tagResult = decodeTag(buffer, currentIndex);

    /* We need to find inside the children of "objectNode" a "p" or "action" node who's "proto" node attribute
     * "index" is equal to tagResult.fieldNumber. This will tell us a lot about what kind of node we are dealing
     * with. Also cache the proto node. */
    XMLDecoder::NodeSPtr pOrActionNode{nullptr};

    /* Keep track at which position relative to other "objectNode" children we found this "p" or "action" node. */
    int64_t pOrActionNodeIndex{0};
    for (const auto& objectNodeChild : objectNode->children)
    {
        if (objectNodeChild->nodeName == "p" || objectNodeChild->nodeName == "action")
        {
            const uint64_t childrenCount = objectNodeChild->children.size();

            /* "proto" will always be the last node.. except for the meta version in which proto in BM is not
             * present..*/
            std::string indexValue;
            if (childrenCount == 0 || objectNodeChild->children[childrenCount - 1]->nodeName != "proto")
            {
                // new BM type..
                indexValue = objectNodeChild->getAttribValue("id").value_or("0");
            }
            else
            {
                const XMLDecoder::NodeSPtr protoNode = objectNodeChild->children[childrenCount - 1];
                indexValue = protoNode->getAttribValue("index").value_or("0");
            }

            /* If "proto" index matches the fieldNumber, then the decoded field name is the "name" attribute of the
             * "p" / "action" node. */
            if (indexValue == std::to_string(tagResult.fieldNumber))
            {
                pOrActionNode = objectNodeChild;
                const std::string fieldName = pOrActionNode->getAttribValue("name").value_or("??");
                decodeResult.name = fieldName;
                // printlne("%s %s name: %s", objectNode->getAttribValue("class")->c_str(),
                // std::to_string(tagResult.fieldNumber).c_str(), fieldName.c_str());
                break;
            }
        }
        pOrActionNodeIndex++;
    }

    /* If we get here, means we did something wrong. There always needs to be a p/action node for a tag field
     * number. In this case some error should be shown.*/
    if (!pOrActionNode)
    {
        // it will enter here for CLOCK, don't care for now
        printlne("pOrActionNode not found for fieldNumber %ld", tagResult.fieldNumber);
        exit(1);
        return decodeResult;
    }

    /* If p/action nodes can have "recurrence" and "type" attributes. "proto" nodes can have "packed" attribute.
       Based on those, we need to decide how to decode further.*/
    const std::string pNodeType = pOrActionNode->getAttribValue("type").value_or("UNKNOWN");
    const std::string pNodeRecurrence = pOrActionNode->getAttribValue("recurrence").value_or("UNKNOWN");
    const bool isIntegerType = pNodeType == "integer";
    const bool isDoubleType = pNodeType == "double";
    const bool isSimpleType = isIntegerType || isDoubleType || pNodeType == "boolean";
    const bool isStringType = pNodeType == "string";
    const bool isFieldRepeated = pNodeRecurrence == "repeated";

    if (isFieldRepeated)
    {
        decodeResult.isRepeated = true;
    }

    if (isSimpleType)
    {
        /* If the simple types are packed, we need to treat them as string_packed hint. */
        const bool protoNodePacked =
            (metaVersion == META_VERSION_TOP_NO_XML && isFieldRepeated) ||
            (isFieldRepeated &&
                pOrActionNode->children[pOrActionNode->children.size() - 1]->getAttribValue("packed").value_or("?") ==
                    "true");

        /* As this isn't a structure of any kind, we can pass "nullptr" as first argument. No need to recurse
         * deeper, decode will always get us an integer/double/bool. No hints are necessary here. */
        const bool isPackedDouble = isDoubleType && protoNodePacked;
        const bool isPackedInteger = isIntegerType && protoNodePacked;
        DecodeHint hint{DecodeHint::NONE};
        if (isPackedDouble)
        {
            hint = DecodeHint::PACKED_DOUBLE;
        }
        else if (isPackedInteger)
        {
            hint = DecodeHint::PACKED_ENUM;
        }
        else if (isStringType)
        {
            hint = DecodeHint::STRING_OR_PACKED;
        }
        FieldValue decodedPayload = decodePayload(nullptr, tagResult, buffer, hint, currentIndex);

        /* In the future we can adapt "decodePayload" to automatically give back a double based on hint, but for
        now, we need to cast it outselves from int -> double. */
        if (isDoubleType && !isPackedDouble)
        {
            double d;
            std::memcpy(&d, &std::get<uint64_t>(decodedPayload), sizeof(d));
            decodeResult.field.second = d;
        }
        else
        {
            decodeResult.field.second = decodedPayload;
        }

        /* Nothing to be done. Proceed to next tag-value pair.*/
        return decodeResult;
    }
    else if (isStringType)
    {
        /* We can still pass "nullptr" as we don't need to recurse down on anything, but the hint is now set as
         * this is a special LEN decoding path. */
        FieldValue decodedPayload = decodePayload(nullptr, tagResult, buffer, DecodeHint::STRING_OR_PACKED,
            currentIndex);
        decodeResult.field.second = decodedPayload;

        /* Nothing to be done. Proceed to next tag-value pair.*/
        return decodeResult;
    }
    else // enums/structs
    {
        const bool protoNodePacked =
            (metaVersion == META_VERSION_TOP_NO_XML && isFieldRepeated) ||
            (isFieldRepeated &&
                pOrActionNode->children[pOrActionNode->children.size() - 1]->getAttribValue("packed").value_or("?") ==
                    "true");

        /* It's either a Structure or an Enum type which are encoded as LEN wire type */
        /* We need to recurse down on the object one above "p"/"action" node. However if that node doesn't
         * exist, we kinda have a problem. We shall not get to this point. Some objects don't follow the META
         * correctly and we will end up here. Skip for those (GNSS). */
        if (pOrActionNodeIndex - 1 < 0)
        {
            printlne("One above index is less than zero!");
            exit(1);
            return decodeResult;
        }

        /* If it was an enum, decode it's value*/
        if (pOrActionNodeIndex - 1 >= 0 && objectNode->children[pOrActionNodeIndex - 1]->nodeName == "enumeration")
        {
            FieldValue decodedPayload = decodePayload(objectNode, tagResult, buffer,
                protoNodePacked ? DecodeHint::PACKED_ENUM : DecodeHint::NONE, currentIndex);
            decodeResult.field.second = decodedPayload;

            XMLDecoder::NodeSPtr nodeAbovePNode = objectNode->children[pOrActionNodeIndex - 1];
            if (std::holds_alternative<IntegerVec>(decodedPayload))
            {
                StringVec sv;
                for (const uint64_t& i : std::get<IntegerVec>(decodedPayload))
                {
                    const XMLDecoder::NodeSPtr& enumNode = nodeAbovePNode->getTagNamedWithAttrib("enum",
                        {"value", std::to_string(i)});
                    if (!enumNode)
                    {
                        printlnHex(buffer);
                        printlne("Didn't find any enum matching description %ld", i);
                        exit(1);
                        return decodeResult;
                    }

                    std::string decodedEnumVal = enumNode->getAttribValue("name").value_or("VALUE_NOT_FOUND");
                    sv.emplace_back(decodedEnumVal);
                }
                decodeResult.field.second = sv;
            }
            else
            {
                uint64_t enumVal = std::get<uint64_t>(decodeResult.field.second);
                const XMLDecoder::NodeSPtr& enumNode = nodeAbovePNode->getTagNamedWithAttrib("enum",
                    {"value", std::to_string(enumVal)});

                /* If this is the case, means one above isn't directly an enumeration. We shall find the
                   referenced enum, but it's slow and not many meta objects do this, so idk */
                if (!enumNode)
                {
                    printlnHex(buffer);
                    printlne("Didn't find any enum matching description %ld", enumVal);
                    exit(1);
                    return decodeResult;
                }
                decodeResult.field.second = enumNode->getAttribValue("name").value_or("VALUE_NOT_FOUND");
            }

            return decodeResult;
        } // enum

        /* This object is gonna play as the struct/enum above the "p"/"action" node from where we will get our
         * next values. We are nesting.*/
        XMLDecoder::NodeSPtr nodeAbovePNode = objectNode->children[pOrActionNodeIndex - 1];
        FieldValue decodedPayload = decodePayload(nodeAbovePNode, tagResult, buffer, DecodeHint::NONE, currentIndex);
        decodeResult.field.second = decodedPayload;

        /* Nothing to be done. Proceed to next tag-value pair.*/
        return decodeResult;
    }
    return decodeResult;
}

void ProtobufDecoder::resolveTopLevelDecodeResult(FieldMap& fieldMap, const DecodeResult& decodeResult)
{
    const auto& [fieldName, repeated, decodedField] = decodeResult;
    auto& field = fieldMap[fieldName];

    if (std::holds_alternative<std::string>(decodedField.second) && repeated)
    {
        if (!std::holds_alternative<StringVec>(field))
        {
            field = StringVec{};
        }
        std::get<StringVec>(field).emplace_back(std::get<std::string>(decodedField.second));
    }
    else if (std::holds_alternative<uint64_t>(decodedField.second) && repeated)
    {
        if (!std::holds_alternative<IntegerVec>(field))
        {
            field = IntegerVec{};
        }
        std::get<IntegerVec>(field).emplace_back(std::get<uint64_t>(decodedField.second));
    }
    else if (std::holds_alternative<double>(decodedField.second) && repeated)
    {
        if (!std::holds_alternative<DoubleVec>(field))
        {
            field = DoubleVec{};
        }
        std::get<DoubleVec>(field).emplace_back(std::get<double>(decodedField.second));
    }
    else if (std::holds_alternative<FieldMap>(decodedField.second))
    {
        fieldMap[fieldName] = decodedField.second;
    }
    else
    {
        field = decodedField.second;
    }
}

uint64_t ProtobufDecoder::decodeVarInt(const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
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

uint64_t ProtobufDecoder::decodeNumber64(const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
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

std::string ProtobufDecoder::getTagString(const TagDecodeResult& tag)
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

ProtobufDecoder::TagDecodeResult ProtobufDecoder::decodeTag(const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
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

std::string
ProtobufDecoder::decodePackedPayload(const uint64_t len, const std::vector<uint8_t>& buffer, uint64_t& currentIndex)
{
    /* Construct string from the next LEN bytes. No need to return bytesRead as it is already known. */
    std::string result(len, '\0');
    for (uint64_t i = 0; i < len; ++i)
    {
        result[i] = buffer[currentIndex++];
    }
    return result;
}

FieldValue ProtobufDecoder::decodePayload(const XMLDecoder::NodeSPtr& objectNode,
    const TagDecodeResult& decodedTag,
    const std::vector<uint8_t>& buffer,
    const DecodeHint hint,
    uint64_t& currentIndex)
{
    switch (decodedTag.type)
    {
        case WireType::I32:
            printlne("I32 decoding not implemeted yet");
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
            if (hint == DecodeHint::STRING_OR_PACKED)
            {
                return decodePackedPayload(payloadLen, buffer, currentIndex);
            }
            else if (hint == DecodeHint::PACKED_DOUBLE)
            {
                DoubleVec doubleVec;
                const uint64_t maxToRead{currentIndex + payloadLen};
                while (currentIndex < maxToRead)
                {
                    uint64_t decodedVarint = decodeNumber64(buffer, currentIndex);
                    double d;
                    std::memcpy(&d, &decodedVarint, sizeof(d));
                    doubleVec.emplace_back(d);
                }
                return doubleVec;
            }
            else if (hint == DecodeHint::PACKED_ENUM)
            {
                IntegerVec integerVec;
                const uint64_t maxToRead{currentIndex + payloadLen};
                while (currentIndex < maxToRead)
                {
                    uint64_t decodedVarint = decodeVarInt(buffer, currentIndex);
                    integerVec.emplace_back(decodedVarint);
                }
                return integerVec;
            }
            else
            {
                FieldMap fieldsMap;
                const uint64_t maxToRead{currentIndex + payloadLen};
                while (currentIndex < maxToRead)
                {
                    resolveTopLevelDecodeResult(fieldsMap, decode(objectNode, buffer, currentIndex));
                }
                return fieldsMap;
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

} // namespace hk