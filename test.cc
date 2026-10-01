#include <iostream>

#include "okara_gltf_importer.h"

int main()
{
    okara::gltf::Loader loader{};
    bool success = loader.LoadScene("../../models/bot.glb");
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}