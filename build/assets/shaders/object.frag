#version 410 core

in VS_OUT {
    vec3 normal;
    vec3 worldPos;
    vec2 uv;
} fs_in;

struct Material {
    vec3 diffuseColor;
    float shininess;
    int hasDiffuseMap;
};

uniform Material uMaterial;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbientColor;
uniform vec3 uCameraPos;
uniform sampler2D uDiffuseMap;

out vec4 FragColor;

void main() {
    vec3 N = normalize(fs_in.normal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uCameraPos - fs_in.worldPos);

    vec4 texColor = vec4(1.0);
    if (uMaterial.hasDiffuseMap == 1) {
        vec4 texRaw = texture(uDiffuseMap, fs_in.uv);
        // Convert from sRGB to linear space (textures are stored in sRGB)
        texColor.rgb = pow(texRaw.rgb, vec3(2.2));
        texColor.a = texRaw.a;
    }
    
    // Use texture color directly, only multiply by diffuse color if no texture
    vec3 albedo = uMaterial.hasDiffuseMap == 1 ? texColor.rgb : uMaterial.diffuseColor;

    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * albedo * uLightColor;

    vec3 specular = vec3(0.0);
    if (diff > 0.0) {
        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), uMaterial.shininess);
        specular = spec * uLightColor * 0.35;
    }

    vec3 ambient = albedo * uAmbientColor;
    vec3 finalColor = ambient + diffuse + specular;
    // Convert from linear to sRGB for display (gamma correction)
    vec3 displayColor = pow(finalColor, vec3(1.0/2.2));
    FragColor = vec4(displayColor, texColor.a);
}


