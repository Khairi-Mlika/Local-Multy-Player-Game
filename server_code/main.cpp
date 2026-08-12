#include <iostream>

#include <format>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

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

    // accepting client
    sockaddr_in clientAddr{};
    int len = sizeof(clientAddr);
    SOCKET clientSocket = accept(TCPsocket, (sockaddr *)&clientAddr, &len);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cout << "ACCEPTIN::CLIENT::ERROR" << std::endl;
        WSACleanup();
        return 1;
    }

    // recieving message
    char buffer[2048];
    while (true)
    {
        result = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (result == SOCKET_ERROR)
        {
            std::cout << "RECV::ERROR" << std::endl;
            WSACleanup();
            return 1;
        }

        // converting the char[n] into a json object
        std::string data_str(buffer, result);

        json data = json::parse(data_str);

        std::string display = std::format("client {} : {} \n",data["id"].get<int>(),data["message"].get<std::string>());

        std::cout << display ;
    }

    closesocket(TCPsocket);
    closesocket(clientSocket);

    WSACleanup();

    return 0;
}