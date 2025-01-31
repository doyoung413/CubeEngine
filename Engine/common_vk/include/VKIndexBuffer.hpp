//Author: JEYOON YU
//Project: CubeEngine
//File: VKIndexBuffer.hpp
#pragma once
#include <vulkan/vulkan.hpp>

class VKInit;

class VKIndexBuffer
{
public:
	VKIndexBuffer(VKInit* init_, VkCommandPool* pool_, std::vector<uint32_t>* indices_);
	~VKIndexBuffer();

	void InitIndexBuffer(std::vector<uint32_t>* indices_);
	void UpdateIndexBuffer(std::vector<uint32_t>* indices_);
	VkBuffer* GetIndexBuffer() { return &vkIndexBuffer; };
private:
	uint32_t FindMemoryTypeIndex(const VkMemoryRequirements requirements_, VkMemoryPropertyFlags properties_);
	VKInit* vkInit;
	VkCommandPool* vkCommandPool;

	VkBuffer vkIndexBuffer{ VK_NULL_HANDLE };
	VkDeviceMemory vkIndexDeviceMemory{ VK_NULL_HANDLE };
};