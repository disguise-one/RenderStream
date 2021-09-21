struct PixelInputType
{
    float4 position : SV_POSITION;
	float4 col : COLOR;
};

float4 main(PixelInputType input) : SV_TARGET
{
	return float4(input.col.xyz, 1.0);
}
