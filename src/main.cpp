#include <fstream>

#include "RedactedDecoder.hpp"
#include "Utility.hpp"

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
    hk::ChangeData changesData;
    changesData.loadFromPath(modelPath);

    hk::FieldMap fm = changesData.frames[3].changeSetData.changes[0].fields;

    printlne("name: %s", changesData.frames[4].changeSetData.changes[0].name.c_str());
    for (const auto& ch : changesData.frames[4].changeSetData.changes)
    {
        hk::ProtobufDecoder::printFields(ch.fields);
    }
    // if (HAS_FIELD(fm, "structure"))
    // {
    //     hk::FieldMap structure = GET_MAP(fm["structure"]);
    //     if (HAS_FIELD(structure, "struct_field"))
    //     {
    //         std::string str = GET_STR(structure["struct_field"]);
    //         printlne("state is: %s", str.c_str());
    //     }
    // }

    println("Version %d", changesData.header.version);
    println("Additional info is: %s", changesData.header.additionalInfo.c_str());
    println("Frames: %ld", changesData.frames.size());

    return 0;
}