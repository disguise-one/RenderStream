cbuffer WorldViewProjectionConstantBuffer : register(b0)
{
    matrix worldViewProjMatrix;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float4 col : COLOR;
};

PixelInputType main(float4 pos : POSITION)
{
    PixelInputType p;
    p.position = mul(pos, worldViewProjMatrix);
    p.col = pos + float4(0.5, 0.5, 0.5, 0.5);
    return p;
}
