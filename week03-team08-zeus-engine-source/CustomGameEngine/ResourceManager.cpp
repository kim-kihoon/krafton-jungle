#include "ResourceManager.h"
#include "VertexSimple.h"
#include "VertexFont.h"
#include "DirectXTex.h"

void ResourceManager::AddCubeMesh(ID3D11Device* Device)
{
	TArray<FVertexSimple> cubeVertices = CreateCubeVertices();
	TArray<uint32> cubeIndices = CreateCubeIndices();

	for (auto& v : cubeVertices)
	{
		v.r = v.x + 0.5f;
		v.g = v.y + 0.5f;
		v.b = v.z + 0.5f;
		v.a = 1.0f;
	}

	CreateGeometry(Device, "Cube", cubeVertices, cubeIndices);
}

void ResourceManager::AddSphereMesh(ID3D11Device* Device)
{
	TArray<FVertexSimple> sphereVertices = CreateSphereVertices();
	TArray<uint32> sphereIndices = CreateSphereIndices();

	CreateGeometry(Device, "Sphere", sphereVertices, sphereIndices);
}

void ResourceManager::AddTriangleMesh(ID3D11Device* Device)
{
	TArray<FVertexSimple> triangleVertices =
	{
		{  0.0f,  0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f }, // Top vertex (red)
		{  0.0f,  1.0f, -1.0f,  0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f }, // Bottom-right vertex (green)
		{  0.0f, -1.0f, -1.0f,  0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f }  // Bottom-left vertex (blue)
	};
	TArray<uint32> triangleIndices = { 0, 1, 2 };

	CreateGeometry(Device, "Triangle", triangleVertices, triangleIndices);
}

void ResourceManager::AddPlaneMesh(ID3D11Device* Device)
{
	TArray<FVertexSimple> planeVertices =
	{
		{ -5.0f, -5.0f, 0.0f,  81 / 255.f,  91 / 255.f, 212 / 255.f, 1.f, 0.0f, 0.0f, 1.0f }, // 0번: 뒤-왼쪽 (Back-Left)
		{  5.0f, -5.0f, 0.0f, 129 / 255.f,  52 / 255.f, 175 / 255.f, 1.f, 0.0f, 0.0f, 1.0f }, // 1번: 앞-왼쪽 (Front-Left)
		{  5.0f,  5.0f, 0.0f, 221 / 255.f,  42 / 255.f, 123 / 255.f, 1.f, 0.0f, 0.0f, 1.0f }, // 2번: 앞-오른쪽 (Front-Right)
		{ -5.0f,  5.0f, 0.0f, 254 / 255.f, 218 / 255.f, 119 / 255.f, 1.f, 0.0f, 0.0f, 1.0f }  // 3번: 뒤-오른쪽 (Back-Right)
	};

	TArray<uint32> planeIndices =
	{
		0, 1, 2, // 첫 번째 삼각형 (뒤왼 -> 앞왼 -> 앞오)
		0, 2, 3  // 두 번째 삼각형 (뒤왼 -> 앞오 -> 뒤오)
	};

	CreateGeometry(Device, "Plane", planeVertices, planeIndices);
}

void ResourceManager::AddGizmoTranslationMesh(ID3D11Device* Device)
{
	{
		TArray<FVertexSimple> vertices;
		TArray<uint32> indices;

		TArray<FVertexSimple> cylinderVertices = CreateCylinderVertices();
		TArray<uint32> cylinderIndices = CreateCylinderIndices();

		FMatrix cylinderScale = FMatrix::ScaleMatrix(0.05f, 0.05f, 1.0f);
		FMatrix cylinderMatrix = cylinderScale;

		for (FVertexSimple& v : cylinderVertices)
		{
			float px = v.x;
			float py = v.y;
			float pz = v.z;

			v.x = px * cylinderMatrix[0][0] + py * cylinderMatrix[1][0] + pz * cylinderMatrix[2][0] + 1.0f * cylinderMatrix[3][0];
			v.y = px * cylinderMatrix[0][1] + py * cylinderMatrix[1][1] + pz * cylinderMatrix[2][1] + 1.0f * cylinderMatrix[3][1];
			v.z = px * cylinderMatrix[0][2] + py * cylinderMatrix[1][2] + pz * cylinderMatrix[2][2] + 1.0f * cylinderMatrix[3][2];
		}
		vertices.insert(vertices.end(), cylinderVertices.begin(), cylinderVertices.end());
		indices.insert(indices.end(), cylinderIndices.begin(), cylinderIndices.end());

		uint32 indexOffset = static_cast<uint32>(vertices.size());

		TArray<FVertexSimple> coneVertices = CreateConeVertices();
		TArray<uint32> coneIndices = CreateConeIndices();
		FMatrix coneTransform = FMatrix::TranslationMatrix(0.0f, 0.0f, 0.5f);
		FMatrix coneScale = FMatrix::ScaleMatrix(0.2f, 0.2f, 0.2f);
		FMatrix coneMatrix = coneScale * coneTransform;

		for (FVertexSimple& v : coneVertices)
		{
			float px = v.x;
			float py = v.y;
			float pz = v.z;

			v.x = px * coneMatrix[0][0] + py * coneMatrix[1][0] + pz * coneMatrix[2][0] + 1.0f * coneMatrix[3][0];
			v.y = px * coneMatrix[0][1] + py * coneMatrix[1][1] + pz * coneMatrix[2][1] + 1.0f * coneMatrix[3][1];
			v.z = px * coneMatrix[0][2] + py * coneMatrix[1][2] + pz * coneMatrix[2][2] + 1.0f * coneMatrix[3][2];
		}
		for (uint32& index : coneIndices)
		{
			index += indexOffset; // 인덱스 오프셋 적용
		}

		vertices.insert(vertices.end(), coneVertices.begin(), coneVertices.end());
		indices.insert(indices.end(), coneIndices.begin(), coneIndices.end());

		CreateGeometry(Device, "GizmoTranslation", vertices, indices);
	}
	{
		TArray<FVertexSimple> vertices;
		TArray<uint32> indices;
		uint32 indexOffset;

		{
			TArray<FVertexSimple> cylinderVertices = CreateCylinderVertices();
			TArray<uint32> cylinderIndices = CreateCylinderIndices();

			FMatrix cylinderScale = FMatrix::ScaleMatrix(0.05f, 0.05f, 0.3f);
			FMatrix cylinderRotation = FMatrix::RotationYMatrix(90.0f);
			FMatrix cylinderTranslation = FMatrix::TranslationMatrix(0.15f, 0.0f, 0.3f);
			FMatrix cylinderMatrix = cylinderScale * cylinderRotation * cylinderTranslation;

			for (FVertexSimple& v : cylinderVertices)
			{
				float px = v.x;
				float py = v.y;
				float pz = v.z;

				v.x = px * cylinderMatrix[0][0] + py * cylinderMatrix[1][0] + pz * cylinderMatrix[2][0] + 1.0f * cylinderMatrix[3][0];
				v.y = px * cylinderMatrix[0][1] + py * cylinderMatrix[1][1] + pz * cylinderMatrix[2][1] + 1.0f * cylinderMatrix[3][1];
				v.z = px * cylinderMatrix[0][2] + py * cylinderMatrix[1][2] + pz * cylinderMatrix[2][2] + 1.0f * cylinderMatrix[3][2];
			}
			vertices.insert(vertices.end(), cylinderVertices.begin(), cylinderVertices.end());
			indices.insert(indices.end(), cylinderIndices.begin(), cylinderIndices.end());

			indexOffset = static_cast<uint32>(vertices.size());
		}
		{
			TArray<FVertexSimple> cylinderVertices = CreateCylinderVertices();
			TArray<uint32> cylinderIndices = CreateCylinderIndices();

			FMatrix cylinderScale = FMatrix::ScaleMatrix(0.05f, 0.05f, 0.3f);
			FMatrix cylinderTranslation = FMatrix::TranslationMatrix(0.3f, 0.0f, 0.15f);
			FMatrix cylinderMatrix = cylinderScale * cylinderTranslation;

			for (FVertexSimple& v : cylinderVertices)
			{
				float px = v.x;
				float py = v.y;
				float pz = v.z;

				v.x = px * cylinderMatrix[0][0] + py * cylinderMatrix[1][0] + pz * cylinderMatrix[2][0] + 1.0f * cylinderMatrix[3][0];
				v.y = px * cylinderMatrix[0][1] + py * cylinderMatrix[1][1] + pz * cylinderMatrix[2][1] + 1.0f * cylinderMatrix[3][1];
				v.z = px * cylinderMatrix[0][2] + py * cylinderMatrix[1][2] + pz * cylinderMatrix[2][2] + 1.0f * cylinderMatrix[3][2];
			}
			for (uint32& index : cylinderIndices)
			{
				index += indexOffset; // 인덱스 오프셋 적용
			}

			vertices.insert(vertices.end(), cylinderVertices.begin(), cylinderVertices.end());
			indices.insert(indices.end(), cylinderIndices.begin(), cylinderIndices.end());
		}

		CreateGeometry(Device, "GizmoTranslationSquare", vertices, indices);
	}
	{
		TArray<FVertexSimple> cubeVertices = CreateCubeVertices();
		TArray<uint32> cubeIndices = CreateCubeIndices();

		FMatrix cubeScale = FMatrix::ScaleMatrix(0.1f, 0.1f, 0.1f);
		FMatrix cylinderMatrix = cubeScale;

		for (FVertexSimple& v : cubeVertices)
		{
			float px = v.x;
			float py = v.y;
			float pz = v.z;

			v.x = px * cylinderMatrix[0][0] + py * cylinderMatrix[1][0] + pz * cylinderMatrix[2][0] + 1.0f * cylinderMatrix[3][0];
			v.y = px * cylinderMatrix[0][1] + py * cylinderMatrix[1][1] + pz * cylinderMatrix[2][1] + 1.0f * cylinderMatrix[3][1];
			v.z = px * cylinderMatrix[0][2] + py * cylinderMatrix[1][2] + pz * cylinderMatrix[2][2] + 1.0f * cylinderMatrix[3][2];
		}

		CreateGeometry(Device, "GizmoTranslationBox", cubeVertices, cubeIndices);
	}
}

void ResourceManager::AddGizmoRotationMesh(ID3D11Device* Device)
{
	{
		TArray<FVertexSimple> vertices = CreateTorusVertices();
		TArray<uint32> indices = CreateTorusIndices();

		FMatrix torusScale = FMatrix::ScaleMatrix(1.5f, 1.5f, 1.5f);
		FMatrix torusMatrix = torusScale;

		for (FVertexSimple& v : vertices)
		{
			float px = v.x;
			float py = v.y;
			float pz = v.z;

			v.x = px * torusMatrix[0][0] + py * torusMatrix[1][0] + pz * torusMatrix[2][0] + 1.0f * torusMatrix[3][0];
			v.y = px * torusMatrix[0][1] + py * torusMatrix[1][1] + pz * torusMatrix[2][1] + 1.0f * torusMatrix[3][1];
			v.z = px * torusMatrix[0][2] + py * torusMatrix[1][2] + pz * torusMatrix[2][2] + 1.0f * torusMatrix[3][2];
		}

		CreateGeometry(Device, "GizmoRotation", vertices, indices);
	}
}

void ResourceManager::AddGizmoScaleMesh(ID3D11Device* Device)
{
	{
		TArray<FVertexSimple> vertices;
		TArray<uint32> indices;

		TArray<FVertexSimple> cylinderVertices = CreateCylinderVertices();
		TArray<uint32> cylinderIndices = CreateCylinderIndices();
		FMatrix cylinderScale = FMatrix::ScaleMatrix(0.05f, 0.05f, 1.0f);
		FMatrix cylinderMatrix = cylinderScale;

		for (FVertexSimple& v : cylinderVertices)
		{
			float px = v.x;
			float py = v.y;
			float pz = v.z;

			v.x = px * cylinderMatrix[0][0] + py * cylinderMatrix[1][0] + pz * cylinderMatrix[2][0] + 1.0f * cylinderMatrix[3][0];
			v.y = px * cylinderMatrix[0][1] + py * cylinderMatrix[1][1] + pz * cylinderMatrix[2][1] + 1.0f * cylinderMatrix[3][1];
			v.z = px * cylinderMatrix[0][2] + py * cylinderMatrix[1][2] + pz * cylinderMatrix[2][2] + 1.0f * cylinderMatrix[3][2];
		}
		vertices.insert(vertices.end(), cylinderVertices.begin(), cylinderVertices.end());
		indices.insert(indices.end(), cylinderIndices.begin(), cylinderIndices.end());

		uint32 indexOffset = static_cast<uint32>(vertices.size());

		TArray<FVertexSimple> cubeVertices = CreateCubeVertices();
		TArray<uint32> cubeIndices = CreateCubeIndices();
		FMatrix cubeTransform = FMatrix::TranslationMatrix(0.0f, 0.0f, 0.5f);
		FMatrix cubeScale = FMatrix::ScaleMatrix(0.2f, 0.2f, 0.2f);
		FMatrix cubeMatrix = cubeScale * cubeTransform;

		for (FVertexSimple& v : cubeVertices)
		{
			float px = v.x;
			float py = v.y;
			float pz = v.z;

			v.x = px * cubeMatrix[0][0] + py * cubeMatrix[1][0] + pz * cubeMatrix[2][0] + 1.0f * cubeMatrix[3][0];
			v.y = px * cubeMatrix[0][1] + py * cubeMatrix[1][1] + pz * cubeMatrix[2][1] + 1.0f * cubeMatrix[3][1];
			v.z = px * cubeMatrix[0][2] + py * cubeMatrix[1][2] + pz * cubeMatrix[2][2] + 1.0f * cubeMatrix[3][2];
		}
		for (uint32& index : cubeIndices)
		{
			index += indexOffset; // 인덱스 오프셋 적용
		}

		vertices.insert(vertices.end(), cubeVertices.begin(), cubeVertices.end());
		indices.insert(indices.end(), cubeIndices.begin(), cubeIndices.end());

		CreateGeometry(Device, "GizmoScale", vertices, indices);
	}
	{
		TArray<FVertexSimple> vertices = CreateCylinderVertices();
		TArray<uint32> indices = CreateCylinderIndices();
		FMatrix cylinderScale = FMatrix::ScaleMatrix(0.05f, 0.05f, 0.75f);
		FMatrix cylinderRotation = FMatrix::EulerRotationMatrix(0.0f, -45.0f, 0.0f);
		FMatrix cylinderTranslation = FMatrix::TranslationMatrix(0.25f, 0.0f, 0.25f);
		FMatrix cylinderMatrix = cylinderScale * cylinderRotation * cylinderTranslation;
		for (FVertexSimple& v : vertices)
		{
			float px = v.x;
			float py = v.y;
			float pz = v.z;
			v.x = px * cylinderMatrix[0][0] + py * cylinderMatrix[1][0] + pz * cylinderMatrix[2][0] + 1.0f * cylinderMatrix[3][0];
			v.y = px * cylinderMatrix[0][1] + py * cylinderMatrix[1][1] + pz * cylinderMatrix[2][1] + 1.0f * cylinderMatrix[3][1];
			v.z = px * cylinderMatrix[0][2] + py * cylinderMatrix[1][2] + pz * cylinderMatrix[2][2] + 1.0f * cylinderMatrix[3][2];
		}
		CreateGeometry(Device, "GizmoScaleLine", vertices, indices);
	}
	{
		TArray<FVertexSimple> vertices = CreateCubeVertices();
		TArray<uint32> indices = CreateCubeIndices();
		FMatrix cubeScale = FMatrix::ScaleMatrix(0.1f, 0.1f, 0.1f);
		FMatrix cubeMatrix = cubeScale;
		for (FVertexSimple& v : vertices)
		{
			float px = v.x;
			float py = v.y;
			float pz = v.z;
			v.x = px * cubeMatrix[0][0] + py * cubeMatrix[1][0] + pz * cubeMatrix[2][0] + 1.0f * cubeMatrix[3][0];
			v.y = px * cubeMatrix[0][1] + py * cubeMatrix[1][1] + pz * cubeMatrix[2][1] + 1.0f * cubeMatrix[3][1];
			v.z = px * cubeMatrix[0][2] + py * cubeMatrix[1][2] + pz * cubeMatrix[2][2] + 1.0f * cubeMatrix[3][2];
		}
		CreateGeometry(Device, "GizmoScaleBox", vertices, indices);
	}
}

void ResourceManager::AddLineMesh(ID3D11Device* Device)
{
	CreateBatchGeometry<FVertexSimple>(Device, "Line", 1000, 1000); // TODO: 적절한 값 넣기
}

void ResourceManager::AddTextBatchMesh(ID3D11Device* Device)
{
	CreateBatchGeometry<FVertexFont>(Device, "TextBatch", 16384 * 4, 16384 * 6);
}

void ResourceManager::AddParticleSubUVMesh(ID3D11Device* Device)
{
	TArray<FVertexTex> planeVertices =
	{
		{ -0.5f, -0.5f, 0.0f,  0.0f, 1.0f },  // UV (0,1)
		{  0.5f, -0.5f, 0.0f,  1.0f, 1.0f },  // UV (1,1)
		{  0.5f,  0.5f, 0.0f,  1.0f, 0.0f },  // UV (1,0)
		{ -0.5f,  0.5f, 0.0f,  0.0f, 0.0f },  // UV (0,0)
	};

	TArray<uint32> planeIndices =
	{
		0, 1, 2, // 첫 번째 삼각형 (뒤왼 -> 앞왼 -> 앞오)
		0, 2, 3  // 두 번째 삼각형 (뒤왼 -> 앞오 -> 뒤오)
	};

	CreateGeometry(Device, "ParticleSubUV", planeVertices, planeIndices);
}

TArray<FVertexSimple> ResourceManager::CreateCubeVertices()
{
	TArray<FVertexSimple> v;

	// 1. +X 면 (앞쪽) - 법선: (1, 0, 0)
	v.push_back({ 0.5f, -0.5f,  0.5f,  1,1,1,1,  1, 0, 0 }); // 0: Top-Left
	v.push_back({ 0.5f,  0.5f,  0.5f,  1,1,1,1,  1, 0, 0 }); // 1: Top-Right
	v.push_back({ 0.5f,  0.5f, -0.5f,  1,1,1,1,  1, 0, 0 }); // 2: Bottom-Right
	v.push_back({ 0.5f, -0.5f, -0.5f,  1,1,1,1,  1, 0, 0 }); // 3: Bottom-Left

	// 2. -X 면 (뒤쪽) - 법선: (-1, 0, 0)
	v.push_back({ -0.5f,  0.5f,  0.5f,  1,1,1,1, -1, 0, 0 }); // 4
	v.push_back({ -0.5f, -0.5f,  0.5f,  1,1,1,1, -1, 0, 0 }); // 5
	v.push_back({ -0.5f, -0.5f, -0.5f,  1,1,1,1, -1, 0, 0 }); // 6
	v.push_back({ -0.5f,  0.5f, -0.5f,  1,1,1,1, -1, 0, 0 }); // 7

	// 3. +Y 면 (우측) - 법선: (0, 1, 0)
	v.push_back({ 0.5f,  0.5f,  0.5f,  1,1,1,1,  0, 1, 0 }); // 8
	v.push_back({ -0.5f,  0.5f,  0.5f,  1,1,1,1,  0, 1, 0 }); // 9
	v.push_back({ -0.5f,  0.5f, -0.5f,  1,1,1,1,  0, 1, 0 }); // 10
	v.push_back({ 0.5f,  0.5f, -0.5f,  1,1,1,1,  0, 1, 0 }); // 11

	// 4. -Y 면 (좌측) - 법선: (0, -1, 0)
	v.push_back({ -0.5f, -0.5f,  0.5f,  1,1,1,1,  0,-1, 0 }); // 12
	v.push_back({ 0.5f, -0.5f,  0.5f,  1,1,1,1,  0,-1, 0 }); // 13
	v.push_back({ 0.5f, -0.5f, -0.5f,  1,1,1,1,  0,-1, 0 }); // 14
	v.push_back({ -0.5f, -0.5f, -0.5f,  1,1,1,1,  0,-1, 0 }); // 15

	// 5. + Z 면(상단) - 정점 순서를 시계 방향(CW)으로 재정렬합니다.
		v.push_back({ 0.5f,  0.5f,  0.5f,  1,1,1,1,  0, 0, 1 }); // 16: Front-Right-Top
	v.push_back({ 0.5f, -0.5f,  0.5f,  1,1,1,1,  0, 0, 1 }); // 17: Front-Left-Top
	v.push_back({ -0.5f, -0.5f,  0.5f,  1,1,1,1,  0, 0, 1 }); // 18: Back-Left-Top
	v.push_back({ -0.5f,  0.5f,  0.5f,  1,1,1,1,  0, 0, 1 }); // 19: Back-Right-Top

	// 6. -Z 면 (하단) - 정점 순서를 재정렬하여 방향성을 맞춥니다.
	v.push_back({ -0.5f, -0.5f, -0.5f,  1,1,1,1,  0, 0, -1 }); // 20: Back-Left-Bottom
	v.push_back({ 0.5f, -0.5f, -0.5f,  1,1,1,1,  0, 0, -1 }); // 21: Front-Left-Bottom
	v.push_back({ 0.5f,  0.5f, -0.5f,  1,1,1,1,  0, 0, -1 }); // 22: Front-Right-Bottom
	v.push_back({ -0.5f,  0.5f, -0.5f,  1,1,1,1,  0, 0, -1 }); // 23: Back-Right-Bottom

	return v;
}
TArray<uint32> ResourceManager::CreateCubeIndices()
{
	TArray<uint32> idx;
	for (int i = 0; i < 6; ++i)
	{
		int offset = i * 4;
		// 순서를 0, 2, 1 / 0, 3, 2 로 바꿔서 '앞면'이 밖을 보게 합니다.
		idx.push_back(offset + 0);
		idx.push_back(offset + 2);
		idx.push_back(offset + 1);

		idx.push_back(offset + 0);
		idx.push_back(offset + 3);
		idx.push_back(offset + 2);
	}
	return idx;
}

TArray<FVertexSimple> ResourceManager::CreateSphereVertices(float radius, int sectorCount, int stackCount)
{
	TArray<FVertexSimple> sphereVertices;
	for (int i = 0; i <= stackCount; ++i)
	{
		float stackAngle = MathHelper::PI / 2 - i * MathHelper::PI / stackCount; // from pi/2 to -pi/2
		float xy = radius * cosf(stackAngle); // r * cos(u)
		float z = radius * sinf(stackAngle);  // r * sin(u)
		for (int j = 0; j <= sectorCount; ++j)
		{
			float sectorAngle = j * 2 * MathHelper::PI / sectorCount; // from 0 to 2pi
			FVertexSimple vertex;
			vertex.x = xy * cosf(sectorAngle); // r * cos(u) * cos(v)
			vertex.y = xy * sinf(sectorAngle); // r * cos(u) * sin(v)
			vertex.z = z;                       // r * sin(u)
			// 법선 벡터 계산
			FVector normal(vertex.x, vertex.y, vertex.z);
			normal.Normalize();
			vertex.nx = normal.x;
			vertex.ny = normal.y;
			vertex.nz = normal.z;
			// 색상 (흰색)
			vertex.r = 1.0f;
			vertex.g = 1.0f;
			vertex.b = 1.0f;
			vertex.a = 1.0f;
			sphereVertices.push_back(vertex);
		}
	}
	return sphereVertices;
}
TArray<uint32> ResourceManager::CreateSphereIndices(int sectorCount, int stackCount)
{
	TArray<uint32> sphereIndices;
	for (int i = 0; i < stackCount; ++i)
	{
		int k1 = i * (sectorCount + 1); // beginning of current stack
		int k2 = k1 + sectorCount + 1;   // beginning of next stack
		for (int j = 0; j < sectorCount; ++j, ++k1, ++k2)
		{
			if (i != 0)
			{
				sphereIndices.push_back(k1);
				sphereIndices.push_back(k2);
				sphereIndices.push_back(k1 + 1);
			}
			if (i != (stackCount - 1))
			{
				sphereIndices.push_back(k1 + 1);
				sphereIndices.push_back(k2);
				sphereIndices.push_back(k2 + 1);
			}
		}
	}
	return sphereIndices;
}

TArray<FVertexSimple> ResourceManager::CreateTorusVertices(float majorRadius, float minorRadius, int radialSegments, int tubularSegments)
{
	TArray<FVertexSimple> torusVertices;

	for (int i = 0; i <= radialSegments; ++i)
	{
		// u: 메인 고리의 회전 각도 (0 ~ 360도 전체 회전을 위해 2.0 * PI 사용)
		float u = (float)i / radialSegments * 0.5f * MathHelper::PI;

		for (int j = 0; j <= tubularSegments; ++j)
		{
			// v: 파이프 단면의 회전 각도 (0 ~ 360도)
			float v = (float)j / tubularSegments * 2.0f * MathHelper::PI;

			// 1. XY 평면에 누운 형태를 위한 좌표 계산
			// Major 원이 XY 평면에서 회전하고, Minor 단면이 Z축 방향으로 두께를 가집니다.
			float cosU = cos(u);
			float sinU = sin(u);
			float cosV = cos(v);
			float sinV = sin(v);

			// World X, Y: (큰 반지름 + 작은 반지름의 수평 성분) * 회전 방향
			// World Z: 작은 반지름의 수직 성분 (높이)
			float x = (majorRadius + minorRadius * cosV) * cosU;
			float y = (majorRadius + minorRadius * cosV) * sinU;
			float z = minorRadius * sinV * 0.2f;

			// 2. 법선(Normal) 계산
			// 단면의 중심(Center)은 XY 평면상에 존재합니다.
			FVector Center(majorRadius * cosU, majorRadius * sinU, 0.0f);
			FVector Pos(x, y, z);
			FVector Normal = (Pos - Center).GetSafeNormal();

			FVertexSimple Vertex;
			Vertex.x = x;
			Vertex.y = y;
			Vertex.z = z;

			// 색상 (흰색)
			Vertex.r = 1.0f; Vertex.g = 1.0f; Vertex.b = 1.0f; Vertex.a = 1.0f;

			// 법선 대입
			Vertex.nx = Normal.x;
			Vertex.ny = Normal.y;
			Vertex.nz = Normal.z;

			torusVertices.push_back(Vertex); // Unreal TArray는 Add를 사용합니다.
		}
	}

	return torusVertices;
}
TArray<uint32> ResourceManager::CreateTorusIndices(int radialSegments, int tubularSegments)
{
	TArray<uint32> torusIndices;
	// 삼각형 인덱스 생성
	for (int i = 0; i < radialSegments; ++i)
	{
		for (int j = 0; j < tubularSegments; ++j)
		{
			int a = (i * (tubularSegments + 1)) + j;
			int b = ((i + 1) * (tubularSegments + 1)) + j;
			int c = ((i + 1) * (tubularSegments + 1)) + (j + 1);
			int d = (i * (tubularSegments + 1)) + (j + 1);
			torusIndices.push_back(a);
			torusIndices.push_back(b);
			torusIndices.push_back(d);
			torusIndices.push_back(b);
			torusIndices.push_back(c);
			torusIndices.push_back(d);
		}
	}
	return torusIndices;
}

TArray<FVertexSimple> ResourceManager::CreateCylinderVertices(int sliceCount, float radius, float height)
{
	TArray<FVertexSimple> cylinderVertices;
	float halfHeight = height * 0.5f;
	float dTheta = 2.0f * MathHelper::PI / sliceCount;

	// UE 변환 헬퍼 (Y-up 기준 데이터를 Unreal Z-up으로 변환)
	auto addVertex = [&](float oldX, float oldY, float oldZ) {
		FVector Normal = FVector(oldX, 0.0f, oldZ).GetSafeNormal();
		
		FVertexSimple v;
		v.x = oldZ; v.y = oldX; v.z = oldY;
		v.r = 1.0f; v.g = 1.0f; v.b = 1.0f; v.a = 1.0f;

		// 법선 벡터 대입 (UE 축 변환 적용)
		v.nx = Normal.z;
		v.ny = Normal.x;
		v.nz = 0.0f;

		cylinderVertices.push_back(v);
		};

	// 1. 하단 원형 정점들 (Index: 0 ~ sliceCount-1)
	for (int i = 0; i < sliceCount; ++i) {
		float theta = i * dTheta;
		addVertex(radius * cosf(theta), -halfHeight, radius * sinf(theta));
	}

	// 2. 상단 원형 정점들 (Index: sliceCount ~ 2*sliceCount-1)
	for (int i = 0; i < sliceCount; ++i) {
		float theta = i * dTheta;
		addVertex(radius * cosf(theta), halfHeight, radius * sinf(theta));
	}

	// 3. 하단 중심점 (Index: 2 * sliceCount)
	addVertex(0.0f, -halfHeight, 0.0f);

	// 4. 상단 중심점 (Index: 2 * sliceCount + 1)
	addVertex(0.0f, halfHeight, 0.0f);

	return cylinderVertices;
}
TArray<uint32> ResourceManager::CreateCylinderIndices(int sliceCount)
{
	TArray<uint32> cylinderIndices;

	// 인덱스 편의를 위한 오프셋
	int bottomCapCenter = 2 * sliceCount;
	int topCapCenter = 2 * sliceCount + 1;

	for (int i = 0; i < sliceCount; ++i)
	{
		int next = (i + 1) % sliceCount;

		// [측면] 사각형 하나당 삼각형 2개 (CW Winding)
		// 하단i, 상단i, 상단next
		cylinderIndices.push_back(i);
		cylinderIndices.push_back(i + sliceCount);
		cylinderIndices.push_back(next + sliceCount);

		// 하단i, 상단next, 하단next
		cylinderIndices.push_back(i);
		cylinderIndices.push_back(next + sliceCount);
		cylinderIndices.push_back(next);

		// [상단 뚜껑] (중심, 현재, 다음)
		cylinderIndices.push_back(topCapCenter);
		cylinderIndices.push_back(i + sliceCount);
		cylinderIndices.push_back(next + sliceCount);

		// [하단 뚜껑] (중심, 다음, 현재) - 아래를 바라보므로 순서 반대
		cylinderIndices.push_back(bottomCapCenter);
		cylinderIndices.push_back(next);
		cylinderIndices.push_back(i);
	}

	return cylinderIndices;
}

TArray<FVertexSimple> ResourceManager::CreateConeVertices(int sliceCount, float radius, float height)
{
	TArray<FVertexSimple> coneVertices;

	float halfHeight = height * 0.5f;
	float dTheta = 2.0f * MathHelper::PI / sliceCount;

	// UE변환 헬퍼 (oldX -> Y, oldZ -> X, oldY -> Z)
	auto pushCone = [&](float oldX, float oldY, float oldZ) {
		FVector Normal = FVector(oldX, oldY + halfHeight, oldZ).GetSafeNormal();
		FVertexSimple v;
		// 위치 변환 (Z-up)
		v.x = oldZ;
		v.y = oldX;
		v.z = oldY;

		v.r = 1.0f; v.g = 1.0f; v.b = 1.0f; v.a = 1.0f;

		// 법선 변환 (Z-up 축 정렬)
		v.nx = Normal.z;
		v.ny = Normal.x;
		v.nz = Normal.y; // 0.0f 대신 실제 높이 방향인 y값을 넣어줍니다.

		coneVertices.push_back(v);
		};

	// 측면
	for (int i = 0; i < sliceCount; ++i)
	{
		float theta0 = i * dTheta;
		float theta1 = (i + 1) * dTheta;

		float oldx0 = radius * cosf(theta0);
		float oldz0 = radius * sinf(theta0);
		float oldx1 = radius * cosf(theta1);
		float oldz1 = radius * sinf(theta1);

		pushCone(oldx0, -halfHeight, oldz0);
		pushCone(0.0f, halfHeight, 0.0f);
		pushCone(oldx1, -halfHeight, oldz1);
	}

	// 밑면
	for (int i = 0; i < sliceCount; ++i)
	{
		float theta0 = i * dTheta;
		float theta1 = (i + 1) * dTheta;

		float oldx0 = radius * cosf(theta0);
		float oldz0 = radius * sinf(theta0);
		float oldx1 = radius * cosf(theta1);
		float oldz1 = radius * sinf(theta1);

		pushCone(0.0f, -halfHeight, 0.0f);
		pushCone(oldx0, -halfHeight, oldz0);
		pushCone(oldx1, -halfHeight, oldz1);
	}

	return coneVertices;
}
TArray<uint32> ResourceManager::CreateConeIndices(int sliceCount)
{
	TArray<uint32> coneIndices;
	// 측면
	for (int i = 0; i < sliceCount; ++i)
	{
		int baseIndex = i * 3;
		coneIndices.push_back(baseIndex);     // 밑면 점
		coneIndices.push_back(baseIndex + 1); // 꼭짓점
		coneIndices.push_back(baseIndex + 2); // 다음 밑면 점
	}
	// 밑면
	int offset = sliceCount * 3; // 측면 정점 수
	for (int i = 0; i < sliceCount; ++i)
	{
		int baseIndex = offset + i * 3;
		coneIndices.push_back(baseIndex);     // 중심점
		coneIndices.push_back(baseIndex + 1); // 현재 밑면 점
		coneIndices.push_back(baseIndex + 2); // 다음 밑면 점
	}
	return coneIndices;
}

ID3D11ShaderResourceView* ResourceManager::CreateFallbackSRV()
{
	D3D11_TEXTURE2D_DESC TexDesc = {};
	TexDesc.Width = 1;
	TexDesc.Height = 1;
	TexDesc.MipLevels = 1;
	TexDesc.ArraySize = 1;
	TexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TexDesc.SampleDesc.Count = 1;
	TexDesc.Usage = D3D11_USAGE_IMMUTABLE;
	TexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	uint32_t pixel = 0x00000000;
	D3D11_SUBRESOURCE_DATA InitData = {};
	InitData.pSysMem = &pixel;
	InitData.SysMemPitch = sizeof(uint32_t);

	ID3D11Texture2D* Tex = nullptr;
	if (FAILED(Device->CreateTexture2D(&TexDesc, &InitData, &Tex)))
		return nullptr;

	ID3D11ShaderResourceView* FallbackSRV = nullptr;
	Device->CreateShaderResourceView(Tex, nullptr, &FallbackSRV);
	Tex->Release();
	return FallbackSRV;
}

TComPtr<ID3D11ShaderResourceView> ResourceManager::LoadTextureSRV(const wchar_t* path)
{
	if (!path || path[0] == L'\0')
		return nullptr;

	auto it = TextureCache.find(path);
	if (it != TextureCache.end())
		return it->second;

	DirectX::TexMetadata Metadata;
	DirectX::ScratchImage Image;
	ID3D11ShaderResourceView* SRV = nullptr;

	HRESULT hr;
	if (std::wcsstr(path, L".dds") || std::wcsstr(path, L".DDS"))
	{
		hr = DirectX::LoadFromDDSFile(path, DirectX::DDS_FLAGS_NONE, &Metadata, Image);
	}
	else
	{
		hr = DirectX::LoadFromWICFile(path, DirectX::WIC_FLAGS_NONE, &Metadata, Image);
	}

	if (FAILED(hr))
	{
		UE_LOG("Fail to load image (HRESULT: %08X): %ls", hr, path);
		return CreateFallbackSRV();
	}

	hr = DirectX::CreateShaderResourceView(Device, Image.GetImages(), Image.GetImageCount(), Metadata, &SRV);

	if (FAILED(hr))
	{
		UE_LOG("Fail to create SRV for: %ls", path);
		return CreateFallbackSRV();
	}

	TextureCache[path] = SRV;

	return TextureCache[path];
}
