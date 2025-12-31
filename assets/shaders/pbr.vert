#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} vs_out;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vs_out.worldPos = wp.xyz;
    vs_out.normal = normalize(uNormalMatrix * aNormal);
    vs_out.uv = aUV;
    gl_Position = uProjection * uView * wp;
}

