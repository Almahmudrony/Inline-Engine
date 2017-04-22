#include "Node_CSM.hpp"

#include "NodeUtility.hpp"

#include "../MeshEntity.hpp"
#include "../Mesh.hpp"
#include "../Image.hpp"
#include "../DirectionalLight.hpp"
#include "../GraphicsCommandList.hpp"

#include <array>

namespace inl::gxeng::nodes {

struct Uniforms
{
	mathfu::VectorPacked<float, 4> model[4];
	uint32_t cascadeIDX;
};

static bool CheckMeshFormat(const Mesh& mesh) {
	for (size_t i = 0; i < mesh.GetNumStreams(); i++) {
		auto& elements = mesh.GetLayout()[0];
		if (elements.size() != 3) return false;
		if (elements[0].semantic != eVertexElementSemantic::POSITION) return false;
		if (elements[1].semantic != eVertexElementSemantic::NORMAL) return false;
		if (elements[2].semantic != eVertexElementSemantic::TEX_COORD) return false;
	}

	return true;
}


static void ConvertToSubmittable(
	Mesh* mesh,
	std::vector<const gxeng::VertexBuffer*>& vertexBuffers,
	std::vector<unsigned>& sizes,
	std::vector<unsigned>& strides
) {
	vertexBuffers.clear();
	sizes.clear();
	strides.clear();

	for (int streamID = 0; streamID < mesh->GetNumStreams(); streamID++) {
		vertexBuffers.push_back(&mesh->GetVertexBuffer(streamID));
		sizes.push_back((unsigned)vertexBuffers.back()->GetSize());
		strides.push_back((unsigned)mesh->GetVertexBufferStride(streamID));
	}

	assert(vertexBuffers.size() == sizes.size());
	assert(sizes.size() == strides.size());
}



CSM::CSM() {}


void CSM::Initialize(EngineContext & context) {
	GraphicsNode::SetTaskSingle(this);
}


void CSM::Setup(SetupContext & context) {
	Texture2D& renderTarget = this->GetInput<0>().Get();
	const gxapi::eFormat currDepthStencil = FormatAnyToDepthStencil(renderTarget.GetFormat());
	gxapi::DsvTexture2DArray dsvDesc;
	dsvDesc.activeArraySize = 1;
	dsvDesc.firstMipLevel = 0;
	m_dsvs.resize(renderTarget.GetArrayCount());
	for (int i = 0; i < m_dsvs.size(); i++) {
		dsvDesc.firstArrayElement = i;
		m_dsvs[i] = context.CreateDsv(renderTarget, currDepthStencil, dsvDesc);
	}

	m_entities = this->GetInput<1>().Get();
	this->GetInput<1>().Clear();

	Texture2D& lightMVPTex = this->GetInput<2>().Get();
	gxapi::SrvTexture2DArray srvDesc;
	srvDesc.activeArraySize = 1;
	srvDesc.firstArrayElement = 0;
	srvDesc.mipLevelClamping = 0;
	srvDesc.mostDetailedMip = 0;
	srvDesc.numMipLevels = 1;
	srvDesc.planeIndex = 0;
	m_lightMVPTexSrv = context.CreateSrv(lightMVPTex, lightMVPTex.GetFormat(), srvDesc);

	this->GetOutput<0>().Set(renderTarget);


	if (!m_binder.has_value()) {
		this->GetInput<0>().Set({});

		BindParameterDesc uniformsBindParamDesc;
		m_uniformsBindParam = BindParameter(eBindParameterType::CONSTANT, 0);
		uniformsBindParamDesc.parameter = m_uniformsBindParam;
		uniformsBindParamDesc.constantSize = sizeof(Uniforms);
		uniformsBindParamDesc.relativeAccessFrequency = 0;
		uniformsBindParamDesc.relativeChangeFrequency = 0;
		uniformsBindParamDesc.shaderVisibility = gxapi::eShaderVisiblity::VERTEX;

		BindParameterDesc lightMVPBindParamDesc;
		m_lightMVPBindParam = BindParameter(eBindParameterType::TEXTURE, 0);
		lightMVPBindParamDesc.parameter = m_lightMVPBindParam;
		lightMVPBindParamDesc.constantSize = 0;
		lightMVPBindParamDesc.relativeAccessFrequency = 0;
		lightMVPBindParamDesc.relativeChangeFrequency = 0;
		lightMVPBindParamDesc.shaderVisibility = gxapi::eShaderVisiblity::VERTEX;

		BindParameterDesc sampBindParamDesc;
		sampBindParamDesc.parameter = BindParameter(eBindParameterType::SAMPLER, 0);
		sampBindParamDesc.constantSize = 0;
		sampBindParamDesc.relativeAccessFrequency = 0;
		sampBindParamDesc.relativeChangeFrequency = 0;
		sampBindParamDesc.shaderVisibility = gxapi::eShaderVisiblity::VERTEX;

		gxapi::StaticSamplerDesc samplerDesc;
		samplerDesc.shaderRegister = 0;
		samplerDesc.filter = gxapi::eTextureFilterMode::MIN_MAG_MIP_LINEAR;
		samplerDesc.addressU = gxapi::eTextureAddressMode::WRAP;
		samplerDesc.addressV = gxapi::eTextureAddressMode::WRAP;
		samplerDesc.addressW = gxapi::eTextureAddressMode::WRAP;
		samplerDesc.mipLevelBias = 0.f;
		samplerDesc.registerSpace = 0;
		samplerDesc.shaderVisibility = gxapi::eShaderVisiblity::PIXEL;

		m_binder = context.CreateBinder({ uniformsBindParamDesc, lightMVPBindParamDesc, sampBindParamDesc },{ samplerDesc });
	}

	if (!m_PSO || currDepthStencil != m_depthStencilFormat) {
		m_depthStencilFormat = currDepthStencil;

		//TODO
		constexpr unsigned cascadeSize = 1024;
		constexpr unsigned numCascades = 4;

		ShaderParts shaderParts;
		shaderParts.vs = true;
		shaderParts.ps = true;

		auto shader = context.CreateShader("CSM", shaderParts, "");

		std::vector<gxapi::InputElementDesc> inputElementDesc = {
			gxapi::InputElementDesc("POSITION", 0, gxapi::eFormat::R32G32B32_FLOAT, 0, 0),
			gxapi::InputElementDesc("NORMAL", 0, gxapi::eFormat::R32G32B32_FLOAT, 0, 12),
			gxapi::InputElementDesc("TEX_COORD", 0, gxapi::eFormat::R32G32_FLOAT, 0, 24),
		};

		gxapi::GraphicsPipelineStateDesc psoDesc;
		psoDesc.inputLayout.elements = inputElementDesc.data();
		psoDesc.inputLayout.numElements = (unsigned)inputElementDesc.size();
		psoDesc.rootSignature = m_binder->GetRootSignature();
		psoDesc.vs = shader.vs;
		psoDesc.ps = shader.ps;
		psoDesc.rasterization = gxapi::RasterizerState(gxapi::eFillMode::SOLID, gxapi::eCullMode::DRAW_CCW);
		psoDesc.primitiveTopologyType = gxapi::ePrimitiveTopologyType::TRIANGLE;

		psoDesc.depthStencilState = gxapi::DepthStencilState(true, true);
		psoDesc.depthStencilFormat = m_depthStencilFormat;

		psoDesc.numRenderTargets = 0;

		m_PSO.reset(context.CreatePSO(psoDesc));
	}
}


void CSM::Execute(RenderContext & context) {
	GraphicsCommandList& commandList = context.AsGraphics();

	assert(m_dsvs.size() > 0);

	Texture2D cascadeTextures = m_dsvs[0].GetResource();
	const uint16_t numCascades = m_dsvs.size();
	const uint64_t cascadeWidth = cascadeTextures.GetWidth();
	const uint64_t cascadeHeight = cascadeTextures.GetHeight();

	gxapi::Rectangle rect{ 0, (int)cascadeTextures.GetHeight(), 0, (int)cascadeTextures.GetWidth() };
	commandList.SetScissorRects(1, &rect);

	commandList.SetPipelineState(m_PSO.get());
	commandList.SetGraphicsBinder(&m_binder.value());
	commandList.SetPrimitiveTopology(gxapi::ePrimitiveTopology::TRIANGLELIST);

	commandList.SetResourceState(m_lightMVPTexSrv.GetResource(), 0, gxapi::eResourceState::PIXEL_SHADER_RESOURCE);
	commandList.BindGraphics(m_lightMVPBindParam, m_lightMVPTexSrv);

	std::vector<const gxeng::VertexBuffer*> vertexBuffers;
	std::vector<unsigned> sizes;
	std::vector<unsigned> strides;

	for (int cascadeIdx = 0; cascadeIdx < numCascades; ++cascadeIdx) {
		commandList.SetRenderTargets(0, nullptr, &m_dsvs[cascadeIdx]);

		commandList.SetResourceState(cascadeTextures, cascadeIdx, gxapi::eResourceState::DEPTH_WRITE);
		commandList.ClearDepthStencil(m_dsvs[cascadeIdx], 1, 0, 0, nullptr, true, true);

		gxapi::Viewport viewport;
		viewport.height = (float)cascadeHeight;
		viewport.width = (float)cascadeWidth;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		viewport.topLeftY = 0;
		viewport.topLeftX = 0;
		commandList.SetViewports(1, &viewport);

		// Iterate over all entities
		for (const MeshEntity* entity : *m_entities) {
			// Get entity parameters
			Mesh* mesh = entity->GetMesh();
			auto position = entity->GetPosition();

			// Draw mesh
			if (!CheckMeshFormat(*mesh)) {
				assert(false);
				continue;
			}

			ConvertToSubmittable(mesh, vertexBuffers, sizes, strides);

			mathfu::Matrix4x4f model = entity->GetTransform();

			Uniforms uniformsCBData;
			model.Pack(uniformsCBData.model);

			uniformsCBData.cascadeIDX = cascadeIdx;

			commandList.BindGraphics(m_uniformsBindParam, &uniformsCBData, sizeof(uniformsCBData));

			commandList.SetVertexBuffers(0, (unsigned)vertexBuffers.size(), vertexBuffers.data(), sizes.data(), strides.data());
			commandList.SetIndexBuffer(&mesh->GetIndexBuffer(), mesh->IsIndexBuffer32Bit());
			commandList.DrawIndexedInstanced((unsigned)mesh->GetIndexBuffer().GetIndexCount());
		}
	}
}


} // namespace inl::gxeng::nodes
