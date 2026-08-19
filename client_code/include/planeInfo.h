#include "mesh.h"

std::vector<Vertex> planeVertices =
{
    {
        glm::vec3(-0.5f, 0.0f, -0.5f),   // Position
        glm::vec3(0.0f, 1.0f, 0.0f),     // Normal
        glm::vec2(0.0f, 0.0f),           // UV
        glm::vec3(1.0f, 0.0f, 0.0f),     // Tangent
        glm::vec3(0.0f, 0.0f, -1.0f),    // Bitangent
        {-1, -1, -1, -1},
        {0.0f, 0.0f, 0.0f, 0.0f}
    },
    {
        glm::vec3(0.5f, 0.0f, -0.5f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec2(1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        {-1, -1, -1, -1},
        {0.0f, 0.0f, 0.0f, 0.0f}
    },
    {
        glm::vec3(0.5f, 0.0f,  0.5f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec2(1.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        {-1, -1, -1, -1},
        {0.0f, 0.0f, 0.0f, 0.0f}
    },
    {
        glm::vec3(-0.5f, 0.0f,  0.5f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec2(0.0f, 1.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        {-1, -1, -1, -1},
        {0.0f, 0.0f, 0.0f, 0.0f}
    }
};

std::vector<unsigned int> planeIndices =
{
    0, 1, 2,
    2, 3, 0
};