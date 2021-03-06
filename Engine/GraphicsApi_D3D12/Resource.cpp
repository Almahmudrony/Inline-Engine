#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Resource.hpp"

#include "NativeCast.hpp"
#include "ExceptionExpansions.hpp"

#include "D3dx12.h"

#include <cassert>


namespace inl {
namespace gxapi_dx12 {

Resource::Resource(ComPtr<ID3D12Resource>& native)
	: m_native{native}
{
	auto desc = GetDesc();

	// calculate number of mip levels
	if (desc.type == gxapi::eResourceType::BUFFER) {
		m_numMipLevels = 1;
	}
	else {
		m_numMipLevels = desc.textureDesc.mipLevels;
	}

	// calculate number of texture planes
	if (desc.type == gxapi::eResourceType::BUFFER) {
		m_numTexturePlanes = 1;
	}
	else {
		DXGI_FORMAT fmt = native_cast(desc.textureDesc.format);
		ComPtr<ID3D12Device1> device;
		HRESULT hr = m_native->GetDevice(IID_PPV_ARGS(&device));
		assert(SUCCEEDED(hr));
		m_numTexturePlanes = D3D12GetFormatPlaneCount(device.Get(), fmt);
	}

	// calculate number of array levels
	if (desc.type == gxapi::eResourceType::BUFFER) {
		m_numArrayLevels = 1;
	}
	else {
		m_numArrayLevels = desc.textureDesc.dimension == gxapi::eTextueDimension::THREE ? 1 : desc.textureDesc.depthOrArraySize;
	}
}

ID3D12Resource* Resource::GetNative() {
	return m_native.Get();
}

const ID3D12Resource* Resource::GetNative() const {
	return m_native.Get();
}


gxapi::ResourceDesc Resource::GetDesc() const {
	return native_cast(m_native->GetDesc());
}


void* Resource::Map(unsigned subresourceIndex, const gxapi::MemoryRange* readRange) {
	void* result;

	D3D12_RANGE nativeRange;
	D3D12_RANGE* pNativeRange = nullptr;
	if (readRange != nullptr) {
		nativeRange = native_cast(*readRange);
		pNativeRange = &nativeRange;
	}

	ThrowIfFailed(m_native->Map(subresourceIndex, pNativeRange, &result));

	return result;
}


void Resource::Unmap(unsigned subresourceIndex, const gxapi::MemoryRange* writtenRange) {
	D3D12_RANGE nativeRange;
	D3D12_RANGE* pNativeRange = nullptr;
	if (writtenRange != nullptr) {
		nativeRange = native_cast(*writtenRange);
		pNativeRange = &nativeRange;
	}

	m_native->Unmap(subresourceIndex, pNativeRange);
}


void* Resource::GetGPUAddress() const {
	return native_cast_ptr(m_native->GetGPUVirtualAddress());
}


unsigned Resource::GetNumMipLevels() {
	return m_numMipLevels;
}
unsigned Resource::GetNumTexturePlanes() {
	return m_numTexturePlanes;
}
unsigned Resource::GetNumArrayLevels() {
	return m_numArrayLevels;
}

unsigned Resource::GetNumSubresources() {
	return m_numMipLevels * m_numTexturePlanes * m_numArrayLevels;
}
unsigned Resource::GetSubresourceIndex(unsigned mipIdx, unsigned arrayIdx, unsigned planeIdx) {
	unsigned index = D3D12CalcSubresource(mipIdx, arrayIdx, planeIdx, GetNumMipLevels(), GetNumArrayLevels());
	assert(index < GetNumSubresources());
	return index;
}


void Resource::SetName(const char* name) {
	size_t count = strlen(name);
	std::unique_ptr<wchar_t[]> dest = std::make_unique<wchar_t[]>(count + 1);
	mbstowcs(dest.get(), name, count);
	m_native->SetName(dest.get());
}


} // namespace gxapi_dx12
} // namespace inl
