#include <iostream>

#include <format>
#include <thread>
#include <map>
#include <cmath>
#include <mutex>

#include <winsock2.h>
#include <ws2tcpip.h>

#include "include/Player.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

float SPEED = 1.0f;

int LastClientId = 0;

std::map<int, Player> ActivePlayers{};
std::map<int, SOCKET> ActiveClients{};

std::mutex playersMutex;

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
    case Movement::MOVE_RIGHT:
        return "MOVE_RIGHT";
    case Movement::MOVE_BACK:
        return "MOVE_BACK";
    case Movement::MOVE_LEFT:
        return "MOVE_LEFT";
    }
    return "unknown";
}

constexpr float PI = 3.14159265358979323846f;

float radians(float degrees)
{
    return degrees * PI / 180.0f;
}

float cleanFloat(float value)
{
    constexpr float epsilon = 0.000001f;

    if (std::abs(value) < epsilon)
        return 0.0f;

    return value;
}

vec3 directionVector(const Movement &m, float yaw)
{
    vec3 vector{};
    float yawRad = radians(yaw);

    switch (m)
    {
    case Movement::MOVE_FORWARD:
        vector = {std::sin(yawRad), 0.0f, -std::cos(yawRad)};
        break;
    case Movement::MOVE_RIGHT:
        vector = {std::cos(yawRad), 0.0f, std::sin(yawRad)};
        break;
    case Movement::MOVE_BACK:
        vector = {-std::sin(yawRad), 0.0f, +std::cos(yawRad)};
        break;
    case Movement::MOVE_LEFT:
        vector = {-std::cos(yawRad), 0.0f, -std::sin(yawRad)};
        break;
    }

    vector.x = cleanFloat(vector.x);
    vector.y = cleanFloat(vector.y);
    vector.z = cleanFloat(vector.z);

    return vector;
}

int recvHandler(SOCKET clientSock, int clientId);

int acceptHandler(SOCKET tcpSock)
{
    while (true)
    {
        sockaddr_in clientAddr{};
        int len = sizeof(clientAddr);

        SOCKET clientSock = accept(tcpSock, (sockaddr *)&clientAddr, &len);
        if (clientSock == INVALID_SOCKET)
        {
            std::cout << "SOCKET::ERROR::ACCEPT::THREAD" << std::endl;
            WSAGetLastError();
            continue;
        }

        playersMutex.lock();

        int id = LastClientId;
        LastClientId++;

        ActivePlayers[id] = Player();
        ActiveClients[id] = clientSock;

        playersMutex.unlock();

        json data = {{"id", id}, {"player", ActivePlayers[id]}};
        std::string data_str = data.dump();

        int result = send(clientSock, data_str.c_str(), data_str.length(), 0);
        if (result == SOCKET_ERROR)
        {
            std::cout << "ID::SENDING::ERROR" << std::endl;
            continue;
        }

        std::thread recvThread(recvHandler, clientSock, id);
        recvThread.detach();
    }

    return 0;
}

int recvHandler(SOCKET clientSock, int clientId)
{
    char buffer[2048];

    Player &currentPlayer = ActivePlayers[clientId];

    while (true)
    {
        int bytesRecieved = recv(clientSock, buffer, sizeof(buffer), 0);

        if (bytesRecieved == SOCKET_ERROR)
        {
            std::cout << "RECV::THREAD::ERROR" << std::endl;

            playersMutex.lock();
            ActivePlayers.erase(clientId);
            ActiveClients.erase(clientId);
            if (ActivePlayers.empty())
            {
                LastClientId = 0;
            }
            else
            {
                LastClientId = ActivePlayers.rbegin()->first + 1;
            }
            playersMutex.unlock();

            break;
        }

        if (bytesRecieved == 0)
        {

            std::cout << std::format("client {} disconnected ", clientId) << std::endl;

            playersMutex.lock();
            ActivePlayers.erase(clientId);
            ActiveClients.erase(clientId);
            if (ActivePlayers.empty())
            {
                LastClientId = 0;
            }
            else
            {
                LastClientId = ActivePlayers.rbegin()->first + 1;
            }
            playersMutex.unlock();

            break;
        }

        std::string data_str(buffer, bytesRecieved);

        try
        {
            json data = json::parse(data_str);
            Movement movement = data["movement"].get<Movement>();
            float yaw = data["yaw"].get<float>();

            currentPlayer.m_yaw = yaw;

            vec3 direction = directionVector(movement, yaw);

            switch (movement)
            {
            case Movement::MOVE_FORWARD:
                currentPlayer.m_position = currentPlayer.m_position + direction * SPEED;
                break;
            case Movement::MOVE_RIGHT:
                currentPlayer.m_position = currentPlayer.m_position + direction * SPEED;
                break;
            case Movement::MOVE_LEFT:
                currentPlayer.m_position = currentPlayer.m_position + direction * SPEED;
                break;
            case Movement::MOVE_BACK:
                currentPlayer.m_position = currentPlayer.m_position + direction * SPEED;
                break;
            }

            std::string display = std::format("Client {} | {} | Yaw {} => Current Position ({},{},{}) ",
                                              clientId,
                                              movement_to_string(movement),
                                              yaw,
                                              currentPlayer.m_position.x,
                                              currentPlayer.m_position.y,
                                              currentPlayer.m_position.z);

            std::cout << display << std::endl;
        }

        catch (const json::exception &e)
        {
            std::cout << "PARSING::ERROR" << std::endl;
            break;
        }
    }

    closesocket(clientSock);
    return 0;
}

int main()
{
    int result = 0;

    // init the winsock library
    WSADATA wsaData;
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != NO_ERROR)
    {
        std::cout << "WINSOCK::INIT::ERROR" << std::endl;
        return 1;
    }

    // creating the address of our server
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // creating a TCP socket for our server
    SOCKET TCPsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (TCPsocket == INVALID_SOCKET)
    {
        std::cout << "SCOKET::CREATION::ERROR" << std::endl;
        WSACleanup();
        return 1;
    }

    // binding the socket with the addr
    result = bind(TCPsocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR)
    {
        std::cout << "BINDING::ERROR" << std::endl;
        WSACleanup();
        return 1;
    }

    // setting the server to listen
    result = listen(TCPsocket, SOMAXCONN);
    if (result == SOCKET_ERROR)
    {
        std::cout << "LISTENING::ERROR" << std::endl;
        WSACleanup();
        return 1;
    }

    std::thread acceptThread(acceptHandler, std::ref(TCPsocket));

    acceptThread.join();

    closesocket(TCPsocket);
    WSACleanup();

    return 0;
}