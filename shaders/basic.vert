#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec3 aNormal;

out vec3 vColor;
out vec3 vNormal;
out vec3 vFragPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uTime;
uniform int uIsWater;

void main()
{
    vec3 pos = aPos;
    vec3 norm = aNormal;

    // Gentle animated sine waves on the water mesh
    if (uIsWater == 1) {
        float wave1 = sin(pos.x * 0.8 + uTime * 1.6) * 0.07;
        float wave2 = cos(pos.z * 0.9 + uTime * 1.3) * 0.06;
        pos.y += wave1 + wave2;

        // Calculate analytical wave normal for realistic reflections
        float dx = 0.8 * 0.07 * cos(pos.x * 0.8 + uTime * 1.6);
        float dz = -0.9 * 0.06 * sin(pos.z * 0.9 + uTime * 1.3);
        norm = normalize(vec3(-dx, 1.0, -dz));
    }

    vec4 worldPos = uModel * vec4(pos, 1.0);
    vFragPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel))) * norm;
    vColor = aColor;

    gl_Position = uProjection * uView * worldPos;
}
