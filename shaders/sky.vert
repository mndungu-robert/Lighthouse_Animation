#version 330 core
layout (location = 0) in vec3 aPos;
out vec3 vDir;

uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    vDir = aPos;
    // Remove translation from view matrix so the sky always surrounds the camera
    mat4 rotView = mat4(mat3(uView));
    vec4 pos = uProjection * rotView * vec4(aPos, 1.0);
    gl_Position = pos.xyww; // Push sky to maximum depth (1.0)
}
