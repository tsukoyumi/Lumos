#pragma once
#include "Graphics/RHI/GraphicsContext.h"

#if defined(LUMOS_RENDER_API_VULKAN)
#include "Platform/Vulkan/VKDevice.h"
#include "Platform/Vulkan/VKCommandBuffer.h"
#endif

#if defined(LUMOS_PROFILE_GPU) && defined(LUMOS_RENDER_API_VULKAN) && defined(LUMOS_PROFILE) && defined(TRACY_ENABLE)
#define GPUProfile(name) TracyVkZone(Lumos::Graphics::VKDevice::Get().GetTracyContext(), static_cast<Lumos::Graphics::VKCommandBuffer*>(Renderer::GetMainSwapChain()->GetCurrentCommandBuffer())->GetHandle(), name)
#else
#define GPUProfile(name)
#endif
