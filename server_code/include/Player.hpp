#pragma once

struct vec3
{
    float x{};
    float y{};
    float z{};

    vec3 operator+(const vec3 other)
    {
        return vec3{x + other.x, y + other.y, z + other.z};
    }

    vec3 operator*(float f)
    {
        return vec3{x * f, y * f, z * f};
    }
};

class Player
{
public:
    vec3 m_position{};
    float m_yaw{};
};