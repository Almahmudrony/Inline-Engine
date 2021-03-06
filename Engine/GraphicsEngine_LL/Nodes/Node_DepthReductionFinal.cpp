#include "Node_DepthReductionFinal.hpp"

#include "NodeUtility.hpp"

#include "../MeshEntity.hpp"
#include "../Mesh.hpp"
#include "../Image.hpp"
#include "../DirectionalLight.hpp"
#include "../PerspectiveCamera.hpp"
#include "../GraphicsCommandList.hpp"
#include "../EntityCollection.hpp"

#include "DebugDrawManager.hpp"

#include <array>

namespace inl::gxeng::nodes {

struct Uniforms
{
	mathfu::VectorPacked<float, 4> invVP[4];
	mathfu::VectorPacked<float, 4> bias_mx[4], inv_mv[4];
	mathfu::VectorPacked<float, 4> cam_pos, cam_view_dir, cam_up_vector;
	mathfu::VectorPacked<float, 4> light_cam_pos, light_cam_view_dir, light_cam_up_vector;
	float cam_near, cam_far, tex_size;
	float dummy;
};


DepthReductionFinal::DepthReductionFinal() {
	this->GetInput<0>().Set({});
}


void DepthReductionFinal::Initialize(EngineContext & context) {
	GraphicsNode::SetTaskSingle(this);
}

void DepthReductionFinal::Reset() {
	m_reductionTexSrv = TextureView2D();
	m_camera = nullptr;
	m_suns = nullptr;

	GetInput<0>().Clear();
	GetInput<1>().Clear();
	GetInput<2>().Clear();
}


void DepthReductionFinal::Setup(SetupContext& context) {
	InitRenderTarget(context);


	Texture2D reductionTex = this->GetInput<0>().Get();

	gxapi::SrvTexture2DArray srvDesc;
	srvDesc.activeArraySize = 1;
	srvDesc.firstArrayElement = 0;
	srvDesc.mipLevelClamping = 0;
	srvDesc.mostDetailedMip = 0;
	srvDesc.numMipLevels = 1;
	srvDesc.planeIndex = 0;
	m_reductionTexSrv = context.CreateSrv(reductionTex, reductionTex.GetFormat(), srvDesc);
	m_reductionTexSrv.GetResource()._GetResourcePtr()->SetName("Depth reduction final reduction tex SRV");

	m_camera = this->GetInput<1>().Get();
	m_suns = this->GetInput<2>().Get();

	this->GetOutput<0>().Set(m_light_mvp_uav.GetResource());
	this->GetOutput<1>().Set(m_shadow_mx_uav.GetResource());
	this->GetOutput<2>().Set(m_csm_splits_uav.GetResource());

	if (!m_binder.has_value()) {
		BindParameterDesc uniformsBindParamDesc;
		m_uniformsBindParam = BindParameter(eBindParameterType::CONSTANT, 0);
		uniformsBindParamDesc.parameter = m_uniformsBindParam;
		uniformsBindParamDesc.constantSize = sizeof(Uniforms);
		uniformsBindParamDesc.relativeAccessFrequency = 0;
		uniformsBindParamDesc.relativeChangeFrequency = 0;
		uniformsBindParamDesc.shaderVisibility = gxapi::eShaderVisiblity::ALL;

		BindParameterDesc sampBindParamDesc;
		sampBindParamDesc.parameter = BindParameter(eBindParameterType::SAMPLER, 0);
		sampBindParamDesc.constantSize = 0;
		sampBindParamDesc.relativeAccessFrequency = 0;
		sampBindParamDesc.relativeChangeFrequency = 0;
		sampBindParamDesc.shaderVisibility = gxapi::eShaderVisiblity::ALL;

		BindParameterDesc reductionBindParamDesc;
		m_reductionBindParam = BindParameter(eBindParameterType::TEXTURE, 0);
		reductionBindParamDesc.parameter = m_reductionBindParam;
		reductionBindParamDesc.constantSize = 0;
		reductionBindParamDesc.relativeAccessFrequency = 0;
		reductionBindParamDesc.relativeChangeFrequency = 0;
		reductionBindParamDesc.shaderVisibility = gxapi::eShaderVisiblity::ALL;

		BindParameterDesc outputBindParamDesc0;
		m_outputBindParam0 = BindParameter(eBindParameterType::UNORDERED, 0);
		outputBindParamDesc0.parameter = m_outputBindParam0;
		outputBindParamDesc0.constantSize = 0;
		outputBindParamDesc0.relativeAccessFrequency = 0;
		outputBindParamDesc0.relativeChangeFrequency = 0;
		outputBindParamDesc0.shaderVisibility = gxapi::eShaderVisiblity::ALL;

		BindParameterDesc outputBindParamDesc1;
		m_outputBindParam1 = BindParameter(eBindParameterType::UNORDERED, 1);
		outputBindParamDesc1.parameter = m_outputBindParam1;
		outputBindParamDesc1.constantSize = 0;
		outputBindParamDesc1.relativeAccessFrequency = 0;
		outputBindParamDesc1.relativeChangeFrequency = 0;
		outputBindParamDesc1.shaderVisibility = gxapi::eShaderVisiblity::ALL;

		BindParameterDesc outputBindParamDesc2;
		m_outputBindParam2 = BindParameter(eBindParameterType::UNORDERED, 2);
		outputBindParamDesc2.parameter = m_outputBindParam2;
		outputBindParamDesc2.constantSize = 0;
		outputBindParamDesc2.relativeAccessFrequency = 0;
		outputBindParamDesc2.relativeChangeFrequency = 0;
		outputBindParamDesc2.shaderVisibility = gxapi::eShaderVisiblity::ALL;

		gxapi::StaticSamplerDesc samplerDesc;
		samplerDesc.shaderRegister = 0;
		samplerDesc.filter = gxapi::eTextureFilterMode::MIN_MAG_MIP_LINEAR;
		samplerDesc.addressU = gxapi::eTextureAddressMode::WRAP;
		samplerDesc.addressV = gxapi::eTextureAddressMode::WRAP;
		samplerDesc.addressW = gxapi::eTextureAddressMode::WRAP;
		samplerDesc.mipLevelBias = 0.f;
		samplerDesc.registerSpace = 0;
		samplerDesc.shaderVisibility = gxapi::eShaderVisiblity::ALL;

		m_binder = context.CreateBinder({ uniformsBindParamDesc, sampBindParamDesc, reductionBindParamDesc, outputBindParamDesc0, outputBindParamDesc1, outputBindParamDesc2 },{ samplerDesc });
	}

	if (!m_CSO) {
		InitRenderTarget(context);

		ShaderParts shaderParts;
		shaderParts.cs = true;

		m_shader = context.CreateShader("DepthReductionFinal", shaderParts, "");

		gxapi::ComputePipelineStateDesc csoDesc;
		csoDesc.rootSignature = m_binder->GetRootSignature();
		csoDesc.cs = m_shader.cs;

		m_CSO.reset(context.CreatePSO(csoDesc));
	}
}


void DepthReductionFinal::Execute(RenderContext& context) {
	ComputeCommandList& commandList = context.AsCompute();

	Uniforms uniformsCBData;

	DebugDrawManager::GetInstance().AddSphere(m_camera->GetPosition() + m_camera->GetLookDirection() * 5, 1, 1);

	mathfu::Matrix4x4f view = m_camera->GetViewMatrixRH();
	mathfu::Matrix4x4f projection = m_camera->GetProjectionMatrixRH();
	mathfu::Matrix4x4f vp = projection * view;

	vp.Inverse().Pack(uniformsCBData.invVP);

	mathfu::Matrix4x4f  bias_matrix(	0.5f,	0,		0,		0,			// column #1
										0,		-0.5f,	0,		0,			// column #2
										0,		0,		1.0f,	0,			// column #3
										0.5f,	0.5f,	0.0f,	1);	// column #4
	bias_matrix.Pack(uniformsCBData.bias_mx);

	view.Inverse().Pack(uniformsCBData.inv_mv);

	const PerspectiveCamera* perpectiveCamera = dynamic_cast<const PerspectiveCamera*>(m_camera);
	if (perpectiveCamera == nullptr) {
		throw std::invalid_argument("Depth reduction only works with perspective camera");
	}

	mathfu::Vector4f cam_pos(perpectiveCamera->GetPosition(), 1.0f);
	mathfu::Vector4f cam_view_dir(perpectiveCamera->GetLookDirection(), 0.0f);
	mathfu::Vector4f cam_up_vector(perpectiveCamera->GetUpVector(), 0.0f);

	cam_pos.Pack(&uniformsCBData.cam_pos);
	cam_view_dir.Pack(&uniformsCBData.cam_view_dir);
	cam_up_vector.Pack(&uniformsCBData.cam_up_vector);

	uniformsCBData.cam_near = perpectiveCamera->GetNearPlane();
	uniformsCBData.cam_far = perpectiveCamera->GetFarPlane();

	assert(m_suns->Size() > 0);
	auto sun = *m_suns->begin();
	//TODO get from somewhere
	mathfu::Vector4f light_cam_pos = mathfu::Vector4f(0, 0, 0, 1);
	mathfu::Vector4f light_cam_view_dir = mathfu::Vector4f(sun->GetDirection().Normalized(), 0);//mathfu::Vector4f(1, 1, 1, 0).Normalized();
	//printf("%f %f %f\n", light_cam_view_dir.x(), light_cam_view_dir.y(), light_cam_view_dir.z());
	mathfu::Vector4f light_cam_up_vector = mathfu::Vector4f(0, 0, 1, 0);

	auto lookat = [](mathfu::Vector3f eye, mathfu::Vector3f lookat, mathfu::Vector3f up, mathfu::Vector3f* result) -> void
	{
		result[0] = (lookat - eye).Normalized(); //view dir
		result[1] = up.Normalized();
		result[2] = eye;
		mathfu::Vector3f right = mathfu::Vector3f::CrossProduct(result[0], result[1]).Normalized();
		result[1] = mathfu::Vector3f::CrossProduct(right, result[0]).Normalized();
	};

	mathfu::Vector3f res[3];

	lookat(light_cam_pos.xyz(), light_cam_view_dir.xyz(), light_cam_up_vector.xyz(), res);

	light_cam_pos = mathfu::Vector4f(res[2], 1);
	light_cam_view_dir = mathfu::Vector4f(res[0], 0);
	light_cam_up_vector = mathfu::Vector4f(res[1], 0);

	light_cam_pos.Pack(&uniformsCBData.light_cam_pos);
	light_cam_view_dir.Pack(&uniformsCBData.light_cam_view_dir);
	light_cam_up_vector.Pack(&uniformsCBData.light_cam_up_vector);

	//TODO get from somewhere
	uniformsCBData.tex_size = 2048;

	//create single-frame only cb
	gxeng::VolatileConstBuffer cb = context.CreateVolatileConstBuffer(&uniformsCBData, sizeof(Uniforms));
	cb._GetResourcePtr()->SetName("Depth reduction final volatile CB");
	gxeng::ConstBufferView cbv = context.CreateCbv(cb, 0, sizeof(Uniforms));
	cbv.GetResource()._GetResourcePtr()->SetName("Depth reduction final CBV");

	commandList.SetResourceState(m_light_mvp_uav.GetResource(), gxapi::eResourceState::UNORDERED_ACCESS);
	commandList.SetResourceState(m_shadow_mx_uav.GetResource(), gxapi::eResourceState::UNORDERED_ACCESS);
	commandList.SetResourceState(m_csm_splits_uav.GetResource(), gxapi::eResourceState::UNORDERED_ACCESS);
	commandList.SetResourceState(m_reductionTexSrv.GetResource(), { gxapi::eResourceState::PIXEL_SHADER_RESOURCE, gxapi::eResourceState::NON_PIXEL_SHADER_RESOURCE });

	commandList.SetPipelineState(m_CSO.get());
	commandList.SetComputeBinder(&m_binder.value());
	commandList.BindCompute(m_reductionBindParam, m_reductionTexSrv);
	commandList.BindCompute(m_outputBindParam0, m_light_mvp_uav);
	commandList.BindCompute(m_outputBindParam1, m_shadow_mx_uav);
	commandList.BindCompute(m_outputBindParam2, m_csm_splits_uav);
	commandList.BindCompute(m_uniformsBindParam, cbv);
	commandList.Dispatch(1, 1, 1);
	commandList.UAVBarrier(m_light_mvp_uav.GetResource());
	commandList.UAVBarrier(m_shadow_mx_uav.GetResource());
	commandList.UAVBarrier(m_csm_splits_uav.GetResource());
}


void DepthReductionFinal::InitRenderTarget(SetupContext& context) {
	if (!m_outputTexturesInited) {
		m_outputTexturesInited = true;

		using gxapi::eFormat;

		auto formatLightMVP = eFormat::R32G32B32A32_FLOAT;
		auto formatShadowMX = eFormat::R32G32B32A32_FLOAT;
		auto formatCSMSplits = eFormat::R32G32_FLOAT;

		gxapi::UavTexture2DArray uavDesc;
		uavDesc.activeArraySize = 1;
		uavDesc.firstArrayElement = 0;
		uavDesc.mipLevel = 0;
		uavDesc.planeIndex = 0;

		gxapi::SrvTexture2DArray srvDesc;
		srvDesc.activeArraySize = 1;
		srvDesc.firstArrayElement = 0;
		srvDesc.numMipLevels = -1;
		srvDesc.mipLevelClamping = 0;
		srvDesc.mostDetailedMip = 0;
		srvDesc.planeIndex = 0;

		//TODO 1D tex
		Texture2D light_mvp_tex = context.CreateRWTexture2D(4 * 4, 1, formatLightMVP, 1);
		light_mvp_tex._GetResourcePtr()->SetName("Depth reduction final light MVP tex");
		m_light_mvp_uav = context.CreateUav(light_mvp_tex, formatLightMVP, uavDesc);
		m_light_mvp_uav.GetResource()._GetResourcePtr()->SetName("Depth reduction final light MVP UAV");
		

		Texture2D shadow_mx_tex = context.CreateRWTexture2D(4 * 4, 1, formatShadowMX, 1);
		shadow_mx_tex._GetResourcePtr()->SetName("Depth reduction final shadow MX tex");
		m_shadow_mx_uav = context.CreateUav(shadow_mx_tex, formatShadowMX, uavDesc);
		m_shadow_mx_uav.GetResource()._GetResourcePtr()->SetName("Depth reduction final shadow MX UAV");

		Texture2D csm_splits_tex = context.CreateRWTexture2D(4, 1, formatCSMSplits, 1);
		csm_splits_tex._GetResourcePtr()->SetName("Depth reduction final csm splits tex");
		m_csm_splits_uav = context.CreateUav(csm_splits_tex, formatCSMSplits, uavDesc);
		m_csm_splits_uav.GetResource()._GetResourcePtr()->SetName("Depth reduction final csm splits UAV");
	}
}


} // namespace inl::gxeng::nodes
