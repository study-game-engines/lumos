#include "Precompiled.h"
#include "GLTexture.h"
#include "Platform/OpenGL/GL.h"
#include "Platform/OpenGL/GLUtilities.h"
#include "Platform/OpenGL/GLShader.h"
#include "Utilities/LoadImage.h"
#include "Maths/Random.h"
#include "Maths/MathsUtilities.h"
#include "GLRenderer.h"

namespace Lumos
{
    namespace Graphics
    {
        GLTexture2D::GLTexture2D(TextureDesc parameters, uint32_t width, uint32_t height)
            : m_Parameters(parameters)
            , m_Width(width)
            , m_Height(height)
        {
            m_Format = m_Parameters.format;
            m_Handle = Load(nullptr);
        }

        GLTexture2D::GLTexture2D(uint32_t width, uint32_t height, void* data, TextureDesc parameters, TextureLoadOptions loadOptions)
            : m_FileName("")
            , m_Name("")
            , m_Parameters(parameters)
            , m_LoadOptions(loadOptions)
            , m_Width(width)
            , m_Height(height)
            , m_Format(parameters.format)
        {
            m_Handle = Load(data);
        }

        GLTexture2D::GLTexture2D(const std::string& name, const std::string& filename, const TextureDesc parameters, const TextureLoadOptions loadOptions)
            : m_FileName(filename)
            , m_Name(name)
            , m_Parameters(parameters)
            , m_LoadOptions(loadOptions)
        {
            m_Format = m_Parameters.format;
            m_Handle = Load(nullptr);
        }

        GLTexture2D::~GLTexture2D()
        {
            GLCall(glDeleteTextures(1, &m_Handle));
        }

        void GLTexture2D::Resize(uint32_t width, uint32_t height)
        {
            m_Width  = width;
            m_Height = height;

            GLCall(glDeleteTextures(1, &m_Handle));
            BuildTexture();
        }

        uint32_t GLTexture2D::LoadTexture(void* data)
        {
            uint32_t handle;
            GLCall(glGenTextures(1, &handle));
            GLCall(glBindTexture(GL_TEXTURE_2D, handle));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (m_Parameters.minFilter == TextureFilter::LINEAR && (m_Parameters.generateMipMaps || (m_Flags & TextureFlags::Texture_CreateMips))) ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_Parameters.magFilter == TextureFilter::LINEAR ? GL_LINEAR : GL_NEAREST));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLUtilities::TextureWrapToGL(m_Parameters.wrap)));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLUtilities::TextureWrapToGL(m_Parameters.wrap)));

            if(m_Parameters.anisotropicFiltering && Renderer::GetCapabilities().MaxAnisotropy > 0)
                GLCall(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, Renderer::GetCapabilities().MaxAnisotropy));

            uint32_t format = GLUtilities::FormatToGL(m_Parameters.format, m_Parameters.srgb);
            m_Flags         = m_Parameters.flags;

            GLCall(glTexImage2D(GL_TEXTURE_2D, 0, format, m_Width, m_Height, 0, GLUtilities::FormatToInternalFormat(format), GLUtilities::GetGLTypefromFormat(m_Parameters.format), data ? data : NULL));

            if(m_Parameters.generateMipMaps || m_Flags & TextureFlags::Texture_CreateMips)
            {
                GLCall(glGenerateMipmap(GL_TEXTURE_2D));
                m_MipLevels = static_cast<uint32_t>(std::floor(std::log2(Maths::Max(m_Width, m_Height)))) + 1;
            }
#ifdef LUMOS_DEBUG
            GLCall(glBindTexture(GL_TEXTURE_2D, 0));
#endif

            return handle;
        }

        uint32_t GLTexture2D::Load(void* data)
        {
            uint8_t* pixels = nullptr;

            if(data != nullptr)
            {
                pixels = reinterpret_cast<uint8_t*>(data);
            }
            else
            {
                if(m_FileName != "")
                {
                    pixels = LoadTextureData();
                }
            }

            uint32_t handle = LoadTexture(pixels);

            m_MipLevels = static_cast<uint32_t>(std::floor(std::log2(Maths::Max(m_Width, m_Height)))) + 1;

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());

            return handle;
        }

        void GLTexture2D::SetData(const void* pixels)
        {
            GLCall(glBindTexture(GL_TEXTURE_2D, m_Handle));
            GLCall(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, GLUtilities::FormatToGL(m_Parameters.format, m_Parameters.srgb), GL_UNSIGNED_BYTE, pixels));
            GLCall(glGenerateMipmap(GL_TEXTURE_2D));
        }

        void GLTexture2D::Bind(uint32_t slot) const
        {
            GLCall(glActiveTexture(GL_TEXTURE0 + slot));
            GLCall(glBindTexture(GL_TEXTURE_2D, m_Handle));
        }

        void GLTexture2D::Unbind(uint32_t slot) const
        {
            GLCall(glActiveTexture(GL_TEXTURE0 + slot));
            GLCall(glBindTexture(GL_TEXTURE_2D, 0));
        }

        void GLTexture2D::Load(uint32_t width, uint32_t height, void* data, TextureDesc parameters, TextureLoadOptions loadOptions)
        {
            GLCall(glDeleteTextures(1, &m_Handle));

            m_FileName    = "";
            m_Name        = "";
            m_Parameters  = parameters;
            m_LoadOptions = loadOptions;
            m_Width       = width;
            m_Height      = height;
            m_Format      = parameters.format;
            m_Handle      = Load(data);
        }

        void GLTexture2D::BuildTexture()
        {
            m_Name = "Texture Attachment";

            uint32_t Format  = GLUtilities::FormatToGL(m_Format, m_Parameters.srgb);
            uint32_t Format2 = GLUtilities::FormatToInternalFormat(Format);
            glGenTextures(1, &m_Handle);
            glBindTexture(GL_TEXTURE_2D, m_Handle);
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (m_Parameters.minFilter == TextureFilter::LINEAR && (m_Parameters.generateMipMaps || (m_Flags & TextureFlags::Texture_CreateMips))) ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_Parameters.magFilter == TextureFilter::LINEAR ? GL_LINEAR : GL_NEAREST));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLUtilities::TextureWrapToGL(m_Parameters.wrap)));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLUtilities::TextureWrapToGL(m_Parameters.wrap)));

            glTexImage2D(GL_TEXTURE_2D, 0, Format, m_Width, m_Height, 0, Format2, GLUtilities::GetGLTypefromFormat(m_Parameters.format), nullptr);

            if(m_Parameters.generateMipMaps || m_Flags & TextureFlags::Texture_CreateMips)
            {
                GLCall(glGenerateMipmap(GL_TEXTURE_2D));
                m_MipLevels = static_cast<uint32_t>(std::floor(std::log2(Maths::Max(m_Width, m_Height)))) + 1;
            }

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());
        }

        uint8_t* GLTexture2D::LoadTextureData()
        {
            uint8_t* pixels = nullptr;
            m_Width         = 0;
            m_Height        = 0;
            if(m_FileName != "NULL")
            {
                uint32_t bits;
                pixels              = Lumos::LoadImageFromFile(m_FileName.c_str(), &m_Width, &m_Height, &bits, &isHDR, !m_LoadOptions.flipY);
                m_Parameters.format = BitsToFormat(bits);
            }

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());

            return pixels;
        };

        GLTextureCube::GLTextureCube(uint32_t size, uint8_t* data, bool hdr)
            : m_Size(size)
            , m_Bits(0)
            , m_NumMips(0)
        {
            m_NumMips           = Texture::CalculateMipMapCount(size, size);
            m_Format            = hdr ? RHIFormat::R32G32B32A32_Float : RHIFormat::R8G8B8A8_Unorm;
            m_Parameters.format = m_Format;

            uint32_t internalFormat = GLUtilities::FormatToGL(m_Parameters.format, m_Parameters.srgb);
            uint32_t format         = GLUtilities::FormatToInternalFormat(internalFormat);

            glGenTextures(1, &m_Handle);
            glBindTexture(GL_TEXTURE_CUBE_MAP, m_Handle);

#ifndef LUMOS_PLATFORM_MOBILE
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP);
#endif
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            // set textures
            for(int i = 0; i < 6; ++i)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat, size, size, 0, format, GL_FLOAT, nullptr);

            GLCall(glGenerateMipmap(GL_TEXTURE_CUBE_MAP));

            m_Width  = size;
            m_Height = size;

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());
        }

        GLTextureCube::GLTextureCube(const std::string& filepath)
            : m_Width(0)
            , m_Height(0)
            , m_Size(0)
            , m_NumMips(0)
            , m_Bits(0)
            , m_Format()
        {
            m_Files[0] = filepath;
            m_Handle   = LoadFromSingleFile();
            m_Format   = m_Parameters.format;

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());
        }

        GLTextureCube::GLTextureCube(const std::string* files)
        {
            for(uint32_t i = 0; i < 6; i++)
                m_Files[i] = files[i];
            m_Handle = LoadFromMultipleFiles();
            m_Format = m_Parameters.format;

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());
        }

        GLTextureCube::GLTextureCube(const std::string* files, uint32_t mips, TextureDesc params, TextureLoadOptions loadOptions)
        {
            m_Parameters = params;
            m_NumMips    = mips;
            for(uint32_t i = 0; i < mips; i++)
                m_Files[i] = files[i];

            m_Handle = LoadFromVCross(mips);

            m_Format = m_Parameters.format;

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());
        }

        GLTextureCube::~GLTextureCube()
        {
            GLCall(glDeleteTextures(m_NumMips, &m_Handle));
        }

        void GLTextureCube::Bind(uint32_t slot) const
        {
            GLCall(glActiveTexture(GL_TEXTURE0 + slot));
            GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, m_Handle));
        }

        void GLTextureCube::Unbind(uint32_t slot) const
        {
            GLCall(glActiveTexture(GL_TEXTURE0 + slot));
            GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
        }

        uint32_t GLTextureCube::LoadFromSingleFile()
        {
            // TODO: Implement
            return 0;
        }

        uint32_t GLTextureCube::LoadFromMultipleFiles()
        {
            const std::string& xpos = m_Files[0];
            const std::string& xneg = m_Files[1];
            const std::string& ypos = m_Files[2];
            const std::string& yneg = m_Files[3];
            const std::string& zpos = m_Files[4];
            const std::string& zneg = m_Files[5];

            m_Parameters.format = RHIFormat::R8G8B8A8_Unorm;

            uint32_t width, height, bits;
            bool isHDR  = false;
            uint8_t* xp = Lumos::LoadImageFromFile(xpos, &width, &height, &bits, &isHDR, true);
            uint8_t* xn = Lumos::LoadImageFromFile(xneg, &width, &height, &bits, &isHDR, true);
            uint8_t* yp = Lumos::LoadImageFromFile(ypos, &width, &height, &bits, &isHDR, true);
            uint8_t* yn = Lumos::LoadImageFromFile(yneg, &width, &height, &bits, &isHDR, true);
            uint8_t* zp = Lumos::LoadImageFromFile(zpos, &width, &height, &bits, &isHDR, true);
            uint8_t* zn = Lumos::LoadImageFromFile(zneg, &width, &height, &bits, &isHDR, true);

            uint32_t result;
            GLCall(glGenTextures(1, &result));
            GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, result));
            GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
            GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

            uint32_t internalFormat = GLUtilities::FormatToGL(m_Parameters.format, m_Parameters.srgb);
            uint32_t format         = GLUtilities::FormatToInternalFormat(internalFormat);

            GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, xp));
            GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, xn));

            GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, yp));
            GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, yn));

            GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, zp));
            GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, zn));

            GLCall(glGenerateMipmap(GL_TEXTURE_CUBE_MAP));

            delete[] xp;
            delete[] xn;
            delete[] yp;
            delete[] yn;
            delete[] zp;
            delete[] zn;

            return result;
        }

        uint32_t GLTextureCube::LoadFromVCross(uint32_t mips)
        {
            uint32_t srcWidth, srcHeight, bits;
            uint8_t*** cubeTextureData = new uint8_t**[mips];
            for(uint32_t i = 0; i < mips; i++)
                cubeTextureData[i] = new uint8_t*[6];

            uint32_t* faceWidths  = new uint32_t[mips];
            uint32_t* faceHeights = new uint32_t[mips];

            for(uint32_t m = 0; m < mips; m++)
            {
                bool isHDR          = false;
                uint8_t* data       = Lumos::LoadImageFromFile(m_Files[m], &srcWidth, &srcHeight, &bits, &isHDR, !m_LoadOptions.flipY);
                m_Parameters.format = BitsToFormat(bits);
                uint32_t stride     = bits / 8;

                uint32_t face       = 0;
                uint32_t faceWidth  = srcWidth / 3;
                uint32_t faceHeight = srcHeight / 4;
                faceWidths[m]       = faceWidth;
                faceHeights[m]      = faceHeight;
                for(uint32_t cy = 0; cy < 4; cy++)
                {
                    for(uint32_t cx = 0; cx < 3; cx++)
                    {
                        if(cy == 0 || cy == 2 || cy == 3)
                        {
                            if(cx != 1)
                                continue;
                        }

                        cubeTextureData[m][face] = new uint8_t[faceWidth * faceHeight * stride];

                        for(uint32_t y = 0; y < faceHeight; y++)
                        {
                            uint32_t offset = y;
                            if(face == 5)
                                offset = faceHeight - (y + 1);
                            uint32_t yp = cy * faceHeight + offset;
                            for(uint32_t x = 0; x < faceWidth; x++)
                            {
                                offset = x;
                                if(face == 5)
                                    offset = faceWidth - (x + 1);
                                uint32_t xp                                                = cx * faceWidth + offset;
                                cubeTextureData[m][face][(x + y * faceWidth) * stride + 0] = data[(xp + yp * srcWidth) * stride + 0];
                                cubeTextureData[m][face][(x + y * faceWidth) * stride + 1] = data[(xp + yp * srcWidth) * stride + 1];
                                cubeTextureData[m][face][(x + y * faceWidth) * stride + 2] = data[(xp + yp * srcWidth) * stride + 2];
                                if(stride >= 4)
                                    cubeTextureData[m][face][(x + y * faceWidth) * stride + 3] = data[(xp + yp * srcWidth) * stride + 3];
                            }
                        }
                        face++;
                    }
                }
                delete[] data;
            }

            uint32_t result;
            GLCall(glGenTextures(1, &result));
            GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, result));

            GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR));
            GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GLCall(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));

            uint32_t internalFormat = GLUtilities::FormatToGL(m_Parameters.format, m_Parameters.srgb);
            uint32_t format         = GLUtilities::FormatToInternalFormat(internalFormat);
            for(uint32_t m = 0; m < mips; m++)
            {
                GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, m, internalFormat, faceWidths[m], faceHeights[m], 0, format, GL_UNSIGNED_BYTE, cubeTextureData[m][3]));
                GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, m, internalFormat, faceWidths[m], faceHeights[m], 0, format, GL_UNSIGNED_BYTE, cubeTextureData[m][1]));

                GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, m, internalFormat, faceWidths[m], faceHeights[m], 0, format, GL_UNSIGNED_BYTE, cubeTextureData[m][0]));
                GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, m, internalFormat, faceWidths[m], faceHeights[m], 0, format, GL_UNSIGNED_BYTE, cubeTextureData[m][4]));

                GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, m, internalFormat, faceWidths[m], faceHeights[m], 0, format, GL_UNSIGNED_BYTE, cubeTextureData[m][2]));
                GLCall(glTexImage2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, m, internalFormat, faceWidths[m], faceHeights[m], 0, format, GL_UNSIGNED_BYTE, cubeTextureData[m][5]));
            }
            GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));

            for(uint32_t m = 0; m < mips; m++)
            {
                for(uint32_t f = 0; f < 6; f++)
                {
                    delete[] cubeTextureData[m][f];
                }
                delete[] cubeTextureData[m];
            }

            delete[] cubeTextureData;
            delete[] faceHeights;
            delete[] faceWidths;

            return result;
        }

        void GLTextureCube::GenerateMipMaps(CommandBuffer* commandBuffer)
        {
            GLCall(glBindTexture(GL_TEXTURE_CUBE_MAP, m_Handle));
            GLCall(glGenerateMipmap(GL_TEXTURE_CUBE_MAP));
            glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        }

        GLTextureDepth::GLTextureDepth(uint32_t width, uint32_t height)
            : m_Width(width)
            , m_Height(height)
        {
            GLCall(glGenTextures(1, &m_Handle));

            m_Format = RHIFormat::D32_Float;

            Init();
        }

        GLTextureDepth::~GLTextureDepth()
        {
            GLCall(glDeleteTextures(1, &m_Handle));
        }

        void GLTextureDepth::Bind(uint32_t slot) const
        {
            GLCall(glActiveTexture(GL_TEXTURE0 + slot));
            GLCall(glBindTexture(GL_TEXTURE_2D, m_Handle));
        }

        void GLTextureDepth::Unbind(uint32_t slot) const
        {
            GLCall(glActiveTexture(GL_TEXTURE0 + slot));
            GLCall(glBindTexture(GL_TEXTURE_2D, 0));
        }

        void GLTextureDepth::Init()
        {
            GLCall(glBindTexture(GL_TEXTURE_2D, m_Handle));

            GLCall(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, m_Width, m_Height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL));
#ifndef LUMOS_PLATFORM_MOBILE
            GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE));
#endif
            GLCall(glBindTexture(GL_TEXTURE_2D, 0));

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());
        }

        void GLTextureDepth::Resize(uint32_t width, uint32_t height)
        {
            m_Width  = width;
            m_Height = height;
            m_Format = RHIFormat::D32_Float;

            Init();
        }

        GLTextureDepthArray::GLTextureDepthArray(uint32_t width, uint32_t height, uint32_t count)
            : m_Width(width)
            , m_Height(height)
            , m_Count(count)
        {
            m_Format = RHIFormat::D32_Float;
            GLTextureDepthArray::Init();
        }

        GLTextureDepthArray::~GLTextureDepthArray()
        {
            GLCall(glDeleteTextures(1, &m_Handle));
        }

        void GLTextureDepthArray::Bind(uint32_t slot) const
        {
            GLCall(glActiveTexture(GL_TEXTURE0 + slot));
            GLCall(glBindTexture(GL_TEXTURE_2D_ARRAY, m_Handle));
        }

        void GLTextureDepthArray::Unbind(uint32_t slot) const
        {
            GLCall(glActiveTexture(GL_TEXTURE0 + slot));
            GLCall(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));
        }

        void GLTextureDepthArray::Init()
        {
            GLCall(glGenTextures(1, &m_Handle));
            GLCall(glBindTexture(GL_TEXTURE_2D_ARRAY, m_Handle));

            GLCall(glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, m_Width, m_Height, m_Count, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr));

            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT));
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT));
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
#ifndef LUMOS_PLATFORM_MOBILE
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE));
            // GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY));
#endif
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL));
            GLCall(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());
        }

        void GLTextureDepthArray::Resize(uint32_t width, uint32_t height, uint32_t count)
        {
            m_Width  = width;
            m_Height = height;
            m_Count  = count;

            GLCall(glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, m_Width, m_Height, m_Count, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr));
            GLCall(glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F, m_Width, m_Height, m_Count, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr));

            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT));
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT));
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
#ifndef LUMOS_PLATFORM_MOBILE
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE));
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY));
#endif
            GLCall(glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL));
            GLCall(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));

            m_UUID = Random64::Rand(0, std::numeric_limits<uint64_t>::max());
        }

        Texture2D* GLTexture2D::CreateFuncGL(TextureDesc parameters, uint32_t width, uint32_t height)
        {
            return new GLTexture2D(parameters, width, height);
        }

        Texture2D* GLTexture2D::CreateFromSourceFuncGL(uint32_t width, uint32_t height, void* data, TextureDesc parameters, TextureLoadOptions loadoptions)
        {
            return new GLTexture2D(width, height, data, parameters, loadoptions);
        }

        Texture2D* GLTexture2D::CreateFromFileFuncGL(const std::string& name, const std::string& filename, TextureDesc parameters, TextureLoadOptions loadoptions)
        {
            return new GLTexture2D(name, filename, parameters, loadoptions);
        }

        TextureCube* GLTextureCube::CreateFuncGL(uint32_t size, void* data, bool hdr)
        {
            return new GLTextureCube(size, (uint8_t*)data, hdr);
        }

        TextureCube* GLTextureCube::CreateFromFileFuncGL(const std::string& filepath)
        {
            return new GLTextureCube(filepath);
        }

        TextureCube* GLTextureCube::CreateFromFilesFuncGL(const std::string* files)
        {
            return new GLTextureCube(files);
        }

        TextureCube* GLTextureCube::CreateFromVCrossFuncGL(const std::string* files, uint32_t mips, TextureDesc params, TextureLoadOptions loadOptions)
        {
            return new GLTextureCube(files, mips, params, loadOptions);
        }

        TextureDepth* GLTextureDepth::CreateFuncGL(uint32_t width, uint32_t height)
        {
            return new GLTextureDepth(width, height);
        }

        TextureDepthArray* GLTextureDepthArray::CreateFuncGL(uint32_t width, uint32_t height, uint32_t count)
        {
            return new GLTextureDepthArray(width, height, count);
        }

        void GLTexture2D::MakeDefault()
        {
            CreateFunc           = CreateFuncGL;
            CreateFromFileFunc   = CreateFromFileFuncGL;
            CreateFromSourceFunc = CreateFromSourceFuncGL;
        }

        void GLTextureCube::MakeDefault()
        {
            CreateFunc           = CreateFuncGL;
            CreateFromFileFunc   = CreateFromFileFuncGL;
            CreateFromFilesFunc  = CreateFromFilesFuncGL;
            CreateFromVCrossFunc = CreateFromVCrossFuncGL;
        }

        void GLTextureDepth::MakeDefault()
        {
            CreateFunc = CreateFuncGL;
        }

        void GLTextureDepthArray::MakeDefault()
        {
            CreateFunc = CreateFuncGL;
        }
    }
}
