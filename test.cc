#include <iostream>

#include "okara_gltf_importer.h"

int main()
{
    okara::gltf::Loader loader{};
    loader.load("../../models/skull.glb");
    return EXIT_SUCCESS;
}