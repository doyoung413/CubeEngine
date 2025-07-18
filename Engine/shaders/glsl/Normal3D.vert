#version 460 core

#if VULKAN
#define MAX_MATRICES 500
#else
#define MAX_MATRICES 20
#endif

layout(location = 0) in vec3 i_pos;

struct vMatrix
{
    mat4 model;
    // @TODO remove after Slang's inverseModel is moved to Push Constants
    mat4 transposeInverseModel;
    mat4 view;
    mat4 projection;
    vec4 color;
    // @TODO move to push constants later
    vec3 viewPosition;
};

#if VULKAN
layout(set = 0, binding = 0) uniform vUniformMatrix
#else
layout(std140, binding = 2) uniform vUniformMatrix
#endif
{
    vMatrix matrix;
};

void main()
{
    gl_Position = matrix.projection * matrix.view * matrix.model * vec4(i_pos, 1.0);
}