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

// Local camera representing this client's own view into the world.
// Its position gets overwritten every frame from the authoritative
// server state (myCam.Position = players.at(myId).m_position), so
// this camera never free-flies independently of the server.
Camera myCam{};

// Last known mouse position, used to compute frame-to-frame mouse
// deltas in mouse_callback. Initialized to the screen center.
float lastX = 800 / 2.0f;
float lastY = 600 / 2.0f;

// True until the first mouse_callback call (or right after toggling
// cursor mode) — prevents a huge camera jump on the first movement
// event, which would otherwise be computed from a stale (0,0)-ish
// lastX/lastY.
bool firstMouse = true;

// Standard delta-time bookkeeping for frame-rate independent updates
// (used for smoothing/physics elsewhere, e.g. inside Camera).
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Client-side render representation of a player. Note this uses
// glm::vec3 directly (unlike the plain `vec3` struct used in the
// non-rendering client versions), since it's consumed straight by
// GLM/OpenGL calls (glm::translate, etc.).
class Player
{
public:
    glm::vec3 m_position{};
    float m_yaw{};
};

// All known players (including this client), keyed by server-assigned
// id. Refreshed every frame from recvPlayersInfo() — this is not
// locally authoritative, it's just a mirror of server state.
std::map<int, Player> players{};

// Same to_json/from_json contract as the server: needed so this
// client can decode player state broadcast by the server and encode
// its own input messages.
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

// Must stay in sync with the server's Movement enum (same order),
// since it's serialized as a raw int with no shared header enforcing
// this contract.
enum class Movement
{
    MOVE_FORWARD,
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_BACK
};

// Debug-only helper, not used in the network protocol itself.
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

// Wraps all the raw Winsock plumbing (init, socket, connect, framed
// send/recv) behind a small RAII-ish interface so main()/the render
// loop doesn't have to deal with sockaddr/WSA details directly.
class NetworkingService
{
public:
    bool m_initialized;

    SOCKET m_mySock{};
    sockaddr_in m_serverAddress{};

    // Initializes Winsock. m_initialized reflects whether that
    // succeeded; callers must check is_initialized() before using
    // any other method, since the rest of the class assumes Winsock
    // is up.
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

    // Ensures the socket and Winsock are always cleaned up when this
    // object goes out of scope, regardless of how main() exits.
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

    // Creates the underlying TCP socket. Must be called before
    // connect().
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

    // Fills in the target server's address/port. ipAdd must be a
    // dotted-decimal IPv4 string (e.g. "127.0.0.1") since inet_pton
    // is used, not a hostname.
    void createServerAddress(int af, int port, std::string ipAdd)
    {
        m_serverAddress.sin_family = af;
        m_serverAddress.sin_port = htons(port);
        inet_pton(af, ipAdd.data(), &m_serverAddress.sin_addr);
    }

    // Establishes the TCP connection to the server address configured
    // via createServerAddress(). Uses ::connect to disambiguate from
    // this class's own connect() method name.
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

    // Reads the server's initial handshake message (assigned client
    // id + starting Player state). This one isn't length-prefixed
    // like later game-state messages — it assumes the whole handshake
    // arrives in a single recv() call, which is fine for its small,
    // fixed-shape payload but wouldn't scale to arbitrary sizes.
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

    // Serializes and sends an arbitrary JSON message to the server
    // (used for movement/id/yaw input packets). No length prefix on
    // outgoing messages — only incoming server broadcasts use the
    // length-prefixed framing (see recvMessageSize/recvMessage).
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

    // TCP doesn't preserve message boundaries — a single recv() can
    // return fewer bytes than requested even if more data is on the way.
    // This loops until exactly `size` bytes are collected, or the
    // connection errors/closes (result <= 0), in which case it bails
    // out early instead of spinning forever.
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

    // Reads the 4-byte big-endian length header the server prefixes
    // every broadcast with, and converts it back to host byte order.
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

    // Reads exactly `size` bytes into a fixed 2048-byte stack buffer.
    // NOTE: there's no check that `size` <= sizeof(rcv_buffer) here —
    // if the server ever broadcasts a payload larger than 2048 bytes
    // (e.g. enough connected players), this overflows the buffer.
    // Worth adding a bounds check or switching to a dynamically sized
    // buffer before this scales up.
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

    // High-level helper: reads one full length-prefixed broadcast
    // message from the server and decodes it into the shared players
    // map. This is the per-frame "get latest world state" call.
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

// Tracks whether the mouse cursor is currently captured (disabled) for
// free-look camera control, vs. released for normal desktop use.
bool cursorDisabled = true;

// Used to detect the rising edge of the Escape key press (i.e. only
// toggle once per press, not every frame the key is held down).
bool escapeWasPressed = false;

// Polls keyboard state once per frame. Handles the Escape-to-toggle-
// cursor UX separately from movement, then returns at most one
// movement direction based on WASD state. Returns std::nullopt if no
// movement key is currently pressed — the caller decides whether that
// means "don't send a movement packet this frame".
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

        // Re-arm firstMouse so that re-enabling the cursor-disabled
        // mode doesn't cause a big camera snap from the stale
        // lastX/lastY captured before the cursor was released.
        firstMouse = true;
    }

    escapeWasPressed = escapePressed;

    // NOTE: only one direction can be returned per frame — if the
    // player holds W and D together (diagonal movement), only
    // MOVE_FORWARD is reported since it's checked first. No
    // diagonal/combined movement support currently.
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

// GLFW cursor-position callback: converts raw mouse position into a
// frame-to-frame offset and feeds it to the camera for look rotation.
// Skipped entirely while the cursor is released (cursorDisabled ==
// false), so moving the mouse over a released cursor doesn't spin
// the camera.
void mouse_callback(GLFWwindow *window, double xposIn, double yposIn)
{
    if (!cursorDisabled)
        return;

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse)
    {
        // First event after enabling capture: just record position,
        // don't apply a rotation (there's no valid previous position
        // to diff against yet).
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

// GLFW scroll callback, used for camera zoom/FOV adjustment.
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    myCam.ProcessMouseScroll(static_cast<float>(yoffset));
}

// Standard normal-matrix computation: transpose(inverse(model)),
// needed so normals stay correct under non-uniform scaling (uniform
// scale/rotation-only models wouldn't strictly need this, but it's
// correct in the general case).
glm::mat3 calculate_normal(glm::mat4 model)
{
    return glm::mat3(glm::transpose(glm::inverse(model)));
}

std::vector<Vertex> generate_Terrain(const int &height, const int &width)
{
    float spacing = 1.0f;
    float tileScale = 10.0f; // ← controls how many times texture repeats
    std::vector<Vertex> vertices;
    for (unsigned int z = 0; z < height; z++)
    {
        for (unsigned int x = 0; x < width; x++)
        {
            float n = -0.5f;
            vertices.push_back({glm::vec3((x - width / 2.0f) * spacing, n, (z - height / 2.0f) * spacing),
                                glm::vec3(0.f, 1.f, 0.f),
                                glm::vec2(
                                    static_cast<float>(x) / tileScale,
                                    static_cast<float>(z) / tileScale)});
        }
    }
    return vertices;
}

// Generate floor indecies using its height and width and returning vector of vertex
// ----------------------------------------------------------------------
std::vector<unsigned int> generate_Indices(const int &height, const int &width)
{
    std::vector<unsigned int> indices;
    for (unsigned int z = 0; z < height - 1; z++)
    {
        for (unsigned int x = 0; x < width - 1; x++)
        {
            int topLeft = z * width + x;
            int topRight = topLeft + 1;

            int bottomLeft = (z + 1) * width + x;
            int bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
    return indices;
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

    // Network handshake happens fully before any window/GL setup —
    // if the server isn't reachable, we bail out before wasting time
    // creating a window.
    if (!netService.recvInitValue(myId, myPlayer))
    {
        return 1;
    }

    // ---- Standard GLFW/GLAD/OpenGL 3.3 core-profile bootstrap ----
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

    // Start with the cursor captured for free-look, matching
    // cursorDisabled's initial value of true.
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }
    glViewport(0, 0, 800, 600);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    unsigned int cubeTexture;
    glGenTextures(1, &cubeTexture);

    // Single reusable cube mesh used to represent every *other*
    // connected player (see the draw loop below — myId is skipped
    // since the local player doesn't render itself in first-person).
    Mesh cube(cubeVertices, cubeIndices, {{cubeTexture, "texture_diffuse", "../assets/wall.jpg"}});

    int size = 100;

    std::vector<Vertex> floorVertex = generate_Terrain(size, size);
    std::vector<unsigned int> floorIndices = generate_Indices(size, size);

    unsigned int floorTexture;
    glGenTextures(1, &floorTexture);

    Mesh plane(floorVertex, floorIndices, {{floorTexture, "texture_diffuse", "../Assets/grass_floor.jpg"}});

    Shader lighting_shader("../shader/lighting_shader.vs", "../shader/lighting_shader.fs");

    json data; // id -> int | movement -> Movement | yaw -> float

    // Main render/network loop: poll input, optionally send a
    // movement packet, always pull the latest broadcast world state,
    // sync the local camera to the server-authoritative position,
    // then render every other player as a cube.
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        std::optional<Movement> movement = processInput(window);

        // Yaw is derived purely from mouse look (myCam.Yaw), updated
        // every frame regardless of whether a movement key was
        // pressed — so turning while stationary still keeps the
        // server-side yaw in sync for the next actual move.
        myPlayer.m_yaw = myCam.Yaw;

        // Only send a network packet when there's actually a movement
        // key held — avoids spamming the server with empty input
        // every single frame.
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

        // Pulled every frame regardless of whether we sent input —
        // keeps this client's view of all players current even when
        // we're not moving, since other players may still be moving.
        //
        // NOTE: this blocks waiting for a broadcast each frame. If
        // the server's tick rate is slower than this render loop (or
        // a send is skipped), this recv could stall frame pacing —
        // worth keeping in mind if the game loop ever feels choppy.
        if (!netService.recvPlayersInfo(players))
        {
            return 1;
        }

        // Server is authoritative: snap the local camera to whatever
        // position the server reports for our own id, rather than
        // predicting movement locally.
        myCam.Position = players.at(myId).m_position;

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)800 / (float)600, 0.1f, 500.0f);
        glm::mat4 view = myCam.GetViewMatrix();

        lighting_shader.use();

        lighting_shader.setVec3("viewPos", myCam.Position);
        lighting_shader.setMat4("view", view);
        lighting_shader.setMat4("projection", projection);

        // Fixed/basic lighting setup — not tied to any real light
        // source position, just constant ambient/diffuse/specular
        // terms for now.
        lighting_shader.setVec3("light.direction", glm::vec3(10.0f));
        lighting_shader.setVec3("light.ambient", glm::vec3(1.0f));
        lighting_shader.setVec3("light.diffuse", glm::vec3(0.5f));
        lighting_shader.setVec3("light.specular", glm::vec3(0.5f));

        // Draw every connected player except ourselves (first-person
        // view — we don't render our own avatar) as a cube placed at
        // their server-reported position.
        for (const auto &[id, player] : players)
        {
            if (id != myId)
            {
                model = glm::translate(glm::mat4(1.0f), player.m_position);

                lighting_shader.setMat4("model", model);
                lighting_shader.setMat3("normalMatrix", calculate_normal(model));

                cube.Draw(lighting_shader);
            }
        }

        model = glm::mat4(1.0f);

        lighting_shader.setMat4("model", model);

        plane.Draw(lighting_shader);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    return 0;
}