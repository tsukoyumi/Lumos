#pragma once
#include "Core/Application.h"
#include "Core/OS/Window.h"

namespace Lumos
{
    namespace Graphics
    {
        class Pipeline;
        class CommandBuffer;
        class DescriptorSet;
        class SwapChain;
        class IndexBuffer;
        class Mesh;
        class Texture;
        class GraphicsContext;

        enum RendererBufferType
        {
            RENDERER_BUFFER_NONE = 0,
            RENDERER_BUFFER_COLOUR = BIT(0),
            RENDERER_BUFFER_DEPTH = BIT(1),
            RENDERER_BUFFER_STENCIL = BIT(2)
        };

        enum class DrawType
        {
            POINT,
            TRIANGLE,
            LINES
        };

        enum class StencilType
        {
            EQUAL,
            NOTEQUAL,
            KEEP,
            REPLACE,
            ZERO,
            ALWAYS
        };

        enum class PixelPackType
        {
            PACK,
            UNPACK
        };

        enum class RendererBlendFunction
        {
            NONE,
            ZERO,
            ONE,
            SOURCE_ALPHA,
            DESTINATION_ALPHA,
            ONE_MINUS_SOURCE_ALPHA
        };

        enum class RendererBlendEquation
        {
            NONE,
            ADD,
            SUBTRACT
        };

        enum class RenderMode
        {
            FILL,
            WIREFRAME
        };

        enum class DataType
        {
            FLOAT,
            UNSIGNED_INT,
            UNSIGNED_BYTE
        };

        struct RenderAPICapabilities
        {
            std::string Vendor;
            std::string Renderer;
            std::string Version;

            int MaxSamples = 0;
            float MaxAnisotropy = 0.0f;
            int MaxTextureUnits = 0;
            int UniformBufferOffsetAlignment = 0;
        };

        class LUMOS_EXPORT Renderer
        {
        public:
            Renderer() = default;
            virtual ~Renderer() = default;

            static void Init();
            static void Release();
            void LoadEngineShaders();
            virtual void InitInternal() = 0;
            virtual void Begin() = 0;
            virtual void OnResize(uint32_t width, uint32_t height) = 0;
            virtual void ClearRenderTarget(Graphics::Texture* texture, Graphics::CommandBuffer* commandBuffer, glm::vec4 clearColour = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f)) { }
            inline static Renderer* GetRenderer()
            {
                return s_Instance;
            }

            virtual void PresentInternal() = 0;
            virtual void PresentInternal(Graphics::CommandBuffer* commandBuffer) = 0;
            virtual void BindDescriptorSetsInternal(Graphics::Pipeline* pipeline, Graphics::CommandBuffer* commandBuffer, uint32_t dynamicOffset, Graphics::DescriptorSet** descriptorSets, uint32_t descriptorCount) = 0;

            virtual const std::string& GetTitleInternal() const = 0;
            virtual void DrawIndexedInternal(CommandBuffer* commandBuffer, DrawType type, uint32_t count, uint32_t start) const = 0;
            virtual void DrawInternal(CommandBuffer* commandBuffer, DrawType type, uint32_t count, DataType datayType, void* indices) const = 0;
            virtual void DrawSplashScreen(Texture* texture) {};
            virtual uint32_t GetGPUCount() const { return 1; }

            inline static void Present()
            {
                s_Instance->PresentInternal();
            }
            inline static void Present(Graphics::CommandBuffer* commandBuffer)
            {
                s_Instance->PresentInternal(commandBuffer);
            }
            inline static void BindDescriptorSets(Graphics::Pipeline* pipeline, Graphics::CommandBuffer* commandBuffer, uint32_t dynamicOffset, Graphics::DescriptorSet** descriptorSets, uint32_t descriptorCount)
            {
                s_Instance->BindDescriptorSetsInternal(pipeline, commandBuffer, dynamicOffset, descriptorSets, descriptorCount);
            }
            inline static void Draw(CommandBuffer* commandBuffer, DrawType type, uint32_t count, DataType datayType = DataType::UNSIGNED_INT, void* indices = nullptr)
            {
                s_Instance->DrawInternal(commandBuffer, type, count, datayType, indices);
            }
            inline static void DrawIndexed(CommandBuffer* commandBuffer, DrawType type, uint32_t count, uint32_t start = 0)
            {
                s_Instance->DrawIndexedInternal(commandBuffer, type, count, start);
            }
            inline static const std::string& GetTitle()
            {
                return s_Instance->GetTitleInternal();
            }

            static RenderAPICapabilities& GetCapabilities()
            {
                static RenderAPICapabilities capabilities;
                return capabilities;
            }

            static GraphicsContext* GetGraphicsContext() { return Application::Get().GetWindow()->GetGraphicsContext(); }
            static SwapChain* GetMainSwapChain() { return Application::Get().GetWindow()->GetSwapChain(); }
            static void DrawMesh(CommandBuffer* commandBuffer, Graphics::Pipeline* pipeline, Graphics::Mesh* mesh);
        
        protected:
            static Renderer* (*CreateFunc)();

            static Renderer* s_Instance;
        };
    }
}
