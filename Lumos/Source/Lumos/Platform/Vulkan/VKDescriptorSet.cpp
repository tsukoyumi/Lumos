#include "Precompiled.h"
#include "VKDescriptorSet.h"
#include "VKPipeline.h"
#include "VKUtilities.h"
#include "VKUniformBuffer.h"
#include "VKTexture.h"
#include "VKDevice.h"
#include "VKRenderer.h"
#include "VKShader.h"

namespace Lumos
{
    namespace Graphics
    {
        uint32_t g_DescriptorSetCount = 0;
        VKDescriptorSet::VKDescriptorSet(const DescriptorDesc& descriptorDesc)
        {
            LUMOS_PROFILE_FUNCTION();
            m_FramesInFlight = uint32_t(VKRenderer::GetMainSwapChain()->GetSwapChainBufferCount());

            VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
            descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAllocateInfo.descriptorPool = VKRenderer::GetDescriptorPool();
            descriptorSetAllocateInfo.pSetLayouts = static_cast<Graphics::VKShader*>(descriptorDesc.shader)->GetDescriptorLayout(descriptorDesc.layoutIndex);
            descriptorSetAllocateInfo.descriptorSetCount = descriptorDesc.count;
            descriptorSetAllocateInfo.pNext = nullptr;

            m_Shader = descriptorDesc.shader;
            m_Descriptors = m_Shader->GetDescriptorInfo(descriptorDesc.layoutIndex);

            for(auto& descriptor : m_Descriptors.descriptors)
            {
                if(descriptor.type == DescriptorType::UNIFORM_BUFFER)
                {
                    for(uint32_t frame = 0; frame < m_FramesInFlight; frame++)
                    {
                        //Uniform Buffer per frame in flight
                        auto buffer = SharedPtr<Graphics::UniformBuffer>(Graphics::UniformBuffer::Create());
                        buffer->Init(descriptor.size, nullptr);
                        m_UniformBuffers[frame][descriptor.name] = buffer;
                    }

                    Buffer localStorage;
                    localStorage.Allocate(descriptor.size);
                    localStorage.InitialiseEmpty();

                    UniformBufferInfo info;
                    info.LocalStorage = localStorage;
                    info.HasUpdated[0] = false;
                    info.HasUpdated[1] = false;
                    info.HasUpdated[2] = false;

                    info.m_Members = descriptor.m_Members;
                    m_UniformBuffersData[descriptor.name] = info;
                }
            }

            for(uint32_t frame = 0; frame < m_FramesInFlight; frame++)
            {
                m_DescriptorDirty[frame] = true;
                m_DescriptorSet[frame] = nullptr;
                g_DescriptorSetCount++;
                VK_CHECK_RESULT(vkAllocateDescriptorSets(VKDevice::GetHandle(), &descriptorSetAllocateInfo, &m_DescriptorSet[frame]));
            }
        }

        VKDescriptorSet::~VKDescriptorSet()
        {
            VKContext::DeletionQueue& deletionQueue = VKRenderer::GetCurrentDeletionQueue();

            for(uint32_t frame = 0; frame < m_FramesInFlight; frame++)
            {
                auto descriptorSet = m_DescriptorSet[frame];
                auto pool = VKRenderer::GetDescriptorPool();
                auto device = VKDevice::GetHandle();

                deletionQueue.PushFunction([descriptorSet, pool, device]
                    { vkFreeDescriptorSets(device, pool, 1, &descriptorSet); });
            }
            g_DescriptorSetCount -= 3;
        }

        void VKDescriptorSet::MakeDefault()
        {
            CreateFunc = CreateFuncVulkan;
        }

        DescriptorSet* VKDescriptorSet::CreateFuncVulkan(const DescriptorDesc& descriptorDesc)
        {
            return new VKDescriptorSet(descriptorDesc);
        }

        void TransitionImageToCorrectLayout(Texture* texture)
        {
            if(!texture)
                return;

            auto commandBuffer = Renderer::GetMainSwapChain()->GetCurrentCommandBuffer();
            if(texture->GetType() == TextureType::COLOUR)
            {
                if(((VKTexture2D*)texture)->GetImageLayout() != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                {
                    ((VKTexture2D*)texture)->TransitionImage(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, (VKCommandBuffer*)commandBuffer);
                }
            }
            else if(texture->GetType() == TextureType::DEPTH)
            {
                ((VKTextureDepth*)texture)->TransitionImage(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, (VKCommandBuffer*)commandBuffer);
            }
            else if(texture->GetType() == TextureType::DEPTHARRAY)
            {
                ((VKTextureDepthArray*)texture)->TransitionImage(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, (VKCommandBuffer*)commandBuffer);
            }
        }

        void VKDescriptorSet::Update()
        {
            LUMOS_PROFILE_FUNCTION();
            m_Dynamic = false;
            int descriptorWritesCount = 0;
            uint32_t currentFrame = Renderer::GetMainSwapChain()->GetCurrentBufferIndex();

            for(auto& bufferInfo : m_UniformBuffersData)
            {
                if(bufferInfo.second.HasUpdated[currentFrame])
                {
                    m_UniformBuffers[currentFrame][bufferInfo.first]->SetData(bufferInfo.second.LocalStorage.Data);
                    bufferInfo.second.HasUpdated[currentFrame] = false;
                }
            }

            if(m_DescriptorDirty[currentFrame])
            {
                m_DescriptorDirty[currentFrame] = false;
                int imageIndex = 0;
                int index = 0;

                for(auto& imageInfo : m_Descriptors.descriptors)
                {
                    if(imageInfo.type == DescriptorType::IMAGE_SAMPLER && (imageInfo.texture || imageInfo.textures))
                    {
                        if(imageInfo.textureCount == 1)
                        {
                            if(imageInfo.texture)
                            {
                                TransitionImageToCorrectLayout(imageInfo.texture);

                                VkDescriptorImageInfo& des = *static_cast<VkDescriptorImageInfo*>(imageInfo.texture->GetDescriptorInfo());
                                m_ImageInfoPool[imageIndex].imageLayout = des.imageLayout;
                                m_ImageInfoPool[imageIndex].imageView = des.imageView;
                                m_ImageInfoPool[imageIndex].sampler = des.sampler;
                            }
                        }
                        else
                        {
                            if(imageInfo.textures)
                            {
                                for(uint32_t i = 0; i < imageInfo.textureCount; i++)
                                {
                                    TransitionImageToCorrectLayout(imageInfo.textures[i]);

                                    VkDescriptorImageInfo& des = *static_cast<VkDescriptorImageInfo*>(imageInfo.textures[i]->GetDescriptorInfo());
                                    m_ImageInfoPool[i + imageIndex].imageLayout = des.imageLayout;
                                    m_ImageInfoPool[i + imageIndex].imageView = des.imageView;
                                    m_ImageInfoPool[i + imageIndex].sampler = des.sampler;
                                }
                            }
                        }

                        VkWriteDescriptorSet writeDescriptorSet {};
                        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        writeDescriptorSet.dstSet = m_DescriptorSet[currentFrame];
                        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        writeDescriptorSet.dstBinding = imageInfo.binding;
                        writeDescriptorSet.pImageInfo = &m_ImageInfoPool[imageIndex];
                        writeDescriptorSet.descriptorCount = imageInfo.textureCount;

                        m_WriteDescriptorSetPool[descriptorWritesCount] = writeDescriptorSet;
                        imageIndex++;
                        descriptorWritesCount++;
                    }
                    else if(imageInfo.type == DescriptorType::UNIFORM_BUFFER)
                    {
                        {
                            VKUniformBuffer* vkUniformBuffer = m_UniformBuffers[currentFrame][imageInfo.name].As<VKUniformBuffer>().get();
                            m_BufferInfoPool[index].buffer = *vkUniformBuffer->GetBuffer();
                            m_BufferInfoPool[index].offset = imageInfo.offset;
                            m_BufferInfoPool[index].range = imageInfo.size;

                            VkWriteDescriptorSet writeDescriptorSet {};
                            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                            writeDescriptorSet.dstSet = m_DescriptorSet[currentFrame];
                            writeDescriptorSet.descriptorType = VKUtilities::DescriptorTypeToVK(imageInfo.type);
                            writeDescriptorSet.dstBinding = imageInfo.binding;
                            writeDescriptorSet.pBufferInfo = &m_BufferInfoPool[index];
                            writeDescriptorSet.descriptorCount = 1;

                            m_WriteDescriptorSetPool[descriptorWritesCount] = writeDescriptorSet;
                            index++;
                            descriptorWritesCount++;

                            if(imageInfo.type == DescriptorType::UNIFORM_BUFFER_DYNAMIC)
                                m_Dynamic = true;
                        }
                    }
                }

                vkUpdateDescriptorSets(VKDevice::Get().GetDevice(), descriptorWritesCount,
                    m_WriteDescriptorSetPool.data(), 0, nullptr);
            }
        }

        void VKDescriptorSet::SetTexture(const std::string& name, Texture* texture, TextureType textureType)
        {
            LUMOS_PROFILE_FUNCTION();

            for(auto& descriptor : m_Descriptors.descriptors)
            {
                if(descriptor.type == DescriptorType::IMAGE_SAMPLER && descriptor.name == name)
                {
                    descriptor.texture = texture;
                    descriptor.textureType = textureType;
                    descriptor.textureCount = texture ? 1 : 0;
                    m_DescriptorDirty[0] = true;
                    m_DescriptorDirty[1] = true;
                    m_DescriptorDirty[2] = true;
                }
            }
        }

        void VKDescriptorSet::SetTexture(const std::string& name, Texture** texture, uint32_t textureCount, TextureType textureType)
        {
            LUMOS_PROFILE_FUNCTION();

            for(auto& descriptor : m_Descriptors.descriptors)
            {
                if(descriptor.type == DescriptorType::IMAGE_SAMPLER && descriptor.name == name)
                {
                    descriptor.textureCount = textureCount;
                    descriptor.textures = texture;
                    descriptor.textureType = textureType;

                    m_DescriptorDirty[0] = true;
                    m_DescriptorDirty[1] = true;
                    m_DescriptorDirty[2] = true;
                }
            }
        }

        void VKDescriptorSet::SetBuffer(const std::string& name, UniformBuffer* buffer)
        {
            LUMOS_PROFILE_FUNCTION();
            uint32_t currentFrame = Renderer::GetMainSwapChain()->GetCurrentBufferIndex();
#if 0
            for(auto& descriptor : m_Descriptors[currentFrame].descriptors)
            {
                if(descriptor.type == DescriptorType::UNIFORM_BUFFER && descriptor.name == name)
                {
                    descriptor.buffer = buffer;
                }
            }
#endif
        }

        Graphics::UniformBuffer* VKDescriptorSet::GetUnifromBuffer(const std::string& name)
        {
            LUMOS_PROFILE_FUNCTION();
            uint32_t currentFrame = Renderer::GetMainSwapChain()->GetCurrentBufferIndex();

            //for(auto& buffers : m_UniformBuffers[currentFrame])
            //{
            //if(descriptor.type == DescriptorType::UNIFORM_BUFFER && descriptor.name == name)
            //{
            //return descriptor.buffer;
            //}
            //}

            LUMOS_LOG_WARN("Buffer not found {0}", name);
            return nullptr;
        }

        void VKDescriptorSet::SetUniform(const std::string& bufferName, const std::string& uniformName, void* data)
        {
            LUMOS_PROFILE_FUNCTION();
            std::map<std::string, UniformBufferInfo>::iterator itr = m_UniformBuffersData.find(bufferName);
            if(itr != m_UniformBuffersData.end())
            {
                for(auto& member : itr->second.m_Members)
                {
                    if(member.name == uniformName)
                    {
                        itr->second.LocalStorage.Write(data, member.size, member.offset);

                        itr->second.HasUpdated[0] = true;
                        itr->second.HasUpdated[1] = true;
                        itr->second.HasUpdated[2] = true;
                        return;
                    }
                }
            }

            LUMOS_LOG_WARN("Uniform not found {0}.{1}", bufferName, uniformName);
        }

        void VKDescriptorSet::SetUniform(const std::string& bufferName, const std::string& uniformName, void* data, uint32_t size)
        {
            LUMOS_PROFILE_FUNCTION();

            std::map<std::string, UniformBufferInfo>::iterator itr = m_UniformBuffersData.find(bufferName);
            if(itr != m_UniformBuffersData.end())
            {
                for(auto& member : itr->second.m_Members)
                {
                    if(member.name == uniformName)
                    {
                        itr->second.LocalStorage.Write(data, size, member.offset);
                        itr->second.HasUpdated[0] = true;
                        itr->second.HasUpdated[1] = true;
                        itr->second.HasUpdated[2] = true;
                        return;
                    }
                }
            }

            LUMOS_LOG_WARN("Uniform not found {0}.{1}", bufferName, uniformName);
        }

        void VKDescriptorSet::SetUniformBufferData(const std::string& bufferName, void* data)
        {
            LUMOS_PROFILE_FUNCTION();

            std::map<std::string, UniformBufferInfo>::iterator itr = m_UniformBuffersData.find(bufferName);
            if(itr != m_UniformBuffersData.end())
            {
                itr->second.LocalStorage.Write(data, itr->second.LocalStorage.GetSize(), 0);
                itr->second.HasUpdated[0] = true;
                itr->second.HasUpdated[1] = true;
                itr->second.HasUpdated[2] = true;
                return;
            }

            LUMOS_LOG_WARN("Uniform not found {0}.{1}", bufferName);
        }
    }
}
