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

//==============================================================
// JSON UTILITY FUNCTIONS
//==============================================================

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

//==============================================================
// MOVEMENT UTILITY FUNCTIONS
//==============================================================
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

//==============================================================
// PLAYER INPUT CLASS IMPLEMENTATION
//==============================================================
class PlayerInput
{
public:
    Movement m_movement{};
    float m_yaw{};

public:
    PlayerInput()
    {
    }
    PlayerInput(Movement movement, float yaw)
    {
        m_movement = movement;
        m_yaw = yaw;
    }
};

//==============================================================
// MOVEMENT LOGIQUE AND HELPER FUNCTIONS
//==============================================================

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

//==============================================================
// SERVER GLOBAL VARIABLES AND CONSTS
//==============================================================

float SPEED = 1.0f;

const float TICK_RATE = 60.0f;
const float TICK_TIME = 1 / TICK_RATE;

float DT{};

int LastClientId = 0;

std::map<int, PlayerInput> playersInput{};

std::map<int, Player> activePlayers{};
std::map<int, SOCKET> activeClients{};

std::mutex playersMutex{};

//==============================================================
// SERVER HANDLER FUNCTIONS
//==============================================================

int recvHandler(SOCKET clientSock, int clientId);

int acceptHandler(SOCKET tcpSock)
{
    int id;
    json data;

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

        {
            std::lock_guard<std::mutex> lock(playersMutex);

            id = LastClientId++;

            activePlayers[id] = Player();
            activeClients[id] = clientSock;

            data = {{"id", id}, {"player", activePlayers.at(id)}};
        }

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

    while (true)
    {
        int bytesRecieved = recv(clientSock, buffer, sizeof(buffer), 0);

        if (bytesRecieved == SOCKET_ERROR)
        {
            std::cout << "RECV::THREAD::ERROR" << std::endl;
            {
                std::lock_guard<std::mutex> lock(playersMutex);

                activePlayers.erase(clientId);
                activeClients.erase(clientId);
                playersInput.erase(clientId);

                if (activePlayers.empty())
                {
                    LastClientId = 0;
                }
                else
                {
                    LastClientId = activePlayers.rbegin()->first + 1;
                }
            }
            break;
        }

        if (bytesRecieved == 0)
        {

            std::cout << std::format("client {} disconnected ", clientId) << std::endl;
            {
                std::lock_guard<std::mutex> lock(playersMutex);

                activePlayers.erase(clientId);
                activeClients.erase(clientId);
                playersInput.erase(clientId);

                if (activePlayers.empty())
                {
                    LastClientId = 0;
                }
                else
                {
                    LastClientId = activePlayers.rbegin()->first + 1;
                }
            }
            break;
        }

        std::string data_str(buffer, bytesRecieved);

        try
        {
            json data = json::parse(data_str);

            Movement movement = data["movement"].get<Movement>();
            float yaw = data["yaw"].get<float>();

            std::lock_guard<std::mutex> lock(playersMutex);
            playersInput[clientId] = PlayerInput(movement, yaw);
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

void inputHandler()
{
    std::map<int, PlayerInput> playersInputCopy;
    {
        std::lock_guard<std::mutex> lock(playersMutex);
        playersInputCopy = playersInput;
        playersInput.clear();
    }

    if (playersInputCopy.empty()){
        return;
    }

    for (auto &[id, playerInput] : playersInputCopy)
    {
        std::string display;
        {
            std::lock_guard<std::mutex> lock(playersMutex);

            Player &currentPlayer = activePlayers.at(id);

            vec3 direction = directionVector(playerInput.m_movement, playerInput.m_yaw);
            currentPlayer.m_position = currentPlayer.m_position + direction * SPEED * DT;

            display = std::format("Client {} | {} | Yaw {} => Current Position ({},{},{}) ",
                                  id,
                                  movement_to_string(playerInput.m_movement),
                                  playerInput.m_yaw,
                                  currentPlayer.m_position.x,
                                  currentPlayer.m_position.y,
                                  currentPlayer.m_position.z);
        }
        std::cout << display << std::endl;
    }
}

void broadcastHandler()
{
    int result;
    json data;
    std::map<int, SOCKET> activeClientsCopy;
    {
        std::lock_guard<std::mutex> lock(playersMutex);
        activeClientsCopy = activeClients;
        data["players"] = activePlayers;
    }
    std::string data_str = data.dump();
    for (auto &[id, clientSock] : activeClientsCopy)
    {
        result = send(clientSock, data_str.c_str(), data_str.length(), 0);
        if (result == SOCKET_ERROR)
        {
            continue;
        }
    }
}

//==============================================================
//==============================================================
//==============================================================

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

    auto previous_time = std::chrono::steady_clock::now();

    while (true)
    {
        auto current_time = std::chrono::steady_clock::now();
        DT = std::chrono::duration<float>(current_time - previous_time).count();
        previous_time = current_time;

        auto tick_start = std::chrono::steady_clock::now();

        inputHandler();
        broadcastHandler();

        auto tick_end = std::chrono::steady_clock::now();
        float tickDuration = std::chrono::duration<float>(tick_end - tick_start).count();
        float sleepTime = TICK_TIME - tickDuration;
        if (sleepTime > 0.0f)
        {
            std::this_thread::sleep_for(std::chrono::duration<float>(sleepTime));
        }
    }

    acceptThread.join();

    closesocket(TCPsocket);
    WSACleanup();

    return 0;
}