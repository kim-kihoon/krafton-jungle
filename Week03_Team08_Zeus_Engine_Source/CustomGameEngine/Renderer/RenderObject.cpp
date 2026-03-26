#include "RenderObject.h"
#include "Geometry.h"
#include "Material.h"
#include "SubUV.h"
#include "EngineStatics.h"

bool RenderObject::Render(ID3D11DeviceContext* Context)
{
	if (!Material || !Geometry) return false;

	// RenderGrid에서 해제된 레이아웃을 다시 복구
	Context->IASetInputLayout(Material->InputLayout.Get());

	Material->BindShaders(Context);
	Geometry->BindBuffers(Context);

	if (SubUV) SubUV->Bind(Context);

	Context->IASetPrimitiveTopology(PrimitiveTopology);
	
	if (Geometry->IndexBuffer)
	{
		Context->DrawIndexed(Geometry->IndexCount, 0, 0);
	}
	else
	{
		Context->Draw(Geometry->VertexCount, 0);
	}

	UEngineStatics::TotalDrawCalls++;
	return true;
}
