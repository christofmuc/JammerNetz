/* ==================================== JUCER_BINARY_RESOURCE ====================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

namespace BinaryData
{

//================== RandomNumbers.bin ==================
static const unsigned char temp_binary_data_0[] =
{ 249,21,209,24,22,204,85,143,233,82,164,220,108,210,51,33,177,215,253,178,3,166,42,190,112,23,8,85,209,214,73,24,135,88,105,224,242,83,84,72,208,136,45,117,75,122,107,236,14,171,29,99,0,75,244,76,70,178,123,115,114,215,7,64,240,74,11,176,3,119,99,142,
0,0 };

const char* RandomNumbers_bin = (const char*) temp_binary_data_0;


const char* getNamedResource (const char* resourceNameUTF8, int& numBytes)
{
    unsigned int hash = 0;

    if (resourceNameUTF8 != nullptr)
        while (*resourceNameUTF8 != 0)
            hash = 31 * hash + (unsigned int) *resourceNameUTF8++;

    switch (hash)
    {
        case 0x678080af:  numBytes = 72; return RandomNumbers_bin;
        default: break;
    }

    numBytes = 0;
    return nullptr;
}

const char* namedResourceList[] =
{
    "RandomNumbers_bin"
};

const char* originalFilenames[] =
{
    "RandomNumbers.bin"
};

const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8)
{
    for (unsigned int i = 0; i < (sizeof (namedResourceList) / sizeof (namedResourceList[0])); ++i)
    {
        if (namedResourceList[i] == resourceNameUTF8)
            return originalFilenames[i];
    }

    return nullptr;
}

}
