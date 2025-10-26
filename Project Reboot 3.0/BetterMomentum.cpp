#include "BetterMomentum.h"
#include "settings.h"
#include <curl/curl.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>
#include "FortGameStateAthena.h"
#include <cstdlib>

#ifndef CURLOPT_RESPONSE_CODE
#define CURLOPT_RESPONSE_CODE CURLINFO_RESPONSE_CODE
#endif
#include "FortGameModeAthena.h"
#include "gui.h"

std::string g_serverSecretKey;
std::string g_serverId;
std::string g_backendUrl = BACKEND_URL;
std::string g_masterAuthKey = SERVER_AUTH_KEY;
std::string g_webhookUptimeUrl = WEBHOOK_UPTIME_URL;
std::string g_publicIp = PUBLIC_IP;
bool g_configLoaded = true;
bool g_serverRegistered = false;

bool g_joinState = true;
int g_gamePort = 0;
std::string g_gamePlaylist;

std::thread g_heartbeatThread;
std::atomic<bool> g_heartbeatRunning{ false };
std::atomic<bool> g_stopHeartbeat{ false };
std::thread g_countThread;
std::atomic<bool> g_countRunning{ false };
std::atomic<bool> g_stopCount{ false };
std::thread g_playerCountThread;
std::atomic<bool> g_playerCountRunning{ false };
std::atomic<bool> g_stopPlayerCount{ false };

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string BuildJsonPayload(const std::string& ip, int port, const std::string& playlist, const std::string& serverKey) {
    std::ostringstream json;
    json << "{"
        << "\"ip\":\"" << ip << "\","
        << "\"port\":" << port << ","
        << "\"playlist\":\"" << playlist << "\","
        << "\"serverKey\":\"" << serverKey << "\""
        << "}";
    return json.str();
}

std::string BuildHeartbeatPayload(const std::string& ip, int port, const std::string& playlist, const std::string& serverKey, bool joinable) {
    std::ostringstream json;
    json << "{"
        << "\"ip\":\"" << ip << "\","
        << "\"port\":" << port << ","
        << "\"playlist\":\"" << playlist << "\","
        << "\"serverKey\":\"" << serverKey << "\","
        << "\"joinable\":" << (joinable ? "true" : "false")
        << "}";
    return json.str();
}

std::string ExtractJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    size_t startPos = json.find(searchKey);
    if (startPos == std::string::npos) return "";

    startPos += searchKey.length();
    size_t endPos = json.find("\"", startPos);
    if (endPos == std::string::npos) return "";

    return json.substr(startPos, endPos - startPos);
}

std::string ParseServerList(const std::string& jsonResponse) {
    std::string result;
    size_t pos = 0;

    while ((pos = jsonResponse.find("\"ip\":", pos)) != std::string::npos) {
        size_t ipStart = jsonResponse.find("\"", pos + 5);
        if (ipStart == std::string::npos) break;
        ipStart++;

        size_t ipEnd = jsonResponse.find("\"", ipStart);
        if (ipEnd == std::string::npos) break;

        std::string ip = jsonResponse.substr(ipStart, ipEnd - ipStart);

        if (ip != g_publicIp) {
            pos = ipEnd;
            continue;
        }

        size_t portPos = jsonResponse.find("\"port\":", ipEnd);
        if (portPos == std::string::npos) break;

        size_t portStart = portPos + 7;
        size_t portEnd = jsonResponse.find_first_of(",}", portStart);
        if (portEnd == std::string::npos) break;

        std::string portStr = jsonResponse.substr(portStart, portEnd - portStart);

        portStr.erase(std::remove_if(portStr.begin(), portStr.end(), ::isspace), portStr.end());

        if (!result.empty()) {
            result += ", ";
        }
        result += portStr;

        pos = portEnd;
    }

    return result;
}

void HeartbeatWorker() {
    constexpr int SLEEP_INTERVALS = 450;
    constexpr int SLEEP_MS = 100;

    while (!g_stopHeartbeat.load(std::memory_order_acquire)) {
        auto start = std::chrono::steady_clock::now();
        auto end = start + std::chrono::milliseconds(SLEEP_INTERVALS * SLEEP_MS);

        while (std::chrono::steady_clock::now() < end) {
            if (g_stopHeartbeat.load(std::memory_order_acquire)) {
                g_heartbeatRunning.store(false, std::memory_order_release);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        SendHeartbeat();
    }
    g_heartbeatRunning.store(false, std::memory_order_release);
}

void CountWorker() {
    constexpr int CHECK_INTERVAL_SEC = 15;
    constexpr int PHASE_ENDED = 5;
    constexpr int PHASE_INGAME = 2;

    AFortGameStateAthena* gameState = nullptr;
    UWorld* world = nullptr;

    while (!g_stopCount.load(std::memory_order_acquire)) {
        auto start = std::chrono::steady_clock::now();
        auto end = start + std::chrono::seconds(CHECK_INTERVAL_SEC);

        while (std::chrono::steady_clock::now() < end) {
            if (g_stopCount.load(std::memory_order_acquire)) {
                g_countRunning.store(false, std::memory_order_release);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (!world) {
            world = GetWorld();
            if (!world) continue;
        }

        gameState = Cast<AFortGameStateAthena>(world->GetGameState());
        if (!gameState) {
            world = nullptr;
            continue;
        }

        int phase = static_cast<int>(gameState->GetGamePhase());
        std::cout << "Game Phase: " << phase << std::endl;

        if (phase == 5) {
            RemoveServer();
            g_countRunning.store(false, std::memory_order_release);
            std::exit(0);
        }

        if (phase == 4) {
            StopCount();
            break;
        }
    }

    g_countRunning.store(false, std::memory_order_release);
}

void PlayerCountWorker() {
    constexpr int CHECK_INTERVAL_SEC = 8;

    AFortGameStateAthena* gameState = nullptr;
    UWorld* world = nullptr;
    int consecutiveZeroCount = 0;
    constexpr int ZERO_THRESHOLD = 3;

    while (!g_stopPlayerCount.load(std::memory_order_relaxed)) {
        auto start = std::chrono::steady_clock::now();
        auto end = start + std::chrono::seconds(CHECK_INTERVAL_SEC);

        while (std::chrono::steady_clock::now() < end) {
            if (g_stopPlayerCount.load(std::memory_order_relaxed)) {
                g_playerCountRunning.store(false, std::memory_order_release);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        if (!world) {
            world = GetWorld();
            if (!world) continue;
        }

        gameState = Cast<AFortGameStateAthena>(world->GetGameState());
        if (!gameState) {
            world = nullptr;
            continue;
        }

        int players = gameState->GetPlayersLeft();
        std::cout << "Players Left: " << players << std::endl;

        if (players == 0) {
            ++consecutiveZeroCount;
            if (consecutiveZeroCount >= ZERO_THRESHOLD) {
                std::cout << "No players left for " << ZERO_THRESHOLD << " checks, shutting down server..." << std::endl;
                RemoveServer();
                g_playerCountRunning.store(false, std::memory_order_release);
                std::exit(0);
            }
        }
        else {
            consecutiveZeroCount = 0;
        }
    }

    g_playerCountRunning.store(false, std::memory_order_release);
}

extern "C" __declspec(dllexport) bool StartCount() {
    bool expected = false;
    if (!g_countRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        StopCount();
        g_countRunning.store(true, std::memory_order_release);
    }

    g_stopCount.store(false, std::memory_order_release);

    try {
        if (g_countThread.joinable()) {
            g_countThread.join();
        }
        g_countThread = std::thread(CountWorker);
        g_countThread.detach();
        return true;
    }
    catch (const std::exception& e) {
        g_countRunning.store(false, std::memory_order_release);
        std::cerr << "StartCount failed: " << e.what() << std::endl;
        return false;
    }
}

extern "C" __declspec(dllexport) void StopCount() {
    if (!g_countRunning.load(std::memory_order_acquire)) {
        return;
    }

    g_stopCount.store(true, std::memory_order_release);

    constexpr int MAX_WAIT_MS = 1000;
    constexpr int CHECK_INTERVAL_MS = 50;

    for (int elapsed = 0; elapsed < MAX_WAIT_MS && g_countRunning.load(std::memory_order_acquire); elapsed += CHECK_INTERVAL_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_INTERVAL_MS));
    }
}

extern "C" __declspec(dllexport) bool StartPlayerCount() {
    bool expected = false;
    if (!g_playerCountRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        StopPlayerCount();
        g_playerCountRunning.store(true, std::memory_order_release);
    }

    g_stopPlayerCount.store(false, std::memory_order_release);

    try {
        if (g_playerCountThread.joinable()) {
            g_playerCountThread.join();
        }
        g_playerCountThread = std::thread(PlayerCountWorker);
        g_playerCountThread.detach();
        return true;
    }
    catch (const std::exception& e) {
        g_playerCountRunning.store(false, std::memory_order_release);
        std::cerr << "StartPlayerCount failed: " << e.what() << std::endl;
        return false;
    }
}

extern "C" __declspec(dllexport) void StopPlayerCount() {
    if (!g_playerCountRunning.load(std::memory_order_relaxed)) {
        return;
    }

    g_stopPlayerCount.store(true, std::memory_order_release);

    constexpr int MAX_WAIT_MS = 1000;
    constexpr int CHECK_INTERVAL_MS = 50;

    for (int elapsed = 0; elapsed < MAX_WAIT_MS && g_playerCountRunning.load(std::memory_order_relaxed); elapsed += CHECK_INTERVAL_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_INTERVAL_MS));
    }
}

extern "C" __declspec(dllexport) bool IsPlayerCountRunning() {
    return g_playerCountRunning.load(std::memory_order_relaxed);
}

extern "C" __declspec(dllexport) int GetAvaPort() {
    static std::string serverList;
    serverList.clear();

    if (!g_configLoaded) {
        return 7777;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return 7777;
    }

    std::string readBuffer;
    std::string url = g_backendUrl + "/bettermomentum/serverlist";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

    CURLcode res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK || response_code != 200) {
        return 7777;
    }

    serverList = ParseServerList(readBuffer);

    if (serverList.empty()) {
        return 7777;
    }

    int highestPort = 7777;
    std::stringstream ss(serverList);
    std::string portStr;

    while (std::getline(ss, portStr, ',')) {
        portStr.erase(std::remove_if(portStr.begin(), portStr.end(), ::isspace), portStr.end());

        try {
            int port = std::stoi(portStr);
            if (port > highestPort) {
                highestPort = port;
            }
        }
        catch (...) {
            continue;
        }
    }

    return highestPort + 3;
}

extern "C" __declspec(dllexport) bool RegisterServer() {
    if (!g_configLoaded) return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string readBuffer;
    std::string jsonString = BuildJsonPayload(g_publicIp, g_gamePort, g_gamePlaylist, g_masterAuthKey);
    std::string url = g_backendUrl + "/bettermomentum/addserver";

    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);

    CURLcode res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return false;

    if (response_code == 201) {
        g_serverRegistered = true;
        g_serverSecretKey = ExtractJsonValue(readBuffer, "serverSecretKey");
        g_serverId = ExtractJsonValue(readBuffer, "serverId");

        return !g_serverSecretKey.empty() && !g_serverId.empty();
    }

    return false;
}

extern "C" __declspec(dllexport) bool RemoveServer() {
    if (g_serverSecretKey.empty() || !g_configLoaded) return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string readBuffer;
    std::string jsonString = BuildJsonPayload(g_publicIp, g_gamePort, g_gamePlaylist, g_serverSecretKey);
    std::string url = g_backendUrl + "/bettermomentum/removeserver";

    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);

    CURLcode res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return false;

    if (response_code == 200) {
        g_serverSecretKey.clear();
        g_serverId.clear();
        g_serverRegistered = false;
        return true;
    }

    return false;
}

extern "C" __declspec(dllexport) bool SendHeartbeat() {
    if (!g_serverRegistered || g_serverSecretKey.empty() || !g_configLoaded) return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string readBuffer;
    std::string jsonString = BuildHeartbeatPayload(g_publicIp, g_gamePort, g_gamePlaylist, g_serverSecretKey, g_joinState);
    std::string url = g_backendUrl + "/bettermomentum/heartbeat";

    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);

    CURLcode res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK && response_code == 200;
}

extern "C" __declspec(dllexport) bool StartHeartbeat() {
    if (!g_serverRegistered || g_serverSecretKey.empty() || !g_configLoaded) {
        return false;
    }

    bool expected = false;
    if (!g_heartbeatRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        StopHeartbeat();
        g_heartbeatRunning.store(true, std::memory_order_release);
    }

    g_stopHeartbeat.store(false, std::memory_order_release);

    try {
        if (g_heartbeatThread.joinable()) {
            g_heartbeatThread.join();
        }
        g_heartbeatThread = std::thread(HeartbeatWorker);
        g_heartbeatThread.detach();
        return true;
    }
    catch (const std::exception& e) {
        g_heartbeatRunning.store(false, std::memory_order_release);
        std::cerr << "StartHeartbeat failed: " << e.what() << std::endl;
        return false;
    }
}

extern "C" __declspec(dllexport) void StopHeartbeat() {
    if (!g_heartbeatRunning.load(std::memory_order_acquire)) {
        return;
    }

    g_stopHeartbeat.store(true, std::memory_order_release);

    constexpr int MAX_WAIT_MS = 1000;
    constexpr int CHECK_INTERVAL_MS = 50;

    for (int elapsed = 0; elapsed < MAX_WAIT_MS && g_heartbeatRunning.load(std::memory_order_acquire); elapsed += CHECK_INTERVAL_MS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_INTERVAL_MS));
    }
}

extern "C" __declspec(dllexport) const char* GetRequiredPlaylist() {
    static std::string cachedGamemode;
    cachedGamemode.clear();

    if (!g_configLoaded) {
        return nullptr;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return nullptr;
    }

    std::string readBuffer;
    std::string url = g_backendUrl + "/bettermomentum/matchmaker/serverInfo";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

    CURLcode res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK || response_code != 200) {
        std::exit(0);
        return nullptr;
    }

    std::string gamemode = ExtractJsonValue(readBuffer, "gamemode");
    if (gamemode.empty() || readBuffer.find("\"gamemode\":null") != std::string::npos) {
        std::exit(0);
        return nullptr;
    }

    cachedGamemode = gamemode;
    SetPlaylistNameX(cachedGamemode.c_str());
    return cachedGamemode.c_str();
}

extern "C" __declspec(dllexport) bool IsHeartbeatRunning() {
    return g_heartbeatRunning.load();
}

extern "C" __declspec(dllexport) const char* GetServerSecretKey() {
    return g_serverSecretKey.empty() ? nullptr : g_serverSecretKey.c_str();
}

extern "C" __declspec(dllexport) const char* GetServerId() {
    return g_serverId.empty() ? nullptr : g_serverId.c_str();
}

extern "C" __declspec(dllexport) bool LoadBetterMomentum() {
    return g_configLoaded;
}

extern "C" __declspec(dllexport) const char* GetBackendUrl() {
    return g_backendUrl.c_str();
}

extern "C" __declspec(dllexport) const char* GetMasterAuthKey() {
    return g_masterAuthKey.c_str();
}

extern "C" __declspec(dllexport) const char* GetWebhookUptimeUrl() {
    return g_webhookUptimeUrl.c_str();
}

extern "C" __declspec(dllexport) const char* GetPublicIp() {
    return g_publicIp.c_str();
}

extern "C" __declspec(dllexport) bool IsConfigLoaded() {
    return g_configLoaded;
}

extern "C" __declspec(dllexport) void SetGamePort(int port) {
    g_gamePort = port;
}

extern "C" __declspec(dllexport) int GetGamePort() {
    return g_gamePort;
}

extern "C" __declspec(dllexport) void SetGamePlaylist(const char* playlist) {
    if (!playlist) return;
    std::string pl = playlist;
    size_t lastSlash = pl.find_last_of('/');
    if (lastSlash != std::string::npos) pl = pl.substr(lastSlash + 1);
    size_t firstDot = pl.find_first_of('.');
    if (firstDot != std::string::npos) pl = pl.substr(0, firstDot);
    std::transform(pl.begin(), pl.end(), pl.begin(), ::tolower);
    g_gamePlaylist = pl;
}

extern "C" __declspec(dllexport) const char* GetGamePlaylist() {
    return g_gamePlaylist.empty() ? nullptr : g_gamePlaylist.c_str();
}

extern "C" __declspec(dllexport) void SetJoinState(bool state) {
    g_joinState = state;
}