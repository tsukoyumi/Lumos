#include "Precompiled.h"
#include "VKFramebuffer.h"
#include "VKDevice.h"
#include "VKTexture.h"
#include "VKInitialisers.h"
#include "VKUtilities.h"
#include "VKRenderer.h"

namespace Lumos
{
    namespace Graphics
    {
        VKFramebuffer::VKFramebuffer(const FramebufferDesc& frameBufferInfo)
        {
            m_Width = frameBufferInfo.width;
            m_Height = frameBufferInfo.height;

            m_AttachmentCount = frameBufferInfo.attachmentCount;

            std::vector<VkImageView> attachments;

            for(uint32_t i = 0; i < m_AttachmentCount; i++)
            {
                switch(frameBufferInfo.attachmentTypes[i])
                {
                case TextureType::COLOUR:
                    attachments.push_back(static_cast<VKTexture2D*>(frameBufferInfo.attachments[i])->GetImageView());
                    break;
                case TextureType::DEPTH:
                    attachments.push_back(static_cast<VKTextureDepth*>(frameBufferInfo.attachments[i])->GetImageView());
                    break;
                case TextureType::DEPTHARRAY:
                    attachments.push_back(static_cast<VKTextureDepthArray*>(frameBufferInfo.attachments[i])->GetImageView(frameBufferInfo.layer));
                    break;
                case TextureType::OTHER:
                    attachments.push_back(static_cast<VKTexture2D*>(frameBufferInfo.attachments[i])->GetImageView());
                    break;
                case TextureType::CUBE:
                    UNIMPLEMENTED;
                    break;
                }
            }

            VkFramebufferCreateInfo framebufferCreateInfo = VKInitialisers::framebufferCreateInfo();

            framebufferCreateInfo.renderPass = static_cast<VKRenderPass*>(frameBufferInfo.renderPass)->GetHandle();
            framebufferCreateInfo.attachmentCount = m_AttachmentCount;
            framebufferCreateInfo.pAttachments = attachments.data();
            framebufferCreateInfo.width = m_Width;
            framebufferCreateInfo.height = m_Height;
            framebufferCreateInfo.layers = 1;
            framebufferCreateInfo.pNext = nullptr;
            framebufferCreateInfo.flags = 0;

            VK_CHECK_RESULT(vkCreateFramebuffer(VKDevice::Get().GetDevice(), &framebufferCreateInfo, VK_NULL_HANDLE, &m_Framebuffer));
        }

        VKFramebuffer::~VKFramebuffer()
        {
            VKContext::DeletionQueue& deletionQueue = VKRenderer::GetCurrentDeletionQueue();

            auto framebuffer = m_Framebuffer;

            deletionQueue.PushFunction([framebuffer]
                { vkDestroyFramebuffer(VKDevice::Get().GetDevice(), framebuffer, VK_NULL_HANDLE); });
        }

        void VKFramebuffer::MakeDefault()
        {
            CreateFunc = CreateFuncVulkan;
        }

        Framebuffer* VKFramebuffer::CreateFuncVulkan(const FramebufferDesc& info)
        {
            return new VKFramebuffer(info);
        }
    }
}
