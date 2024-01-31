cbuffer ConstantBuffer : register(b0)
{
    matrix worldViewProjMatrix;
    float4 color;
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
    p.col = color;
    return p;
}
