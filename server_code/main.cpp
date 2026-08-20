#include <iostream>

#include <format>
#include <thread>
#include <map>
#include <cmath>
#include <mutex>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

//==============================================================
// PLAYER CLASS IMPLEMENTATION
//==============================================================
struct vec3
{
    float x{};
    float y{};
    float z{};

    vec3 operator+(const vec3 other)
    {
        return vec3{x + other.x, y + other.y, z + other.z};
    }

    vec3 operator*(float f)
    {
        return vec3{x * f, y * f, z * f};
    }
};

class Player
{
public:
    vec3 m_position{};
    float m_yaw{};
};

//==============================================================
// JSON UTILITY FUNCTIONS
//==============================================================

// Tells nlohmann::json how to serialize a Player into JSON.
// Only position and yaw are sent to clients — no need to expose
// internal server-only state.
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

// Tells nlohmann::json how to deserialize a Player from JSON.
// Throws json::exception if a required field is missing/wrong type,
// which callers should catch (see recvHandler).
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

// The four directions a client can request each tick.
enum class Movement
{
    MOVE_FORWARD,
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_BACK
};

// Human-readable name, used only for server-side logging.
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

// One tick's worth of input from a client: which direction they
// want to move in, and the yaw (facing angle) they had at the time.
// Movement is always resolved relative to the yaw the client reports,
// so strafing/backing up works correctly regardless of camera direction.
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

// Converts degrees to radians since std::sin/std::cos expect radians
// but yaw is sent by clients in degrees.
float radians(float degrees)
{
    return degrees * PI / 180.0f;
}

// Snaps values extremely close to zero (e.g. -1e-8 from floating point
// rounding in sin/cos) down to exactly 0.0f. Prevents ugly near-zero
// noise from showing up in logged/broadcast positions.
float cleanFloat(float value)
{
    constexpr float epsilon = 0.000001f;

    if (std::abs(value) < epsilon)
        return 0.0f;

    return value;
}

// Turns a movement command + yaw into a unit-ish direction vector in
// world space. Forward/back move along the look direction; left/right
// are perpendicular to it (strafe). Y is always 0 since movement here
// is horizontal-only (no flying/jumping).
vec3 directionVector(const Movement &m, float yaw)
{
    vec3 vector{};
    float yawRad = radians(yaw);

    switch (m)
    {
    case Movement::MOVE_FORWARD:
        vector = {
            std::cos(yawRad),
            0.0f,
            std::sin(yawRad)};
        break;

    case Movement::MOVE_BACK:
        vector = {
            -std::cos(yawRad),
            0.0f,
            -std::sin(yawRad)};
        break;

    case Movement::MOVE_RIGHT:
        vector = {
            -std::sin(yawRad),
            0.0f,
            std::cos(yawRad)};
        break;

    case Movement::MOVE_LEFT:
        vector = {
            std::sin(yawRad),
            0.0f,
            -std::cos(yawRad)};
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

// Units per second a player moves; multiplied by DT each tick so
// speed stays consistent regardless of actual tick duration.
float SPEED = 5.0f;

const float TICK_RATE = 60.f;
const float TICK_TIME = 1 / TICK_RATE;

// Delta time between the last two ticks, in seconds. Recomputed every
// loop iteration in main() and used to scale movement.
float DT{};

// Next id to assign to a newly connected client. Reset/recomputed
// whenever a client disconnects (see recvHandler) so ids stay compact.
int LastClientId = 0;

// Latest unprocessed input per client, written by each client's recv
// thread and drained once per tick by inputHandler(). Only ever holds
// at most one (the most recent) input per client between ticks.
std::map<int, PlayerInput> playersInput{};

// Authoritative server-side state for every connected player, keyed
// by client id.
std::map<int, Player> activePlayers{};

// Open TCP sockets for every connected client, keyed by the same id
// used in activePlayers, so broadcastHandler can send state updates
// to everyone.
std::map<int, SOCKET> activeClients{};

// Guards all three maps above (activePlayers, activeClients,
// playersInput) since they're read/written from multiple threads:
// the accept thread, one recv thread per client, and the main tick loop.
std::mutex playersMutex{};

//==============================================================
// SERVER HANDLER FUNCTIONS
//==============================================================

int recvHandler(SOCKET clientSock, int clientId);

// Runs on its own thread for the lifetime of the server. Blocks on
// accept() waiting for new TCP connections; for each new client it
// assigns an id, registers them in the shared state, sends back their
// assigned id + initial player state, then spins up a dedicated
// recv thread to handle that client's incoming input.
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
            // Lock only around the shared-state mutation, not the
            // network send below, to minimize how long other threads
            // are blocked waiting on this mutex.
            std::lock_guard<std::mutex> lock(playersMutex);

            id = LastClientId++;

            activePlayers[id] = Player();
            activeClients[id] = clientSock;

            data = {{"id", id}, {"player", activePlayers.at(id)}};
        }

        std::string data_str = data.dump();

        // Let the client know its assigned id and starting state so
        // it can start sending correctly-tagged input.
        int result = send(clientSock, data_str.c_str(), data_str.length(), 0);
        if (result == SOCKET_ERROR)
        {
            std::cout << "ID::SENDING::ERROR" << std::endl;
            {
                std::lock_guard<std::mutex> lock(playersMutex);
                activePlayers.erase(id);
                activeClients.erase(id);
            }
            closesocket(clientSock);
            continue;
        }

        // Detached: this thread outlives the current scope and cleans
        // itself up (returns) when the client disconnects or errors out.
        std::thread recvThread(recvHandler, clientSock, id);
        recvThread.detach();
    }

    return 0;
}

// One instance of this runs per connected client, for that client's
// entire session. Continuously reads input messages from the socket,
// parses them, and stashes the latest one in playersInput for the
// tick loop to consume. Exits (and cleans up that client's state) on
// disconnect, socket error, or malformed JSON.
int recvHandler(SOCKET clientSock, int clientId)
{
    char buffer[2048];

    while (true)
    {
        int bytesRecieved = recv(clientSock, buffer, sizeof(buffer), 0);

        if (bytesRecieved == SOCKET_ERROR)
        {
            int error = WSAGetLastError();

            if (error == WSAECONNRESET)
            {
                std::cout << std::format(
                    "client {} disconnected\n",
                    clientId);
            }
            else
            {
                std::cout << std::format(
                    "RECV::THREAD::ERROR | client {} | WSA error: {}\n",
                    clientId,
                    error);
            }

            {
                std::lock_guard<std::mutex> lock(playersMutex);

                activePlayers.erase(clientId);
                activeClients.erase(clientId);
                playersInput.erase(clientId);

                // Reclaim ids so they don't grow unbounded over a long
                // server session with lots of connect/disconnect churn.
                // NOTE: this reassigns LastClientId based on the highest
                // remaining id, which can cause a *new* client to reuse
                // an id freed by someone else mid-session if ids aren't
                // strictly sequential — worth double-checking against
                // intended id semantics.
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
            // A recv() of 0 bytes means the peer performed a graceful
            // shutdown (closed the connection), as opposed to an error.
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

            // Overwrite any previous unconsumed input for this client —
            // only the most recent input before each tick matters.
            std::lock_guard<std::mutex> lock(playersMutex);
            playersInput[clientId] = PlayerInput(movement, yaw);
        }

        catch (const json::exception &e)
        {
            // Malformed packet: rather than trying to resync, just drop
            // the connection. Simple but means one bad packet kicks
            // the client — fine for a prototype, worth revisiting later.
            std::cout << "PARSING::ERROR" << std::endl;
            break;
        }
    }

    closesocket(clientSock);
    return 0;
}

// Called once per tick from the main loop. Applies each connected
// client's most recent queued input to their player's position, then
// clears the queue so the same input isn't applied twice.
void inputHandler()
{
    std::map<int, PlayerInput> playersInputCopy;
    {
        // Grab and clear the shared input map under the lock, then do
        // all the actual work below on the local copy — keeps the lock
        // held for as short a time as possible.
        std::lock_guard<std::mutex> lock(playersMutex);
        playersInputCopy = playersInput;
        playersInput.clear();
    }

    if (playersInputCopy.empty())
    {
        return;
    }

    for (auto &[id, playerInput] : playersInputCopy)
    {
        std::string display;
        {
            // Still need the lock here since we're mutating
            // activePlayers, which recv/accept threads can also touch.
            std::lock_guard<std::mutex> lock(playersMutex);

            Player &currentPlayer = activePlayers.at(id);

            // Move the player along their requested direction, scaled
            // by speed and elapsed time so movement speed is
            // frame-rate/tick-rate independent.
            vec3 direction = directionVector(playerInput.m_movement, playerInput.m_yaw);

            currentPlayer.m_position = currentPlayer.m_position + direction * SPEED * DT;
            currentPlayer.m_yaw = playerInput.m_yaw;

            display = std::format("Client {} | {} | Yaw {} => Current Position ({},{},{}) ",
                                  id,
                                  movement_to_string(playerInput.m_movement),
                                  currentPlayer.m_yaw,
                                  currentPlayer.m_position.x,
                                  currentPlayer.m_position.y,
                                  currentPlayer.m_position.z);
        }
        std::cout << display << std::endl;
    }
}

// Called once per tick from the main loop. Sends the full current
// game state (every active player) to every connected client, so all
// clients stay in sync with the authoritative server state.
void broadcastHandler()
{
    int result;
    json data;
    std::map<int, SOCKET> activeClientsCopy;
    {
        // Snapshot both the socket list and the serialized player data
        // under one lock so we broadcast a single consistent state to
        // everyone, rather than risking a partial update mid-loop.
        std::lock_guard<std::mutex> lock(playersMutex);
        activeClientsCopy = activeClients;
        data["players"] = activePlayers;
    }

    std::string data_str = data.dump();
    for (auto &[id, clientSock] : activeClientsCopy)
    {
        // Length-prefix the message with a 4-byte big-endian size header
        // (htonl = host-to-network byte order) so the client knows exactly
        // how many bytes to read for this JSON payload. Without this,
        // TCP gives no message boundaries — the client could get partial
        // or merged messages, especially once packets are large or
        // sends are frequent.
        uint32_t size = htonl(static_cast<uint32_t>(data_str.size()));

        // Two sends: first the fixed-size header, then the actual
        // payload. The client is expected to always read exactly 4
        // bytes first, then read `size` bytes for the JSON body.
        send(clientSock, reinterpret_cast<char *>(&size), sizeof(size), 0);
        send(clientSock, data_str.c_str(), data_str.size(), 0);

        if (result == SOCKET_ERROR)
        {
            // Drop clients we can't reach: remove them from both shared
            // maps under the lock, then close the socket outside any
            // assumption about lock scope (closesocket doesn't need
            // the mutex, so it's fine after the block ends).
            {
                std::lock_guard<std::mutex> lock(playersMutex);

                activeClients.erase(id);
                activePlayers.erase(id);
            }

            closesocket(clientSock);
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

    // Winsock must be initialized before any socket calls on Windows.
    WSADATA wsaData;
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != NO_ERROR)
    {
        std::cout << "WINSOCK::INIT::ERROR" << std::endl;
        return 1;
    }

    // Server listens on all local interfaces, port 8080.
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    SOCKET TCPsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (TCPsocket == INVALID_SOCKET)
    {
        std::cout << "SCOKET::CREATION::ERROR" << std::endl;
        WSACleanup();
        return 1;
    }

    result = bind(TCPsocket, (sockaddr *)&serverAddr, sizeof(serverAddr));
    if (result == SOCKET_ERROR)
    {
        std::cout << "BINDING::ERROR" << std::endl;
        closesocket(TCPsocket);
        WSACleanup();
        return 1;
    }

    // SOMAXCONN: let the OS use its max backlog size for pending
    // connections rather than picking an arbitrary number ourselves.
    result = listen(TCPsocket, SOMAXCONN);
    if (result == SOCKET_ERROR)
    {
        std::cout << "LISTENING::ERROR" << std::endl;
        closesocket(TCPsocket);
        WSACleanup();
        return 1;
    }

    // Accepting new connections happens on its own thread so it never
    // blocks the fixed-rate game loop below.
    std::thread acceptThread(acceptHandler, std::ref(TCPsocket));

    auto previous_time = std::chrono::steady_clock::now();

    // Main fixed-tick-rate game loop: process input, broadcast state,
    // then sleep off whatever time is left in the tick budget so the
    // server runs at a steady ~60 updates/sec instead of spinning as
    // fast as possible.
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
            // Only sleep if the tick finished early; if processing took
            // longer than the tick budget, skip straight to the next
            // tick instead of sleeping negative time.
            std::this_thread::sleep_for(std::chrono::duration<float>(sleepTime));
        }
    }

    // Unreachable under the current design (the loop above never breaks),
    // but kept as the intended graceful-shutdown path.
    acceptThread.join();

    closesocket(TCPsocket);
    WSACleanup();

    return 0;
}