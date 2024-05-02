#include "API/ARK/Ark.h"
#include "json.hpp"
#include <fstream>

int configCacheDecayMins;

FString damClassPath = "Blueprint'/Game/PrimalEarth/CoreBlueprints/Inventories/PrimalInventoryBP_BeaverDam.PrimalInventoryBP_BeaverDam'";
UClass* damClass;
FString woodClassPath = "Blueprint'/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_Wood.PrimalItemResource_Wood'";
UClass* woodClass;

DECLARE_HOOK(UPrimalInventoryComponent_ServerCloseRemoteInventory, void, UPrimalInventoryComponent*, AShooterPlayerController*);
void Hook_UPrimalInventoryComponent_ServerCloseRemoteInventory(UPrimalInventoryComponent* _this, AShooterPlayerController* ByPC)
{
    bool isOnlyWood = true;

    UPrimalInventoryComponent_ServerCloseRemoteInventory_original(_this, ByPC);

    if (!damClass || !woodClass)
        return;

    // If the closed inventory is a dam that only contains wood, drop it into an item cache
    if (_this->IsA(damClass) && (_this->InventoryItemsField().Num() > 0)) {
        for (UPrimalItem* item : _this->InventoryItemsField()) {
            if (item && !item->IsA(woodClass)) {
                isOnlyWood = false;
                break;
            }
        }
        if (isOnlyWood) {
            FVector vec = { 0, 0, 0 };
            _this->BPDropInventoryDeposit(ArkApi::GetApiUtils().GetWorld()->GetTimeSeconds() + (configCacheDecayMins * 60), INT_MAX, false, vec);
        }
    }
}

// Called when the server is ready, do post-"server ready" initialization here
void OnServerReady()
{
    damClass = UVictoryCore::BPLoadClass(&damClassPath);
    if (!damClass)
        Log::GetLog()->error("Plugin_ServerReadyInit() - Beaver dam class not found");

    woodClass = UVictoryCore::BPLoadClass(&woodClassPath);
    if (!woodClass)
        Log::GetLog()->error("Plugin_ServerReadyInit() - Wood class not found");
}

// Hook that triggers once when the server is ready
DECLARE_HOOK(AShooterGameMode_BeginPlay, void, AShooterGameMode*);
void Hook_AShooterGameMode_BeginPlay(AShooterGameMode* _this)
{
    AShooterGameMode_BeginPlay_original(_this);

    OnServerReady();
}

void ReadConfig()
{
    nlohmann::json config;
    const std::string config_path = ArkApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" + PROJECT_NAME + "/config.json";
    std::ifstream file{ config_path };
    if (!file.is_open())
        throw std::runtime_error("Can't open config.json");
    file >> config;
    file.close();

    configCacheDecayMins = config["CacheDecayMins"];
}

void ReloadConfigConsoleCmd(APlayerController* playerController, FString*, bool)
{
    AShooterPlayerController* shooterController = static_cast<AShooterPlayerController*>(playerController);

    try {
        ReadConfig();
    } catch (const std::exception& error) {
        ArkApi::GetApiUtils().SendServerMessage(shooterController, FColorList::Red, "Failed to reload config");

        Log::GetLog()->error(error.what());
        return;
    }

    ArkApi::GetApiUtils().SendServerMessage(shooterController, FColorList::Green, "Reloaded config");
}

void ReloadConfigRconCmd(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
{
    FString reply;

    try {
        ReadConfig();
    } catch (const std::exception& error) {
        Log::GetLog()->error(error.what());

        reply = error.what();
        connection->SendMessageW(packet->Id, 0, &reply);
        return;
    }

    reply = "Reloaded config";
    connection->SendMessageW(packet->Id, 0, &reply);
}

// Called by ArkServerApi when the plugin is loaded, do pre-"server ready" initialization here
extern "C" __declspec(dllexport) void Plugin_Init()
{
    Log::Get().Init(PROJECT_NAME);

    ReadConfig();

    ArkApi::GetCommands().AddConsoleCommand(PROJECT_NAME".Reload", &ReloadConfigConsoleCmd);
    ArkApi::GetCommands().AddRconCommand(PROJECT_NAME".Reload", &ReloadConfigRconCmd);

    ArkApi::GetHooks().SetHook("AShooterGameMode.BeginPlay",
        &Hook_AShooterGameMode_BeginPlay,
        &AShooterGameMode_BeginPlay_original);
    ArkApi::GetHooks().SetHook("UPrimalInventoryComponent.ServerCloseRemoteInventory",
        &Hook_UPrimalInventoryComponent_ServerCloseRemoteInventory,
        &UPrimalInventoryComponent_ServerCloseRemoteInventory_original);

    // If the server is ready, call OnServerReady() for post-"server ready" initialization
    if (ArkApi::GetApiUtils().GetStatus() == ArkApi::ServerStatus::Ready)
        OnServerReady();
}

// Called by ArkServerApi when the plugin is unloaded, do cleanup here
extern "C" __declspec(dllexport) void Plugin_Unload()
{
    ArkApi::GetCommands().RemoveConsoleCommand(PROJECT_NAME".Reload");
    ArkApi::GetCommands().RemoveRconCommand(PROJECT_NAME".Reload");

    ArkApi::GetHooks().DisableHook("AShooterGameMode.BeginPlay",
        &Hook_AShooterGameMode_BeginPlay);
    ArkApi::GetHooks().DisableHook("UPrimalInventoryComponent.ServerCloseRemoteInventory",
        &Hook_UPrimalInventoryComponent_ServerCloseRemoteInventory);
}