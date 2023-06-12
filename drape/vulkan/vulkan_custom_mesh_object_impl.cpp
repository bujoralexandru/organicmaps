#include "drape/custom_mesh_object.hpp"
#include "drape/pointers.hpp"
#include "drape/vulkan/vulkan_base_context.hpp"
#include "drape/vulkan/vulkan_staging_buffer.hpp"
#include "drape/vulkan/vulkan_param_descriptor.hpp"
#include "drape/vulkan/vulkan_utils.hpp"

#include "base/assert.hpp"
#include <android/log.h>

#include <cstdint>
#include <vector>
#include <3party/stb_image/stb_image.h>

namespace dp {
    namespace vulkan {
        namespace {
            VkPrimitiveTopology GetPrimitiveType(CustomMeshObject::DrawPrimitive primitive) {
                switch (primitive) {
                    case CustomMeshObject::DrawPrimitive::Triangles:
                        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    case CustomMeshObject::DrawPrimitive::TriangleStrip:
                        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                    case CustomMeshObject::DrawPrimitive::LineStrip:
                        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                }
                UNREACHABLE();
            }
        }  // namespace

        class CustomVulkanMeshObjectImpl : public CustomMeshObjectImpl {
        public:
            CustomVulkanMeshObjectImpl(ref_ptr<VulkanObjectManager> objectManager,
                                       ref_ptr<dp::CustomMeshObject> mesh)
                    : m_mesh(std::move(mesh)), m_objectManager(objectManager),
                      m_descriptorUpdater(objectManager) {}

            void
            Build(ref_ptr<dp::GraphicsContext> context, ref_ptr<dp::GpuProgram> program) override {
                m_geometryBuffers.resize(m_mesh->m_buffers.size());
                m_textureNames.resize(m_mesh->m_textures.size());
                m_bindingInfoCount = static_cast<uint8_t>(m_mesh->m_buffers.size());
                CHECK_LESS_OR_EQUAL(m_bindingInfoCount, kMaxBindingInfo, ());
                for (size_t i = 0; i < m_mesh->m_buffers.size(); i++) {
                    if (m_mesh->m_buffers[i].m_data.empty())
                        continue;

                    auto const sizeInBytes = static_cast<uint32_t>(
                            m_mesh->m_buffers[i].m_data.size() *
                            sizeof(m_mesh->m_buffers[i].m_data[0]));
                    m_geometryBuffers[i] = m_objectManager->CreateBuffer(
                            VulkanMemoryManager::ResourceType::Geometry,
                            sizeInBytes, 0 /* batcherHash */);
                    m_objectManager->Fill(m_geometryBuffers[i], m_mesh->m_buffers[i].m_data.data(),
                                          sizeInBytes);

                    m_bindingInfo[i] = dp::BindingInfo(
                            static_cast<uint8_t>(m_mesh->m_buffers[i].m_attributes.size()),
                            static_cast<uint8_t>(i));
                    for (size_t j = 0; j < m_mesh->m_buffers[i].m_attributes.size(); ++j) {
                        auto const &attr = m_mesh->m_buffers[i].m_attributes[j];
                        auto &binding = m_bindingInfo[i].GetBindingDecl(static_cast<uint16_t>(j));
                        binding.m_attributeName = attr.m_attributeName;
                        binding.m_componentCount = static_cast<uint8_t>(attr.m_componentsCount);
                        binding.m_componentType = gl_const::GLFloatType;
                        binding.m_offset = static_cast<uint8_t>(attr.m_offset);
                        binding.m_stride = static_cast<uint8_t>(m_mesh->m_buffers[i].m_stride);
                    }
                }

                // New code starts here:
                for (size_t i = 0; i < m_mesh->m_textures.size(); i++) {
                    if (m_mesh->m_textures[i] == "")
                        continue;

                    int texWidth, texHeight, texChannels;
                    stbi_uc *pixels = stbi_load(m_mesh->m_textures[i].c_str(), &texWidth,
                                                &texHeight, &texChannels, STBI_rgb_alpha);
                    VkDeviceSize imageSize = texWidth * texHeight * 4;

                    if (!pixels) {
                        throw std::runtime_error("failed to load texture image!");
                    }

                    __android_log_print(ANDROID_LOG_INFO, "Car3d", "Image size: %lu", imageSize);


                    ref_ptr<dp::vulkan::VulkanBaseContext> vulkanContext = context;
                    VkCommandBuffer commandBuffer = vulkanContext->GetCurrentMemoryCommandBuffer();
                    CHECK(commandBuffer != nullptr, ());

                    std::vector<ParamDescriptor> paramDescriptors = vulkanContext->GetCurrentParamDescriptors();
                    ParamDescriptor textureDescriptor;

                    for(size_t i = 0; i < paramDescriptors.size(); i++) {
                        if(paramDescriptors[i].m_type == dp::vulkan::ParamDescriptor::Type::Texture) {
                            textureDescriptor = paramDescriptors[i];
                            break;
                        }
                    }

                    if(textureDescriptor.m_type != dp::vulkan::ParamDescriptor::Type::Texture) {
                        throw std::runtime_error("failed to find texture descriptor!");
                    }

                    auto image = textureDescriptor.image;


                    VkImageMemoryBarrier preTransferBarrier = {};
                    preTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    preTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    preTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    preTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    preTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    preTransferBarrier.image = image;
                    preTransferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    preTransferBarrier.subresourceRange.baseMipLevel = 0;
                    preTransferBarrier.subresourceRange.levelCount = 1;
                    preTransferBarrier.subresourceRange.baseArrayLayer = 0;
                    preTransferBarrier.subresourceRange.layerCount = 1;
                    vkCmdPipelineBarrier(commandBuffer,
                                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         0,
                                         0, nullptr,
                                         0, nullptr,
                                         1, &preTransferBarrier);

                    // Here we use temporary staging object, which will be destroyed after the nearest
                    // command queue submitting.
                    VulkanStagingBuffer tempStagingBuffer(m_objectManager, imageSize);
                    CHECK(tempStagingBuffer.HasEnoughSpace(imageSize), ());
                    auto staging = tempStagingBuffer.Reserve(imageSize);
                    memcpy(staging.m_pointer, pixels, imageSize);
                    tempStagingBuffer.Flush();

                    // Schedule command to copy from the staging buffer to our image.
                    VkBufferImageCopy copyRegion = {};
                    copyRegion.bufferOffset = staging.m_offset;
                    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copyRegion.imageSubresource.mipLevel = 0;
                    copyRegion.imageSubresource.baseArrayLayer = 0;
                    copyRegion.imageSubresource.layerCount = 1;
                    copyRegion.imageOffset = {0, 0, 0};
                    copyRegion.imageExtent = {static_cast<uint32_t>(texWidth),
                                              static_cast<uint32_t>(texHeight), 1};
                    vkCmdCopyBufferToImage(commandBuffer, staging.m_stagingBuffer, image,
                                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

                    VkImageMemoryBarrier postTransferBarrier = {};
                    postTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    postTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    postTransferBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    postTransferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    postTransferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    postTransferBarrier.image = image;
                    postTransferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    postTransferBarrier.subresourceRange.baseMipLevel = 0;
                    postTransferBarrier.subresourceRange.levelCount = 1;
                    postTransferBarrier.subresourceRange.baseArrayLayer = 0;
                    postTransferBarrier.subresourceRange.layerCount = 1;
                    vkCmdPipelineBarrier(commandBuffer,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                         0,
                                         0, nullptr,
                                         0, nullptr,
                                         1, &postTransferBarrier);

                    stbi_image_free(pixels);
                }
            }

            void Reset() override {
                m_descriptorUpdater.Destroy();
                for (auto const &b: m_geometryBuffers)
                    m_objectManager->DestroyObject(b);
                m_geometryBuffers.clear();
            }

            void UpdateBuffer(ref_ptr<dp::GraphicsContext> context, uint32_t bufferInd) override {
                CHECK_LESS(bufferInd, static_cast<uint32_t>(m_geometryBuffers.size()), ());

                ref_ptr<dp::vulkan::VulkanBaseContext> vulkanContext = context;
                VkCommandBuffer commandBuffer = vulkanContext->GetCurrentMemoryCommandBuffer();
                CHECK(commandBuffer != nullptr, ());

                auto &buffer = m_mesh->m_buffers[bufferInd];
                CHECK(!buffer.m_data.empty(), ());

                // Copy to default or temporary staging buffer.
                auto const sizeInBytes = static_cast<uint32_t>(buffer.m_data.size() *
                                                               sizeof(buffer.m_data[0]));
                auto stagingBuffer = vulkanContext->GetDefaultStagingBuffer();
                if (stagingBuffer->HasEnoughSpace(sizeInBytes)) {
                    auto staging = stagingBuffer->Reserve(sizeInBytes);
                    memcpy(staging.m_pointer, buffer.m_data.data(), sizeInBytes);

                    // Schedule command to copy from the staging buffer to our geometry buffer.
                    VkBufferCopy copyRegion = {};
                    copyRegion.dstOffset = 0;
                    copyRegion.srcOffset = staging.m_offset;
                    copyRegion.size = sizeInBytes;
                    vkCmdCopyBuffer(commandBuffer, staging.m_stagingBuffer,
                                    m_geometryBuffers[bufferInd].m_buffer,
                                    1, &copyRegion);
                } else {
                    // Here we use temporary staging object, which will be destroyed after the nearest
                    // command queue submitting.
                    VulkanStagingBuffer tempStagingBuffer(m_objectManager, sizeInBytes);
                    CHECK(tempStagingBuffer.HasEnoughSpace(sizeInBytes), ());
                    auto staging = tempStagingBuffer.Reserve(sizeInBytes);
                    memcpy(staging.m_pointer, buffer.m_data.data(), sizeInBytes);
                    tempStagingBuffer.Flush();

                    // Schedule command to copy from the staging buffer to our geometry buffer.
                    VkBufferCopy copyRegion = {};
                    copyRegion.dstOffset = 0;
                    copyRegion.srcOffset = staging.m_offset;
                    copyRegion.size = sizeInBytes;
                    vkCmdCopyBuffer(commandBuffer, staging.m_stagingBuffer,
                                    m_geometryBuffers[bufferInd].m_buffer,
                                    1, &copyRegion);
                }

                // Set up a barrier to prevent data collisions.
                VkBufferMemoryBarrier barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.pNext = nullptr;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = m_geometryBuffers[bufferInd].m_buffer;
                barrier.offset = 0;
                barrier.size = sizeInBytes;
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr,
                                     1, &barrier, 0, nullptr);
            }

            void
            DrawPrimitives(ref_ptr<dp::GraphicsContext> context, uint32_t verticesCount) override {
                ref_ptr<dp::vulkan::VulkanBaseContext> vulkanContext = context;
                VkCommandBuffer commandBuffer = vulkanContext->GetCurrentRenderingCommandBuffer();
                CHECK(commandBuffer != nullptr, ());

                vulkanContext->SetPrimitiveTopology(GetPrimitiveType(m_mesh->m_drawPrimitive));
                vulkanContext->SetBindingInfo(m_bindingInfo, m_bindingInfoCount);

                m_descriptorUpdater.Update(context);
                auto descriptorSet = m_descriptorUpdater.GetDescriptorSet();

                uint32_t dynamicOffset = vulkanContext->GetCurrentDynamicBufferOffset();
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        vulkanContext->GetCurrentPipelineLayout(), 0, 1,
                                        &descriptorSet, 1, &dynamicOffset);

                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  vulkanContext->GetCurrentPipeline());

                VkDeviceSize offsets[1] = {0};
                for (uint32_t i = 0; i < static_cast<uint32_t>(m_geometryBuffers.size()); ++i)
                    vkCmdBindVertexBuffers(commandBuffer, i, 1, &m_geometryBuffers[i].m_buffer,
                                           offsets);

                vkCmdDraw(commandBuffer, verticesCount, 1, 0, 0);
            }

            void Bind(ref_ptr<dp::GpuProgram> program) override {}

            void Unbind() override {}

        private:
            ref_ptr<dp::CustomMeshObject> m_mesh;
            ref_ptr<VulkanObjectManager> m_objectManager;
            std::vector<VulkanObject> m_geometryBuffers;
            std::vector<std::string> m_textureNames;
            BindingInfoArray m_bindingInfo;
            uint8_t m_bindingInfoCount = 0;
            ParamDescriptorUpdater m_descriptorUpdater;
        };
    }  // namespace vulkan

    void CustomMeshObject::InitForVulkan(ref_ptr<dp::GraphicsContext> context) {
        ref_ptr<dp::vulkan::VulkanBaseContext> vulkanContext = context;
        m_impl = make_unique_dp<vulkan::CustomVulkanMeshObjectImpl>(
                vulkanContext->GetObjectManager(),
                make_ref(this));
    }
}  // namespace dp
