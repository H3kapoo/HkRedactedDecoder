#include <cstdint>
#include <filesystem>
#include <fstream>
#include <minizip/unzip.h>
#include <vector>
#include <zlib.h>

#include "Utility.hpp"

namespace hk
{
namespace fs = std::filesystem;

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
    std::string protoBufData{}; // TODO: should be vec of bytes
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

class Model
{

public:
    void readIn(std::ifstream& stream)
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

            println("FrameType: %d | Compressed: %d | FrameSize: %d", (uint8_t)frame.type, (uint8_t)frame.compression,
                frame.frameSize);
            if (frame.type == FrameType::META)
            {
                readMetaType(stream, frame.frameSize);
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

        println("Done unzipping META");
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
                change.protoBufData = utils::readStringBytes(stream, change.protoBufSize);
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

public:
    Header header;
    std::vector<Frame> frames;
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

    hk::Model model;
    model.readIn(modelPath);

    println("Version %d", model.header.version);
    println("Additional info is: %s", model.header.additionalInfo.c_str());

    return 0;
}