#include <iostream>

// to use the int limits
#include <limits>
#include <format>
#include <optional>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

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

std::map<int, Player> players{};

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

bool recvAll(SOCKET socket, char *buffer, int size)
{
    int received = 0;

    while (received < size)
    {
        int result = recv(
            socket,
            buffer + received,
            size - received,
            0);

        if (result <= 0)
            return false;

        received += result;
    }

    return true;
}

class NetworkingService
{
public:
    bool m_initialized;

    SOCKET m_mySock{};
    sockaddr_in m_serverAddress{};

    NetworkingService()
    {
        WSAData wsaData;

        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR)
        {
            m_initialized = false;
            return;
        }

        m_initialized = true;
    }

    ~NetworkingService()
    {
        if (m_mySock != INVALID_SOCKET)
        {
            closesocket(m_mySock);
        }

        WSACleanup();
    }

    bool is_initialized()
    {
        return m_initialized;
    }

    bool createSocket(int af, int type, int protocol)
    {
        m_mySock = socket(af, type, protocol);
        if (m_mySock == INVALID_SOCKET)
        {
            std::cout << "SOCKET::CREATION::ERROR" << WSAGetLastError() << std::endl;
            return false;
        }

        return true;
    }

    void createServerAddress(int af, int port, std::string ipAdd)
    {
        m_serverAddress.sin_family = af;
        m_serverAddress.sin_port = htons(port);
        inet_pton(af, ipAdd.data(), &m_serverAddress.sin_addr);
    }

    bool connect()
    {
        int result;
        result = ::connect(m_mySock, (sockaddr *)&m_serverAddress, sizeof(m_serverAddress));
        if (result == SOCKET_ERROR)
        {
            std::cout << "CONNECTING::ERROR" << WSAGetLastError() << std::endl;
            return false;
        }
        return true;
    }

    bool recvInitValue(int &id, Player &myPlayer)
    {
        char buffer[1024];

        int bytesRecieved = recv(m_mySock, buffer, sizeof(buffer), 0);
        if (bytesRecieved == SOCKET_ERROR)
        {
            std::cout << "ID::RECV::ERROR" << WSAGetLastError() << std::endl;
            return false;
        }

        std::string recvData_str(buffer, bytesRecieved);

        try
        {
            json recvData = json::parse(recvData_str);

            id = recvData["id"].get<int>();
            myPlayer = recvData["player"].get<Player>();
        }
        catch (json::exception &e)
        {
            std::cout << "PARSING::ERROR" << e.what() << std::endl;
            return false;
        }

        return true;
    }

    bool sendDataToServer(const json &data)
    {
        std::string data_str = data.dump();

        if (send(m_mySock, data_str.c_str(), data_str.length(), 0) == SOCKET_ERROR)
        {
            std::cout << "SENDING::ERROR" << WSAGetLastError() << std::endl;
            return false;
        }

        return true;
    }

    bool recvMessageSize(uint32_t &size)
    {
        if (!recvAll(
                m_mySock,
                reinterpret_cast<char *>(&size),
                sizeof(size)))
        {
            return false;
        }

        size = ntohl(size);
        return true;
    }

    bool recvMessage(uint32_t size, std::string &recv_data_str)
    {
        char rcv_buffer[2048];

        if (!recvAll(
                m_mySock,
                rcv_buffer,
                size))
        {
            return false;
        }
        recv_data_str = std::string(rcv_buffer, size);
        return true;
    }

    bool recvPlayersInfo(std::map<int, Player> &players)
    {
        uint32_t size;
        if (!recvMessageSize(size))
        {
            return false;
        }

        std::string recv_data_str;
        if (!recvMessage(size, recv_data_str))
        {
            return false;
        }

        try
        {
            json recv_data = json::parse(recv_data_str);
            players = recv_data["players"].get<std::map<int, Player>>();
        }
        catch (json::exception &e)
        {
            return false;
        }

        return true;
    }
};

std::optional<Movement> processInput()
{
    std::string input;
    std::cout << "==> ";
    std::getline(std::cin, input);

    if (input == "exit")
    {
        return std::nullopt;
    }

    if (input == "z")
    {
        return Movement::MOVE_FORWARD;
    }
    else if (input == "q")
    {
        return Movement::MOVE_LEFT;
    }
    else if (input == "d")
    {
        return Movement::MOVE_RIGHT;
    }
    else if (input == "s")
    {
        return Movement::MOVE_BACK;
    }

    return std::nullopt;
}

int main()
{
    int myId = 999;
    Player myPlayer{};
    NetworkingService netService{};

    if (!netService.is_initialized())
    {
        return 1;
    }

    // creating the socket
    if (!netService.createSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP))
    {
        return 1;
    }

    // creating the server address
    netService.createServerAddress(AF_INET, 8080, "127.0.0.1");

    // connect to the server
    if (!netService.connect())
    {
        return 1;
    }

    if (!netService.recvInitValue(myId, myPlayer))
    {
        return 1;
    }

    std::string init = std::format("init => position : ({},{},{}) | yaw : {} ", myPlayer.m_position.x, myPlayer.m_position.y, myPlayer.m_position.z, myPlayer.m_yaw);
    std::cout << init << std::endl;

    json data; // id -> int | movement -> Moevment | yaw -> float
    std::map<int, Player> players;

    while (true)
    {

        std::optional<Movement> movement = processInput();
        if (!movement)
        {
            break;
        }

        data["id"] = myId;
        data["movement"] = movement;
        data["yaw"] = myPlayer.m_yaw;

        if (!netService.sendDataToServer(data))
        {
            return 1;
        }

        if (!netService.recvPlayersInfo(players))
        {
            return 1;
        }

        myPlayer = players.at(myId);
        vec3 myPosition = myPlayer.m_position;

        std::string output = std::format("position ({},{},{}) | yaw {}",
                                         myPosition.x,
                                         myPosition.y,
                                         myPosition.z,
                                         myPlayer.m_yaw);

        std::cout << output << std::endl;
    }

    return 0;
}