#pragma once


#include <mathfu/vector.h>
#include <mathfu/vector_3.h>
#include <mathfu/quaternion.h>
#include <mathfu/matrix_4x4.h>

namespace inl::gxeng {


class Mesh;
class Material;
class Image;


class MeshEntity {
public:
	MeshEntity();

	void SetMesh(Mesh* mesh);
	Mesh* GetMesh() const;
	void SetMaterial(Material* material);
	Material* GetMaterial() const;

	void SetPosition(mathfu::Vector<float, 3> pos);
	void SetRotation(mathfu::Quaternion<float> rotation);
	void SetScale(mathfu::Vector<float, 3> scale);

	mathfu::Vector<float, 3> GetPosition() const;
	mathfu::Quaternion<float> GetRotation() const;
	mathfu::Vector<float, 3> GetScale() const;

	mathfu::Matrix<float, 4, 4> GetTransform() const;

private:
	Mesh* m_mesh;
	Material* m_material;
	mathfu::Vector<float, 3> m_position;
	mathfu::Quaternion<float> m_rotation;
	mathfu::Vector<float, 3> m_scale;
};



} // namespace inl::gxeng
