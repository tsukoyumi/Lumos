#pragma once
#include "DescriptorSet.h"
#include "Core/Profiler.h"

namespace spirv_cross
{
    struct SPIRType;
}
namespace Lumos
{
    namespace Graphics
    {
        enum class ShaderType : int
        {
            VERTEX,
            FRAGMENT,
            GEOMETRY,
            TESSELLATION_CONTROL,
            TESSELLATION_EVALUATION,
            COMPUTE,
            UNKNOWN
        };

        struct PushConstant
        {
            uint32_t size;
            ShaderType shaderStage;
            uint8_t* data;
            uint32_t offset = 0;
            std::string name;

            std::vector<BufferMemberInfo> m_Members;

            void SetValue(const std::string& name, void* value)
            {
                LUMOS_PROFILE_FUNCTION();
                for(auto& member : m_Members)
                {
                    if(member.name == name)
                    {
                        memcpy(&data[member.offset], value, member.size);
                        break;
                    }
                }
            }

            void SetData(void* value)
            {
                LUMOS_PROFILE_FUNCTION();
                memcpy(data, value, size);
            }
        };

        class CommandBuffer;
        class Pipeline;
        class DescriptorSet;

        class LUMOS_EXPORT Shader
        {
        public:
            static const Shader* s_CurrentlyBound;

        public:
            virtual void Bind() const = 0;
            virtual void Unbind() const = 0;

            virtual ~Shader() = default;

            virtual const std::vector<ShaderType> GetShaderTypes() const = 0;
            virtual const std::string& GetName() const = 0;
            virtual const std::string& GetFilePath() const = 0;

            virtual void* GetHandle() const = 0;

            virtual std::vector<PushConstant>& GetPushConstants() = 0;
            virtual PushConstant* GetPushConstant(uint32_t index) { return nullptr; }
            virtual void BindPushConstants(Graphics::CommandBuffer* commandBuffer, Graphics::Pipeline* pipeline) = 0;
            virtual DescriptorSetInfo GetDescriptorInfo(uint32_t index) { return DescriptorSetInfo(); }

            ShaderDataType SPIRVTypeToLumosDataType(const spirv_cross::SPIRType type);

        public:
            static Shader* CreateFromFile(const std::string& filepath);
            static Shader* CreateFromEmbeddedArray(const uint32_t* vertData, uint32_t vertDataSize, const uint32_t* fragData, uint32_t fragDataSize); //TODO: support other shader types

        protected:
            static Shader* (*CreateFunc)(const std::string&);
            static Shader* (*CreateFuncFromEmbedded)(const uint32_t*, uint32_t, const uint32_t*, uint32_t);
        };
    }
}
