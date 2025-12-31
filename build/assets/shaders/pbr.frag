#version 410 core
in VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} fs_in;

out vec4 FragColor;

struct Material {
    vec4 baseColorFactor;
    float metallic;
    float roughness;
    int hasBaseColorTex;
    int hasMetalRoughTex;
};

uniform Material uMaterial;
uniform sampler2D uBaseColorMap;
uniform sampler2D uMetalRoughMap;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uCameraPos;
uniform vec3 uAmbientColor;

// Simple PBR helpers
const float PI = 3.14159265359;

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom + 1e-5);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / (denom + 1e-5);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

void main() {
    vec3 N = normalize(fs_in.normal);
    vec3 V = normalize(uCameraPos - fs_in.worldPos);
    vec3 L = normalize(-uLightDir);
    vec3 H = normalize(V + L);

    // Base color
    vec4 baseColor = uMaterial.baseColorFactor;
    if (uMaterial.hasBaseColorTex == 1) {
        baseColor *= texture(uBaseColorMap, fs_in.uv);
    }
    float metallic = clamp(uMaterial.metallic, 0.0, 1.0);
    float roughness = clamp(uMaterial.roughness, 0.05, 1.0);
    if (uMaterial.hasMetalRoughTex == 1) {
        vec4 mrSample = texture(uMetalRoughMap, fs_in.uv);
        // glTF packs roughness in G, metallic in B
        roughness = clamp(mrSample.g, 0.05, 1.0);
        metallic = clamp(mrSample.b, 0.0, 1.0);
    }

    // Compute F0
    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);

    // Direct lighting
    float NdotL = max(dot(N, L), 0.0);
    vec3 radiance = uLightColor;

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denom       = 4.0 * max(dot(N, V), 0.0) * NdotL + 1e-5;
    vec3 specular     = numerator / denom;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    vec3 diffuse = kD * baseColor.rgb / PI;
    vec3 direct  = (diffuse + specular) * radiance * NdotL;

    // Simple ambient term (fallback if no IBL)
    vec3 ambient = baseColor.rgb * uAmbientColor;

    vec3 color = ambient + direct;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2)); // gamma

    FragColor = vec4(color, baseColor.a);
}

