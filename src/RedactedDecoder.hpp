#pragma once

#include <cstdint>
#include <filesystem>

#include "../deps/HkXML/src/HkXml.hpp"
#include "CommonTypes.hpp"
#include "ProtoDecoder.hpp"

namespace hk
{

namespace fs = std::filesystem;

class ChangeData
{

public:
    enum class ChangeType : uint8_t
    {
        CREATE_UPDATE = 0,
        DELETED = 1,
        UNKNOWN = 10
    };

    struct SingleChange
    {
        std::string name{};
        ChangeType type{ChangeType::UNKNOWN};
        uint32_t protoBufSize{0};
        FieldMap fields{};
    };

    struct ChangeSetData
    {
        uint64_t timeStamp{0};
        uint32_t numberOfChanges{0};
        std::vector<SingleChange> changes;
    };

    enum class FrameType : uint8_t
    {
        CHANGE_SET = 0,
        RESET = 1,
        META = 2,
        NODE_DETECTION = 3,
        UNKNOWN = 10
    };

    enum class CompressionType : uint8_t
    {
        NO_COMPRESSION = 0,
        GZIP = 1,
        UNKNOWN = 10
    };

    struct Frame
    {
        FrameType type{FrameType::UNKNOWN};
        CompressionType compression{CompressionType::UNKNOWN};
        uint32_t frameSize{0};
        ChangeSetData changeSetData;
    };

    struct Header
    {
        uint32_t version{0};
        std::string additionalInfo{};
    };

    void loadFromPath(std::ifstream& stream);

private:
    void readFrames(std::ifstream& stream);
    void readMetaType(std::ifstream& stream, const uint64_t size);
    void loadInMetaAsXML(const fs::path metaPath);

    ChangeSetData readChangeSetType(std::ifstream& stream, const CompressionType cType, const uint64_t size);
    ChangeSetData internalReadChangeSetType(std::ifstream& stream, const fs::path& tempPath);

    FieldMap populateChangedFieldsFromProtobuf(const std::string& changePath, const std::vector<uint8_t> bytes);

    bool decompressGZipChangeSetFrame(std::ifstream& stream, uint64_t size, fs::path outputPath);

private:
    XMLDecoder::XmlResult beXmlResult;
    XMLDecoder::XmlResult elXmlResult;
    ProtobufDecoder protoDecoder;

public:
    Header header;
    std::vector<Frame> frames;
};

} // namespace hk
