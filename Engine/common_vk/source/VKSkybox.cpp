//Author: JEYOON YU
//Project: CubeEngine
//File: VKSkybox.cpp
#include "VKTexture.hpp"
#include "VKSkybox.hpp"
#include "VKInit.hpp"
#include "VKDescriptor.hpp"
#include "VKPipeLine.hpp"
#include "VKShader.hpp"
#include "VKVertexBuffer.hpp"
#include "VKUniformBuffer.hpp"
#include "VKHelper.hpp"
#include <iostream>
#include <filesystem>

VKSkybox::VKSkybox(const std::filesystem::path& path, VKInit* init_, VkCommandPool* pool_) : vkInit(init_), vkCommandPool(pool_)
{
	skyboxTexture = new VKTexture(vkInit, vkCommandPool);
	skyboxTexture->LoadTexture(true, path, "skybox", true);
	faceSize = skyboxTexture->GetHeight();

	//Create command buffer info
	VkCommandBufferAllocateInfo allocateInfo{};
	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.commandPool = *vkCommandPool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;

	//Create command buffer
	VKHelper::ThrowIfFailed(vkAllocateCommandBuffers(*vkInit->GetDevice(), &allocateInfo, &skyboxCommandBuffer));

	//projection[1][1] *= -1.0f;
	EquirectangularToCube(&skyboxCommandBuffer);
	CalculateIrradiance(&skyboxCommandBuffer);
	PrefilteredEnvironmentMap(&skyboxCommandBuffer);
	BRDFLUT(&skyboxCommandBuffer);
}

VKSkybox::~VKSkybox()
{
	//Destroy Sampler
	vkDestroySampler(*vkInit->GetDevice(), vkTextureSamplerCubemap, nullptr);
	vkDestroySampler(*vkInit->GetDevice(), vkTextureSamplerIrradiance, nullptr);
	vkDestroySampler(*vkInit->GetDevice(), vkTextureSamplerPrefilter, nullptr);
	vkDestroySampler(*vkInit->GetDevice(), vkTextureSamplerBRDFLUT, nullptr);
	//Destroy ImageView
	vkDestroyImageView(*vkInit->GetDevice(), vkTextureImageViewCubemap, nullptr);
	vkDestroyImageView(*vkInit->GetDevice(), vkTextureImageViewIrradiance, nullptr);
	vkDestroyImageView(*vkInit->GetDevice(), vkTextureImageViewPrefilter, nullptr);
	vkDestroyImageView(*vkInit->GetDevice(), vkTextureImageViewBRDFLUT, nullptr);
	//Free Memory
	vkFreeMemory(*vkInit->GetDevice(), vkTextureDeviceMemoryCubemap, nullptr);
	vkFreeMemory(*vkInit->GetDevice(), vkTextureDeviceMemoryIrradiance, nullptr);
	vkFreeMemory(*vkInit->GetDevice(), vkTextureDeviceMemoryPrefilter, nullptr);
	vkFreeMemory(*vkInit->GetDevice(), vkTextureDeviceMemoryBRDFLUT, nullptr);
	//Destroy Image
	vkDestroyImage(*vkInit->GetDevice(), vkTextureImageCubemap, nullptr);
	vkDestroyImage(*vkInit->GetDevice(), vkTextureImageIrradiance, nullptr);
	vkDestroyImage(*vkInit->GetDevice(), vkTextureImagePrefilter, nullptr);
	vkDestroyImage(*vkInit->GetDevice(), vkTextureImageBRDFLUT, nullptr);

	//for (auto& view : cubeFaceViews)
	//{
	//	vkDestroyImageView(*vkInit->GetDevice(), view, nullptr);
	//}

	//vkDestroyRenderPass(*vkInit->GetDevice(), renderPassIBL, nullptr);

	//for (auto& fb : cubeFaceFramebuffers)
	//{
	//	vkDestroyFramebuffer(*vkInit->GetDevice(), fb, nullptr);
	//}

	delete skyboxTexture;

	vkFreeCommandBuffers(*vkInit->GetDevice(), *vkCommandPool, 1, &skyboxCommandBuffer);
}

void VKSkybox::EquirectangularToCube(VkCommandBuffer* commandBuffer)
{
	{
		//Define an image to create
		VkImageCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		createInfo.imageType = VK_IMAGE_TYPE_2D;
		createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		createInfo.extent = { faceSize, faceSize, 1 };
		createInfo.mipLevels = 1;
		createInfo.arrayLayers = 6; //6 Layers for CubeMap
		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		//Use Optimal Tiling to make GPU effectively process image
		createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		//Usage for copying, shader, and renderpass
		createInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		//Create image
		VKHelper::ThrowIfFailed(vkCreateImage(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureImageCubemap));
	}

	//Declare a variable which will take memory requirements
	VkMemoryRequirements requirements{};
	//Get Memory Requirements for Image
	vkGetImageMemoryRequirements(*vkInit->GetDevice(), vkTextureImageCubemap, &requirements);

	//Create Memory Allocation Info
	VkMemoryAllocateInfo allocateInfo{};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	//Select memory type which has fast access from GPU
	allocateInfo.memoryTypeIndex = VKHelper::FindMemoryTypeIndex(*vkInit->GetPhysicalDevice(), requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//Allocate Memory
	VKHelper::ThrowIfFailed(vkAllocateMemory(*vkInit->GetDevice(), &allocateInfo, nullptr, &vkTextureDeviceMemoryCubemap));

	//Bind Image and Memory
	VKHelper::ThrowIfFailed(vkBindImageMemory(*vkInit->GetDevice(), vkTextureImageCubemap, vkTextureDeviceMemoryCubemap, 0));

	{
		//To access image from graphics pipeline, Image View is needed
		//Create ImageView Info
		VkImageViewCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = vkTextureImageCubemap;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.layerCount = 6;
		createInfo.subresourceRange.baseArrayLayer = 0;

		//Create ImageView
		VKHelper::ThrowIfFailed(vkCreateImageView(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureImageViewCubemap));
	}

	{
		//Sampler is needed for shader to read image
		//Create Sampler Info
		VkSamplerCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		createInfo.magFilter = VK_FILTER_LINEAR;
		createInfo.minFilter = VK_FILTER_LINEAR;
		createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		//createInfo.anisotropyEnable = VK_TRUE;
		//createInfo.maxAnisotropy = 16.f;
		//createInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		createInfo.unnormalizedCoordinates = VK_FALSE;

		//Create Sampler
		VKHelper::ThrowIfFailed(vkCreateSampler(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureSamplerCubemap));
	}

	//Prepare RenderPass
	//Create Attachment Description
	VkAttachmentDescription colorAattachmentDescription{};
	colorAattachmentDescription.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	colorAattachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAattachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAattachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAattachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAattachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//Define which attachment should subpass refernece of renderpass
	VkAttachmentReference colorAttachmentReference{};
	//attachment == Index of VkAttachmentDescription array
	colorAttachmentReference.attachment = 0;
	colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	//Create Subpass Description
	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorAttachmentReference;

	VkRenderPassCreateInfo rpCreateInfo{};
	rpCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpCreateInfo.attachmentCount = 1;
	rpCreateInfo.pAttachments = &colorAattachmentDescription;
	rpCreateInfo.subpassCount = 1;
	rpCreateInfo.pSubpasses = &subpassDescription;

	//Create Renderpass
	VKHelper::ThrowIfFailed(vkCreateRenderPass(*vkInit->GetDevice(), &rpCreateInfo, nullptr, &renderPassIBL));

	for (uint32_t f = 0; f < 6; ++f)
	{
		//Create ImageView
		//To access image from graphics pipeline, Image View is needed
		//Create ImageView Info
		VkImageViewCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = vkTextureImageCubemap;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.layerCount = 1;
		createInfo.subresourceRange.baseArrayLayer = f;

		VKHelper::ThrowIfFailed(vkCreateImageView(*vkInit->GetDevice(), &createInfo, nullptr, &cubeFaceViews[f]));
	}

	for (uint32_t f = 0; f < 6; ++f)
	{
		VkImageView attachments[] = { cubeFaceViews[f] };

		//Create framebuffer info
		VkFramebufferCreateInfo fbCreateInfo{};
		fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbCreateInfo.renderPass = renderPassIBL;
		fbCreateInfo.attachmentCount = 1;
		fbCreateInfo.pAttachments = attachments;
		fbCreateInfo.width = faceSize;
		fbCreateInfo.height = faceSize;
		fbCreateInfo.layers = 1;

		//Create framebuffer
		VKHelper::ThrowIfFailed(vkCreateFramebuffer(*vkInit->GetDevice(), &fbCreateInfo, nullptr, &cubeFaceFramebuffers[f]));
	}

	//Render Images to Cube
	VKShader shaderIBL{ vkInit->GetDevice() };
	shaderIBL.LoadShader("../Engine/shaders/spirv/Cubemap.vert.spv", "../Engine/shaders/spirv/Equirectangular.frag.spv");

	VKDescriptorLayout fragmentLayout;
	fragmentLayout.descriptorType = VKDescriptorLayout::SAMPLER;
	fragmentLayout.descriptorCount = 1;
	VKDescriptor descriptorIBL{ vkInit, {}, { fragmentLayout } };

	VKAttributeLayout position_layout;
	position_layout.vertex_layout_location = 0;
	position_layout.format = VK_FORMAT_R32G32B32_SFLOAT;
	position_layout.offset = 0;

	VKPipeLine pipelineIBL{ vkInit->GetDevice(), descriptorIBL.GetDescriptorSetLayout() };
	VkExtent2D extentIBL{ faceSize, faceSize };
	pipelineIBL.InitPipeLine(shaderIBL.GetVertexModule(), shaderIBL.GetFragmentModule(), &extentIBL, &renderPassIBL, sizeof(float) * 3, { position_layout }, VK_SAMPLE_COUNT_1_BIT, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE, POLYGON_MODE::FILL, true, sizeof(glm::mat4) * 2, VK_SHADER_STAGE_VERTEX_BIT);

	VKVertexBuffer vertexBufferIBL{ vkInit, sizeof(glm::vec3) * skyboxVertices.size(), skyboxVertices.data()};

	VkWriteDescriptorSet descriptorWrite{};
	VkDescriptorImageInfo skyboxDescriptorImageInfo{};
	skyboxDescriptorImageInfo.sampler = *skyboxTexture->GetSampler();
	skyboxDescriptorImageInfo.imageView = *skyboxTexture->GetImageView();
	skyboxDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//Define which resource descriptor set will point
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = (*descriptorIBL.GetFragmentDescriptorSets())[0];
	descriptorWrite.dstBinding = 0;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.pImageInfo = &skyboxDescriptorImageInfo;

	//Update DescriptorSet
	//DescriptorSet does not have to update every frame since it points same uniform buffer
	vkUpdateDescriptorSets(*vkInit->GetDevice(), 1, &descriptorWrite, 0, nullptr);

	//Create Viewport and Scissor for Dynamic State
	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = static_cast<float>(faceSize);
	viewport.height = static_cast<float>(faceSize);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = { faceSize, faceSize };

	//Create command buffer begin info
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	//Begin command buffer
	VKHelper::ThrowIfFailed(vkBeginCommandBuffer(*commandBuffer, &beginInfo));

	//Create renderpass begin info
	VkRenderPassBeginInfo renderpassBeginInfo{};
	renderpassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderpassBeginInfo.renderPass = renderPassIBL;
	renderpassBeginInfo.renderArea.offset = { 0, 0 };
	renderpassBeginInfo.renderArea.extent = { faceSize, faceSize };
	renderpassBeginInfo.clearValueCount = 1;
	renderpassBeginInfo.pClearValues = &clearColor;

	for (int f = 0; f < 6; ++f)
	{
		renderpassBeginInfo.framebuffer = cubeFaceFramebuffers[f];

		vkCmdBeginRenderPass(*commandBuffer, &renderpassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		//Bind Vertex Buffer
		VkDeviceSize vertexBufferOffset{ 0 };
		vkCmdBindVertexBuffers(*commandBuffer, 0, 1, vertexBufferIBL.GetVertexBuffer(), &vertexBufferOffset);
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineIBL.GetPipeLine());
		//Dynamic Viewport & Scissor
		vkCmdSetViewport(*commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(*commandBuffer, 0, 1, &scissor);
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineIBL.GetPipeLineLayout(), 0, 1, &(*descriptorIBL.GetFragmentDescriptorSets())[0], 0, nullptr);
		//Push Constant World-To_NDC
		glm::mat4 transform[2] = { views[f], projection };
		vkCmdPushConstants(*commandBuffer, *pipelineIBL.GetPipeLineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) * 2, &transform[0]);

		vkCmdDraw(*commandBuffer, 36, 1, 0, 0);

		vkCmdEndRenderPass(*commandBuffer);
	}

	VKHelper::ThrowIfFailed(vkEndCommandBuffer(*commandBuffer));

	//Create submit info
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = commandBuffer;

	//Submit queue to command buffer
	VKHelper::ThrowIfFailed(vkQueueSubmit(*vkInit->GetQueue(), 1, &submitInfo, VK_NULL_HANDLE));

	//Wait until all submitted command buffers are handled
	VKHelper::ThrowIfFailed(vkDeviceWaitIdle(*vkInit->GetDevice()));

	//Deallocate Resources
	for (auto& view : cubeFaceViews)
	{
		vkDestroyImageView(*vkInit->GetDevice(), view, nullptr);
	}

	vkDestroyRenderPass(*vkInit->GetDevice(), renderPassIBL, nullptr);

	for (auto& fb : cubeFaceFramebuffers)
	{
		vkDestroyFramebuffer(*vkInit->GetDevice(), fb, nullptr);
	}
}

void VKSkybox::CalculateIrradiance(VkCommandBuffer* commandBuffer)
{
	{
		//Define an image to create
		VkImageCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		createInfo.imageType = VK_IMAGE_TYPE_2D;
		createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		createInfo.extent = { irradianceSize, irradianceSize, 1 };
		createInfo.mipLevels = 1;
		createInfo.arrayLayers = 6; //6 Layers for CubeMap
		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		//Use Optimal Tiling to make GPU effectively process image
		createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		//Usage for copying, shader, and renderpass
		createInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		//Create image
		VKHelper::ThrowIfFailed(vkCreateImage(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureImageIrradiance));
	}

	//Declare a variable which will take memory requirements
	VkMemoryRequirements requirements{};
	//Get Memory Requirements for Image
	vkGetImageMemoryRequirements(*vkInit->GetDevice(), vkTextureImageIrradiance, &requirements);

	//Create Memory Allocation Info
	VkMemoryAllocateInfo allocateInfo{};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	//Select memory type which has fast access from GPU
	allocateInfo.memoryTypeIndex = VKHelper::FindMemoryTypeIndex(*vkInit->GetPhysicalDevice(), requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//Allocate Memory
	VKHelper::ThrowIfFailed(vkAllocateMemory(*vkInit->GetDevice(), &allocateInfo, nullptr, &vkTextureDeviceMemoryIrradiance));

	//Bind Image and Memory
	VKHelper::ThrowIfFailed(vkBindImageMemory(*vkInit->GetDevice(), vkTextureImageIrradiance, vkTextureDeviceMemoryIrradiance, 0));

	{
		//To access image from graphics pipeline, Image View is needed
		//Create ImageView Info
		VkImageViewCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = vkTextureImageIrradiance;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.layerCount = 6;
		createInfo.subresourceRange.baseArrayLayer = 0;

		//Create ImageView
		VKHelper::ThrowIfFailed(vkCreateImageView(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureImageViewIrradiance));
	}

	{
		//Sampler is needed for shader to read image
		//Create Sampler Info
		VkSamplerCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		createInfo.magFilter = VK_FILTER_LINEAR;
		createInfo.minFilter = VK_FILTER_LINEAR;
		createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		//createInfo.anisotropyEnable = VK_TRUE;
		//createInfo.maxAnisotropy = 16.f;
		//createInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		createInfo.unnormalizedCoordinates = VK_FALSE;

		//Create Sampler
		VKHelper::ThrowIfFailed(vkCreateSampler(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureSamplerIrradiance));
	}

	//Prepare RenderPass
	//Create Attachment Description
	VkAttachmentDescription colorAattachmentDescription{};
	colorAattachmentDescription.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	colorAattachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAattachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAattachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAattachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAattachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//Define which attachment should subpass refernece of renderpass
	VkAttachmentReference colorAttachmentReference{};
	//attachment == Index of VkAttachmentDescription array
	colorAttachmentReference.attachment = 0;
	colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	//Create Subpass Description
	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorAttachmentReference;

	VkRenderPassCreateInfo rpCreateInfo{};
	rpCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpCreateInfo.attachmentCount = 1;
	rpCreateInfo.pAttachments = &colorAattachmentDescription;
	rpCreateInfo.subpassCount = 1;
	rpCreateInfo.pSubpasses = &subpassDescription;

	//Create Renderpass
	VKHelper::ThrowIfFailed(vkCreateRenderPass(*vkInit->GetDevice(), &rpCreateInfo, nullptr, &renderPassIBL));

	for (uint32_t f = 0; f < 6; ++f)
	{
		//Create ImageView
		//To access image from graphics pipeline, Image View is needed
		//Create ImageView Info
		VkImageViewCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = vkTextureImageIrradiance;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.layerCount = 1;
		createInfo.subresourceRange.baseArrayLayer = f;

		VKHelper::ThrowIfFailed(vkCreateImageView(*vkInit->GetDevice(), &createInfo, nullptr, &cubeFaceViews[f]));
	}

	for (uint32_t f = 0; f < 6; ++f)
	{
		VkImageView attachments[] = { cubeFaceViews[f] };

		//Create framebuffer info
		VkFramebufferCreateInfo fbCreateInfo{};
		fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbCreateInfo.renderPass = renderPassIBL;
		fbCreateInfo.attachmentCount = 1;
		fbCreateInfo.pAttachments = attachments;
		fbCreateInfo.width = irradianceSize;
		fbCreateInfo.height = irradianceSize;
		fbCreateInfo.layers = 1;

		//Create framebuffer
		VKHelper::ThrowIfFailed(vkCreateFramebuffer(*vkInit->GetDevice(), &fbCreateInfo, nullptr, &cubeFaceFramebuffers[f]));
	}

	//Render Images to Cube
	VKShader shaderIBL{ vkInit->GetDevice() };
	shaderIBL.LoadShader("../Engine/shaders/spirv/Cubemap.vert.spv", "../Engine/shaders/spirv/Irradiance.frag.spv");

	VKDescriptorLayout fragmentLayout;
	fragmentLayout.descriptorType = VKDescriptorLayout::SAMPLER;
	fragmentLayout.descriptorCount = 1;
	VKDescriptor descriptorIBL{ vkInit, {}, { fragmentLayout } };

	VKAttributeLayout position_layout;
	position_layout.vertex_layout_location = 0;
	position_layout.format = VK_FORMAT_R32G32B32_SFLOAT;
	position_layout.offset = 0;

	VKPipeLine pipelineIBL{ vkInit->GetDevice(), descriptorIBL.GetDescriptorSetLayout() };
	VkExtent2D extentIBL{ irradianceSize, irradianceSize };
	pipelineIBL.InitPipeLine(shaderIBL.GetVertexModule(), shaderIBL.GetFragmentModule(), &extentIBL, &renderPassIBL, sizeof(float) * 3, { position_layout }, VK_SAMPLE_COUNT_1_BIT, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE, POLYGON_MODE::FILL, true, sizeof(glm::mat4) * 2, VK_SHADER_STAGE_VERTEX_BIT);

	VKVertexBuffer vertexBufferIBL{ vkInit, sizeof(glm::vec3) * skyboxVertices.size(), skyboxVertices.data() };

	VkWriteDescriptorSet descriptorWrite{};
	VkDescriptorImageInfo skyboxDescriptorImageInfo{};
	skyboxDescriptorImageInfo.sampler = *GetCubeMap().first;
	skyboxDescriptorImageInfo.imageView = *GetCubeMap().second;
	skyboxDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//Define which resource descriptor set will point
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = (*descriptorIBL.GetFragmentDescriptorSets())[0];
	descriptorWrite.dstBinding = 0;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.pImageInfo = &skyboxDescriptorImageInfo;

	//Update DescriptorSet
	//DescriptorSet does not have to update every frame since it points same uniform buffer
	vkUpdateDescriptorSets(*vkInit->GetDevice(), 1, &descriptorWrite, 0, nullptr);

	//Create Viewport and Scissor for Dynamic State
	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = static_cast<float>(irradianceSize);
	viewport.height = static_cast<float>(irradianceSize);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = { irradianceSize, irradianceSize };

	//Create command buffer begin info
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	//Begin command buffer
	VKHelper::ThrowIfFailed(vkBeginCommandBuffer(*commandBuffer, &beginInfo));

	//Create renderpass begin info
	VkRenderPassBeginInfo renderpassBeginInfo{};
	renderpassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderpassBeginInfo.renderPass = renderPassIBL;
	renderpassBeginInfo.renderArea.offset = { 0, 0 };
	renderpassBeginInfo.renderArea.extent = { irradianceSize, irradianceSize };
	renderpassBeginInfo.clearValueCount = 1;
	renderpassBeginInfo.pClearValues = &clearColor;

	for (int f = 0; f < 6; ++f)
	{
		renderpassBeginInfo.framebuffer = cubeFaceFramebuffers[f];

		vkCmdBeginRenderPass(*commandBuffer, &renderpassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		//Bind Vertex Buffer
		VkDeviceSize vertexBufferOffset{ 0 };
		vkCmdBindVertexBuffers(*commandBuffer, 0, 1, vertexBufferIBL.GetVertexBuffer(), &vertexBufferOffset);
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineIBL.GetPipeLine());
		//Dynamic Viewport & Scissor
		vkCmdSetViewport(*commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(*commandBuffer, 0, 1, &scissor);
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineIBL.GetPipeLineLayout(), 0, 1, &(*descriptorIBL.GetFragmentDescriptorSets())[0], 0, nullptr);
		//Push Constant World-To_NDC
		glm::mat4 transform[2] = { views[f], projection };
		vkCmdPushConstants(*commandBuffer, *pipelineIBL.GetPipeLineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) * 2, &transform[0]);

		vkCmdDraw(*commandBuffer, 36, 1, 0, 0);

		vkCmdEndRenderPass(*commandBuffer);
	}

	VKHelper::ThrowIfFailed(vkEndCommandBuffer(*commandBuffer));

	//Create submit info
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = commandBuffer;

	//Submit queue to command buffer
	VKHelper::ThrowIfFailed(vkQueueSubmit(*vkInit->GetQueue(), 1, &submitInfo, VK_NULL_HANDLE));

	//Wait until all submitted command buffers are handled
	VKHelper::ThrowIfFailed(vkDeviceWaitIdle(*vkInit->GetDevice()));

	//Deallocate Resources
	for (auto& view : cubeFaceViews)
	{
		vkDestroyImageView(*vkInit->GetDevice(), view, nullptr);
	}

	vkDestroyRenderPass(*vkInit->GetDevice(), renderPassIBL, nullptr);

	for (auto& fb : cubeFaceFramebuffers)
	{
		vkDestroyFramebuffer(*vkInit->GetDevice(), fb, nullptr);
	}
}

void VKSkybox::PrefilteredEnvironmentMap(VkCommandBuffer* commandBuffer)
{
	{
		//Define an image to create
		VkImageCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		createInfo.imageType = VK_IMAGE_TYPE_2D;
		createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		createInfo.extent = { baseSize, baseSize, 1 };
		createInfo.mipLevels = mipLevels;
		createInfo.arrayLayers = 6; //6 Layers for CubeMap
		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		//Use Optimal Tiling to make GPU effectively process image
		createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		//Usage for copying, shader, and renderpass
		createInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		createInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		//Create image
		VKHelper::ThrowIfFailed(vkCreateImage(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureImagePrefilter));
	}

	//Declare a variable which will take memory requirements
	VkMemoryRequirements requirements{};
	//Get Memory Requirements for Image
	vkGetImageMemoryRequirements(*vkInit->GetDevice(), vkTextureImagePrefilter, &requirements);

	//Create Memory Allocation Info
	VkMemoryAllocateInfo allocateInfo{};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	//Select memory type which has fast access from GPU
	allocateInfo.memoryTypeIndex = VKHelper::FindMemoryTypeIndex(*vkInit->GetPhysicalDevice(), requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//Allocate Memory
	VKHelper::ThrowIfFailed(vkAllocateMemory(*vkInit->GetDevice(), &allocateInfo, nullptr, &vkTextureDeviceMemoryPrefilter));

	//Bind Image and Memory
	VKHelper::ThrowIfFailed(vkBindImageMemory(*vkInit->GetDevice(), vkTextureImagePrefilter, vkTextureDeviceMemoryPrefilter, 0));

	{
		//To access image from graphics pipeline, Image View is needed
		//Create ImageView Info
		VkImageViewCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = vkTextureImagePrefilter;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.levelCount = mipLevels;
		createInfo.subresourceRange.layerCount = 6;
		createInfo.subresourceRange.baseArrayLayer = 0;

		//Create ImageView
		VKHelper::ThrowIfFailed(vkCreateImageView(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureImageViewPrefilter));
	}

	{
		//Sampler is needed for shader to read image
		//Create Sampler Info
		VkSamplerCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		createInfo.magFilter = VK_FILTER_LINEAR;
		createInfo.minFilter = VK_FILTER_LINEAR;
		createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.minLod = 0.f;
		createInfo.maxLod = static_cast<float>(mipLevels);
		createInfo.mipLodBias = 0.f;
		//createInfo.anisotropyEnable = VK_TRUE;
		//createInfo.maxAnisotropy = 16.f;
		//createInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		createInfo.unnormalizedCoordinates = VK_FALSE;

		//Create Sampler
		VKHelper::ThrowIfFailed(vkCreateSampler(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureSamplerPrefilter));
	}

	//Prepare RenderPass
	//Create Attachment Description
	VkAttachmentDescription colorAattachmentDescription{};
	colorAattachmentDescription.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	colorAattachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAattachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAattachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAattachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAattachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//Define which attachment should subpass refernece of renderpass
	VkAttachmentReference colorAttachmentReference{};
	//attachment == Index of VkAttachmentDescription array
	colorAttachmentReference.attachment = 0;
	colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	//Create Subpass Description
	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorAttachmentReference;

	VkRenderPassCreateInfo rpCreateInfo{};
	rpCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpCreateInfo.attachmentCount = 1;
	rpCreateInfo.pAttachments = &colorAattachmentDescription;
	rpCreateInfo.subpassCount = 1;
	rpCreateInfo.pSubpasses = &subpassDescription;

	//Create Renderpass
	VKHelper::ThrowIfFailed(vkCreateRenderPass(*vkInit->GetDevice(), &rpCreateInfo, nullptr, &renderPassIBL));

	//Render Images to Cube
	VKShader shaderIBL{ vkInit->GetDevice() };
	shaderIBL.LoadShader("../Engine/shaders/spirv/Cubemap.vert.spv", "../Engine/shaders/spirv/Prefilter.frag.spv");

	VKDescriptorLayout fragmentLayout[2];
	fragmentLayout[0].descriptorType = VKDescriptorLayout::SAMPLER;
	fragmentLayout[0].descriptorCount = 1;
	fragmentLayout[1].descriptorType = VKDescriptorLayout::UNIFORM;
	fragmentLayout[1].descriptorCount = 1;
	VKDescriptor descriptorIBL{ vkInit, {}, { fragmentLayout[0], fragmentLayout[1] }};

	VKAttributeLayout position_layout;
	position_layout.vertex_layout_location = 0;
	position_layout.format = VK_FORMAT_R32G32B32_SFLOAT;
	position_layout.offset = 0;

	VKPipeLine pipelineIBL{ vkInit->GetDevice(), descriptorIBL.GetDescriptorSetLayout() };
	VkExtent2D extentIBL{ baseSize, baseSize };
	pipelineIBL.InitPipeLine(shaderIBL.GetVertexModule(), shaderIBL.GetFragmentModule(), &extentIBL, &renderPassIBL, sizeof(float) * 3, { position_layout }, VK_SAMPLE_COUNT_1_BIT, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_CULL_MODE_NONE, POLYGON_MODE::FILL, true, sizeof(glm::mat4) * 2, VK_SHADER_STAGE_VERTEX_BIT);

	VKVertexBuffer vertexBufferIBL{ vkInit, sizeof(glm::vec3) * skyboxVertices.size(), skyboxVertices.data() };

	VKUniformBuffer<float> prefilterUniform{ vkInit, 1 };

	VkWriteDescriptorSet descriptorWrite[2]{};
	VkDescriptorImageInfo skyboxDescriptorImageInfo{};
	skyboxDescriptorImageInfo.sampler = *GetCubeMap().first;
	skyboxDescriptorImageInfo.imageView = *GetCubeMap().second;
	skyboxDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//Define which resource descriptor set will point
	descriptorWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite[0].dstSet = (*descriptorIBL.GetFragmentDescriptorSets())[0];
	descriptorWrite[0].dstBinding = 0;
	descriptorWrite[0].descriptorCount = 1;
	descriptorWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite[0].pImageInfo = &skyboxDescriptorImageInfo;

	VkDescriptorBufferInfo bufferInfo;
	bufferInfo.buffer = (*(prefilterUniform.GetUniformBuffers()))[0];
	bufferInfo.offset = 0;
	bufferInfo.range = sizeof(float);

	//Define which resource descriptor set will point
	descriptorWrite[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite[1].dstSet = (*descriptorIBL.GetFragmentDescriptorSets())[0];
	descriptorWrite[1].dstBinding = 1;
	descriptorWrite[1].descriptorCount = 1;
	descriptorWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite[1].pBufferInfo = &bufferInfo;

	//Update DescriptorSet
	//DescriptorSet does not have to update every frame since it points same uniform buffer
	vkUpdateDescriptorSets(*vkInit->GetDevice(), 2, &descriptorWrite[0], 0, nullptr);

	for (uint32_t mip = 0; mip < mipLevels; ++mip)
	{
		uint32_t dim = baseSize >> mip;
		float roughness = static_cast<float>(mip) / static_cast<float>(mipLevels - 1);
		prefilterUniform.UpdateUniform(1, &roughness, 0);

		//Create command buffer begin info
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		//Begin command buffer
		VKHelper::ThrowIfFailed(vkBeginCommandBuffer(*commandBuffer, &beginInfo));

		//Create renderpass begin info
		VkRenderPassBeginInfo renderpassBeginInfo{};
		renderpassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderpassBeginInfo.renderPass = renderPassIBL;
		renderpassBeginInfo.renderArea.offset = { 0, 0 };
		renderpassBeginInfo.renderArea.extent = { dim, dim };
		renderpassBeginInfo.clearValueCount = 1;
		renderpassBeginInfo.pClearValues = &clearColor;

		for (uint32_t f = 0; f < 6; ++f)
		{
			//Create ImageView
			//To access image from graphics pipeline, Image View is needed
			//Create ImageView Info
			VkImageViewCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image = vkTextureImagePrefilter;
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			createInfo.subresourceRange.levelCount = 1;
			createInfo.subresourceRange.layerCount = 1;
			createInfo.subresourceRange.baseArrayLayer = f;
			createInfo.subresourceRange.baseMipLevel = mip;

			VKHelper::ThrowIfFailed(vkCreateImageView(*vkInit->GetDevice(), &createInfo, nullptr, &cubeFaceViews[f]));

			//Create framebuffer info
			VkImageView attachments[] = { cubeFaceViews[f] };
			VkFramebufferCreateInfo fbCreateInfo{};
			fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbCreateInfo.renderPass = renderPassIBL;
			fbCreateInfo.attachmentCount = 1;
			fbCreateInfo.pAttachments = attachments;
			fbCreateInfo.width = dim;
			fbCreateInfo.height = dim;
			fbCreateInfo.layers = 1;

			//Create framebuffer
			VKHelper::ThrowIfFailed(vkCreateFramebuffer(*vkInit->GetDevice(), &fbCreateInfo, nullptr, &cubeFaceFramebuffers[f]));

			//Create Viewport and Scissor for Dynamic State
			VkViewport viewport{};
			viewport.x = 0.f;
			viewport.y = 0.f;
			viewport.width = static_cast<float>(dim);
			viewport.height = static_cast<float>(dim);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;

			VkRect2D scissor{};
			scissor.offset = { 0, 0 };
			scissor.extent = { dim, dim };

			renderpassBeginInfo.framebuffer = cubeFaceFramebuffers[f];

			vkCmdBeginRenderPass(*commandBuffer, &renderpassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			//Bind Vertex Buffer
			VkDeviceSize vertexBufferOffset{ 0 };
			vkCmdBindVertexBuffers(*commandBuffer, 0, 1, vertexBufferIBL.GetVertexBuffer(), &vertexBufferOffset);
			vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineIBL.GetPipeLine());
			//Dynamic Viewport & Scissor
			vkCmdSetViewport(*commandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(*commandBuffer, 0, 1, &scissor);
			vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineIBL.GetPipeLineLayout(), 0, 1, &(*descriptorIBL.GetFragmentDescriptorSets())[0], 0, nullptr);
			//Push Constant World-To_NDC
			glm::mat4 transform[2] = { views[f], projection };
			vkCmdPushConstants(*commandBuffer, *pipelineIBL.GetPipeLineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) * 2, &transform[0]);

			vkCmdDraw(*commandBuffer, 36, 1, 0, 0);

			vkCmdEndRenderPass(*commandBuffer);
		}

		VKHelper::ThrowIfFailed(vkEndCommandBuffer(*commandBuffer));

		//Create submit info
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = commandBuffer;

		//Submit queue to command buffer
		VKHelper::ThrowIfFailed(vkQueueSubmit(*vkInit->GetQueue(), 1, &submitInfo, VK_NULL_HANDLE));

		//Wait until all submitted command buffers are handled
		VKHelper::ThrowIfFailed(vkDeviceWaitIdle(*vkInit->GetDevice()));

		//Deallocate Resources
		for (auto& view : cubeFaceViews)
		{
			vkDestroyImageView(*vkInit->GetDevice(), view, nullptr);
		}

		for (auto& fb : cubeFaceFramebuffers)
		{
			vkDestroyFramebuffer(*vkInit->GetDevice(), fb, nullptr);
		}
	}

	vkDestroyRenderPass(*vkInit->GetDevice(), renderPassIBL, nullptr);
}

void VKSkybox::BRDFLUT(VkCommandBuffer* commandBuffer)
{
	{
		//Define an image to create
		VkImageCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		createInfo.imageType = VK_IMAGE_TYPE_2D;
		createInfo.format = VK_FORMAT_R16G16_SFLOAT;
		createInfo.extent = { lutSize, lutSize, 1 };
		createInfo.mipLevels = 1;
		createInfo.arrayLayers = 1;
		createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		//Use Optimal Tiling to make GPU effectively process image
		createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		//Usage for copying, shader, and renderpass
		createInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		//Create image
		VKHelper::ThrowIfFailed(vkCreateImage(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureImageBRDFLUT));
	}

	//Declare a variable which will take memory requirements
	VkMemoryRequirements requirements{};
	//Get Memory Requirements for Image
	vkGetImageMemoryRequirements(*vkInit->GetDevice(), vkTextureImageBRDFLUT, &requirements);

	//Create Memory Allocation Info
	VkMemoryAllocateInfo allocateInfo{};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	//Select memory type which has fast access from GPU
	allocateInfo.memoryTypeIndex = VKHelper::FindMemoryTypeIndex(*vkInit->GetPhysicalDevice(), requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//Allocate Memory
	VKHelper::ThrowIfFailed(vkAllocateMemory(*vkInit->GetDevice(), &allocateInfo, nullptr, &vkTextureDeviceMemoryBRDFLUT));

	//Bind Image and Memory
	VKHelper::ThrowIfFailed(vkBindImageMemory(*vkInit->GetDevice(), vkTextureImageBRDFLUT, vkTextureDeviceMemoryBRDFLUT, 0));

	{
		//To access image from graphics pipeline, Image View is needed
		//Create ImageView Info
		VkImageViewCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = vkTextureImageBRDFLUT;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = VK_FORMAT_R16G16_SFLOAT;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.layerCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;

		//Create ImageView
		VKHelper::ThrowIfFailed(vkCreateImageView(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureImageViewBRDFLUT));
	}

	{
		//Sampler is needed for shader to read image
		//Create Sampler Info
		VkSamplerCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		createInfo.magFilter = VK_FILTER_LINEAR;
		createInfo.minFilter = VK_FILTER_LINEAR;
		createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		//createInfo.anisotropyEnable = VK_TRUE;
		//createInfo.maxAnisotropy = 16.f;
		//createInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		createInfo.unnormalizedCoordinates = VK_FALSE;

		//Create Sampler
		VKHelper::ThrowIfFailed(vkCreateSampler(*vkInit->GetDevice(), &createInfo, nullptr, &vkTextureSamplerBRDFLUT));
	}

	//Prepare RenderPass
	//Create Attachment Description
	VkAttachmentDescription colorAattachmentDescription{};
	colorAattachmentDescription.format = VK_FORMAT_R16G16_SFLOAT;
	colorAattachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAattachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAattachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAattachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAattachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//Define which attachment should subpass refernece of renderpass
	VkAttachmentReference colorAttachmentReference{};
	//attachment == Index of VkAttachmentDescription array
	colorAttachmentReference.attachment = 0;
	colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	//Create Subpass Description
	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorAttachmentReference;

	VkRenderPassCreateInfo rpCreateInfo{};
	rpCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpCreateInfo.attachmentCount = 1;
	rpCreateInfo.pAttachments = &colorAattachmentDescription;
	rpCreateInfo.subpassCount = 1;
	rpCreateInfo.pSubpasses = &subpassDescription;

	//Create Renderpass
	VKHelper::ThrowIfFailed(vkCreateRenderPass(*vkInit->GetDevice(), &rpCreateInfo, nullptr, &renderPassIBL));

	VkImageView attachments[] = { vkTextureImageViewBRDFLUT };

	//Create framebuffer info
	VkFramebufferCreateInfo fbCreateInfo{};
	fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbCreateInfo.renderPass = renderPassIBL;
	fbCreateInfo.attachmentCount = 1;
	fbCreateInfo.pAttachments = attachments;
	fbCreateInfo.width = lutSize;
	fbCreateInfo.height = lutSize;
	fbCreateInfo.layers = 1;

	VkFramebuffer fb;

	//Create framebuffer
	VKHelper::ThrowIfFailed(vkCreateFramebuffer(*vkInit->GetDevice(), &fbCreateInfo, nullptr, &fb));

	//Render Images to Cube
	VKShader shaderIBL{ vkInit->GetDevice() };
	shaderIBL.LoadShader("../Engine/shaders/spirv/BRDF.vert.spv", "../Engine/shaders/spirv/BRDF.frag.spv");

	VKDescriptor descriptorIBL{ vkInit, {}, {} };

	VKPipeLine pipelineIBL{ vkInit->GetDevice(), descriptorIBL.GetDescriptorSetLayout() };
	VkExtent2D extentIBL{ lutSize, lutSize };
	pipelineIBL.InitPipeLine(shaderIBL.GetVertexModule(), shaderIBL.GetFragmentModule(), &extentIBL, &renderPassIBL, 0, {}, VK_SAMPLE_COUNT_1_BIT, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_CULL_MODE_NONE, POLYGON_MODE::FILL, false);

	//Create Viewport and Scissor for Dynamic State
	VkViewport viewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = static_cast<float>(lutSize);
	viewport.height = static_cast<float>(lutSize);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = { lutSize, lutSize };

	//Create command buffer begin info
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	//Begin command buffer
	VKHelper::ThrowIfFailed(vkBeginCommandBuffer(*commandBuffer, &beginInfo));

	//Create renderpass begin info
	VkRenderPassBeginInfo renderpassBeginInfo{};
	renderpassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderpassBeginInfo.renderPass = renderPassIBL;
	renderpassBeginInfo.framebuffer = fb;
	renderpassBeginInfo.renderArea.offset = { 0, 0 };
	renderpassBeginInfo.renderArea.extent = { lutSize, lutSize };
	renderpassBeginInfo.clearValueCount = 1;
	renderpassBeginInfo.pClearValues = &clearColor;

	vkCmdBeginRenderPass(*commandBuffer, &renderpassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	//Bind Vertex Buffer
	vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineIBL.GetPipeLine());
	//Dynamic Viewport & Scissor
	vkCmdSetViewport(*commandBuffer, 0, 1, &viewport);
	vkCmdSetScissor(*commandBuffer, 0, 1, &scissor);

	vkCmdDraw(*commandBuffer, 4, 1, 0, 0);

	vkCmdEndRenderPass(*commandBuffer);

	VKHelper::ThrowIfFailed(vkEndCommandBuffer(*commandBuffer));

	//Create submit info
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = commandBuffer;

	//Submit queue to command buffer
	VKHelper::ThrowIfFailed(vkQueueSubmit(*vkInit->GetQueue(), 1, &submitInfo, VK_NULL_HANDLE));

	//Wait until all submitted command buffers are handled
	VKHelper::ThrowIfFailed(vkDeviceWaitIdle(*vkInit->GetDevice()));

	//Deallocate Resources
	vkDestroyRenderPass(*vkInit->GetDevice(), renderPassIBL, nullptr);
	vkDestroyFramebuffer(*vkInit->GetDevice(), fb, nullptr);
}
