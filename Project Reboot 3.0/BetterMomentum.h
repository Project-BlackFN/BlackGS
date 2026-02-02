#ifndef BETTER_MOMENTUM_H
#define BETTER_MOMENTUM_H

#include <stdbool.h>

extern "C" {
    // Configuration
    __declspec(dllexport) bool LoadBetterMomentum();
    __declspec(dllexport) const char* GetBackendUrl();
    __declspec(dllexport) const char* GetMasterAuthKey();
    __declspec(dllexport) const char* GetWebhookUptimeUrl();
    __declspec(dllexport) const char* GetPublicIp();
    __declspec(dllexport) bool IsConfigLoaded();

    // Game Settings
    __declspec(dllexport) void SetGamePort(int port);
    __declspec(dllexport) int GetGamePort();
    __declspec(dllexport) void SetGamePlaylist(const char* playlist);
    __declspec(dllexport) const char* GetGamePlaylist();
    __declspec(dllexport) void SetJoinState(bool state);

    // Server Management
    __declspec(dllexport) bool RegisterServer();
    __declspec(dllexport) const char* GetServerSecretKey();
    __declspec(dllexport) const char* GetServerId();
    __declspec(dllexport) bool RemoveServer();
    __declspec(dllexport) int GetAvaPort();
    __declspec(dllexport) const char* GetRequiredPlaylist();

    // Heartbeat System
    __declspec(dllexport) bool SendHeartbeat();
    __declspec(dllexport) bool StartHeartbeat();
    __declspec(dllexport) void StopHeartbeat();
    __declspec(dllexport) bool IsHeartbeatRunning();

    // Game Phase Counter
    __declspec(dllexport) bool StartCount();
    __declspec(dllexport) void StopCount();

    // Player Count Monitor
    __declspec(dllexport) bool StartPlayerCount();
    __declspec(dllexport) void StopPlayerCount();
    __declspec(dllexport) bool IsPlayerCountRunning();
}

#endif // BETTER_MOMENTUM_H