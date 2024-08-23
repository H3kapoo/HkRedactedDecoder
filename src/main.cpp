#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <variant>
#include <vector>

#include <minizip/unzip.h>
#include <zlib.h>

#include "../deps/HkXML/src/HkXml.hpp"
#include "ProtoDecoder.hpp"
#include "Utility.hpp"

namespace hk
{

namespace fs = std::filesystem;

class ChangesData
{

public:
    enum class ChangeType : uint8_t
    {
        CREATE_UPDATE = 0,
        DELETED = 1,
        UNKNOWN = 10
    };

    // struct Field;
    // using FieldVec = std::vector<Field>;
    // using FieldValue = std::variant<std::string, FieldVec>;
    // struct Field
    // {
    //     std::string name;
    //     FieldValue value;
    // };

    struct SingleChange
    {
        std::string name{}; // maybe path?
        ChangeType type{ChangeType::UNKNOWN};
        uint32_t protoBufSize{0};
        std::vector<uint8_t> protoBufData{};
        FieldVec fields{};
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

    void loadFromPath(std::ifstream& stream)
    {
        // header section
        header.version = utils::read4(stream);
        utils::read4(stream); /* Unused */
        uint32_t additionalInfoSize = utils::read4(stream);
        header.additionalInfo = utils::readStringBytes(stream, additionalInfoSize);

        readFrames(stream);
    }

private:
    void readFrames(std::ifstream& stream)
    {
        uint64_t frameCount = 0;

        while (stream.peek() != EOF)
        {
            // each frame starts with a magic number
            bool magic = utils::isMagicNumberNext(stream);
            if (!magic)
            {
                printlne("Something bad happened while reading frames. Not magic number.");
                return;
            }

            Frame frame;
            frame.type = static_cast<FrameType>(utils::read4(stream));
            frame.compression = static_cast<CompressionType>(utils::read4(stream));
            frame.frameSize = utils::read4(stream);

            // println("FrameType: %d | Compressed: %d | FrameSize: %d", (uint8_t)frame.type,
            // (uint8_t)frame.compression,
            //     frame.frameSize);
            if (frame.type == FrameType::META)
            {
                readMetaType(stream, frame.frameSize);
                loadInMetaAsXML("metaTmp");

                // for (const XMLDecoder::NodeSPtr& node : res.first)
                // {
                //     node->show();
                // }
                // return;
            }
            else if (frame.type == FrameType::CHANGE_SET)
            {
                frame.changeSetData = std::move(readChangeSetType(stream, frame.compression, frame.frameSize));
            }
            else
            {
                printlne("Reading %d frame type not supported. Skip", (uint8_t)frame.type);
                stream.seekg(frame.frameSize, std::ios::cur);
                continue; // go back at the top
            }

            frameCount++;
        }

        // println("Frame count: %ld", frameCount);
    }

    void readMetaType(std::ifstream& stream, const uint64_t size)
    {
        println("Unzipping meta..");

        fs::path metaTmpFolderPath = "metaTmp/";
        fs::path metaZipName = "meta.zip";
        fs::path metaZipPath = metaTmpFolderPath / metaZipName;
        fs::create_directories(metaTmpFolderPath.parent_path());

        /* Read .zip into a file */
        std::ofstream outMeta{metaZipPath};
        char* mBuffer = new char[size];
        stream.read(mBuffer, size);
        outMeta.write(mBuffer, size);
        outMeta.close();

        /* cleanup */
        delete[] mBuffer;

        /* Open file and prepare minizip to unzip it*/
        unzFile zipFile = unzOpen(metaZipPath.c_str());
        if (zipFile == nullptr)
        {
            printlne("Failed to open zip file at %s", metaZipPath.c_str());
            return;
        }

        unz_global_info globalInfo;
        if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK)
        {
            printlne("Failed to get global info");
            unzClose(zipFile);
            return;
        }

        /* Read files inside zip */
        std::string fileName(256, '\0');
        for (uint64_t i = 0; i < globalInfo.number_entry; i++)
        {
            if (unzGetCurrentFileInfo64(zipFile, nullptr, fileName.data(), fileName.size(), nullptr, 0, nullptr, 0) !=
                UNZ_OK)
            {
                printlne("Failed to get file info");
                unzClose(zipFile);
                return;
            }

            /* Construct path of found file */
            fs::path filePath = metaTmpFolderPath / fileName;

            if (unzOpenCurrentFile(zipFile) != UNZ_OK)
            {
                printlne("Failed to open file %s inside the zip", fileName.c_str());
                unzClose(zipFile);
                return;
            }

            /* Create directories if needed, just in case */
            fs::create_directories(filePath.parent_path());

            /* Decompress the data and write to file */
            std::ofstream outDecompressed{filePath, std::ios::binary};

            char buffer[4096];
            uint32_t bytesRead{0};
            while ((bytesRead = unzReadCurrentFile(zipFile, buffer, sizeof(buffer))) > 0)
            {
                outDecompressed.write(buffer, bytesRead);
            }

            outDecompressed.close();
            unzCloseCurrentFile(zipFile);

            /* Move to the next file in zip*/
            if (i + 1 < globalInfo.number_entry)
            {
                if (unzGoToNextFile(zipFile) != UNZ_OK)
                {
                    printlne("Failed to move to the next file %lu", i + 1);
                    unzClose(zipFile);
                    return;
                }
            }
        }

        /* Close zip descriptor and remove the meta.zip placeholder */
        unzClose(zipFile);
        fs::remove_all(metaZipPath);

        println("Done unzipping meta");
    }

    void loadInMetaAsXML(const fs::path metaPath)
    {
        println("Loading meta XML in..");
        std::ifstream beMeta{metaPath / "bm/meta.xml"};
        std::ifstream elMeta{metaPath / "lte/meta.xml"};

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

        elXmlResult = XMLDecoder().decodeFromStream(elMeta);
        if (!elXmlResult.second.empty())
        {
            printlne("Error while parsing XML: %s", elXmlResult.second.c_str());
            elXmlResult = XMLDecoder::XmlResult{}; // reset it to nothing
            return;
        }

        protoDecoder.referenceXMLs(beXmlResult, elXmlResult);
        println("Loading meta XML done");
    }

    ChangeSetData readChangeSetType(std::ifstream& stream, const CompressionType cType, const uint64_t size)
    {
        if (cType == CompressionType::GZIP)
        {
            fs::path tempPath = "tmp/decomp_tmp.bin";
            if (decompressGZipChangeSetFrame(stream, size, tempPath))
            {
                std::ifstream decompressedData{tempPath};
                return internalReadChangeSetType(decompressedData, tempPath);
            }
            else
            {
                printlne("Failed to decompress frame. Skipping over it.");
                stream.seekg(size, std::ios::cur);
                return ChangeSetData{};
            }
        }
        if (cType == CompressionType::NO_COMPRESSION)
        {
            return internalReadChangeSetType(stream, "");
        }
        else
        {
            printlne("Compression type %d not supported. Skipping over it.", (uint8_t)cType);
            stream.seekg(size, std::ios::cur);
            return ChangeSetData{};
        }

        return ChangeSetData{};
    }

    ChangeSetData internalReadChangeSetType(std::ifstream& stream, const fs::path tempPath)
    {
        // hope that the compiler does RVO
        ChangeSetData changeSet;

        changeSet.timeStamp = utils::read8(stream);
        changeSet.numberOfChanges = utils::read4(stream);

        // printlne("TIMESTAMP: %ld", changeSet.timeStamp);
        for (uint32_t i = 0; i < changeSet.numberOfChanges; i++)
        {
            SingleChange change;
            uint32_t nameSize = utils::read2(stream);
            change.name = utils::readStringBytes(stream, nameSize);
            change.type = static_cast<ChangeType>(utils::read1(stream));

            if (change.type == ChangeType::DELETED)
            {
                // nothing more to do. NO payload
            }
            else if (change.type == ChangeType::CREATE_UPDATE)
            {
                change.protoBufSize = utils::read4(stream);
                std::vector<uint8_t> changeProtoBytes = utils::readBytes(stream, change.protoBufSize);
                if (change.name.contains("GNSS"))
                {
                    continue;
                }
                change.fields = std::move(populateChangedFieldsFromProtobuf(change.name, changeProtoBytes));
                if (change.name.ends_with("NRCELL_L-1"))
                {
                    //     // if (i > 27)
                    //     // {
                    //     //     exit(1);
                    //     // }
                    println("name: %s", change.name.c_str());
                    //     printlnHex(changeProtoBytes);
                    protoDecoder.printFields(change.fields);
                    // exit(1);
                }
            }
            else
            {
                printlne("Change type not suppored: %d", static_cast<uint8_t>(change.type));
            }

            // printlne("Name: %s | Type: %d | PSize: %ld", change.name.c_str(), (uint8_t)change.type,
            // change.protoBufData.size());
            changeSet.changes.emplace_back(change);
        }

        /* Perform cleanup due to frame being unzipped */
        if (!tempPath.empty())
        {
            fs::remove_all(tempPath.parent_path());
        }

        return changeSet;
    }

    FieldVec populateChangedFieldsFromProtobuf(const std::string& changePath, const std::vector<uint8_t> bytes)
    {
        const auto itStart = changePath.find_last_of('/') + 1;
        const auto itEnd = changePath.find_last_of('-');
        std::string name = changePath.substr(itStart, itEnd - itStart);
        return protoDecoder.parseProtobufFromBuffer(name, bytes);
        // return {};
    }

    bool decompressGZipChangeSetFrame(std::ifstream& stream, uint64_t size, fs::path outputPath)
    {
        /* Create parent tmp directory since opening a path will not create the directories */
        fs::create_directories(outputPath.parent_path());

        /* Current strategy: read all data inside a buffer, then pass this buffer to zlib for decompression, then
         * write the result to an output file. Callee will open this file and read changes as if nothing happened.
         */
        char* buff = new char[size];
        stream.read(buff, size);

        /* Setup zlib */
        z_stream zstream;
        zstream.zalloc = Z_NULL;
        zstream.zfree = Z_NULL;
        zstream.opaque = Z_NULL;
        zstream.avail_in = size;
        zstream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(buff));

        if (inflateInit2(&zstream, 16 + MAX_WBITS) != Z_OK)
        {
            delete[] buff;
            printlne("Failed to initialize zlib");
            return false;
        }

        std::ofstream outFile{outputPath};
        char outBuffer[4096];
        int32_t retStatus;

        do
        {
            zstream.avail_out = sizeof(outBuffer);
            zstream.next_out = reinterpret_cast<Bytef*>(outBuffer);

            retStatus = inflate(&zstream, Z_NO_FLUSH);
            if (retStatus == Z_STREAM_ERROR)
            {
                inflateEnd(&zstream);
                delete[] buff;
                printlne("Failed to inflate some part of the data");
                return false;
            }

            outFile.write(outBuffer, sizeof(outBuffer) - zstream.avail_out);
        } while (retStatus != Z_STREAM_END);

        inflateEnd(&zstream);
        delete[] buff;
        outFile.close();

        return true;
    }

private:
    XMLDecoder::XmlResult beXmlResult;
    XMLDecoder::XmlResult elXmlResult;
    ProtobufDecoder protoDecoder;

public:
    Header header;
    std::vector<Frame> frames;
};

class Model
{
public:
    struct Field;
    using FieldVec = std::vector<std::shared_ptr<Field>>;
    using FieldValue = std::variant<std::string, FieldVec>;

    struct Field
    {
        std::string name;
        FieldValue value;
    };

    struct Object
    {
        std::string name;
        std::vector<Field> fields;
    };

    void loadFromPath(const fs::path& path)
    {

        fs::path beMetaPath = path / "bm/meta.xml";
        std::ifstream beMeta{beMetaPath};

        if (beMeta.fail())
        {
            printlne("Failed to open %s", beMetaPath.c_str());
            return;
        }

        XMLDecoder::XmlResult beXml = XMLDecoder().decodeFromStream(beMeta);
        if (!beXml.second.empty())
        {
            printlne("Error while reading %s", beMetaPath.c_str());
            return;
        }

        // skip xml tag (0 index)
        XMLDecoder::NodeVec allObjects = beXml.first[1]->getTagsNamed("managedObject");
        size_t size = allObjects.size();

        for (const auto& object : allObjects)
        {
            std::string className = object->getAttribValue("class").value_or("Not found");
            XMLDecoder::NodeVec pNodes = object->getTagsNamed("p");
            println("%s %ld", className.c_str(), pNodes.size());
        }
        println("Count %ld", size);
    }

    void applyChanges(const ChangesData& changesData) {}

    // helper funcs
    void findObject();

public:
    std::vector<Object> objects;
};

} // namespace hk

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printlne("Incorrect number of arguments: %d", argc);
        printlne("Usage %s <file_path>", argv[0]);
        return 1;
    }

    std::ifstream modelPath{argv[1], std::ios::binary};

    if (modelPath.fail())
    {
        printlne("Failed to find/open: %s", argv[1]);
        return 1;
    }

    /* Read in all the changes */
    hk::ChangesData changesData;
    changesData.loadFromPath(modelPath);

    /* Create a model to which to apply the changes */
    // hk::Model model;
    // model.loadFromPath("metaTmp");
    // model.applyChanges(changesData);

    println("Version %d", changesData.header.version);
    println("Additional info is: %s", changesData.header.additionalInfo.c_str());

    return 0;
}