#include "PerspectiveCamera.hpp"


namespace inl::gxeng {


PerspectiveCamera::PerspectiveCamera() : 
	m_fovH(60.f / 180.f*3.14159f),
	m_fovV(45.f / 180.f*3.14159f)
{}


void PerspectiveCamera::SetFOVAspect(float horizontalFov, float aspectRatio) {
	m_fovH = horizontalFov;
	m_fovV = m_fovH / aspectRatio;
}
void PerspectiveCamera::SetFOVAxis(float horizontalFov, float verticalFov) {
	m_fovH = horizontalFov;
	m_fovV = verticalFov;
}


// Get rendering properties.
float PerspectiveCamera::GetFOVVertical() const {
	return m_fovV;
}
float PerspectiveCamera::GetFOVHorizontal() const {
	return m_fovH;
}
float PerspectiveCamera::GetAspectRatio() const {
	return m_fovH / m_fovV;
}


// Matrices
mathfu::Matrix4x4f PerspectiveCamera::GetViewMatrixRH() const {
	return mathfu::Matrix4x4f::LookAt(m_position + m_lookdir, m_position, m_upVector, +1.0f);
}
mathfu::Matrix4x4f PerspectiveCamera::GetViewMatrixLH() const {
	return mathfu::Matrix4x4f::LookAt(m_position + m_lookdir, m_position, m_upVector, -1.0f);
}
mathfu::Matrix4x4f PerspectiveCamera::GetProjectionMatrixRH() const {
	return mathfu::Matrix4x4f::Perspective(m_fovV, m_fovH / m_fovV, m_nearPlane, m_farPlane, +1.0f);
}
mathfu::Matrix4x4f PerspectiveCamera::GetProjectionMatrixLH() const {
	return mathfu::Matrix4x4f::Perspective(m_fovV, m_fovH / m_fovV, m_nearPlane, m_farPlane, -1.0f);
}



} // namespace inl::gxeng