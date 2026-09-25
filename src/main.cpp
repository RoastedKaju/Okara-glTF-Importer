#include <print>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <cstddef>
#include <cassert>
#include <cJSON.h>

static constexpr std::uint32_t GLB_MAGIC = 0x46546C67;
static constexpr std::uint32_t GLB_VERSION = 2;
static constexpr std::uint32_t JSON_CHUNK = 0x4E4F534A;
static constexpr std::uint32_t BIN_CHUNK = 0x004E4942;

struct GLBHeader
{
    uint32_t magic{};
    uint32_t version{};
    uint32_t length{};
};

struct GLBChunkHeader
{
    uint32_t length{};
    uint32_t type{};
};

// TODO: these should be size_t
struct GLBBufferView
{
    int buffer{};
    int byte_offset{};
    int byte_length{};
    int byte_stride{};
};
// TODO: these should be size_t
struct GLBAccessor
{
    int buffer_view{};
    int byte_offset{};
    int component_type{};
    int count{};
    std::string type{};
};

struct GLBVec3
{
    float x{};
    float y{};
    float z{};
};

class GLBParser
{
public:
    bool load(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            std::print("Failed to open file: {}.\n", path);
            return false;
        }

        // read the first 12-bytes
        file.read(reinterpret_cast<char *>(&header_), sizeof(GLBHeader));

        if (!file)
        {
            std::printf("Failed to read GLB header.\n");
            return false;
        }

        // check if file is GLB
        if (header_.magic != GLB_MAGIC)
        {
            std::print("Invalid GLB file.\n");
            return false;
        }
        if (header_.version != GLB_VERSION)
        {
            std::print("Unsupported GLB version.\n");
            return false;
        }

        // verify file size
        file.seekg(0, std::ios::end);
        const auto file_size = file.tellg();
        if (file_size == std::streampos(-1))
        {
            std::print("Failed to determine file size.\n");
            return false;
        }

        // return the position back to 12 bytes
        file.seekg(sizeof(GLBHeader), std::ios::beg);

        printHeaderData();

        // TODO: length variable in header is only 4 bytes but stream position is 64 bits

        if (header_.length != file_size)
        {
            std::print("File size does not match value provided by header length.\n");
            return false;
        }

        // read the first chunk header
        GLBChunkHeader chunk_header{};
        // for a valid glTF 2.0 GLB, the first chunk must be JSON
        // type: 0x4E4F534A which is JSON in ASCII
        file.read(reinterpret_cast<char *>(&chunk_header), sizeof(GLBChunkHeader));
        if (!file)
        {
            std::print("Failed to read chunk data.\n");
            return false;
        }

        // read JSON chunk
        std::vector<char> json_data(chunk_header.length);
        file.read(json_data.data(), json_data.size());
        if (!file)
        {
            std::print("Failed to read JSON chunk.\n");
            return false;
        }

        // validate
        if (chunk_header.type != JSON_CHUNK)
        {
            std::print("First GLB chunk is not a JSON chunk.\n");
            return false;
        }
        printChunkHeaderData(&chunk_header);

        cJSON *root = cJSON_ParseWithLength(json_data.data(), json_data.size());
        if (!root)
        {
            std::print("Failed to parse GLB JSON.\n");
            return false;
        }
        // get asset object
        cJSON *asset = cJSON_GetObjectItemCaseSensitive(root, "asset");
        if (!cJSON_IsObject(asset))
        {
            std::print("GLB JSON does not contain a valid asset object.\n");
            cJSON_Delete(root);
            return false;
        }
        // get version
        cJSON *version = cJSON_GetObjectItemCaseSensitive(asset, "version");
        if (!cJSON_IsString(version))
        {
            std::print("GLB asset does not contain a valid version.\n");
            cJSON_Delete(root);
            return false;
        }

        if (std::strcmp(version->valuestring, "2.0") != 0)
        {
            std::print("Unsupported glTF version: {}\n", version->valuestring);
            cJSON_Delete(root);
            return false;
        }

        // the relationship required for getting geometry out of glTF is:
        // buffers -> bufferViews -> accessors -> meshes -> primitives
        // binary chunk is just raw bytes, buffer describes a region, view describes how to interpret it, accessors are attributes

        // buffers
        cJSON *buffers = cJSON_GetObjectItemCaseSensitive(root, "buffers");
        if (!cJSON_IsArray(buffers))
        {
            std::print("GLB does not contain a valid buffers array.\n");
            cJSON_Delete(root);
            return false;
        }
        if (!iterateOverBuffers(root, buffers))
        {
            return false;
        }

        // buffer views
        cJSON *buffer_views = cJSON_GetObjectItemCaseSensitive(root, "bufferViews");
        if (!cJSON_IsArray(buffer_views))
        {
            std::print("GLB does not contain a valid buffer views array.\n");
            cJSON_Delete(root);
            return false;
        }
        if (!iterateOverBufferViews(root, buffer_views))
        {
            return false;
        }

        // accessors know how to interpert the data where buffer views point
        cJSON *accessors = cJSON_GetObjectItemCaseSensitive(root, "accessors");
        if (!cJSON_IsArray(accessors))
        {
            std::print("GLB does not contain a valid accessor array.\n");
            cJSON_Delete(root);
            return false;
        }
        if (!iterateOverAccessors(root, accessors))
        {
            return false;
        }

        // inspect meshes
        cJSON *meshes = cJSON_GetObjectItemCaseSensitive(root, "meshes");
        if (!cJSON_IsArray(meshes))
        {
            std::print("GLB does not contain a valid meshes array.\n");
            cJSON_Delete(root);
            return false;
        }

        std::print("Meshes: {}\n", cJSON_GetArraySize(meshes));

        cJSON *mesh = nullptr;
        cJSON_ArrayForEach(mesh, meshes)
        {
            cJSON *primitives = cJSON_GetObjectItemCaseSensitive(mesh, "primitives");
            if (!cJSON_IsArray(primitives))
            {
                std::print("Mesh does not contain a valid primitives array.\n");
                cJSON_Delete(root);
                return false;
            }

            std::print("Primitives: {}\n", cJSON_GetArraySize(primitives));

            cJSON *primitive = nullptr;
            cJSON_ArrayForEach(primitive, primitives)
            {
                cJSON *attributes = cJSON_GetObjectItemCaseSensitive(primitive, "attributes");
                // attributes value in glTF is an object not an array
                if (!cJSON_IsObject(attributes))
                {
                    std::print("Primitive does not contain valid attributes.\n");
                    cJSON_Delete(root);
                    return false;
                }

                cJSON *attribute = nullptr;
                cJSON_ArrayForEach(attribute, attributes)
                {
                    std::print("Attribute: {} -> Accessor {}\n", attribute->string, attribute->valueint);
                    // store position accessor index
                    if (std::strcmp(attribute->string, "POSITION") == 0)
                    {
                        position_accessor_idx_ = static_cast<size_t>(attribute->valueint);
                    }
                }

                cJSON *indices = cJSON_GetObjectItemCaseSensitive(primitive, "indices");
                if (cJSON_IsNumber(indices))
                {
                    std::print("Indices accessor: {}\n", indices->valueint);
                }
            }
        }

        // by this point we have all of our meta data
        cJSON_Delete(root);

        // read binary chunk header
        GLBChunkHeader bin_chunk_header{};
        file.read(reinterpret_cast<char *>(&bin_chunk_header), sizeof(GLBChunkHeader));
        if (!file)
        {
            std::print("Failed to read binary chunk data.\n");
            return false;
        }
        if (bin_chunk_header.type != BIN_CHUNK)
        {
            std::print("Second GLB chunk is not a BIN chunk.\n");
            return false;
        }
        std::print("Binary Chunk Header Type: {:08X}, Length: {}\n", bin_chunk_header.type, bin_chunk_header.length);

        // read binary data
        binary_data_.clear();
        binary_data_.resize(bin_chunk_header.length);
        file.read(reinterpret_cast<char *>(binary_data_.data()), binary_data_.size());
        if (!file)
        {
            std::print("Failed to load binary chunk.\n");
            return false;
        }
        std::print("Read {} bytes of binary data.\n", binary_data_.size());

        const auto p0 = readPosition(0);
        const auto p1 = readPosition(1);
        const auto p2 = readPosition(2);

        std::print("Position: {}, {}, {}\n", p0.x, p0.y, p0.z);
        std::print("Position: {}, {}, {}\n", p1.x, p1.y, p1.z);
        std::print("Position: {}, {}, {}\n", p2.x, p2.y, p2.z);

        return true;
    }

private:
    void printHeaderData()
    {
        std::print("Magic: 0x{:08X}, Version: {}, Length: {}\n", header_.magic, header_.version, header_.length);
    }

    void printChunkHeaderData(GLBChunkHeader *chunk_header)
    {
        std::print("Chunk Header Type: {:08X}, Length: {}\n", chunk_header->type, chunk_header->length);
    }

    bool iterateOverBuffers(cJSON *root, cJSON *buffers)
    {
        std::print("Buffers: {}\n", cJSON_GetArraySize(buffers));

        cJSON *buffer = nullptr;
        cJSON_ArrayForEach(buffer, buffers)
        {
            cJSON *byte_length = cJSON_GetObjectItem(buffer, "byteLength");
            if (!cJSON_IsNumber(byte_length))
            {
                std::print("Buffer is missing 'byteLength'.\n");
                cJSON_Delete(root);
                return false;
            }

            std::print("Byte length: {}\n", byte_length->valueint);
        }

        return true;
    }

    bool iterateOverBufferViews(cJSON *root, cJSON *buffer_views)
    {
        buffer_views_.clear();
        buffer_views_.reserve(cJSON_GetArraySize(buffer_views));

        std::print("Buffer views: {}\n", cJSON_GetArraySize(buffer_views));

        cJSON *buffer_view = nullptr;
        cJSON_ArrayForEach(buffer_view, buffer_views)
        {
            cJSON *buffer = cJSON_GetObjectItemCaseSensitive(buffer_view, "buffer");
            cJSON *byte_offset = cJSON_GetObjectItemCaseSensitive(buffer_view, "byteOffset");
            cJSON *byte_length = cJSON_GetObjectItemCaseSensitive(buffer_view, "byteLength");
            cJSON *byte_stride = cJSON_GetObjectItemCaseSensitive(buffer_view, "byteStride");
            if (!cJSON_IsNumber(buffer) || !cJSON_IsNumber(byte_length))
            {
                std::print("Invalid buffer view.\n");
                cJSON_Delete(root);
                return false;
            }
            // offset is an optional field, default value is 0
            const int offset = cJSON_IsNumber(byte_offset) ? byte_offset->valueint : 0;
            const int stride = cJSON_IsNumber(byte_stride) ? byte_stride->valueint : 0;
            std::print("Buffer: {}, Offset: {}, Length: {}, Stride: {}\n", buffer->valueint, offset, byte_length->valueint, stride);

            buffer_views_.push_back({buffer->valueint, offset, byte_length->valueint, stride});
        }

        return true;
    }

    bool iterateOverAccessors(cJSON *root, cJSON *accessors)
    {
        accessors_.clear();
        accessors_.reserve(cJSON_GetArraySize(accessors));

        std::print("Accessors: {}\n", cJSON_GetArraySize(accessors));

        cJSON *accessor = nullptr;
        cJSON_ArrayForEach(accessor, accessors)
        {
            cJSON *buffer_view = cJSON_GetObjectItemCaseSensitive(accessor, "bufferView");
            cJSON *byte_offset = cJSON_GetObjectItemCaseSensitive(accessor, "byteOffset");
            cJSON *component_type = cJSON_GetObjectItemCaseSensitive(accessor, "componentType");
            cJSON *count = cJSON_GetObjectItemCaseSensitive(accessor, "count");
            cJSON *type = cJSON_GetObjectItemCaseSensitive(accessor, "type");

            if (!cJSON_IsNumber(component_type) || !cJSON_IsNumber(count) || !cJSON_IsString(type))
            {
                std::print("Invalid accessor.\n");
                cJSON_Delete(root);
                return false;
            }

            // optional fields
            const int view = cJSON_IsNumber(buffer_view) ? buffer_view->valueint : -1;
            const int offset = cJSON_IsNumber(byte_offset) ? byte_offset->valueint : 0;

            std::print("Buffer View: {}, Offset: {}, Component Type: {}, Count: {}, Type: {}\n", view, offset, component_type->valueint, count->valueint, type->valuestring);

            accessors_.push_back({view, offset, component_type->valueint, count->valueint, type->valuestring});
        }

        return true;
    }

    size_t componentTypeSize(uint32_t component_type)
    {
        switch (component_type)
        {
        case 5126: // float
            return sizeof(float);
        case 5125: // unsigned integer
            return sizeof(uint32_t);
        default:
            return 0;
        }
    }

    size_t componentCount(const std::string &type)
    {
        if (type == "SCALAR")
            return 1;
        if (type == "VEC2")
            return 2;
        if (type == "VEC3")
            return 3;
        if (type == "VEC4")
            return 4;

        return 0;
    }

    GLBVec3 readPosition(std::size_t index)
    {
        const GLBAccessor &accessor = accessors_[position_accessor_idx_];
        assert(index < accessor.count);

        const std::size_t component_size = componentTypeSize(accessor.component_type);
        const std::size_t component_count = componentCount(accessor.type);
        const std::size_t element_size = component_size * component_count;
        const GLBBufferView &position_view = buffer_views_[accessor.buffer_view];
        const size_t position_offset = position_view.byte_offset + accessor.byte_offset;

        const std::size_t offset = position_offset + index * element_size;

        GLBVec3 result{};

        std::memcpy(&result.x, binary_data_.data() + offset + 0, sizeof(float));
        std::memcpy(&result.y, binary_data_.data() + offset + 4, sizeof(float));
        std::memcpy(&result.z, binary_data_.data() + offset + 8, sizeof(float));

        return result;
    }

    GLBHeader header_{};
    std::vector<std::byte> binary_data_;
    std::vector<GLBBufferView> buffer_views_;
    std::vector<GLBAccessor> accessors_;

    size_t position_accessor_idx_;
};

int main()
{
    GLBParser parser{};
    parser.load("../../models/skull.glb");
    return EXIT_SUCCESS;
}