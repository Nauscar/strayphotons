#version 450

#include "../lib/lighting_util.glsl"

layout(binding = 0) uniform sampler2D baseColorTex;
layout(binding = 1) uniform sampler2D metallicRoughnessTex;

layout(location = 0) in vec3 viewPos;
layout(location = 1) in vec3 viewNormal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(viewNormal);

    vec3 lightDir = -viewPos; // light is at view origin
    float distance = length(lightDir);
    distance = distance * distance;
    lightDir = normalize(lightDir);

    float diffusePower = max(dot(lightDir, normal), 0.0) * 4;
    vec3 viewDir = normalize(-viewPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float specAngle = max(dot(halfDir, normal), 0.0);
    float specPower = pow(specAngle, 16);

    vec4 baseColor = texture(baseColorTex, inTexCoord);
    if (baseColor.a < 0.5) discard;

    vec4 metallicRoughnessSample = texture(metallicRoughnessTex, inTexCoord);
    float roughness = metallicRoughnessSample.g;
    float metallic = metallicRoughnessSample.b;

    vec3 specColor = vec3(roughness);

    vec3 colorLinear = baseColor.rgb * diffusePower / distance + specColor * specPower / distance;
    outColor = vec4(colorLinear * 4, 1.0);
}
