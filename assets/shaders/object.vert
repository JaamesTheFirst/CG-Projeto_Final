#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out VS_OUT {
    vec3 normal;
    vec3 worldPos;
    vec2 uv;
} vs_out;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vs_out.worldPos = worldPosition.xyz;
    vs_out.normal = normalize(uNormalMatrix * aNormal);
    vs_out.uv = aTexCoord;

    gl_Position = uProjection * uView * worldPosition;
}


