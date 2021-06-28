cbuffer WorldViewProjectionConstantBuffer : register(b0)
{
    matrix worldViewProjMatrix;
};

float4 main( float4 pos : POSITION ) : SV_POSITION
{
	return mul(pos, worldViewProjMatrix);
}
