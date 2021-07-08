cbuffer WorldViewProjectionConstantBuffer : register(b0)
{
    matrix worldViewProjMatrix;
};

struct Output
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

Output main(float4 pos : POSITION, float2 uv : TEXCOORD0)
{
    Output output;
    output.pos = mul(pos, worldViewProjMatrix);
    output.uv = uv;
    return output;
}
