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

int main()
{
    int result = 0;

    int id;
    Player myPlayer{};

    myPlayer.m_position = {4.0f , 2.0f , 3.0f};
    myPlayer.m_yaw = 60.f;

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
        std::string message;
        std::cout << "type your message : ";
        std::getline(std::cin, message);

        if (message == "exit")
        {
            break;
        }

        json player_json = myPlayer;
        data["player"] = player_json;

        std::string data_str = data.dump();

        send(mySock, data_str.c_str(), data_str.length(), 0);
    }

    closesocket(mySock);
    WSACleanup();

    return 0;
}