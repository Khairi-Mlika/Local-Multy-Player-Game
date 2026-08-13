#include <iostream>

#include <format>
#include <thread>

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

int recvHandler(SOCKET clientSock);

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

        std::thread recvThread(recvHandler, std::ref(clientSock));
        recvThread.detach();
    }

    return 0;
}

int recvHandler(SOCKET clientSock)
{
    char buffer[2048];
    bool first = true;

    int clientId;

    while (true)
    {
        int bytesRecieved = recv(clientSock, buffer, sizeof(buffer), 0);

        if (bytesRecieved == SOCKET_ERROR)
        {
            std::cout << "RECV::THREAD::ERROR" << std::endl;
            break;
        }

        if (bytesRecieved == 0)
        {
            if (first)
            {
                std::cout << "client disconnected before identification " << std::endl;
            }
            else
            {
                std::cout << std::format("client {} disconnected ", clientId) << std::endl;
            }
            break;
        }

        std::string data_str(buffer, bytesRecieved);

        try
        {

            json data = json::parse(data_str);

            if (first)
            {
                clientId = data["id"].get<int>();
                first = false;
            }

            Player player{};
            player = data["player"].get<Player>();

            std::string display = std::format("Client {} | Position ({},{},{}) | Yaw {} ",
                                              clientId,
                                              player.m_position.x,
                                              player.m_position.y,
                                              player.m_position.z,
                                              player.m_yaw);

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