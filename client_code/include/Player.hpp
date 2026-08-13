#pragma once

struct vec3
{
    float x{};
    float y{};
    float z{};
};

class Player
{
public:
    vec3 m_position{};
    float m_yaw{};
};