struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VS_OUTPUT VSMain(uint id : SV_VertexID) {
    VS_OUTPUT output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.pos = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

Texture2D sourceTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 PSMain(VS_OUTPUT input) : SV_Target {
    return sourceTexture.Sample(linearSampler, input.uv);
}

float4 PSMainGray(VS_OUTPUT input) : SV_Target {
    float4 col = sourceTexture.Sample(linearSampler, input.uv);
    float gray = dot(col.rgb, float3(0.299f, 0.587f, 0.114f));
    return float4(gray, gray, gray, col.a);
}