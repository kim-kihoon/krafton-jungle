#include "Transform.h"

FTransform::FTransform(const FMatrix& Mat)
{
	Location = Mat.GetLocation();
	Scale = Mat.GetScale();

	FMatrix RotationMatrix = Mat;
	if (Scale.X > 1.0e-6f)
	{
		RotationMatrix.M[0][0] /= Scale.X;
		RotationMatrix.M[0][1] /= Scale.X;
		RotationMatrix.M[0][2] /= Scale.X;
	}
	if (Scale.Y > 1.0e-6f)
	{
		RotationMatrix.M[1][0] /= Scale.Y;
		RotationMatrix.M[1][1] /= Scale.Y;
		RotationMatrix.M[1][2] /= Scale.Y;
	}
	if (Scale.Z > 1.0e-6f)
	{
		RotationMatrix.M[2][0] /= Scale.Z;
		RotationMatrix.M[2][1] /= Scale.Z;
		RotationMatrix.M[2][2] /= Scale.Z;
	}
	RotationMatrix.M[3][0] = 0.0f;
	RotationMatrix.M[3][1] = 0.0f;
	RotationMatrix.M[3][2] = 0.0f;
	RotationMatrix.M[3][3] = 1.0f;

	Rotation = RotationMatrix.ToQuat();
}

FMatrix FTransform::ToMatrix() const
{
	FMatrix translateMatrix = FMatrix::MakeTranslationMatrix(Location);

	FMatrix rotationMatrix = Rotation.ToMatrix();

	FMatrix scaleMatrix = FMatrix::MakeScaleMatrix(Scale);

	return scaleMatrix * rotationMatrix * translateMatrix;
}
