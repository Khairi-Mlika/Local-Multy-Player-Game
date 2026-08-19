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

#include "camera.h"
#include "stb_image.h"
#include "mesh.h"
#include "shader.h"

#include "cubeInfo.h"
#include "planeInfo.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

Camera myCam{};
float lastX = 800 / 2.0f;
float lastY = 600 / 2.0f;
bool firstMouse = true;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

class Player
{
public:
    glm::vec3 m_position{};
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

bool cursorDisabled = true;
bool escapeWasPressed = false;

std::optional<Movement> processInput(GLFWwindow *window)
{
    bool escapePressed =
        glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;

    if (escapePressed && !escapeWasPressed)
    {
        cursorDisabled = !cursorDisabled;

        glfwSetInputMode(
            window,
            GLFW_CURSOR,
            cursorDisabled
                ? GLFW_CURSOR_DISABLED
                : GLFW_CURSOR_NORMAL);

        firstMouse = true;
    }

    escapeWasPressed = escapePressed;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        return Movement::MOVE_FORWARD;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        return Movement::MOVE_BACK;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        return Movement::MOVE_LEFT;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        return Movement::MOVE_RIGHT;

    return std::nullopt;
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow *window, double xposIn, double yposIn)
{
    if (!cursorDisabled)
        return;

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
        return;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

    lastX = xpos;
    lastY = ypos;

    myCam.ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    myCam.ProcessMouseScroll(static_cast<float>(yoffset));
}

glm::mat3 calculate_normal(glm::mat4 model)
{
    return glm::mat3(glm::transpose(glm::inverse(model)));
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

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow *window = glfwCreateWindow(800, 600, "test", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "WINDOW:ERROR" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }
    glViewport(0, 0, 800, 600);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    Mesh cube(cubeVertices, cubeIndices, {});

    Shader lighting_shader("../shader/lighting_shader.vs", "../shader/lighting_shader.fs");

    json data; // id -> int | movement -> Movement | yaw -> float
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        std::optional<Movement> movement = processInput(window);

        myPlayer.m_yaw = myCam.Yaw;

        if (movement)
        {
            data["id"] = myId;
            data["movement"] = movement;
            data["yaw"] = myPlayer.m_yaw;

            if (!netService.sendDataToServer(data))
            {
                return 1;
            }
        }

        if (!netService.recvPlayersInfo(players))
        {
            return 1;
        }

        myCam.Position = players.at(myId).m_position;

        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)800 / (float)600, 0.1f, 500.0f);
        glm::mat4 view = myCam.GetViewMatrix();

        lighting_shader.use();

        lighting_shader.setVec3("viewPos", myCam.Position);
        lighting_shader.setMat4("view", view);
        lighting_shader.setMat4("projection", projection);

        lighting_shader.setVec3("light.direction", glm::vec3(10.0f));
        lighting_shader.setVec3("light.ambient", glm::vec3(1.0f));
        lighting_shader.setVec3("light.diffuse", glm::vec3(0.5f));
        lighting_shader.setVec3("light.specular", glm::vec3(0.5f));

        for (const auto &[id, player] : players)
        {
            if (id != myId)
            {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), player.m_position);

                lighting_shader.setMat4("model", model);
                lighting_shader.setMat3("normalMatrix", calculate_normal(model));

                cube.Draw(lighting_shader);
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    return 0;
}