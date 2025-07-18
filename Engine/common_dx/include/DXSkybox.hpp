//Author: JEYOON YU
//Project: CubeEngine
//File: DXSkybox.hpp
#pragma once
#include <directx/d3dx12.h>
#include <wrl.h>

#include "DebugTools.hpp"

#include "DXVertexBuffer.hpp"
#include "DXPipeLine.hpp"

#include <filesystem>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

class DXTexture;

using Microsoft::WRL::ComPtr;

class DXSkybox
{
public:
	DXSkybox(const ComPtr<ID3D12Device>& device,
		const ComPtr<ID3D12CommandQueue>& commandQueue,
		const ComPtr<ID3D12DescriptorHeap>& srvHeap,
		const UINT& srvHeapStartOffset);
	~DXSkybox();
	void Initialize(const std::filesystem::path& path);
	void ExecuteCommandList();

	void EquirectangularToCube();
	void CalculateIrradiance();
	void PrefilteredEnvironmentMap();
	void BRDFLUT();

	D3D12_GPU_DESCRIPTOR_HANDLE GetEquirectangularSrv()
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE handle{ m_srvHeap->GetGPUDescriptorHandleForHeapStart() };
		handle.Offset(static_cast<INT>(m_srvHeapStartOffset), m_srvDescriptorSize);
		return handle;
	}
	D3D12_GPU_DESCRIPTOR_HANDLE GetCubemapSrv() const
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE handle{ m_srvHeap->GetGPUDescriptorHandleForHeapStart() };
		handle.Offset(static_cast<INT>(m_srvHeapStartOffset) + 1, m_srvDescriptorSize);
		return handle;
	}
	D3D12_GPU_DESCRIPTOR_HANDLE GetIrradianceMapSrv() const
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE handle{ m_srvHeap->GetGPUDescriptorHandleForHeapStart() };
		handle.Offset(static_cast<INT>(m_srvHeapStartOffset) + 2, m_srvDescriptorSize);
		return handle;
	}
	D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilterMapSrv() const
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE handle{ m_srvHeap->GetGPUDescriptorHandleForHeapStart() };
		handle.Offset(static_cast<INT>(m_srvHeapStartOffset) + 3, m_srvDescriptorSize);
		return handle;
	}
	D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv() const
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE handle{ m_srvHeap->GetGPUDescriptorHandleForHeapStart() };
		handle.Offset(static_cast<INT>(m_srvHeapStartOffset) + 4, m_srvDescriptorSize);
		return handle;
	}
private:
	std::unique_ptr<DXTexture> m_equirectangularMap;

	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12CommandQueue> m_commandQueue;

	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12Fence> m_fence;
	HANDLE m_fenceEvent;
	UINT64 m_fenceValue{ 1 };

#if USE_NSIGHT_AFTERMATH
	// Nsight Aftermath instrumentation
	GFSDK_Aftermath_ContextHandle m_hAftermathCommandListContext{ nullptr };
#endif

	ComPtr<ID3D12DescriptorHeap> m_srvHeap;
	UINT m_srvHeapStartOffset;
	UINT m_srvDescriptorSize;

	ComPtr<ID3D12Resource> m_cubemap;
	ComPtr<ID3D12Resource> m_irradianceMap;
	ComPtr<ID3D12Resource> m_prefilterMap;
	ComPtr<ID3D12Resource> m_brdfLUT;
	// 0 = cubemap, 1 = irradiance, 2 = prefilter, 3 = BRDFLUT
	ComPtr<ID3D12RootSignature> m_rootSignatures[4];
	std::unique_ptr<DXPipeLine> m_pipelines[4];

	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	UINT m_rtvDescriptorSize;

	// Equirectangular to Cube
	uint32_t faceSize{ 0 };
	// Irradiance
	uint32_t irradianceSize{ 64 };
	// Prefilter
	uint32_t baseSize{ 512 };
	UINT16 mipLevels{ 5 };
	// BRDF LUT
	uint32_t lutSize{ 512 };

	std::unique_ptr<DXVertexBuffer> m_skyboxVertexBuffer;
	std::vector<glm::vec3> m_skyboxVertices = {
		{-1.0f, -1.0f, -1.0f},
		{-1.0f,  1.0f, -1.0f},
		{ 1.0f,  1.0f, -1.0f},
		{ 1.0f,  1.0f, -1.0f},
		{ 1.0f, -1.0f, -1.0f},
		{-1.0f, -1.0f, -1.0f},

		{-1.0f,  1.0f,  1.0f},
		{-1.0f,  1.0f, -1.0f},
		{-1.0f, -1.0f, -1.0f},
		{-1.0f, -1.0f, -1.0f},
		{-1.0f, -1.0f,  1.0f},
		{-1.0f,  1.0f,  1.0f},

		{ 1.0f,  1.0f, -1.0f},
		{ 1.0f,  1.0f,  1.0f},
		{ 1.0f, -1.0f,  1.0f},
		{ 1.0f, -1.0f,  1.0f},
		{ 1.0f, -1.0f, -1.0f},
		{ 1.0f,  1.0f, -1.0f},

		{-1.0f,  1.0f,  1.0f},
		{-1.0f, -1.0f,  1.0f},
		{ 1.0f, -1.0f,  1.0f},
		{ 1.0f, -1.0f,  1.0f},
		{ 1.0f,  1.0f,  1.0f},
		{-1.0f,  1.0f,  1.0f},

		{-1.0f, -1.0f, -1.0f},
		{ 1.0f, -1.0f, -1.0f},
		{ 1.0f, -1.0f,  1.0f},
		{ 1.0f, -1.0f,  1.0f},
		{-1.0f, -1.0f,  1.0f},
		{-1.0f, -1.0f, -1.0f},

		{-1.0f,  1.0f, -1.0f},
		{-1.0f,  1.0f,  1.0f},
		{ 1.0f,  1.0f, -1.0f},
		{ 1.0f,  1.0f, -1.0f},
		{-1.0f,  1.0f,  1.0f},
		{ 1.0f,  1.0f,  1.0f}
	};

	const float clearColor[4] = { 0.f, 0.f, 0.f, 1.0f };
	struct WorldToNDC { glm::mat4 view; glm::mat4 projection; };
	glm::mat4 projection = glm::perspectiveLH_ZO(glm::radians(90.f), 1.f, 0.1f, 10.f);
	glm::mat4 views[6] = {
	glm::lookAtLH(glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f)),
	glm::lookAtLH(glm::vec3(0.f, 0.f, 0.f), glm::vec3(-1.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f)),
	glm::lookAtLH(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f,0.f, -1.f)),
	glm::lookAtLH(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, -1.f, 0.f), glm::vec3(0.f,0.f, 1.f)),
	glm::lookAtLH(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 1.f, 0.f)),
	glm::lookAtLH(glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, 1.f, 0.f))
	};
};