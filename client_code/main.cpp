#include <iostream>

// to use the int limits
#include <limits>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "include/Player.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

void to_json(json &j, const Player &p)
{
    json position = json{
        {"x", p.m_position.x},
        {"y", p.m_position.y},
        {"z", p.m_position.z},
    };

    j = json{
        {"position", position},
        {"yaw", p.m_yaw},
    };
}

void from_json(const json &j, Player &p)
{
    j.at("position").at("x").get_to(p.m_position.x);
    j.at("position").at("y").get_to(p.m_position.y);
    j.at("position").at("z").get_to(p.m_position.z);

    j.at("yaw").get_to(p.m_yaw);
}

enum class Movement
{
    MOVE_FORWARD,
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_BACK
};

std::string movement_to_string(const Movement &m)
{
    switch (m)
    {
    case Movement::MOVE_FORWARD:
        return "MOVE_FORWARD";
    case Movement::MOVE_LEFT:
        return "MOVE_LEFT";
    case Movement::MOVE_RIGHT:
        return "MOVE_RIGHT";
    case Movement::MOVE_BACK:
        return "MOVE_BACK";
    }
    return "unknown";
}

int main()
{
    int result = 0;

    int id;
    Player myPlayer{};

    myPlayer.m_position = {4.0f, 2.0f, 3.0f};
    myPlayer.m_yaw = 90.f;

    std::cout << "Type your Id : ";
    std::cin >> id;

    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    json data{{"id", id}};

    // init
    WSAData wsaData;
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != NO_ERROR)
    {
        std::cout << "WINSOCK::INIT::ERROR" << std::endl;
        return 1;
    }

    // creating the socket
    SOCKET mySock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mySock == INVALID_SOCKET)
    {
        std::cout << "SCOKET::CREATION::ERROR" << std::endl;
        WSACleanup();
        return 1;
    }

    // creating the server address
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    // connect to the server
    result = connect(mySock, (sockaddr *)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR)
    {
        std::cout << "CONNECTING::ERROR" << std::endl;
        WSACleanup();
        return 1;
    }

    // sending to the server
    while (true)
    {
        std::string input;
        std::cout << "==> ";
        std::getline(std::cin, input);

        if (input == "exit")
        {
            break;
        }

        Movement movement;
        if (input == "z")
        {
            movement = Movement::MOVE_FORWARD;
        }
        else if (input == "q")
        {
            movement = Movement::MOVE_LEFT;
        }
        else if (input == "d")
        {
            movement = Movement::MOVE_RIGHT;
        }
        else if (input == "s")
        {
            movement = Movement::MOVE_BACK;
        }
        else {
            continue;
        }

        data["movement"] = movement;
        data["yaw"] = myPlayer.m_yaw;

        std::string data_str = data.dump();
        send(mySock, data_str.c_str(), data_str.length(), 0);
    }

    closesocket(mySock);
    WSACleanup();

    return 0;
}