#version 300 es

// (location = 0): Vertex position in local coordinates.
layout (location = 0) in vec3 aPos;

// Uniform variables: matrices for coordinate transformation.
uniform mat4 model;      // Model matrix (cube rotation)
uniform mat4 view;       // View matrix (camera)
uniform mat4 projection; // Projection matrix

void main()
{
    // Calculate the final vertex position on the screen.
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}