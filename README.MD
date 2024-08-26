# Redacted File Decoder

## Usage

Clone: ```git clone --recurse-submodules https://github.com/H3kapoo/HkRedactedDecoder.git```

Quick usage overview, examples to be added:

```Cpp
    /* Read in all the changes */
    hk::ChangesData changesData;
    changesData.loadFromPath(modelPath);

    hk::FieldMap fm = changesData.frames[1].changeSetData.changes[0].fields;

    if (HAS_FIELD(fm, "structure"))
    {
        hk::FieldMap structure = GET_MAP(fm["structure"]);
        if (HAS_FIELD(structure, "struct_field"))
        {
            std::string str = GET_STR(structure["struct_field"]);
            printlne("state is: %s", str.c_str());
        }
    }
```
```bash
    ./redactedDecoder <path/to/file>
```
## Requirements

Program requires module (already have it with --recurse-submodules): ```https://github.com/H3kapoo/HkXML```
Libs required: 
 - everything HkXML requires
 - zlib1g-dev (Ubuntu: sudo apt-get install zlib1g-dev)
 - libminizip-dev (Ubuntu: sudo apt-get install libminizip-dev)

## Build

```bash
    ./build.sh
```
## Notes

No Windows/MacOS support. However since only some libs are required, if you manage to find them for your OS, feel free to do so.
