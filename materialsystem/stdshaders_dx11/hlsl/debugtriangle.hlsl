// Debug triangle for the DX11 toolchain spike (migration M2).
// Compiled offline into shaders/dx11/debugtriangle_vs.vcsx / _ps.vcsx by
// devtools/build_shaders_dx11.py, and hot-reloadable from source at runtime.

cbuffer PerDraw : register( b0 )
{
	float4 g_TintColor;
};

struct VsInput
{
	float3 vPos : POSITION;
	float4 vColor : COLOR0;
};

struct VsOutput
{
	float4 vProjPos : SV_Position;
	float4 vColor : COLOR0;
};

VsOutput MainVs( VsInput i )
{
	VsOutput o;
	o.vProjPos = float4( i.vPos, 1.0 );
	o.vColor = i.vColor;
	return o;
}

float4 MainPs( VsOutput i ) : SV_Target0
{
	return i.vColor * g_TintColor;
}
