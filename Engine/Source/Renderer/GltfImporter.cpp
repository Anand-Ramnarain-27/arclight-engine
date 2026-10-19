#include "Renderer/GltfImporter.h"

#include "Core/Log.h"
#include "Platform/FileSystem.h"

#include <tiny_gltf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <string_view>

namespace arc
{
    namespace
    {
        // Required extensions this importer handles. KHR_mesh_quantization only allows integer
        // component types for attributes, which the accessor reader converts anyway.
        constexpr std::array<std::string_view, 1> kSupportedRequiredExtensions = {"KHR_mesh_quantization"};

        // Deeper hierarchies than this are treated as a cycle, which glTF forbids but a broken file
        // can still contain.
        constexpr int kMaxNodeDepth = 256;

        /// The state of one import: the parsed file, the model being built and the name for logs.
        struct Importer
        {
            const tinygltf::Model& gltf;
            ModelData& model;
            std::string_view name;
            std::vector<bool> imageNeeded;
        };

        // One component of an accessor element as a float. Normalized integers map to [0, 1] or
        // [-1, 1] as glTF defines (signed values use max(c / max, -1), so the most negative value
        // and the next one both give -1); other integers convert to their value.
        float readComponent(const unsigned char* source, int componentType, bool normalized)
        {
            switch (componentType)
            {
                case TINYGLTF_COMPONENT_TYPE_FLOAT:
                {
                    float value = 0.0f;
                    std::memcpy(&value, source, sizeof(value));
                    return value;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                {
                    const uint8_t value = *source;
                    return normalized ? static_cast<float>(value) / 255.0f : static_cast<float>(value);
                }
                case TINYGLTF_COMPONENT_TYPE_BYTE:
                {
                    int8_t value = 0;
                    std::memcpy(&value, source, sizeof(value));
                    return normalized ? std::max(static_cast<float>(value) / 127.0f, -1.0f) : static_cast<float>(value);
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                {
                    uint16_t value = 0;
                    std::memcpy(&value, source, sizeof(value));
                    return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
                }
                case TINYGLTF_COMPONENT_TYPE_SHORT:
                {
                    int16_t value = 0;
                    std::memcpy(&value, source, sizeof(value));
                    return normalized ? std::max(static_cast<float>(value) / 32767.0f, -1.0f) : static_cast<float>(value);
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                {
                    uint32_t value = 0;
                    std::memcpy(&value, source, sizeof(value));
                    return static_cast<float>(value);
                }
                default:
                    return 0.0f;
            }
        }

        uint32_t readIndexValue(const unsigned char* source, int componentType)
        {
            switch (componentType)
            {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    return *source;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                {
                    uint16_t value = 0;
                    std::memcpy(&value, source, sizeof(value));
                    return value;
                }
                default:
                {
                    uint32_t value = 0;
                    std::memcpy(&value, source, sizeof(value));
                    return value;
                }
            }
        }

        bool isIndexType(int componentType)
        {
            return componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                   componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ||
                   componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        }

        /// The first of `count` elements of `elementSize` bytes, `stride` apart, starting `byteOffset`
        /// bytes into a buffer view, after checking that every one of them lies inside the view and
        /// its buffer. Null, with a warning, if they do not. glTF files are untrusted input: a wrong
        /// offset or count must not read past the end of the data.
        const unsigned char* elementsInView(const Importer& importer, int bufferViewIndex, size_t byteOffset, size_t count,
                                            size_t stride, size_t elementSize, std::string_view what)
        {
            const tinygltf::Model& gltf = importer.gltf;
            if (bufferViewIndex < 0 || static_cast<size_t>(bufferViewIndex) >= gltf.bufferViews.size())
            {
                ARC_LOG_WARN("{}: {} refers to buffer view {}, which does not exist", importer.name, what, bufferViewIndex);
                return nullptr;
            }
            const tinygltf::BufferView& view = gltf.bufferViews[static_cast<size_t>(bufferViewIndex)];
            if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= gltf.buffers.size())
            {
                ARC_LOG_WARN("{}: buffer view {} refers to a buffer that does not exist", importer.name, bufferViewIndex);
                return nullptr;
            }
            const std::vector<unsigned char>& buffer = gltf.buffers[static_cast<size_t>(view.buffer)].data;

            // Written as divisions rather than multiplying count by stride, which a hostile count
            // could overflow.
            const bool viewInsideBuffer = view.byteOffset <= buffer.size() && view.byteLength <= buffer.size() - view.byteOffset;
            bool elementsInside = true;
            if (count > 0)
            {
                const size_t available = byteOffset <= view.byteLength ? view.byteLength - byteOffset : 0;
                elementsInside = available >= elementSize && (count - 1) <= (available - elementSize) / stride;
            }
            if (!viewInsideBuffer || !elementsInside)
            {
                ARC_LOG_WARN("{}: {} reads past the end of its buffer", importer.name, what);
                return nullptr;
            }
            return buffer.data() + view.byteOffset + byteOffset;
        }

        /// Where an accessor's elements are and how to read them.
        struct AccessorView
        {
            const tinygltf::Accessor* accessor = nullptr;
            const unsigned char* data = nullptr;   // Null when the accessor has no buffer view: all zeros.
            size_t stride = 0;
            size_t elementSize = 0;
            int componentSize = 0;
        };

        bool viewAccessor(const Importer& importer, int accessorIndex, int expectedComponents, AccessorView& view,
                          std::string_view what)
        {
            const tinygltf::Model& gltf = importer.gltf;
            if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= gltf.accessors.size())
            {
                ARC_LOG_WARN("{}: {} refers to accessor {}, which does not exist", importer.name, what, accessorIndex);
                return false;
            }
            const tinygltf::Accessor& accessor = gltf.accessors[static_cast<size_t>(accessorIndex)];
            view.accessor = &accessor;
            const int components = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type));
            if (components != expectedComponents)
            {
                ARC_LOG_WARN("{}: {} has {} components per element, expected {}", importer.name, what, components,
                             expectedComponents);
                return false;
            }
            view.componentSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
            if (view.componentSize <= 0)
            {
                ARC_LOG_WARN("{}: {} has an invalid component type {}", importer.name, what, accessor.componentType);
                return false;
            }
            view.elementSize = static_cast<size_t>(view.componentSize) * static_cast<size_t>(expectedComponents);

            if (accessor.bufferView < 0)
                return true;

            // byteStride 0 means tightly packed; otherwise it is the distance between elements, which
            // may hold other attributes in between (interleaved vertices).
            const int stride = accessor.bufferView < static_cast<int>(gltf.bufferViews.size())
                                   ? accessor.ByteStride(gltf.bufferViews[static_cast<size_t>(accessor.bufferView)])
                                   : -1;
            if (stride <= 0 || static_cast<size_t>(stride) < view.elementSize)
            {
                ARC_LOG_WARN("{}: {} has an invalid byte stride", importer.name, what);
                return false;
            }
            view.stride = static_cast<size_t>(stride);
            view.data = elementsInView(importer, accessor.bufferView, accessor.byteOffset, accessor.count, view.stride,
                                       view.elementSize, what);
            return view.data != nullptr;
        }

        /// Reads an accessor as floats, N per element, converting integer components. Sparse
        /// accessors are supported: their listed elements replace those of the base data, which is
        /// all zeros when the accessor has no buffer view.
        template <size_t N>
        bool readFloats(const Importer& importer, int accessorIndex, std::vector<std::array<float, N>>& out,
                        std::string_view what)
        {
            AccessorView view;
            if (!viewAccessor(importer, accessorIndex, static_cast<int>(N), view, what))
                return false;
            const tinygltf::Accessor& accessor = *view.accessor;

            const auto readElement = [&](const unsigned char* source, std::array<float, N>& element) {
                for (size_t component = 0; component < N; ++component)
                    element[component] = readComponent(source + component * static_cast<size_t>(view.componentSize),
                                                       accessor.componentType, accessor.normalized);
            };

            out.assign(accessor.count, {});
            if (view.data)
                for (size_t i = 0; i < accessor.count; ++i)
                    readElement(view.data + i * view.stride, out[i]);

            if (!accessor.sparse.isSparse)
                return true;

            const auto& sparse = accessor.sparse;
            const int indexSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(sparse.indices.componentType));
            if (sparse.count <= 0 || static_cast<size_t>(sparse.count) > accessor.count ||
                !isIndexType(sparse.indices.componentType))
            {
                ARC_LOG_WARN("{}: {} has invalid sparse data", importer.name, what);
                return false;
            }
            const auto count = static_cast<size_t>(sparse.count);
            const auto indexStride = static_cast<size_t>(indexSize);
            // Sparse indices and values are always tightly packed.
            const unsigned char* indices = elementsInView(importer, sparse.indices.bufferView, sparse.indices.byteOffset,
                                                          count, indexStride, indexStride, what);
            const unsigned char* values = elementsInView(importer, sparse.values.bufferView, sparse.values.byteOffset, count,
                                                         view.elementSize, view.elementSize, what);
            if (!indices || !values)
                return false;
            for (size_t i = 0; i < count; ++i)
            {
                const uint32_t target = readIndexValue(indices + i * indexStride, sparse.indices.componentType);
                if (target >= out.size())
                {
                    ARC_LOG_WARN("{}: {} has a sparse index beyond its {} elements", importer.name, what, out.size());
                    return false;
                }
                readElement(values + i * view.elementSize, out[target]);
            }
            return true;
        }

        bool readIndices(const Importer& importer, int accessorIndex, std::vector<uint32_t>& out, std::string_view what)
        {
            AccessorView view;
            if (!viewAccessor(importer, accessorIndex, 1, view, what))
                return false;
            const tinygltf::Accessor& accessor = *view.accessor;
            if (!isIndexType(accessor.componentType))
            {
                ARC_LOG_WARN("{}: {} must be unsigned 8, 16 or 32-bit integers", importer.name, what);
                return false;
            }
            if (accessor.sparse.isSparse)
            {
                ARC_LOG_WARN("{}: {} are sparse, which only makes sense for attributes", importer.name, what);
                return false;
            }

            out.assign(accessor.count, 0);
            if (view.data)
                for (size_t i = 0; i < accessor.count; ++i)
                    out[i] = readIndexValue(view.data + i * view.stride, accessor.componentType);
            return true;
        }

        // Strips and fans to lists, keeping the winding of every triangle as glTF defines it.
        std::vector<uint32_t> toTriangleList(const std::vector<uint32_t>& indices, int mode)
        {
            if (mode == TINYGLTF_MODE_TRIANGLES || indices.size() < 3)
                return indices;

            std::vector<uint32_t> list;
            list.reserve((indices.size() - 2) * 3);
            for (size_t i = 0; i + 2 < indices.size(); ++i)
            {
                if (mode == TINYGLTF_MODE_TRIANGLE_STRIP)
                {
                    // Every other triangle of a strip is wound the other way; swapping its first two
                    // vertices puts it back.
                    if (i % 2 == 0)
                        list.insert(list.end(), {indices[i], indices[i + 1], indices[i + 2]});
                    else
                        list.insert(list.end(), {indices[i + 1], indices[i], indices[i + 2]});
                }
                else
                {
                    list.insert(list.end(), {indices[i + 1], indices[i + 2], indices[0]});
                }
            }
            return list;
        }

        const char* modeName(int mode)
        {
            switch (mode)
            {
                case TINYGLTF_MODE_POINTS:     return "points";
                case TINYGLTF_MODE_LINE:       return "lines";
                case TINYGLTF_MODE_LINE_LOOP:  return "line loops";
                case TINYGLTF_MODE_LINE_STRIP: return "line strips";
                default:                       return "an unknown mode";
            }
        }

        void importPrimitive(Importer& importer, const tinygltf::Primitive& source, std::string_view meshName)
        {
            ModelData& model = importer.model;
            const int mode = source.mode < 0 ? TINYGLTF_MODE_TRIANGLES : source.mode;
            if (mode != TINYGLTF_MODE_TRIANGLES && mode != TINYGLTF_MODE_TRIANGLE_STRIP && mode != TINYGLTF_MODE_TRIANGLE_FAN)
            {
                ARC_LOG_WARN("{}: a primitive of mesh '{}' is drawn as {}; only triangles are supported", importer.name,
                             meshName, modeName(mode));
                return;
            }

            const auto attribute = [&source](const char* attributeName) {
                const auto found = source.attributes.find(attributeName);
                return found == source.attributes.end() ? -1 : found->second;
            };

            std::vector<std::array<float, 3>> positions;
            if (attribute("POSITION") < 0)
            {
                ARC_LOG_WARN("{}: a primitive of mesh '{}' has no POSITION", importer.name, meshName);
                return;
            }
            if (!readFloats(importer, attribute("POSITION"), positions, "POSITION") || positions.empty())
                return;
            const size_t vertexCount = positions.size();

            std::vector<std::array<float, 3>> normals;
            if (attribute("NORMAL") >= 0 &&
                (!readFloats(importer, attribute("NORMAL"), normals, "NORMAL") || normals.size() != vertexCount))
            {
                ARC_LOG_WARN("{}: ignoring the unreadable normals of a primitive of mesh '{}'", importer.name, meshName);
                normals.clear();
            }

            std::vector<std::array<float, 2>> uvs;
            if (attribute("TEXCOORD_0") >= 0 &&
                (!readFloats(importer, attribute("TEXCOORD_0"), uvs, "TEXCOORD_0") || uvs.size() != vertexCount))
            {
                ARC_LOG_WARN("{}: ignoring the unreadable texture coordinates of a primitive of mesh '{}'", importer.name,
                             meshName);
                uvs.clear();
            }

            // Without indices, the vertices are used in order.
            std::vector<uint32_t> indices;
            if (source.indices >= 0)
            {
                if (!readIndices(importer, source.indices, indices, "indices"))
                    return;
            }
            else
            {
                indices.resize(vertexCount);
                std::iota(indices.begin(), indices.end(), 0u);
            }

            // An out-of-range index would make the GPU read outside the vertex buffer.
            if (std::any_of(indices.begin(), indices.end(), [vertexCount](uint32_t index) { return index >= vertexCount; }))
            {
                ARC_LOG_WARN("{}: a primitive of mesh '{}' has indices beyond its {} vertices", importer.name, meshName,
                             vertexCount);
                return;
            }

            indices = toTriangleList(indices, mode);
            if (indices.size() % 3 != 0)
            {
                ARC_LOG_WARN("{}: a primitive of mesh '{}' has {} indices, not a multiple of 3; the rest are dropped",
                             importer.name, meshName, indices.size());
                indices.resize(indices.size() - indices.size() % 3);
            }
            if (indices.empty())
                return;

            const auto toVertex = [&](uint32_t index, const Vec3& normal) {
                MeshVertex vertex;
                const std::array<float, 3>& p = positions[index];
                vertex.position = {p[0], p[1], p[2]};
                vertex.normal = normal;
                if (!uvs.empty())
                    vertex.uv = {uvs[index][0], uvs[index][1]};
                return vertex;
            };

            Primitive primitive;
            primitive.firstIndex = static_cast<uint32_t>(model.indices.size());
            primitive.baseVertex = static_cast<uint32_t>(model.vertices.size());
            if (source.material >= 0 && static_cast<size_t>(source.material) < importer.gltf.materials.size())
                primitive.material = source.material;

            if (normals.empty())
            {
                // glTF: "When normals are not specified, client implementations MUST calculate flat
                // normals." A flat normal belongs to a triangle, not a vertex, so vertices shared
                // between triangles are split: every triangle gets three vertices of its own.
                for (size_t i = 0; i < indices.size(); i += 3)
                {
                    const auto position = [&](size_t corner) {
                        const std::array<float, 3>& p = positions[indices[i + corner]];
                        return Vec3{p[0], p[1], p[2]};
                    };
                    const Vec3 normal = normalize(cross(position(1) - position(0), position(2) - position(0)));
                    for (size_t corner = 0; corner < 3; ++corner)
                    {
                        model.indices.push_back(static_cast<uint32_t>(i + corner));
                        model.vertices.push_back(toVertex(indices[i + corner], normal));
                    }
                }
                primitive.vertexCount = static_cast<uint32_t>(indices.size());
            }
            else
            {
                for (size_t i = 0; i < vertexCount; ++i)
                {
                    const std::array<float, 3>& n = normals[i];
                    model.vertices.push_back(toVertex(static_cast<uint32_t>(i), normalize(Vec3{n[0], n[1], n[2]})));
                }
                model.indices.insert(model.indices.end(), indices.begin(), indices.end());
                primitive.vertexCount = static_cast<uint32_t>(vertexCount);
            }

            primitive.indexCount = static_cast<uint32_t>(model.indices.size()) - primitive.firstIndex;
            for (uint32_t i = 0; i < primitive.vertexCount; ++i)
                primitive.bounds.expand(model.vertices[primitive.baseVertex + i].position);
            model.primitives.push_back(primitive);
        }

        Decomposed localTransform(const tinygltf::Node& node)
        {
            const auto f = [](double value) { return static_cast<float>(value); };
            if (node.matrix.size() == 16)
            {
                // Column-major, like Mat4.
                const std::vector<double>& m = node.matrix;
                const Mat4 matrix{{f(m[0]), f(m[1]), f(m[2]), f(m[3])},
                                  {f(m[4]), f(m[5]), f(m[6]), f(m[7])},
                                  {f(m[8]), f(m[9]), f(m[10]), f(m[11])},
                                  {f(m[12]), f(m[13]), f(m[14]), f(m[15])}};
                // glTF requires node matrices to be decomposable to TRS, apart from zero scale, which
                // files use to hide nodes.
                if (const std::optional<Decomposed> trs = decompose(matrix))
                    return *trs;
                return {matrix.translationPart(), Quat::identity(), Vec3::zero()};
            }

            Decomposed trs;
            if (node.translation.size() == 3)
                trs.translation = {f(node.translation[0]), f(node.translation[1]), f(node.translation[2])};
            if (node.rotation.size() == 4)   // x, y, z, w, as Quat stores them.
                trs.rotation = normalize(Quat{f(node.rotation[0]), f(node.rotation[1]), f(node.rotation[2]), f(node.rotation[3])});
            if (node.scale.size() == 3)
                trs.scale = {f(node.scale[0]), f(node.scale[1]), f(node.scale[2])};
            return trs;
        }

        void importNode(Importer& importer, int nodeIndex, int32_t parent, const Mat4& parentWorld, int depth)
        {
            const tinygltf::Model& gltf = importer.gltf;
            if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= gltf.nodes.size())
            {
                ARC_LOG_WARN("{}: node {} does not exist", importer.name, nodeIndex);
                return;
            }
            if (depth > kMaxNodeDepth)
            {
                ARC_LOG_WARN("{}: the node hierarchy is deeper than {} or has a cycle; it is cut off there",
                             importer.name, kMaxNodeDepth);
                return;
            }

            const tinygltf::Node& node = gltf.nodes[static_cast<size_t>(nodeIndex)];
            const Decomposed local = localTransform(node);
            const Mat4 world = parentWorld * Mat4::trs(local.translation, local.rotation, local.scale);

            ModelData& model = importer.model;
            NodeData nodeData{
                .name = node.name,
                .translation = local.translation,
                .rotation = local.rotation,
                .scale = local.scale,
                .parent = parent,
            };
            if (node.mesh >= 0 && static_cast<size_t>(node.mesh) < model.meshes.size())
            {
                const auto meshIndex = static_cast<uint32_t>(node.mesh);
                nodeData.mesh = node.mesh;
                model.instances.push_back({node.name, world, meshIndex});
                const MeshData& mesh = model.meshes[meshIndex];
                for (uint32_t i = 0; i < mesh.primitiveCount; ++i)
                    model.bounds.expand(model.primitives[mesh.firstPrimitive + i].bounds.transformed(world));
            }
            const auto self = static_cast<int32_t>(model.nodes.size());
            model.nodes.push_back(std::move(nodeData));

            for (const int child : node.children)
                importNode(importer, child, self, world, depth + 1);
        }

        void importScene(Importer& importer)
        {
            const tinygltf::Model& gltf = importer.gltf;
            std::vector<int> roots;
            if (!gltf.scenes.empty())
            {
                const bool validDefault = gltf.defaultScene >= 0 && static_cast<size_t>(gltf.defaultScene) < gltf.scenes.size();
                roots = gltf.scenes[validDefault ? static_cast<size_t>(gltf.defaultScene) : 0].nodes;
            }
            else
            {
                // No scene: every node that is nobody's child is a root.
                std::vector<bool> isChild(gltf.nodes.size(), false);
                for (const tinygltf::Node& node : gltf.nodes)
                    for (const int child : node.children)
                        if (child >= 0 && static_cast<size_t>(child) < isChild.size())
                            isChild[static_cast<size_t>(child)] = true;
                for (size_t i = 0; i < gltf.nodes.size(); ++i)
                    if (!isChild[i])
                        roots.push_back(static_cast<int>(i));
            }

            for (const int root : roots)
                importNode(importer, root, -1, Mat4::identity(), 0);
        }

        rhi::SamplerDesc toSamplerDesc(const tinygltf::Sampler* sampler)
        {
            // glTF leaves filtering to the implementation when unspecified: trilinear with
            // anisotropy, the best quality for surfaces seen at an angle.
            rhi::SamplerDesc desc;
            desc.maxAnisotropy = 8;
            desc.debugName = "glTF Default Sampler";
            if (!sampler)
                return desc;

            const auto address = [](int wrap) {
                switch (wrap)
                {
                    case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:   return rhi::AddressMode::ClampToEdge;
                    case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT: return rhi::AddressMode::MirroredRepeat;
                    default:                                    return rhi::AddressMode::Repeat;
                }
            };
            desc.addressU = address(sampler->wrapS);
            desc.addressV = address(sampler->wrapT);
            desc.debugName = sampler->name.empty() ? "glTF Sampler" : sampler->name;

            if (sampler->magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST)
                desc.magFilter = rhi::Filter::Nearest;

            // Any filter set explicitly turns anisotropy off: it would override the filters.
            if (sampler->magFilter >= 0 || sampler->minFilter >= 0)
                desc.maxAnisotropy = 1;
            switch (sampler->minFilter)
            {
                case TINYGLTF_TEXTURE_FILTER_NEAREST:
                    desc.minFilter = rhi::Filter::Nearest;
                    desc.maxLod = 0.0f;   // No mipmapping: always the full-size image.
                    break;
                case TINYGLTF_TEXTURE_FILTER_LINEAR:
                    desc.maxLod = 0.0f;
                    break;
                case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
                    desc.minFilter = rhi::Filter::Nearest;
                    desc.mipFilter = rhi::Filter::Nearest;
                    break;
                case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
                    desc.mipFilter = rhi::Filter::Nearest;
                    break;
                case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
                    desc.minFilter = rhi::Filter::Nearest;
                    break;
                default:
                    break;
            }
            return desc;
        }

        void importMaterials(Importer& importer)
        {
            const tinygltf::Model& gltf = importer.gltf;
            ModelData& model = importer.model;

            for (const tinygltf::Texture& source : gltf.textures)
            {
                TextureData texture;
                if (source.source >= 0 && static_cast<size_t>(source.source) < gltf.images.size())
                    texture.image = source.source;
                const bool validSampler = source.sampler >= 0 && static_cast<size_t>(source.sampler) < gltf.samplers.size();
                texture.sampler = toSamplerDesc(validSampler ? &gltf.samplers[static_cast<size_t>(source.sampler)] : nullptr);
                model.textures.push_back(texture);
            }

            for (const tinygltf::Material& source : gltf.materials)
            {
                const tinygltf::PbrMetallicRoughness& pbr = source.pbrMetallicRoughness;
                MaterialData material;
                material.name = source.name;
                if (pbr.baseColorFactor.size() == 4)
                    material.baseColorFactor = {static_cast<float>(pbr.baseColorFactor[0]), static_cast<float>(pbr.baseColorFactor[1]),
                                                static_cast<float>(pbr.baseColorFactor[2]), static_cast<float>(pbr.baseColorFactor[3])};
                material.metallicFactor = static_cast<float>(pbr.metallicFactor);
                material.roughnessFactor = static_cast<float>(pbr.roughnessFactor);
                material.alphaCutoff = static_cast<float>(source.alphaCutoff);
                material.doubleSided = source.doubleSided;
                if (source.alphaMode == "MASK")
                    material.alphaMode = AlphaMode::Mask;
                else if (source.alphaMode == "BLEND")
                    material.alphaMode = AlphaMode::Blend;

                const int texture = pbr.baseColorTexture.index;
                if (texture >= 0 && static_cast<size_t>(texture) < model.textures.size())
                {
                    material.baseColorTexture = texture;
                    const int32_t image = model.textures[static_cast<size_t>(texture)].image;
                    if (image >= 0)
                        importer.imageNeeded[static_cast<size_t>(image)] = true;
                    if (pbr.baseColorTexture.texCoord != 0)
                        ARC_LOG_WARN("{}: material '{}' uses TEXCOORD_{} for its base colour; only TEXCOORD_0 is "
                                     "imported, so it is used instead",
                                     importer.name, material.name, pbr.baseColorTexture.texCoord);
                }
                model.materials.push_back(material);
            }
        }

        void importImages(Importer& importer)
        {
            const tinygltf::Model& gltf = importer.gltf;
            importer.model.images.resize(gltf.images.size());
            for (size_t i = 0; i < gltf.images.size(); ++i)
            {
                if (!importer.imageNeeded[i])
                    continue;
                const tinygltf::Image& source = gltf.images[i];
                const std::string imageName =
                    std::string(importer.name) + " image " + std::to_string(i) + (source.name.empty() ? "" : " '" + source.name + "'");
                if (source.image.empty())
                {
                    ARC_LOG_WARN("{} has no data", imageName);
                    continue;
                }
                importer.model.images[i] = decodeImage(std::as_bytes(std::span(source.image)), imageName);
            }
        }

        // Keeps each image's encoded bytes as they are. Decoding happens afterwards, only for the
        // images a material uses, and with the engine's own decoder.
        bool keepEncodedImage(tinygltf::Image* image, int /*imageIndex*/, std::string* /*error*/, std::string* /*warning*/,
                              int /*requestedWidth*/, int /*requestedHeight*/, const unsigned char* bytes, int size,
                              void* /*userData*/)
        {
            image->image.assign(bytes, bytes + size);
            image->as_is = true;
            return true;
        }

        // File access through the engine, which reads UTF-8 paths on every platform. tinygltf
        // requires all five callbacks; the importer never writes.
        tinygltf::FsCallbacks fileCallbacks()
        {
            tinygltf::FsCallbacks callbacks{};
            callbacks.FileExists = [](const std::string& path, void*) { return fileExists(path); };
            callbacks.ExpandFilePath = [](const std::string& path, void*) { return path; };
            callbacks.ReadWholeFile = [](std::vector<unsigned char>* out, std::string* error, const std::string& path, void*) {
                const std::optional<std::vector<std::byte>> bytes = readFile(path);
                if (!bytes)
                {
                    if (error)
                        *error += "Cannot read '" + path + "'\n";
                    return false;
                }
                const auto* data = reinterpret_cast<const unsigned char*>(bytes->data());
                out->assign(data, data + bytes->size());
                return true;
            };
            callbacks.WriteWholeFile = [](std::string* error, const std::string&, const std::vector<unsigned char>&, void*) {
                if (error)
                    *error += "Writing files is not supported\n";
                return false;
            };
            callbacks.GetFileSizeInBytes = [](size_t* size, std::string* error, const std::string& path, void*) {
                const std::optional<uint64_t> bytes = fileSize(path);
                if (!bytes)
                {
                    if (error)
                        *error += "Cannot read '" + path + "'\n";
                    return false;
                }
                *size = static_cast<size_t>(*bytes);
                return true;
            };
            return callbacks;
        }

        void logLines(log::Level level, std::string_view name, std::string_view text)
        {
            while (!text.empty())
            {
                const size_t end = text.find('\n');
                const std::string_view line = text.substr(0, end);
                if (!line.empty())
                    ARC_LOG_IMPL(level, "{}: {}", name, line);
                if (end == std::string_view::npos)
                    break;
                text.remove_prefix(end + 1);
            }
        }

        std::string fileName(const std::string& path)
        {
            const size_t slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    }

    std::optional<ModelData> importGltf(const std::string& path)
    {
        const std::optional<std::vector<std::byte>> file = readFile(path);
        if (!file)
        {
            ARC_LOG_ERROR("Cannot read '{}'", path);
            return std::nullopt;
        }
        const size_t slash = path.find_last_of("/\\");
        const std::string baseDirectory = slash == std::string::npos ? std::string() : path.substr(0, slash);
        return importGltf(*file, baseDirectory, fileName(path));
    }

    std::optional<ModelData> importGltf(std::span<const std::byte> file, const std::string& baseDirectory,
                                        std::string_view name)
    {
        if (file.size() > std::numeric_limits<unsigned int>::max())
        {
            ARC_LOG_ERROR("{}: files above 4 GB are not supported", name);
            return std::nullopt;
        }

        tinygltf::TinyGLTF loader;
        std::string error;
        if (!loader.SetFsCallbacks(fileCallbacks(), &error))
        {
            ARC_LOG_ERROR("{}: {}", name, error);
            return std::nullopt;
        }
        loader.SetImageLoader(keepEncodedImage, nullptr);

        // A binary glTF starts with the magic "glTF"; anything else is parsed as JSON.
        const auto* bytes = reinterpret_cast<const unsigned char*>(file.data());
        const auto size = static_cast<unsigned int>(file.size());
        const bool binary = file.size() >= 4 && std::memcmp(bytes, "glTF", 4) == 0;

        tinygltf::Model gltf;
        std::string warning;
        const bool loaded = binary ? loader.LoadBinaryFromMemory(&gltf, &error, &warning, bytes, size, baseDirectory)
                                   : loader.LoadASCIIFromString(&gltf, &error, &warning, reinterpret_cast<const char*>(bytes),
                                                                size, baseDirectory);
        logLines(log::Level::Warning, name, warning);
        if (!loaded)
        {
            logLines(log::Level::Error, name, error.empty() ? std::string("cannot be parsed") : error);
            return std::nullopt;
        }

        for (const std::string& extension : gltf.extensionsRequired)
        {
            if (std::find(kSupportedRequiredExtensions.begin(), kSupportedRequiredExtensions.end(), extension) ==
                kSupportedRequiredExtensions.end())
            {
                ARC_LOG_ERROR("{}: requires the extension {}, which is not supported", name, extension);
                return std::nullopt;
            }
        }
        if (!gltf.extensionsUsed.empty())
        {
            std::string used;
            for (const std::string& extension : gltf.extensionsUsed)
                used += (used.empty() ? "" : ", ") + extension;
            ARC_LOG_INFO("{}: uses {} (optional; ignored where not supported)", name, used);
        }

        ModelData model;
        model.name = std::string(name);
        Importer importer{gltf, model, name, std::vector<bool>(gltf.images.size(), false)};

        importMaterials(importer);
        importImages(importer);

        for (size_t i = 0; i < gltf.meshes.size(); ++i)
        {
            const tinygltf::Mesh& source = gltf.meshes[i];
            MeshData mesh;
            mesh.name = source.name.empty() ? "#" + std::to_string(i) : source.name;
            mesh.firstPrimitive = static_cast<uint32_t>(model.primitives.size());
            for (const tinygltf::Primitive& primitive : source.primitives)
                importPrimitive(importer, primitive, mesh.name);
            mesh.primitiveCount = static_cast<uint32_t>(model.primitives.size()) - mesh.firstPrimitive;
            model.meshes.push_back(mesh);
        }

        importScene(importer);

        if (model.instances.empty())
            ARC_LOG_WARN("{}: the scene contains no meshes", name);
        return model;
    }
}
