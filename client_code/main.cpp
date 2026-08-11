#include <iostream>

#include <winsock2.h>
#include <ws2tcpip.h>

int main()
{
    int result;

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

        send(mySock, message.c_str(), message.length(), 0);
    }

    closesocket(mySock);
    WSACleanup();

    return 0;
}