#if defined(__GNUC__) && defined(_DEBUG) && defined(__OPTIMIZE__)
#warning "Undefing __OPTIMIZE__"
#undef __OPTIMIZE__
#endif

#include "Precompiled.h"
#include "Graphics/Model.h"
#include "Graphics/Mesh.h"
#include "Graphics/Material.h"

#include "Graphics/RHI/Texture.h"
#include "Maths/MathsBasicTypes.h"

#include "Maths/Transform.h"
#include "Core/Application.h"
#include "Core/StringUtilities.h"
#include "Utilities/AssetManager.h"

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_USE_CPP14
#define STB_IMAGE_WRITE_IMPLEMENTATION

#ifdef LUMOS_PRODUCTION
#define TINYGLTF_NOEXCEPTION
#endif
#include <ModelLoaders/tinygltf/tiny_gltf.h>

#include <ozz-animation/src/animation/offline/gltf/gltf2ozz.cc>

namespace Lumos::Graphics
{
    std::string AlbedoTexName   = "baseColorTexture";
    std::string NormalTexName   = "normalTexture";
    std::string MetallicTexName = "metallicRoughnessTexture";
    std::string GlossTexName    = "metallicRoughnessTexture";
    std::string AOTexName       = "occlusionTexture";
    std::string EmissiveTexName = "emissiveTexture";

    struct GLTFTexture
    {
        tinygltf::Image* Image;
        tinygltf::Sampler* Sampler;
    };

    static std::map<int32_t, size_t> ComponentSize {
        { TINYGLTF_COMPONENT_TYPE_BYTE, sizeof(int8_t) },
        { TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, sizeof(uint8_t) },
        { TINYGLTF_COMPONENT_TYPE_SHORT, sizeof(int16_t) },
        { TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, sizeof(uint16_t) },
        { TINYGLTF_COMPONENT_TYPE_INT, sizeof(int32_t) },
        { TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, sizeof(uint32_t) },
        { TINYGLTF_COMPONENT_TYPE_FLOAT, sizeof(float) },
        { TINYGLTF_COMPONENT_TYPE_DOUBLE, sizeof(double) }
    };

    static std::map<int, int> GLTF_COMPONENT_LENGTH_LOOKUP = {
        { TINYGLTF_TYPE_SCALAR, 1 },
        { TINYGLTF_TYPE_VEC2, 2 },
        { TINYGLTF_TYPE_VEC3, 3 },
        { TINYGLTF_TYPE_VEC4, 4 },
        { TINYGLTF_TYPE_MAT2, 4 },
        { TINYGLTF_TYPE_MAT3, 9 },
        { TINYGLTF_TYPE_MAT4, 16 }
    };

    static std::map<int, int> GLTF_COMPONENT_BYTE_SIZE_LOOKUP = {
        { TINYGLTF_COMPONENT_TYPE_BYTE, 1 },
        { TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, 1 },
        { TINYGLTF_COMPONENT_TYPE_SHORT, 2 },
        { TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, 2 },
        { TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, 4 },
        { TINYGLTF_COMPONENT_TYPE_FLOAT, 4 }
    };

    static Graphics::TextureWrap GetWrapMode(int mode)
    {
        switch(mode)
        {
        case TINYGLTF_TEXTURE_WRAP_REPEAT:
            return Graphics::TextureWrap::REPEAT;
        case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
            return Graphics::TextureWrap::CLAMP_TO_EDGE;
        case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
            return Graphics::TextureWrap::MIRRORED_REPEAT;
        default:
            return Graphics::TextureWrap::REPEAT;
        }
    }

    static Graphics::TextureFilter GetFilter(int value)
    {
        switch(value)
        {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
            return Graphics::TextureFilter::NEAREST;
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            return Graphics::TextureFilter::LINEAR;
        default:
            return Graphics::TextureFilter::LINEAR;
        }
    }

    std::vector<SharedPtr<Material>> LoadMaterials(tinygltf::Model& gltfModel)
    {
        LUMOS_PROFILE_FUNCTION();
        std::vector<SharedPtr<Graphics::Texture2D>> loadedTextures;
        std::vector<SharedPtr<Material>> loadedMaterials;
        loadedTextures.reserve(gltfModel.textures.size());
        loadedMaterials.reserve(gltfModel.materials.size());

        for(tinygltf::Texture& gltfTexture : gltfModel.textures)
        {
            GLTFTexture imageAndSampler {};

            if(gltfTexture.source != -1)
            {
                imageAndSampler.Image = &gltfModel.images.at(gltfTexture.source);
            }

            if(gltfTexture.sampler != -1)
            {
                imageAndSampler.Sampler = &gltfModel.samplers.at(gltfTexture.sampler);
            }

            if(imageAndSampler.Image)
            {
                Graphics::TextureDesc params;
                if(gltfTexture.sampler != -1)
                    params = Graphics::TextureDesc(GetFilter(imageAndSampler.Sampler->minFilter), GetFilter(imageAndSampler.Sampler->magFilter), GetWrapMode(imageAndSampler.Sampler->wrapS));
                else
                    LUMOS_LOG_WARN("MISSING SAMPLER");
                Graphics::Texture2D* texture2D = Graphics::Texture2D::CreateFromSource(imageAndSampler.Image->width, imageAndSampler.Image->height, imageAndSampler.Image->image.data(), params);
                loadedTextures.push_back(SharedPtr<Graphics::Texture2D>(texture2D ? texture2D : nullptr));
            }
        }

        auto TextureName = [&](int index)
        {
            if(index >= 0)
            {
                const tinygltf::Texture& tex = gltfModel.textures[index];
                if(tex.source >= 0 && tex.source < loadedTextures.size())
                {
                    return loadedTextures[tex.source];
                }
            }
            return SharedPtr<Graphics::Texture2D>();
        };

        for(tinygltf::Material& mat : gltfModel.materials)
        {
            // TODO : if(isAnimated) Load deferredColourAnimated;
            // auto shader = Application::Get().GetShaderLibrary()->GetResource("//CoreShaders/ForwardPBR.shader");
            auto shader = Application::Get().GetShaderLibrary()->GetResource("ForwardPBR");

            SharedPtr<Material> pbrMaterial = CreateSharedPtr<Material>(shader);
            PBRMataterialTextures textures;
            Graphics::MaterialProperties properties;

            const tinygltf::PbrMetallicRoughness& pbr = mat.pbrMetallicRoughness;
            textures.albedo                           = TextureName(pbr.baseColorTexture.index);
            textures.normal                           = TextureName(mat.normalTexture.index);
            textures.ao                               = TextureName(mat.occlusionTexture.index);
            textures.emissive                         = TextureName(mat.emissiveTexture.index);
            textures.metallic                         = TextureName(pbr.metallicRoughnessTexture.index);

            // TODO: correct way of handling this
            if(textures.metallic)
                properties.workflow = PBR_WORKFLOW_METALLIC_ROUGHNESS;
            else
                properties.workflow = PBR_WORKFLOW_SEPARATE_TEXTURES;

            // metallic-roughness workflow:
            auto baseColourFactor = mat.values.find("baseColorFactor");
            auto roughnessFactor  = mat.values.find("roughnessFactor");
            auto metallicFactor   = mat.values.find("metallicFactor");

            if(roughnessFactor != mat.values.end())
            {
                properties.roughness = static_cast<float>(roughnessFactor->second.Factor());
            }

            if(metallicFactor != mat.values.end())
            {
                properties.metallic = static_cast<float>(metallicFactor->second.Factor());
            }

            if(baseColourFactor != mat.values.end())
            {
                properties.albedoColour = glm::vec4((float)baseColourFactor->second.ColorFactor()[0], (float)baseColourFactor->second.ColorFactor()[1], (float)baseColourFactor->second.ColorFactor()[2], 1.0f);
            }

            // Extensions
            auto metallicGlossinessWorkflow = mat.extensions.find("KHR_materials_pbrSpecularGlossiness");
            if(metallicGlossinessWorkflow != mat.extensions.end())
            {
                if(metallicGlossinessWorkflow->second.Has("diffuseTexture"))
                {
                    int index       = metallicGlossinessWorkflow->second.Get("diffuseTexture").Get("index").Get<int>();
                    textures.albedo = loadedTextures[gltfModel.textures[index].source];
                }

                if(metallicGlossinessWorkflow->second.Has("metallicGlossinessTexture"))
                {
                    int index           = metallicGlossinessWorkflow->second.Get("metallicGlossinessTexture").Get("index").Get<int>();
                    textures.roughness  = loadedTextures[gltfModel.textures[index].source];
                    properties.workflow = PBR_WORKFLOW_SPECULAR_GLOSINESS;
                }

                if(metallicGlossinessWorkflow->second.Has("diffuseFactor"))
                {
                    auto& factor              = metallicGlossinessWorkflow->second.Get("diffuseFactor");
                    properties.albedoColour.x = factor.ArrayLen() > 0 ? float(factor.Get(0).IsNumber() ? factor.Get(0).Get<double>() : factor.Get(0).Get<int>()) : 1.0f;
                    properties.albedoColour.y = factor.ArrayLen() > 1 ? float(factor.Get(1).IsNumber() ? factor.Get(1).Get<double>() : factor.Get(1).Get<int>()) : 1.0f;
                    properties.albedoColour.z = factor.ArrayLen() > 2 ? float(factor.Get(2).IsNumber() ? factor.Get(2).Get<double>() : factor.Get(2).Get<int>()) : 1.0f;
                    properties.albedoColour.w = factor.ArrayLen() > 3 ? float(factor.Get(3).IsNumber() ? factor.Get(3).Get<double>() : factor.Get(3).Get<int>()) : 1.0f;
                }
                if(metallicGlossinessWorkflow->second.Has("metallicFactor"))
                {
                    auto& factor        = metallicGlossinessWorkflow->second.Get("metallicFactor");
                    properties.metallic = factor.ArrayLen() > 0 ? float(factor.Get(0).IsNumber() ? factor.Get(0).Get<double>() : factor.Get(0).Get<int>()) : 1.0f;
                }
                if(metallicGlossinessWorkflow->second.Has("glossinessFactor"))
                {
                    auto& factor         = metallicGlossinessWorkflow->second.Get("glossinessFactor");
                    properties.roughness = 1.0f - float(factor.IsNumber() ? factor.Get<double>() : factor.Get<int>());
                }
            }

            auto ext_ior = mat.extensions.find("KHR_materials_ior");
            if(ext_ior != mat.extensions.end())
            {
                // https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_materials_ior

                if(ext_ior->second.Has("ior"))
                {
                    auto& factor = ext_ior->second.Get("ior");
                    float ior    = float(factor.IsNumber() ? factor.Get<double>() : factor.Get<int>());

                    properties.reflectance = std::sqrt(std::pow((ior - 1.0f) / (ior + 1.0f), 2.0f) / 16.0f);
                }
            }

            pbrMaterial->SetTextures(textures);
            pbrMaterial->SetMaterialProperites(properties);
            pbrMaterial->SetName(mat.name);

            if(mat.doubleSided)
                pbrMaterial->SetFlag(Graphics::Material::RenderFlags::TWOSIDED);

            if(mat.alphaMode != "OPAQUE")
                pbrMaterial->SetFlag(Graphics::Material::RenderFlags::ALPHABLEND);

            loadedMaterials.push_back(pbrMaterial);
        }

        return loadedMaterials;
    }

    std::vector<Graphics::Mesh*> LoadMesh(tinygltf::Model& model, tinygltf::Mesh& mesh, std::vector<SharedPtr<Material>>& materials, Maths::Transform& parentTransform)
    {
        std::vector<Graphics::Mesh*> meshes;

        for(auto& primitive : mesh.primitives)
        {
            const tinygltf::Accessor& indicesAccessor = model.accessors[primitive.indices];

            std::vector<uint32_t> indices;
            std::vector<Graphics::Vertex> vertices;

            uint32_t vertexCount = (uint32_t)(primitive.attributes.empty() ? 0 : model.accessors.at(primitive.attributes["POSITION"]).count);

            indices.resize(indicesAccessor.count);
            vertices.resize(vertexCount);

            bool hasTangents   = false;
            bool hasBitangents = false;

            for(auto& attribute : primitive.attributes)
            {
                // Get accessor info
                auto& accessor            = model.accessors.at(attribute.second);
                auto& bufferView          = model.bufferViews.at(accessor.bufferView);
                auto& buffer              = model.buffers.at(bufferView.buffer);
                int componentLength       = GLTF_COMPONENT_LENGTH_LOOKUP.at(accessor.type);
                int componentTypeByteSize = GLTF_COMPONENT_BYTE_SIZE_LOOKUP.at(accessor.componentType);

                // Extra vertex data from buffer
                size_t bufferOffset       = bufferView.byteOffset + accessor.byteOffset;
                int bufferLength          = static_cast<int>(accessor.count) * componentLength * componentTypeByteSize;
                auto first                = buffer.data.begin() + bufferOffset;
                auto last                 = buffer.data.begin() + bufferOffset + bufferLength;
                std::vector<uint8_t> data = std::vector<uint8_t>(first, last);

                // -------- Position attribute -----------

                if(attribute.first == "POSITION")
                {
                    size_t positionCount            = accessor.count;
                    Maths::Vector3Simple* positions = reinterpret_cast<Maths::Vector3Simple*>(data.data());
                    for(auto p = 0; p < positionCount; ++p)
                    {
                        vertices[p].Position = parentTransform.GetWorldMatrix() * Maths::ToVector4(positions[p]);
                    }
                }

                // -------- Normal attribute -----------

                else if(attribute.first == "NORMAL")
                {
                    size_t normalCount            = accessor.count;
                    Maths::Vector3Simple* normals = reinterpret_cast<Maths::Vector3Simple*>(data.data());
                    for(auto p = 0; p < normalCount; ++p)
                    {
                        //vertices[p].Normal = (parentTransform.GetWorldMatrix() * Maths::ToVector4(normals[p]));
                        vertices[p].Normal = glm::transpose(glm::inverse(glm::mat3(parentTransform.GetWorldMatrix()))) * (glm::vec3(Maths::ToVector4(normals[p])));
                        vertices[p].Normal = glm::normalize(vertices[p].Normal);
                    }
                }

                // -------- Texcoord attribute -----------

                else if(attribute.first == "TEXCOORD_0")
                {
                    size_t uvCount            = accessor.count;
                    Maths::Vector2Simple* uvs = reinterpret_cast<Maths::Vector2Simple*>(data.data());
                    for(auto p = 0; p < uvCount; ++p)
                    {
                        vertices[p].TexCoords = ToVector(uvs[p]);
                    }
                }

                // -------- Colour attribute -----------

                else if(attribute.first == "COLOR_0")
                {
                    size_t uvCount                = accessor.count;
                    Maths::Vector4Simple* colours = reinterpret_cast<Maths::Vector4Simple*>(data.data());
                    for(auto p = 0; p < uvCount; ++p)
                    {
                        vertices[p].Colours = ToVector(colours[p]);
                    }
                }

                // -------- Tangent attribute -----------

                else if(attribute.first == "TANGENT")
                {
                    hasTangents               = true;
                    size_t uvCount            = accessor.count;
                    Maths::Vector3Simple* uvs = reinterpret_cast<Maths::Vector3Simple*>(data.data());
                    for(auto p = 0; p < uvCount; ++p)
                    {
                        vertices[p].Tangent = glm::normalize(parentTransform.GetWorldMatrix() * ToVector4(uvs[p]));
                    }
                }

                else if(attribute.first == "BINORMAL")
                {
                    hasBitangents             = true;
                    size_t uvCount            = accessor.count;
                    Maths::Vector3Simple* uvs = reinterpret_cast<Maths::Vector3Simple*>(data.data());
                    for(auto p = 0; p < uvCount; ++p)
                    {
                        vertices[p].Bitangent = glm::normalize(parentTransform.GetWorldMatrix() * ToVector4(uvs[p]));
                    }
                }
            }

            // -------- Indices ----------
            {
                // Get accessor info
                auto indexAccessor   = model.accessors.at(primitive.indices);
                auto indexBufferView = model.bufferViews.at(indexAccessor.bufferView);
                auto indexBuffer     = model.buffers.at(indexBufferView.buffer);

                int componentLength       = GLTF_COMPONENT_LENGTH_LOOKUP.at(indexAccessor.type);
                int componentTypeByteSize = GLTF_COMPONENT_BYTE_SIZE_LOOKUP.at(indexAccessor.componentType);

                // Extra index data
                size_t bufferOffset       = indexBufferView.byteOffset + indexAccessor.byteOffset;
                int bufferLength          = static_cast<int>(indexAccessor.count) * componentLength * componentTypeByteSize;
                auto first                = indexBuffer.data.begin() + bufferOffset;
                auto last                 = indexBuffer.data.begin() + bufferOffset + bufferLength;
                std::vector<uint8_t> data = std::vector<uint8_t>(first, last);

                size_t indicesCount = indexAccessor.count;
                if(componentTypeByteSize == 1)
                {
                    uint8_t* in = reinterpret_cast<uint8_t*>(data.data());
                    for(auto iCount = 0; iCount < indicesCount; iCount++)
                    {
                        indices[iCount] = (uint32_t)in[iCount];
                    }
                }
                else if(componentTypeByteSize == 2)
                {
                    uint16_t* in = reinterpret_cast<uint16_t*>(data.data());
                    for(auto iCount = 0; iCount < indicesCount; iCount++)
                    {
                        indices[iCount] = (uint32_t)in[iCount];
                    }
                }
                else if(componentTypeByteSize == 4)
                {
                    auto in = reinterpret_cast<uint32_t*>(data.data());
                    for(auto iCount = 0; iCount < indicesCount; iCount++)
                    {
                        indices[iCount] = in[iCount];
                    }
                }
                else
                {
                    LUMOS_LOG_WARN("Unsupported indices data type - {0}", componentTypeByteSize);
                }
            }

            if(!hasTangents || !hasBitangents)
                Graphics::Mesh::GenerateTangentsAndBitangents(vertices.data(), uint32_t(vertices.size()), indices.data(), uint32_t(indices.size()));

            auto lMesh = new Graphics::Mesh(indices, vertices);

            meshes.emplace_back(lMesh);
        }

        return meshes;
    }

    void LoadNode(Model* mainModel, int nodeIndex, const glm::mat4& parentTransform, tinygltf::Model& model, std::vector<SharedPtr<Material>>& materials, std::vector<std::vector<Graphics::Mesh*>>& meshes)
    {
        LUMOS_PROFILE_FUNCTION();
        if(nodeIndex < 0)
        {
            return;
        }

        auto& node = model.nodes[nodeIndex];
        auto name  = node.name;

#ifdef DEBUG_PRINT_GLTF_LOADING
        LUMOS_LOG_INFO("asset.copyright : {0}", model.asset.copyright);
        LUMOS_LOG_INFO("asset.generator : {0}", model.asset.generator);
        LUMOS_LOG_INFO("asset.version : {0}", model.asset.version);
        LUMOS_LOG_INFO("asset.minVersion : {0}", model.asset.minVersion);
#endif

        Maths::Transform transform;
        glm::mat4 matrix;
        glm::mat4 position = glm::mat4(1.0f);
        glm::mat4 rotation = glm::mat4(1.0f);
        glm::mat4 scale    = glm::mat4(1.0f);

        if(!node.scale.empty())
        {
            scale = glm::scale(glm::mat4(1.0), glm::vec3(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2])));
            // transform.SetLocalScale(glm::vec3(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2])));
        }

        if(!node.rotation.empty())
        {
            rotation = glm::toMat4(glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])));

            // transform.SetLocalOrientation(glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])));
        }

        if(!node.translation.empty())
        {
            // transform.SetLocalPosition(glm::vec3(static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2])));
            position = glm::translate(glm::mat4(1.0), glm::vec3(static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2])));
        }

        if(!node.matrix.empty())
        {
            float matrixData[16];
            for(int i = 0; i < 16; i++)
                matrixData[i] = float(node.matrix.data()[i]);
            matrix = glm::make_mat4(matrixData);
            transform.SetLocalTransform(matrix);
        }
        else
        {
            matrix = position * rotation * scale;
            transform.SetLocalTransform(matrix);
        }

        transform.ApplyTransform();
        transform.SetWorldMatrix(parentTransform);

        if(node.mesh >= 0)
        {
            int subIndex = 0;

            auto meshes = LoadMesh(model, model.meshes[node.mesh], materials, transform);

            for(auto& mesh : meshes)
            {
                auto subname = model.meshes[node.mesh].name;
                auto lMesh   = SharedPtr<Graphics::Mesh>(mesh);
                lMesh->SetName(subname);

                int materialIndex = model.meshes[node.mesh].primitives[subIndex].material;
                if(materialIndex >= 0)
                    lMesh->SetMaterial(materials[materialIndex]);

                mainModel->AddMesh(lMesh);

                /*if (node.skin >= 0)
                {
                }*/

                subIndex++;
            }
        }

        if(!node.children.empty())
        {
            for(int child : node.children)
            {
                LoadNode(mainModel, child, transform.GetLocalMatrix(), model, materials, meshes);
            }
        }
    }

    void Model::LoadGLTF(const std::string& path)
    {
        LUMOS_PROFILE_FUNCTION();
        tinygltf::Model model;
        tinygltf::TinyGLTF loader;
        std::string err;
        std::string warn;

        std::string ext = StringUtilities::GetFilePathExtension(path);

        // loader.SetImageLoader(tinygltf::LoadImageData, nullptr);
        // loader.SetImageWriter(tinygltf::WriteImageData, nullptr);

        bool ret;

        if(ext == "glb") // assume binary glTF.
        {
            LUMOS_PROFILE_SCOPE(".glb binary loading");
            ret = tinygltf::TinyGLTF().LoadBinaryFromFile(&model, &err, &warn, path);
        }
        else // assume ascii glTF.
        {
            LUMOS_PROFILE_SCOPE(".gltf loading");
            ret = tinygltf::TinyGLTF().LoadASCIIFromFile(&model, &err, &warn, path);
        }

        if(!err.empty())
        {
            LUMOS_LOG_ERROR(err);
        }

        if(!warn.empty())
        {
            LUMOS_LOG_ERROR(warn);
        }

        if(!ret)
        {
            LUMOS_LOG_ERROR("Failed to parse glTF");
        }
        {
            LUMOS_PROFILE_SCOPE("Parse GLTF Model");

            auto LoadedMaterials = LoadMaterials(model);

            std::string name = path.substr(path.find_last_of('/') + 1);

            auto meshes                      = std::vector<std::vector<Graphics::Mesh*>>();
            const tinygltf::Scene& gltfScene = model.scenes[Lumos::Maths::Max(0, model.defaultScene)];
            for(size_t i = 0; i < gltfScene.nodes.size(); i++)
            {
                LoadNode(this, gltfScene.nodes[i], glm::mat4(1.0f), model, LoadedMaterials, meshes);
            }

            auto skins = model.skins;
            if(!skins.empty())
            {
                using namespace ozz::animation::offline;
                GltfImporter impl;
                ozz::animation::offline::OzzImporter& importer = impl;
                OzzImporter::NodeType types                    = {};

                importer.Load(path.c_str());
                RawSkeleton* rawSkeleton = new RawSkeleton();
                importer.Import(rawSkeleton, types);

                ozz::animation::offline::SkeletonBuilder skeletonBuilder;

                m_Skeleton = SharedPtr<ozz::animation::Skeleton>(skeletonBuilder(*rawSkeleton).release());

                ozz::animation::offline::AnimationBuilder animBuilder;
                auto animationNames = importer.GetAnimationNames();

                for(auto& animName : animationNames)
                {
                    RawAnimation* rawAnimation = new RawAnimation();
                    importer.Import(animName.c_str(), *m_Skeleton.get(), 30.0f, rawAnimation);

                    m_Animation.push_back(SharedPtr<ozz::animation::Animation>(animBuilder(*rawAnimation).release()));
                    LUMOS_LOG_INFO("Loaded Anim : {0}", animName);
                }
            }
        }
    }
}
