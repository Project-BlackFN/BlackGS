#include "FortPlayerController.h"

#include "Rotator.h"
#include "BuildingSMActor.h"
#include "FortGameModeAthena.h"

#include "FortPlayerState.h"
#include "BuildingWeapons.h"

#include "ActorComponent.h"
#include "FortPlayerStateAthena.h"
#include "globals.h"
#include "FortPlayerControllerAthena.h"
#include "BuildingContainer.h"
#include "FortLootPackage.h"
#include "FortPickup.h"
#include "FortPlayerPawn.h"
#include <memcury.h>
#include "KismetStringLibrary.h"
#include "FortGadgetItemDefinition.h"
#include "FortAbilitySet.h"
#include "vendingmachine.h"
#include "KismetSystemLibrary.h"
#include "gui.h"
#include "FortAthenaMutator_InventoryOverride.h"
#include "FortAthenaMutator_TDM.h"
#include "BetterMomentum.h"

void AFortPlayerController::ClientReportDamagedResourceBuilding(ABuildingSMActor* BuildingSMActor, EFortResourceType PotentialResourceType, int PotentialResourceCount, bool bDestroyed, bool bJustHitWeakspot)
{
	static auto fn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ClientReportDamagedResourceBuilding");

	struct { ABuildingSMActor* BuildingSMActor; EFortResourceType PotentialResourceType; int PotentialResourceCount; bool bDestroyed; bool bJustHitWeakspot; }
	AFortPlayerController_ClientReportDamagedResourceBuilding_Params{BuildingSMActor, PotentialResourceType, PotentialResourceCount, bDestroyed, bJustHitWeakspot};

	this->ProcessEvent(fn, &AFortPlayerController_ClientReportDamagedResourceBuilding_Params);
}

void AFortPlayerController::ClientEquipItem(const FGuid& ItemGuid, bool bForceExecution)
{
	static auto ClientEquipItemFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerControllerAthena.ClientEquipItem") 
		? FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerControllerAthena.ClientEquipItem") 
		: FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ClientEquipItem");

	if (ClientEquipItemFn)
	{
		struct
		{
			FGuid                                       ItemGuid;                                                 // (ConstParm, Parm, ZeroConstructor, ReferenceParm, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
			bool                                               bForceExecution;                                          // (Parm, ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
		} AFortPlayerController_ClientEquipItem_Params{ ItemGuid, bForceExecution };

		this->ProcessEvent(ClientEquipItemFn, &AFortPlayerController_ClientEquipItem_Params);
	}
}

void AFortPlayerController::ClientForceCancelBuildingTool()
{
	static auto ClientForceCancelBuildingToolFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ClientForceCancelBuildingTool");

	if (ClientForceCancelBuildingToolFn)
		this->ProcessEvent(ClientForceCancelBuildingToolFn);
}

bool AFortPlayerController::DoesBuildFree()
{
	if (Globals::bInfiniteMaterials)
		return true;

	static auto bBuildFreeOffset = GetOffset("bBuildFree");
	static auto bBuildFreeFieldMask = GetFieldMask(GetProperty("bBuildFree"));
	return ReadBitfieldValue(bBuildFreeOffset, bBuildFreeFieldMask);
}

void AFortPlayerController::DropAllItems(const std::vector<UFortItemDefinition*>& IgnoreItemDefs, bool bIgnoreSecondaryQuickbar, bool bRemoveIfNotDroppable, bool RemovePickaxe)
{
	auto Pawn = this->GetMyFortPawn();

	if (!Pawn)
		return;

	auto WorldInventory = this->GetWorldInventory();

	if (!WorldInventory)
		return;

	auto& ItemInstances = WorldInventory->GetItemList().GetItemInstances();
	auto Location = Pawn->GetActorLocation();

	std::vector<std::pair<FGuid, int>> GuidAndCountsToRemove;

	auto PickaxeInstance = WorldInventory->GetPickaxeInstance();

	for (int i = 0; i < ItemInstances.Num(); ++i)
	{
		auto ItemInstance = ItemInstances.at(i);

		if (!ItemInstance)
			continue;

		auto ItemEntry = ItemInstance->GetItemEntry();

		if (RemovePickaxe && ItemInstance == PickaxeInstance)
		{
			GuidAndCountsToRemove.push_back({ ItemEntry->GetItemGuid(), ItemEntry->GetCount() });
			continue;
		}

		auto WorldItemDefinition = Cast<UFortWorldItemDefinition>(ItemEntry->GetItemDefinition());

		if (!WorldItemDefinition || std::find(IgnoreItemDefs.begin(), IgnoreItemDefs.end(), WorldItemDefinition) != IgnoreItemDefs.end())
			continue;

		if (bIgnoreSecondaryQuickbar && !IsPrimaryQuickbar(WorldItemDefinition))
			continue;

		if (!bRemoveIfNotDroppable && !WorldItemDefinition->CanBeDropped())
			continue;

		GuidAndCountsToRemove.push_back({ ItemEntry->GetItemGuid(), ItemEntry->GetCount() });

		if (bRemoveIfNotDroppable && !WorldItemDefinition->CanBeDropped())
			continue;
	
		PickupCreateData CreateData;
		CreateData.ItemEntry = ItemEntry;
		CreateData.SpawnLocation = Location;
		CreateData.SourceType = EFortPickupSourceTypeFlag::GetPlayerValue();

		AFortPickup::SpawnPickup(CreateData);
	}

	for (auto& Pair : GuidAndCountsToRemove)
	{
		WorldInventory->RemoveItem(Pair.first, nullptr, Pair.second, true);
	}

	WorldInventory->Update();
}

void AFortPlayerController::ApplyCosmeticLoadout()
{
	auto PlayerStateAsFort = Cast<AFortPlayerStateAthena>(GetPlayerState());

	if (!PlayerStateAsFort)
		return;

	auto PawnAsFort = Cast<AFortPlayerPawn>(GetMyFortPawn());

	if (!PawnAsFort)
		return;

	static auto UpdatePlayerCustomCharacterPartsVisualizationFn = FindObject<UFunction>(L"/Script/FortniteGame.FortKismetLibrary.UpdatePlayerCustomCharacterPartsVisualization");

	if (!UpdatePlayerCustomCharacterPartsVisualizationFn)
	{
		if (Addresses::ApplyCharacterCustomization)
		{
			static void* (*ApplyCharacterCustomizationOriginal)(AFortPlayerState* a1, AFortPawn* a3) = decltype(ApplyCharacterCustomizationOriginal)(Addresses::ApplyCharacterCustomization);
			ApplyCharacterCustomizationOriginal(PlayerStateAsFort, PawnAsFort);

			PlayerStateAsFort->ForceNetUpdate();
			PawnAsFort->ForceNetUpdate();
			this->ForceNetUpdate();

			return;
		}

		auto CosmeticLoadout = GetCosmeticLoadoutOffset() != -1 ? this->GetCosmeticLoadout() : nullptr;

		if (CosmeticLoadout)
		{
			/* static auto Pawn_CosmeticLoadoutOffset = PawnAsFort->GetOffset("CosmeticLoadout");

			if (Pawn_CosmeticLoadoutOffset != -1)
			{
				CopyStruct(PawnAsFort->GetPtr<__int64>(Pawn_CosmeticLoadoutOffset), CosmeticLoadout, FFortAthenaLoadout::GetStructSize());
			} */

			auto Character = CosmeticLoadout->GetCharacter();

			// LOG_INFO(LogDev, "Character: {}", __int64(Character));
			// LOG_INFO(LogDev, "Character Name: {}", Character ? Character->GetFullName() : "InvalidObject");

			if (PawnAsFort)
			{
				ApplyCID(PawnAsFort, Character, false);

				auto Backpack = CosmeticLoadout->GetBackpack();

				if (Backpack)
				{
					static auto CharacterPartsOffset = Backpack->GetOffset("CharacterParts");

					if (CharacterPartsOffset != -1)
					{
						auto& BackpackCharacterParts = Backpack->Get<TArray<UObject*>>(CharacterPartsOffset);

						for (int i = 0; i < BackpackCharacterParts.Num(); ++i)
						{
							auto BackpackCharacterPart = BackpackCharacterParts.at(i);

							if (!BackpackCharacterPart)
								continue;

							PawnAsFort->ServerChoosePart(EFortCustomPartType::Backpack, BackpackCharacterPart);
						}

						// UFortKismetLibrary::ApplyCharacterCosmetics(GetWorld(), BackpackCharacterParts, PlayerStateAsFort, &aa);
					}
				}
			}
		}
		else
		{
			static auto HeroTypeOffset = PlayerStateAsFort->GetOffset("HeroType");
			ApplyHID(PawnAsFort, PlayerStateAsFort->Get(HeroTypeOffset));
		}

		PlayerStateAsFort->ForceNetUpdate();
		PawnAsFort->ForceNetUpdate();
		this->ForceNetUpdate();

		return;
	}

	UFortKismetLibrary::StaticClass()->ProcessEvent(UpdatePlayerCustomCharacterPartsVisualizationFn, &PlayerStateAsFort);

	PlayerStateAsFort->ForceNetUpdate();
	PawnAsFort->ForceNetUpdate();
	this->ForceNetUpdate();
}

void AFortPlayerController::ServerLoadingScreenDroppedHook(UObject* Context, FFrame* Stack, void* Ret)
{
	LOG_INFO(LogDev, "ServerLoadingScreenDroppedHook!");

	auto PlayerController = (AFortPlayerController*)Context;

	PlayerController->ApplyCosmeticLoadout();

	return ServerLoadingScreenDroppedOriginal(Context, Stack, Ret);
}

void AFortPlayerController::ServerRepairBuildingActorHook(AFortPlayerController* PlayerController, ABuildingSMActor* BuildingActorToRepair)
{
	if (!BuildingActorToRepair 
		// || !BuildingActorToRepair->GetWorld()
		)
		return;

	if (BuildingActorToRepair->GetEditingPlayer())
	{
		// ClientSendMessage
		return;
	}

	float BuildingHealthPercent = BuildingActorToRepair->GetHealthPercent();

	// todo not hardcode these

	float BuildingCost = 10;
	float RepairCostMultiplier = 0.75;

	float BuildingHealthPercentLost = 1.0 - BuildingHealthPercent;
	float RepairCostUnrounded = (BuildingCost * BuildingHealthPercentLost) * RepairCostMultiplier;
	float RepairCost = std::floor(RepairCostUnrounded > 0 ? RepairCostUnrounded < 1 ? 1 : RepairCostUnrounded : 0);

	if (RepairCost < 0)
		return;

	auto ResourceItemDefinition = UFortKismetLibrary::K2_GetResourceItemDefinition(BuildingActorToRepair->GetResourceType());

	if (!ResourceItemDefinition)
		return;

	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return;

	if (!PlayerController->DoesBuildFree())
	{
		auto ResourceInstance = WorldInventory->FindItemInstance(ResourceItemDefinition);

		if (!ResourceInstance)
			return;

		bool bShouldUpdate = false;

		if (!WorldInventory->RemoveItem(ResourceInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, RepairCost))
			return;

		if (bShouldUpdate)
			WorldInventory->Update();
	}

	struct { AFortPlayerController* RepairingController; int ResourcesSpent; } ABuildingSMActor_RepairBuilding_Params{ PlayerController, RepairCost };

	static auto RepairBuildingFn = FindObject<UFunction>(L"/Script/FortniteGame.BuildingSMActor.RepairBuilding");
	BuildingActorToRepair->ProcessEvent(RepairBuildingFn, &ABuildingSMActor_RepairBuilding_Params);
	// PlayerController->FortClientPlaySoundAtLocation(PlayerController->StartRepairSound, BuildingActorToRepair->K2_GetActorLocation(), 0, 0);
}

void AFortPlayerController::ServerExecuteInventoryItemHook(AFortPlayerController* PlayerController, FGuid ItemGuid)
{
	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return;

	auto ItemInstance = WorldInventory->FindItemInstance(ItemGuid);
	auto Pawn = Cast<AFortPlayerPawn>(PlayerController->GetPawn());

	if (!ItemInstance || !Pawn)
		return;

	FGuid OldGuid = Pawn->GetCurrentWeapon() ? Pawn->GetCurrentWeapon()->GetItemEntryGuid() : FGuid(-1, -1, -1, -1);
	UFortItem* OldInstance = OldGuid == FGuid(-1, -1, -1, -1) ? nullptr : WorldInventory->FindItemInstance(OldGuid);
	auto ItemDefinition = ItemInstance->GetItemEntry()->GetItemDefinition();

	if (!ItemDefinition)
		return;

	// LOG_INFO(LogDev, "Equipping ItemDefinition: {}", ItemDefinition->GetFullName());

	static auto FortGadgetItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.FortGadgetItemDefinition");

	UFortGadgetItemDefinition* GadgetItemDefinition = Cast<UFortGadgetItemDefinition>(ItemDefinition);

	if (GadgetItemDefinition)
	{
		static auto GetWeaponItemDefinition = FindObject<UFunction>(L"/Script/FortniteGame.FortGadgetItemDefinition.GetWeaponItemDefinition");

		if (GetWeaponItemDefinition)
		{
			ItemDefinition->ProcessEvent(GetWeaponItemDefinition, &ItemDefinition);
		}
		else
		{
			static auto GetDecoItemDefinition = FindObject<UFunction>(L"/Script/FortniteGame.FortGadgetItemDefinition.GetDecoItemDefinition");
			ItemDefinition->ProcessEvent(GetDecoItemDefinition, &ItemDefinition);
		}

		// LOG_INFO(LogDev, "Equipping Gadget: {}", ItemDefinition->GetFullName());
	}

	if (auto DecoItemDefinition = Cast<UFortDecoItemDefinition>(ItemDefinition))
	{
		if (Fortnite_Version < 18) // gg
		{
			if (Pawn->PickUpActor(nullptr, DecoItemDefinition))
			{
				Pawn->GetCurrentWeapon()->GetItemEntryGuid() = ItemGuid;

				static auto FortDecoTool_ContextTrapStaticClass = FindObject<UClass>(L"/Script/FortniteGame.FortDecoTool_ContextTrap");

				if (Pawn->GetCurrentWeapon()->IsA(FortDecoTool_ContextTrapStaticClass))
				{
					static auto ContextTrapItemDefinitionOffset = Pawn->GetCurrentWeapon()->GetOffset("ContextTrapItemDefinition");
					Pawn->GetCurrentWeapon()->Get<UObject*>(ContextTrapItemDefinitionOffset) = DecoItemDefinition;
				}
			}
		}

		return;
	}

	if (!ItemDefinition)
		return;

	if (auto Weapon = Pawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)ItemDefinition, ItemInstance->GetItemEntry()->GetItemGuid()))
	{
		if (Engine_Version < 420)
		{
			static auto FortWeap_BuildingToolClass = FindObject<UClass>(L"/Script/FortniteGame.FortWeap_BuildingTool");

			if (!Weapon->IsA(FortWeap_BuildingToolClass))
				return;

			auto BuildingTool = Weapon;

			using UBuildingEditModeMetadata = UObject;
			using UFortBuildingItemDefinition = UObject;

			static auto OnRep_DefaultMetadataFn = FindObject<UFunction>(L"/Script/FortniteGame.FortWeap_BuildingTool.OnRep_DefaultMetadata");
			static auto DefaultMetadataOffset = BuildingTool->GetOffset("DefaultMetadata");

			static auto RoofPiece = FindObject<UFortBuildingItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS");
			static auto FloorPiece = FindObject<UFortBuildingItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor");
			static auto WallPiece = FindObject<UFortBuildingItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall");
			static auto StairPiece = FindObject<UFortBuildingItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W");

			UBuildingEditModeMetadata* OldMetadata = nullptr; // Newer versions
			OldMetadata = BuildingTool->Get<UBuildingEditModeMetadata*>(DefaultMetadataOffset);

			if (ItemDefinition == RoofPiece)
			{
				static auto RoofMetadata = FindObject<UBuildingEditModeMetadata>(L"/Game/Building/EditModePatterns/Roof/EMP_Roof_RoofC.EMP_Roof_RoofC");
				BuildingTool->Get<UBuildingEditModeMetadata*>(DefaultMetadataOffset) = RoofMetadata;
			}
			else if (ItemDefinition == StairPiece)
			{
				static auto StairMetadata = FindObject<UBuildingEditModeMetadata>(L"/Game/Building/EditModePatterns/Stair/EMP_Stair_StairW.EMP_Stair_StairW");
				BuildingTool->Get<UBuildingEditModeMetadata*>(DefaultMetadataOffset) = StairMetadata;
			}
			else if (ItemDefinition == WallPiece)
			{
				static auto WallMetadata = FindObject<UBuildingEditModeMetadata>(L"/Game/Building/EditModePatterns/Wall/EMP_Wall_Solid.EMP_Wall_Solid");
				BuildingTool->Get<UBuildingEditModeMetadata*>(DefaultMetadataOffset) = WallMetadata;
			}
			else if (ItemDefinition == FloorPiece)
			{
				static auto FloorMetadata = FindObject<UBuildingEditModeMetadata>(L"/Game/Building/EditModePatterns/Floor/EMP_Floor_Floor.EMP_Floor_Floor");
				BuildingTool->Get<UBuildingEditModeMetadata*>(DefaultMetadataOffset) = FloorMetadata;
			}

			BuildingTool->ProcessEvent(OnRep_DefaultMetadataFn, &OldMetadata);
		}
	}
}

void AFortPlayerController::ServerAttemptInteractHook(UObject* Context, FFrame* Stack)
{
	// static auto LlamaClass = FindObject<UClass>(L"/Game/Athena/SupplyDrops/Llama/AthenaSupplyDrop_Llama.AthenaSupplyDrop_Llama_C");
	static auto FortAthenaSupplyDropClass = FindObject<UClass>(L"/Script/FortniteGame.FortAthenaSupplyDrop");
	static auto BuildingItemCollectorActorClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingItemCollectorActor");

	LOG_INFO(LogInteraction, "ServerAttemptInteract!");

	auto Params = Stack->Locals;

	static bool bIsUsingComponent = FindObject<UClass>(L"/Script/FortniteGame.FortControllerComponent_Interaction");

	AFortPlayerControllerAthena* PlayerController = bIsUsingComponent ? Cast<AFortPlayerControllerAthena>(((UActorComponent*)Context)->GetOwner()) :
		Cast<AFortPlayerControllerAthena>(Context);

	if (!PlayerController)
		return;

	std::string StructName = bIsUsingComponent ? "/Script/FortniteGame.FortControllerComponent_Interaction.ServerAttemptInteract" : "/Script/FortniteGame.FortPlayerController.ServerAttemptInteract";

	static auto ReceivingActorOffset = FindOffsetStruct(StructName, "ReceivingActor");
	auto ReceivingActor = *(AActor**)(__int64(Params) + ReceivingActorOffset);

	// LOG_INFO(LogInteraction, "ReceivingActor: {}", __int64(ReceivingActor));

	if (!ReceivingActor)
		return;

	// LOG_INFO(LogInteraction, "ReceivingActor Name: {}", ReceivingActor->GetFullName());

	FVector LocationToSpawnLoot = ReceivingActor->GetActorLocation() + ReceivingActor->GetActorRightVector() * 70.f + FVector{ 0, 0, 50 };

	static auto FortAthenaVehicleClass = FindObject<UClass>(L"/Script/FortniteGame.FortAthenaVehicle");
	static auto SearchAnimationCountOffset = FindOffsetStruct("/Script/FortniteGame.FortSearchBounceData", "SearchAnimationCount");

	if (auto BuildingContainer = Cast<ABuildingContainer>(ReceivingActor))
	{
		static auto bAlreadySearchedOffset = BuildingContainer->GetOffset("bAlreadySearched");
		static auto SearchBounceDataOffset = BuildingContainer->GetOffset("SearchBounceData");
		static auto bAlreadySearchedFieldMask = GetFieldMask(BuildingContainer->GetProperty("bAlreadySearched"));
		
		auto SearchBounceData = BuildingContainer->GetPtr<void>(SearchBounceDataOffset);

		if (BuildingContainer->ReadBitfieldValue(bAlreadySearchedOffset, bAlreadySearchedFieldMask))
			return;

		// LOG_INFO(LogInteraction, "bAlreadySearchedFieldMask: {}", bAlreadySearchedFieldMask);

		BuildingContainer->SpawnLoot(PlayerController->GetMyFortPawn());

		BuildingContainer->SetBitfieldValue(bAlreadySearchedOffset, bAlreadySearchedFieldMask, true);
		(*(int*)(__int64(SearchBounceData) + SearchAnimationCountOffset))++;
		BuildingContainer->BounceContainer();

		BuildingContainer->ForceNetUpdate(); // ?

		static auto OnRep_bAlreadySearchedFn = FindObject<UFunction>(L"/Script/FortniteGame.BuildingContainer.OnRep_bAlreadySearched");
		// BuildingContainer->ProcessEvent(OnRep_bAlreadySearchedFn);

		// if (BuildingContainer->ShouldDestroyOnSearch())
			// BuildingContainer->K2_DestroyActor();
	}
	else if (ReceivingActor->IsA(FortAthenaVehicleClass))
	{
		auto Vehicle = (AFortAthenaVehicle*)ReceivingActor;
		ServerAttemptInteractOriginal(Context, Stack);
		
		if (!AreVehicleWeaponsEnabled())
			return;

		auto Pawn = (AFortPlayerPawn*)PlayerController->GetMyFortPawn();

		if (!Pawn)
			return;

		auto VehicleWeaponDefinition = Pawn->GetVehicleWeaponDefinition(Vehicle);

		if (!VehicleWeaponDefinition)
		{
			LOG_INFO(LogDev, "Invalid VehicleWeaponDefinition!");
			return;
		}

		LOG_INFO(LogDev, "Equipping {}", VehicleWeaponDefinition->GetFullName());

		auto WorldInventory = PlayerController->GetWorldInventory();

		if (!WorldInventory)
			return;

		auto NewAndModifiedInstances = WorldInventory->AddItem(VehicleWeaponDefinition, nullptr, 1, 9999);

		auto NewVehicleInstance = NewAndModifiedInstances.first[0];

		if (!NewVehicleInstance)
			return;

		static auto FortItemEntrySize = FFortItemEntry::GetStructSize();

		auto& ReplicatedEntries = WorldInventory->GetItemList().GetReplicatedEntries();

		for (int i = 0; i < ReplicatedEntries.Num(); i++)
		{
			auto ReplicatedEntry = ReplicatedEntries.AtPtr(i, FortItemEntrySize);

			if (ReplicatedEntry->GetItemGuid() == NewVehicleInstance->GetItemEntry()->GetItemGuid())
			{
				WorldInventory->GetItemList().MarkItemDirty(ReplicatedEntry);
				WorldInventory->GetItemList().MarkItemDirty(NewVehicleInstance->GetItemEntry());
				WorldInventory->HandleInventoryLocalUpdate();

				PlayerController->ServerExecuteInventoryItemHook(PlayerController, NewVehicleInstance->GetItemEntry()->GetItemGuid());
			}
		}

		return;
	}
	else if (ReceivingActor->IsA(BuildingItemCollectorActorClass))
	{
		if (Engine_Version >= 424 && Fortnite_Version < 15 && ReceivingActor->GetFullName().contains("Wumba"))
		{
			static auto InteractionBeingAttemptedOffset = FindOffsetStruct(StructName, "InteractionBeingAttempted");
			auto InteractionBeingAttempted = *(EInteractionBeingAttempted*)(__int64(Params) + InteractionBeingAttemptedOffset);

			bool bIsSidegrading = InteractionBeingAttempted == EInteractionBeingAttempted::SecondInteraction ? true : false;
	
			LOG_INFO(LogDev, "bIsSidegrading: {}", (bool)bIsSidegrading);
	
			struct FWeaponUpgradeItemRow
			{
				void* FTableRowBaseInheritance;
				UFortWeaponItemDefinition*		  CurrentWeaponDef;                                  // 0x8(0x8)(Edit, ZeroConstructor, DisableEditOnInstance, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
				UFortWeaponItemDefinition*		  UpgradedWeaponDef;                                 // 0x10(0x8)(Edit, ZeroConstructor, DisableEditOnInstance, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
				EFortWeaponUpgradeCosts           WoodCost;                                          // 0x18(0x1)(Edit, ZeroConstructor, DisableEditOnInstance, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
				EFortWeaponUpgradeCosts           MetalCost;                                         // 0x19(0x1)(Edit, ZeroConstructor, DisableEditOnInstance, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
				EFortWeaponUpgradeCosts           BrickCost;                                         // 0x1A(0x1)(Edit, ZeroConstructor, DisableEditOnInstance, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
				EFortWeaponUpgradeDirection       Direction;
			};
	
			static auto WumbaDataTable = FindObject<UDataTable>(L"/Game/Items/Datatables/AthenaWumbaData.AthenaWumbaData");
	
			auto& LootPackagesRowMap = WumbaDataTable->GetRowMap();
	
			auto Pawn = Cast<AFortPawn>(PlayerController->GetPawn());
			auto CurrentHeldWeapon = Pawn->GetCurrentWeapon();
			auto CurrentHeldWeaponDef = CurrentHeldWeapon->GetWeaponData();
	
			auto Direction = bIsSidegrading ? EFortWeaponUpgradeDirection::Horizontal : EFortWeaponUpgradeDirection::Vertical;
	
			LOG_INFO(LogDev, "Direction: {}", (int)Direction);
	
			FWeaponUpgradeItemRow* FoundRow = nullptr;
	
			for (int i = 0; i < LootPackagesRowMap.Pairs.Elements.Data.Num(); i++)
			{
				auto& Pair = LootPackagesRowMap.Pairs.Elements.Data.at(i).ElementData.Value;
				auto First = Pair.First;
				auto Row = (FWeaponUpgradeItemRow*)Pair.Second;
	
				if (Row->CurrentWeaponDef == CurrentHeldWeaponDef && Row->Direction == Direction)
				{
					FoundRow = Row;
					break;
				}
			}
	
			if (!FoundRow)
			{
				LOG_WARN(LogGame, "Failed to find row!");
				return;
			}
	
			auto NewDefinition = FoundRow->UpgradedWeaponDef;
	
			LOG_INFO(LogDev, "UpgradedWeaponDef: {}", NewDefinition->GetFullName());
	
			int WoodCost = (int)FoundRow->WoodCost * 50;
			int StoneCost = (int)FoundRow->BrickCost * 50 - 400;
			int MetalCost = (int)FoundRow->MetalCost * 50 - 200;
	
			if (bIsSidegrading)
			{
				WoodCost = 20;
				StoneCost = 20;
				MetalCost = 20;
			}
	
			static auto WoodItemData = FindObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/WoodItemData.WoodItemData");
			static auto StoneItemData = FindObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/StoneItemData.StoneItemData");
			static auto MetalItemData = FindObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/MetalItemData.MetalItemData");
	
			auto WorldInventory = PlayerController->GetWorldInventory();
	
			auto WoodInstance = WorldInventory->FindItemInstance(WoodItemData);
			auto WoodCount = WoodInstance->GetItemEntry()->GetCount();
	
			auto StoneInstance = WorldInventory->FindItemInstance(StoneItemData);
			auto StoneCount = StoneInstance->GetItemEntry()->GetCount();
	
			auto MetalInstance = WorldInventory->FindItemInstance(MetalItemData);
			auto MetalCount = MetalInstance->GetItemEntry()->GetCount();
	
			bool bShouldUpdate = false;
	
			WorldInventory->RemoveItem(WoodInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, WoodCost);
			WorldInventory->RemoveItem(StoneInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, StoneCost);
			WorldInventory->RemoveItem(MetalInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, MetalCost);
	
			WorldInventory->RemoveItem(CurrentHeldWeapon->GetItemEntryGuid(), &bShouldUpdate, 1, true);
	
			WorldInventory->AddItem(NewDefinition, &bShouldUpdate);
	
			if (bShouldUpdate)
				WorldInventory->Update();
		}
		else
		{
			auto WorldInventory = PlayerController->GetWorldInventory();
	
			if (!WorldInventory)
				return ServerAttemptInteractOriginal(Context, Stack);
	
			auto ItemCollector = ReceivingActor;
			static auto ActiveInputItemOffset = ItemCollector->GetOffset("ActiveInputItem");
			auto CurrentMaterial = ItemCollector->Get<UFortWorldItemDefinition*>(ActiveInputItemOffset); // InteractType->OptionalObjectData
	
			if (!CurrentMaterial)
				return ServerAttemptInteractOriginal(Context, Stack);
	
			int Index = 0;
	
			// this is a weird way of getting the current item collection we are on.
	
			static auto StoneItemData = FindObject<UFortResourceItemDefinition>(L"/Game/Items/ResourcePickups/StoneItemData.StoneItemData");
			static auto MetalItemData = FindObject<UFortResourceItemDefinition>(L"/Game/Items/ResourcePickups/MetalItemData.MetalItemData");
	
			if (CurrentMaterial == StoneItemData)
				Index = 1;
			else if (CurrentMaterial == MetalItemData)
				Index = 2;
	
			static auto ItemCollectionsOffset = ItemCollector->GetOffset("ItemCollections");
			auto& ItemCollections = ItemCollector->Get<TArray<FCollectorUnitInfo>>(ItemCollectionsOffset);
	
			auto ItemCollection = ItemCollections.AtPtr(Index, FCollectorUnitInfo::GetPropertiesSize());
	
			if (Fortnite_Version < 8.10)
			{
				auto Cost = ItemCollection->GetInputCount()->GetValue();
	
				if (!CurrentMaterial)
					return ServerAttemptInteractOriginal(Context, Stack);
	
				auto MatInstance = WorldInventory->FindItemInstance(CurrentMaterial);
	
				if (!MatInstance)
					return ServerAttemptInteractOriginal(Context, Stack);
	
				bool bShouldUpdate = false;
	
				if (!WorldInventory->RemoveItem(MatInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, Cost, true))
					return ServerAttemptInteractOriginal(Context, Stack);
	
				if (bShouldUpdate)
					WorldInventory->Update();
			}
	
			for (int z = 0; z < ItemCollection->GetOutputItemEntry()->Num(); z++)
			{
				auto Entry = ItemCollection->GetOutputItemEntry()->AtPtr(z, FFortItemEntry::GetStructSize());
	
				PickupCreateData CreateData;
				CreateData.ItemEntry = FFortItemEntry::MakeItemEntry(Entry->GetItemDefinition(), Entry->GetCount(), Entry->GetLoadedAmmo(), MAX_DURABILITY, Entry->GetLevel());
				CreateData.SpawnLocation = LocationToSpawnLoot;
				CreateData.PawnOwner = PlayerController->GetMyFortPawn(); // hmm
				CreateData.bShouldFreeItemEntryWhenDeconstructed = true;
	
				AFortPickup::SpawnPickup(CreateData);
			}
	
			static auto bCurrentInteractionSuccessOffset = ItemCollector->GetOffset("bCurrentInteractionSuccess", false);
	
			if (bCurrentInteractionSuccessOffset != -1)
			{
				static auto bCurrentInteractionSuccessFieldMask = GetFieldMask(ItemCollector->GetProperty("bCurrentInteractionSuccess"));
				ItemCollector->SetBitfieldValue(bCurrentInteractionSuccessOffset, bCurrentInteractionSuccessFieldMask, true); // idek if this is needed
			}
	
			static auto DoVendDeath = FindObject<UFunction>(L"/Game/Athena/Items/Gameplay/VendingMachine/B_Athena_VendingMachine.B_Athena_VendingMachine_C.DoVendDeath");
	
			if (DoVendDeath)
			{
				ItemCollector->ProcessEvent(DoVendDeath);
				ItemCollector->K2_DestroyActor();
			}
		}
	}

	return ServerAttemptInteractOriginal(Context, Stack);
}

void AFortPlayerController::ServerAttemptAircraftJumpHook(AFortPlayerController* PC, FRotator ClientRotation)
{
	auto PlayerController = Cast<AFortPlayerControllerAthena>(Engine_Version < 424 ? PC : ((UActorComponent*)PC)->GetOwner());

	if (Engine_Version < 424 && !Globals::bLateGame.load())
		return ServerAttemptAircraftJumpOriginal(PC, ClientRotation);

	if (!PlayerController)
		return ServerAttemptAircraftJumpOriginal(PC, ClientRotation);

	// if (!PlayerController->bInAircraft) 
		// return;

	LOG_INFO(LogDev, "ServerAttemptAircraftJumpHook!");

	auto GameMode = (AFortGameModeAthena*)GetWorld()->GetGameMode();
	auto GameState = GameMode->GetGameStateAthena();

	AActor* AircraftToJumpFrom = nullptr;

	static auto AircraftsOffset = GameState->GetOffset("Aircrafts", false);

	if (AircraftsOffset == -1)
	{
		static auto AircraftOffset = GameState->GetOffset("Aircraft");
		AircraftToJumpFrom = GameState->Get<AActor*>(AircraftOffset);
	}
	else
	{
		auto Aircrafts = GameState->GetPtr<TArray<AActor*>>(AircraftsOffset);
		AircraftToJumpFrom = Aircrafts->Num() > 0 ? Aircrafts->at(0) : nullptr; // skunky
	}

	if (!AircraftToJumpFrom)
		return ServerAttemptAircraftJumpOriginal(PC, ClientRotation);

	if (false)
	{
		auto NewPawn = GameMode->SpawnDefaultPawnForHook(GameMode, (AController*)PlayerController, AircraftToJumpFrom);
		PlayerController->Possess(NewPawn);
	}
	else
	{
		if (false)
		{
			// honestly idk why this doesnt work ( ithink its suppsoed to be spectator)

			auto NAME_Inactive = UKismetStringLibrary::Conv_StringToName(L"NAME_Inactive");

			LOG_INFO(LogDev, "name Comp: {}", NAME_Inactive.ComparisonIndex.Value);

			PlayerController->GetStateName() = NAME_Inactive;
			PlayerController->SetPlayerIsWaiting(true);
			PlayerController->ServerRestartPlayer();
		}
		else
		{
			GameMode->RestartPlayer(PlayerController);
		}

		// we are supposed to do some skydivign stuff here but whatever
	}

	auto NewPawnAsFort = PlayerController->GetMyFortPawn();

	if (Fortnite_Version >= 18) // TODO (Milxnor) Find a better fix and move this
	{
		static auto StormEffectClass = FindObject<UClass>(L"/Game/Athena/SafeZone/GE_OutsideSafeZoneDamage.GE_OutsideSafeZoneDamage_C");
		auto PlayerState = PlayerController->GetPlayerStateAthena();

		PlayerState->GetAbilitySystemComponent()->RemoveActiveGameplayEffectBySourceEffect(StormEffectClass, 1, PlayerState->GetAbilitySystemComponent());
	}

	if (NewPawnAsFort)
	{
		NewPawnAsFort->SetHealth(100); // needed with server restart player?
		
		if (Globals::bLateGame)
		{
			NewPawnAsFort->SetShield(100);

			NewPawnAsFort->TeleportTo(AircraftToJumpFrom->GetActorLocation(), FRotator());
		}
	}

	// PlayerController->ServerRestartPlayer();
	// return ServerAttemptAircraftJumpOriginal(PC, ClientRotation);
}

void AFortPlayerController::ServerSuicideHook(AFortPlayerController* PlayerController)
{
	LOG_INFO(LogDev, "Suicide!");

	auto Pawn = PlayerController->GetPawn();

	if (!Pawn)
		return;

	// theres some other checks here idk

	if (!Pawn->IsA(AFortPlayerPawn::StaticClass())) // Why FortPlayerPawn? Ask Fortnite
		return;

	// suicide doesn't actually call force kill but its basically the same function

	static auto ForceKillFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPawn.ForceKill"); // exists on 1.2 and 19.10 with same params so I assume it's the same on every other build.

	FGameplayTag DeathReason; // unused on 1.7.2
	AActor* KillerActor = nullptr; // its just 0 in suicide (not really but easiest way to explain it)

	struct { FGameplayTag DeathReason; AController* KillerController; AActor* KillerActor; } AFortPawn_ForceKill_Params{ DeathReason, PlayerController, KillerActor };

	Pawn->ProcessEvent(ForceKillFn, &AFortPawn_ForceKill_Params);

	//PlayerDeathReport->ServerTimeForRespawn && PlayerDeathReport->ServerTimeForResurrect = 0? // I think this is what they do on 1.7.2 I'm too lazy to double check though.
}

void AFortPlayerController::ServerDropAllItemsHook(AFortPlayerController* PlayerController, UFortItemDefinition* IgnoreItemDef)
{
	LOG_INFO(LogDev, "DropAllItems!");
	PlayerController->DropAllItems({ IgnoreItemDef });
}

void AFortPlayerController::ServerCreateBuildingActorHook(UObject* Context, FFrame* Stack, void* Ret)
{
	auto PlayerController = (AFortPlayerController*)Context;

	// if (!PlayerController) // ??
		// return ServerCreateBuildingActorOriginal(Context, Stack, Ret);

	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);

	auto PlayerStateAthena = Cast<AFortPlayerStateAthena, false>(PlayerController->GetPlayerState());

	if (!PlayerStateAthena)
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);

	UClass* BuildingClass = nullptr;
	FVector BuildLocation;
	FRotator BuildRotator;
	bool bMirrored;

	if (Fortnite_Version >= 8.30)
	{
		struct FCreateBuildingActorData { uint32_t BuildingClassHandle; FVector BuildLoc; FRotator BuildRot; bool bMirrored; };
		auto CreateBuildingData = (FCreateBuildingActorData*)Stack->Locals;

		BuildLocation = CreateBuildingData->BuildLoc;
		BuildRotator = CreateBuildingData->BuildRot;
		bMirrored = CreateBuildingData->bMirrored;

		static auto BroadcastRemoteClientInfoOffset = PlayerController->GetOffset("BroadcastRemoteClientInfo");
		UObject* BroadcastRemoteClientInfo = PlayerController->Get(BroadcastRemoteClientInfoOffset);

		static auto RemoteBuildableClassOffset = BroadcastRemoteClientInfo->GetOffset("RemoteBuildableClass");
		BuildingClass = BroadcastRemoteClientInfo->Get<UClass*>(RemoteBuildableClassOffset);
	}
	else
	{
		struct FBuildingClassData { UClass* BuildingClass; int PreviousBuildingLevel; int UpgradeLevel; };
		struct SCBAParams { FBuildingClassData BuildingClassData; FVector BuildLoc; FRotator BuildRot; bool bMirrored; };

		auto Params = (SCBAParams*)Stack->Locals;

		BuildingClass = Params->BuildingClassData.BuildingClass;
		BuildLocation = Params->BuildLoc;
		BuildRotator = Params->BuildRot;
		bMirrored = Params->bMirrored;
	}

	// LOG_INFO(LogDev, "BuildingClass {}", __int64(BuildingClass));

	if (!BuildingClass)
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);

	auto GameState = Cast<AFortGameStateAthena, false>(Cast<AFortGameMode, false>(GetWorld()->GetGameMode())->GetGameState());

	auto StructuralSupportSystem = GameState->GetStructuralSupportSystem();

	if (StructuralSupportSystem)
	{
		if (!StructuralSupportSystem->IsWorldLocValid(BuildLocation))
		{
			return ServerCreateBuildingActorOriginal(Context, Stack, Ret);
		}
	}

	if (!GameState->IsPlayerBuildableClass(BuildingClass))
	{
		LOG_INFO(LogDev, "Cheater most likely.");
		// PlayerController->GetAnticheatComponent().AddAndCheck(Severity::HIGH);
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);
	}

	TArray<ABuildingSMActor*> ExistingBuildings;
	char idk;
	static __int64 (*CantBuild)(UObject*, UObject*, FVector, FRotator, char, TArray<ABuildingSMActor*>*, char*) = decltype(CantBuild)(Addresses::CantBuild);
	bool bCanBuild = !CantBuild(GetWorld(), BuildingClass, BuildLocation, BuildRotator, bMirrored, &ExistingBuildings, &idk);

	if (!bCanBuild)
	{
		ExistingBuildings.Free();
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);
	}

	FTransform Transform{};
	Transform.Translation = BuildLocation;
	Transform.Rotation = BuildRotator.Quaternion();
	Transform.Scale3D = { 1, 1, 1 };

	auto BuildingActor = GetWorld()->SpawnActor<ABuildingSMActor>(BuildingClass, Transform);

	if (!BuildingActor)
	{
		ExistingBuildings.Free();
		return ServerCreateBuildingActorOriginal(Context, Stack, Ret);
	}

	auto MatDefinition = UFortKismetLibrary::K2_GetResourceItemDefinition(BuildingActor->GetResourceType());

	bool bBuildFree = PlayerController->DoesBuildFree();

	// LOG_INFO(LogDev, "MatInstance->GetItemEntry()->GetCount(): {}", MatInstance->GetItemEntry()->GetCount());
	
	if (!bBuildFree)
	{
		int MaterialCost = 10;

		UFortItem* MatInstance = WorldInventory->FindItemInstance(MatDefinition);

		if (!MatInstance || MatInstance->GetItemEntry()->GetCount() < MaterialCost)
		{
			ExistingBuildings.Free();
			BuildingActor->SilentDie();
			return ServerCreateBuildingActorOriginal(Context, Stack, Ret);
		}

		bool bShouldUpdate = false;
		WorldInventory->RemoveItem(MatInstance->GetItemEntry()->GetItemGuid(), &bShouldUpdate, MaterialCost);

		if (bShouldUpdate)
			WorldInventory->Update();
	}

	for (int i = 0; i < ExistingBuildings.Num(); ++i)
	{
		auto ExistingBuilding = ExistingBuildings.At(i);

		ExistingBuilding->K2_DestroyActor();
	}

	ExistingBuildings.Free();

	// BuildingActor->SetCurrentBuildingLevel()
	BuildingActor->SetPlayerPlaced(true);
	BuildingActor->InitializeBuildingActor(PlayerController, BuildingActor, true);
	BuildingActor->SetTeam(PlayerStateAthena->GetTeamIndex()); // required?

	/*

	GET_PLAYLIST(GameState);

	if (CurrentPlaylist)
	{
		// CurrentPlaylist->ApplyModifiersToActor(BuildingActor); // seems automatic
	} */

	return ServerCreateBuildingActorOriginal(Context, Stack, Ret);
}

AActor* AFortPlayerController::SpawnToyInstanceHook(UObject* Context, FFrame* Stack, AActor** Ret)
{
	LOG_INFO(LogDev, "SpawnToyInstance!");

	auto PlayerController = Cast<AFortPlayerController>(Context);

	UClass* ToyClass = nullptr;
	FTransform SpawnPosition;

	Stack->StepCompiledIn(&ToyClass);
	Stack->StepCompiledIn(&SpawnPosition);

	SpawnToyInstanceOriginal(Context, Stack, Ret);

	if (!ToyClass)
		return nullptr;

	auto Params = CreateSpawnParameters(ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn, false, PlayerController);
	auto NewToy = GetWorld()->SpawnActor<AActor>(ToyClass, SpawnPosition, Params);
	// free(Params); // ?

	static auto ActiveToyInstancesOffset = PlayerController->GetOffset("ActiveToyInstances");
	auto& ActiveToyInstances = PlayerController->Get<TArray<AActor*>>(ActiveToyInstancesOffset);
	
	static auto ToySummonCountsOffset = PlayerController->GetOffset("ToySummonCounts");
	auto& ToySummonCounts = PlayerController->Get<TMap<UClass*, int>>(ToySummonCountsOffset);

	// ActiveToyInstances.Add(NewToy);

	*Ret = NewToy;
	return *Ret;
}

void AFortPlayerController::DropSpecificItemHook(UObject* Context, FFrame& Stack, void* Ret)
{
	UFortItemDefinition* DropItemDef = nullptr;

	Stack.StepCompiledIn(&DropItemDef);

	if (!DropItemDef)
		return;

	auto PlayerController = Cast<AFortPlayerController>(Context);

	if (!PlayerController)
		return DropSpecificItemOriginal(Context, Stack, Ret);

	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return DropSpecificItemOriginal(Context, Stack, Ret);

	auto ItemInstance = WorldInventory->FindItemInstance(DropItemDef);

	if (!ItemInstance)
		return DropSpecificItemOriginal(Context, Stack, Ret);

	PlayerController->ServerAttemptInventoryDropHook(PlayerController, ItemInstance->GetItemEntry()->GetItemGuid(), ItemInstance->GetItemEntry()->GetCount());

	return DropSpecificItemOriginal(Context, Stack, Ret);
}

void AFortPlayerController::ServerAttemptInventoryDropHook(AFortPlayerController* PlayerController, FGuid ItemGuid, int Count)
{
	LOG_INFO(LogDev, "ServerAttemptInventoryDropHook Dropping: {}", Count);

	auto Pawn = PlayerController->GetMyFortPawn();

	if (Count < 0 || !Pawn)
		return;

	if (auto PlayerControllerAthena = Cast<AFortPlayerControllerAthena>(PlayerController))
	{
		if (PlayerControllerAthena->IsInGhostMode())
			return;
	}

	// TODO If the player is in a vehicle and has a vehicle weapon, don't let them drop.

	auto WorldInventory = PlayerController->GetWorldInventory();
	auto ReplicatedEntry = WorldInventory->FindReplicatedEntry(ItemGuid);

	if (!ReplicatedEntry || ReplicatedEntry->GetCount() < Count)
		return;

	auto ItemDefinition = Cast<UFortWorldItemDefinition>(ReplicatedEntry->GetItemDefinition());

	if (!ItemDefinition || !ItemDefinition->CanBeDropped())
		return;

	static auto DropBehaviorOffset = ItemDefinition->GetOffset("DropBehavior", false);

	EWorldItemDropBehavior DropBehavior = DropBehaviorOffset != -1 ? ItemDefinition->GetDropBehavior() : EWorldItemDropBehavior::EWorldItemDropBehavior_MAX;

	if (!ItemDefinition->ShouldIgnoreRespawningOnDrop() && DropBehavior != EWorldItemDropBehavior::DestroyOnDrop)
	{
		PickupCreateData CreateData;
		CreateData.ItemEntry = ReplicatedEntry;
		CreateData.SpawnLocation = Pawn->GetActorLocation();
		CreateData.bToss = true;
		CreateData.OverrideCount = Count;
		CreateData.PawnOwner = Pawn;
		CreateData.bRandomRotation = true;
		CreateData.SourceType = EFortPickupSourceTypeFlag::GetPlayerValue();
		CreateData.bShouldFreeItemEntryWhenDeconstructed = false;

		auto Pickup = AFortPickup::SpawnPickup(CreateData);

		if (!Pickup)
			return;
	}

	bool bShouldUpdate = false;

	if (!WorldInventory->RemoveItem(ItemGuid, &bShouldUpdate, Count, true, DropBehavior == EWorldItemDropBehavior::DropAsPickupDestroyOnEmpty))
		return;

	if (bShouldUpdate)
		WorldInventory->Update();
}

void AFortPlayerController::ServerPlayEmoteItemHook(AFortPlayerController* PlayerController, UObject* EmoteAsset)
{
	auto PlayerState = (AFortPlayerStateAthena*)PlayerController->GetPlayerState();
	auto Pawn = PlayerController->GetPawn();

	if (!EmoteAsset || !PlayerState || !Pawn)
		return;

	auto AbilitySystemComponent = PlayerState->GetAbilitySystemComponent();

	if (!AbilitySystemComponent)
		return;

	UObject* AbilityToUse = nullptr;
	bool bShouldBeAbilityToUse = false;

	static auto AthenaSprayItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.AthenaSprayItemDefinition");
	static auto AthenaToyItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.AthenaToyItemDefinition");
	static auto BGAClass = FindObject<UClass>(L"/Script/Engine.BlueprintGeneratedClass");

	if (EmoteAsset->IsA(AthenaSprayItemDefinitionClass))
	{
		auto SprayGameplayAbilityDefault = FindObject(L"/Game/Abilities/Sprays/GAB_Spray_Generic.Default__GAB_Spray_Generic_C");
		AbilityToUse = SprayGameplayAbilityDefault;
	}

	else if (EmoteAsset->IsA(AthenaToyItemDefinitionClass))
	{
		static auto ToySpawnAbilityOffset = EmoteAsset->GetOffset("ToySpawnAbility");
		auto& ToySpawnAbilitySoft = EmoteAsset->Get<TSoftObjectPtr<UClass>>(ToySpawnAbilityOffset);

		auto ToySpawnAbility = ToySpawnAbilitySoft.Get(BGAClass, true);

		if (ToySpawnAbility)
			AbilityToUse = ToySpawnAbility->CreateDefaultObject();
	}

	// LOG_INFO(LogDev, "Before AbilityToUse: {}", AbilityToUse ? AbilityToUse->GetFullName() : "InvalidObject");

	if (!AbilityToUse)
	{
		static auto EmoteGameplayAbilityDefault = FindObject(L"/Game/Abilities/Emotes/GAB_Emote_Generic.Default__GAB_Emote_Generic_C");
		AbilityToUse = EmoteGameplayAbilityDefault;
	}

	if (!AbilityToUse)
		return;

	static auto AthenaDanceItemDefinitionClass = FindObject<UClass>(L"/Script/FortniteGame.AthenaDanceItemDefinition");

	if (EmoteAsset->IsA(AthenaDanceItemDefinitionClass))
	{
		static auto EmoteAsset_bMovingEmoteOffset = EmoteAsset->GetOffset("bMovingEmote", false);
		static auto bMovingEmoteOffset = Pawn->GetOffset("bMovingEmote", false);

		if (bMovingEmoteOffset != -1 && EmoteAsset_bMovingEmoteOffset != -1)
		{
			static auto bMovingEmoteFieldMask = GetFieldMask(Pawn->GetProperty("bMovingEmote"));
			static auto EmoteAsset_bMovingEmoteFieldMask = GetFieldMask(EmoteAsset->GetProperty("bMovingEmote"));
			Pawn->SetBitfieldValue(bMovingEmoteOffset, bMovingEmoteFieldMask, EmoteAsset->ReadBitfieldValue(EmoteAsset_bMovingEmoteOffset, EmoteAsset_bMovingEmoteFieldMask));
		}

		static auto bMoveForwardOnlyOffset = EmoteAsset->GetOffset("bMoveForwardOnly", false);
		static auto bMovingEmoteForwardOnlyOffset = Pawn->GetOffset("bMovingEmoteForwardOnly", false);

		if (bMovingEmoteForwardOnlyOffset != -1 && bMoveForwardOnlyOffset != -1)
		{
			static auto bMovingEmoteForwardOnlyFieldMask = GetFieldMask(Pawn->GetProperty("bMovingEmoteForwardOnly"));
			static auto bMoveForwardOnlyFieldMask = GetFieldMask(EmoteAsset->GetProperty("bMoveForwardOnly"));
			Pawn->SetBitfieldValue(bMovingEmoteOffset, bMovingEmoteForwardOnlyFieldMask, EmoteAsset->ReadBitfieldValue(bMoveForwardOnlyOffset, bMoveForwardOnlyFieldMask));
		}

		static auto WalkForwardSpeedOffset = EmoteAsset->GetOffset("WalkForwardSpeed", false);
		static auto EmoteWalkSpeedOffset = Pawn->GetOffset("EmoteWalkSpeed", false);

		if (EmoteWalkSpeedOffset != -1 && WalkForwardSpeedOffset != -1)
		{
			Pawn->Get<float>(EmoteWalkSpeedOffset) = EmoteAsset->Get<float>(WalkForwardSpeedOffset);
		}
	}

	int outHandle = 0;

	FGameplayAbilitySpec* Spec = MakeNewSpec((UClass*)AbilityToUse, EmoteAsset, true);

	if (!Spec)
		return;

	static unsigned int* (*GiveAbilityAndActivateOnce)(UAbilitySystemComponent* ASC, int* outHandle, __int64 Spec, FGameplayEventData* TriggerEventData) = decltype(GiveAbilityAndActivateOnce)(Addresses::GiveAbilityAndActivateOnce); // EventData is only on ue500?

	if (GiveAbilityAndActivateOnce)
	{
		GiveAbilityAndActivateOnce(AbilitySystemComponent, &outHandle, __int64(Spec), nullptr);
	}
}

void AFortPlayerController::ServerPlaySprayItemHook(AFortPlayerController* PlayerController, UAthenaSprayItemDefinition* SprayAsset)
{
	PlayerController->ServerPlayEmoteItemHook(PlayerController, SprayAsset);
}

uint8 ToDeathCause(const FGameplayTagContainer& TagContainer, bool bWasDBNO = false, AFortPawn* Pawn = nullptr)
{
	static auto ToDeathCauseFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerStateAthena.ToDeathCause");

	if (ToDeathCauseFn)
	{
		struct
		{
			FGameplayTagContainer                       InTags;                                                   // (ConstParm, Parm, OutParm, ReferenceParm, NativeAccessSpecifierPublic)
			bool                                               bWasDBNO;                                                 // (Parm, ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
			uint8_t                                        ReturnValue;                                              // (Parm, OutParm, ZeroConstructor, ReturnParm, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
		} AFortPlayerStateAthena_ToDeathCause_Params{ TagContainer, bWasDBNO };

		AFortPlayerStateAthena::StaticClass()->ProcessEvent(ToDeathCauseFn, &AFortPlayerStateAthena_ToDeathCause_Params);

		return AFortPlayerStateAthena_ToDeathCause_Params.ReturnValue;
	}

	static bool bHaveFoundAddress = false;

	static uint64 Addr = 0;

	if (!bHaveFoundAddress)
	{
		bHaveFoundAddress = true;

		if (Engine_Version == 419)
			Addr = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 41 0F B6 F8 48 8B DA 48 8B F1 E8 ? ? ? ? 33 ED").Get();
		if (Engine_Version == 420)
			Addr = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 0F B6 FA 48 8B D9 E8 ? ? ? ? 33 F6 48 89 74 24").Get();
		if (Engine_Version == 421) // 5.1
			Addr = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 0F B6 FA 48 8B D9 E8 ? ? ? ? 33").Get();

		if (!Addr)
		{
			LOG_WARN(LogPlayer, "Failed to find ToDeathCause address!");
			return 0;
		}
	}

	if (!Addr)
	{
		return 0;
	}

	if (Engine_Version == 419)
	{
		static uint8(*sub_7FF7AB499410)(AFortPawn* Pawn, FGameplayTagContainer TagContainer, char bWasDBNOIg) = decltype(sub_7FF7AB499410)(Addr);
		return sub_7FF7AB499410(Pawn, TagContainer, bWasDBNO);
	}

	static uint8 (*sub_7FF7AB499410)(FGameplayTagContainer TagContainer, char bWasDBNOIg) = decltype(sub_7FF7AB499410)(Addr);
	return sub_7FF7AB499410(TagContainer, bWasDBNO);
}

DWORD WINAPI RestartThread(LPVOID)
{
	// We should probably use unreal engine's timing system for this.
	// There is no way to restart that I know of without closing the connection to the clients.

	bIsInAutoRestart = true;

	float SecondsBeforeRestart = 10;
	Sleep(SecondsBeforeRestart * 1000);

	LOG_INFO(LogDev, "Auto restarting!");

	Restart();

	bIsInAutoRestart = false;

	return 0;
}

void ShutdownAfter40Sec()
{

	std::thread([]() {

		std::this_thread::sleep_for(std::chrono::seconds(40));

		std::exit(0);
		}).detach();
}

void AFortPlayerController::ClientOnPawnDiedHook(AFortPlayerController* PlayerController, void* DeathReport)
{
	LOG_INFO(LogDev, "=== ClientOnPawnDiedHook START ===");
	LOG_INFO(LogDev, "PlayerController: {}", __int64(PlayerController));
	LOG_INFO(LogDev, "DeathReport: {}", __int64(DeathReport));

	auto GameState = Cast<AFortGameStateAthena>(((AFortGameMode*)GetWorld()->GetGameMode())->GetGameState());
	auto DeadPawn = Cast<AFortPlayerPawn>(PlayerController->GetPawn());
	auto DeadPlayerState = Cast<AFortPlayerStateAthena>(PlayerController->GetPlayerState());
	auto KillerPawn = Cast<AFortPlayerPawn>(*(AFortPawn**)(__int64(DeathReport) + MemberOffsets::DeathReport::KillerPawn));
	auto KillerPlayerState = Cast<AFortPlayerStateAthena>(*(AFortPlayerState**)(__int64(DeathReport) + MemberOffsets::DeathReport::KillerPlayerState));

	LOG_INFO(LogDev, "GameState: {}", __int64(GameState));
	LOG_INFO(LogDev, "DeadPawn: {}", __int64(DeadPawn));
	LOG_INFO(LogDev, "DeadPlayerState: {}", __int64(DeadPlayerState));
	LOG_INFO(LogDev, "KillerPawn: {}", __int64(KillerPawn));
	LOG_INFO(LogDev, "KillerPlayerState: {}", __int64(KillerPlayerState));

	if (!DeadPawn || !GameState || !DeadPlayerState)
	{
		LOG_INFO(LogDev, "Early return: Missing critical actors");
		return ClientOnPawnDiedOriginal(PlayerController, DeathReport);
	}

	auto DeathLocation = DeadPawn->GetActorLocation();
	LOG_INFO(LogDev, "DeathLocation: X={}, Y={}, Z={}", DeathLocation.X, DeathLocation.Y, DeathLocation.Z);

	static auto FallDamageEnumValue = 1;
	uint8_t DeathCause = 0;

	if (Fortnite_Version > 1.8 || Fortnite_Version == 1.11)
	{
		LOG_INFO(LogDev, "Processing DeathInfo (Version > 1.8 or == 1.11)");

		auto DeathInfo = DeadPlayerState->GetDeathInfo();
		LOG_INFO(LogDev, "DeathInfo pointer: {}", __int64(DeathInfo));

		DeadPlayerState->ClearDeathInfo();
		LOG_INFO(LogDev, "DeathInfo cleared");

		auto Tags = MemberOffsets::FortPlayerPawn::CorrectTags == 0 ? FGameplayTagContainer()
			: DeadPawn->Get<FGameplayTagContainer>(MemberOffsets::FortPlayerPawn::CorrectTags);

		LOG_INFO(LogDev, "Tags retrieved: {}", Tags.ToStringSimple(true));
		LOG_INFO(LogDev, "Tags.GameplayTags.Num(): {}", Tags.GameplayTags.Num());
		LOG_INFO(LogDev, "Tags.ParentTags.Num(): {}", Tags.ParentTags.Num());

		DeathCause = ToDeathCause(Tags, false, DeadPawn);
		LOG_INFO(LogDev, "DeathCause calculated: {}", (int)DeathCause);
		LOG_INFO(LogDev, "DeadPawn->IsDBNO(): {}", DeadPawn->IsDBNO());

		// Copy tags
		FGameplayTagContainer CopyTags;
		for (int i = 0; i < Tags.GameplayTags.Num(); ++i)
		{
			CopyTags.GameplayTags.Add(Tags.GameplayTags.at(i));
		}
		for (int i = 0; i < Tags.ParentTags.Num(); ++i)
		{
			CopyTags.ParentTags.Add(Tags.ParentTags.at(i));
		}
		LOG_INFO(LogDev, "Tags copied - GameplayTags: {}, ParentTags: {}", CopyTags.GameplayTags.Num(), CopyTags.ParentTags.Num());

		// Set DeathInfo fields
		*(bool*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::bDBNO) = DeadPawn->IsDBNO();
		LOG_INFO(LogDev, "Set bDBNO in DeathInfo");

		*(uint8*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::DeathCause) = DeathCause;
		LOG_INFO(LogDev, "Set DeathCause in DeathInfo");

		auto FinisherOrDowner = KillerPlayerState ? KillerPlayerState : DeadPlayerState;
		LOG_INFO(LogDev, "FinisherOrDowner: {}", __int64(FinisherOrDowner));

		if (MemberOffsets::DeathInfo::bIsWeakFinisherOrDowner)
		{
			LOG_INFO(LogDev, "Using weak pointer for FinisherOrDowner");
			TWeakObjectPtr<AActor> WeakFinisherOrDowner{};
			WeakFinisherOrDowner.ObjectIndex = FinisherOrDowner->InternalIndex;
			WeakFinisherOrDowner.ObjectSerialNumber = GetItemByIndex(FinisherOrDowner->InternalIndex)->SerialNumber;
			LOG_INFO(LogDev, "WeakPtr - ObjectIndex: {}, SerialNumber: {}", WeakFinisherOrDowner.ObjectIndex, WeakFinisherOrDowner.ObjectSerialNumber);
		}
		else
		{
			*(AActor**)(__int64(DeathInfo) + MemberOffsets::DeathInfo::FinisherOrDowner) = FinisherOrDowner;
			LOG_INFO(LogDev, "Set raw pointer for FinisherOrDowner");
		}

		if (MemberOffsets::DeathInfo::DeathLocation != -1)
		{
			*(FVector*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::DeathLocation) = DeathLocation;
			LOG_INFO(LogDev, "Set DeathLocation in DeathInfo");
		}

		if (MemberOffsets::DeathInfo::DeathTags != -1)
		{
			*(FGameplayTagContainer*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::DeathTags) = CopyTags;
			LOG_INFO(LogDev, "Set DeathTags in DeathInfo");
		}

		if (MemberOffsets::DeathInfo::bInitialized != -1)
		{
			*(bool*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::bInitialized) = true;
			LOG_INFO(LogDev, "Set bInitialized in DeathInfo");
		}

		// Distance calculation
		if (DeathCause == FallDamageEnumValue)
		{
			if (MemberOffsets::FortPlayerPawnAthena::LastFallDistance != -1)
			{
				float fallDist = DeadPawn->Get<float>(MemberOffsets::FortPlayerPawnAthena::LastFallDistance);
				*(float*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::Distance) = fallDist;
				LOG_INFO(LogDev, "Set fall distance: {}", fallDist);
			}
		}
		else
		{
			if (MemberOffsets::DeathInfo::Distance != -1)
			{
				float distance = KillerPawn ? KillerPawn->GetDistanceTo(DeadPawn) : 0;
				*(float*)(__int64(DeathInfo) + MemberOffsets::DeathInfo::Distance) = distance;
				LOG_INFO(LogDev, "Set kill distance: {}", distance);
			}
		}

		if (MemberOffsets::FortPlayerState::PawnDeathLocation != -1)
		{
			DeadPlayerState->Get<FVector>(MemberOffsets::FortPlayerState::PawnDeathLocation) = DeathLocation;
			LOG_INFO(LogDev, "Set PawnDeathLocation in PlayerState");
		}

		// OnRep_DeathInfo
		static auto OnRep_DeathInfoFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerStateAthena.OnRep_DeathInfo");
		if (OnRep_DeathInfoFn)
		{
			LOG_INFO(LogDev, "Calling OnRep_DeathInfo");
			DeadPlayerState->ProcessEvent(OnRep_DeathInfoFn);
			LOG_INFO(LogDev, "OnRep_DeathInfo completed");
		}
		else
		{
			LOG_INFO(LogDev, "WARNING: OnRep_DeathInfoFn not found!");
		}

		// Handle killer stats and siphon
		if (KillerPlayerState && KillerPlayerState != DeadPlayerState)
		{
			LOG_INFO(LogDev, "Processing killer stats (different from dead player)");

			if (MemberOffsets::FortPlayerStateAthena::KillScore != -1)
			{
				int oldKills = KillerPlayerState->Get<int>(MemberOffsets::FortPlayerStateAthena::KillScore);
				KillerPlayerState->Get<int>(MemberOffsets::FortPlayerStateAthena::KillScore)++;
				LOG_INFO(LogDev, "KillScore: {} -> {}", oldKills, oldKills + 1);
			}

			if (MemberOffsets::FortPlayerStateAthena::TeamKillScore != -1)
			{
				int oldTeamKills = KillerPlayerState->Get<int>(MemberOffsets::FortPlayerStateAthena::TeamKillScore);
				KillerPlayerState->Get<int>(MemberOffsets::FortPlayerStateAthena::TeamKillScore)++;
				LOG_INFO(LogDev, "TeamKillScore: {} -> {}", oldTeamKills, oldTeamKills + 1);
			}

			KillerPlayerState->ClientReportKill(DeadPlayerState);
			LOG_INFO(LogDev, "ClientReportKill called");

			// Health siphon
			if (AmountOfHealthSiphon > 0)
			{
				LOG_INFO(LogDev, "Processing health siphon (Amount: {})", AmountOfHealthSiphon);

				auto KillerAbilityComp = KillerPlayerState->GetAbilitySystemComponent();
				LOG_INFO(LogDev, "KillerAbilityComp: {}", __int64(KillerAbilityComp));

				if (KillerAbilityComp)
				{
					auto ActivatableAbilities = KillerAbilityComp->GetActivatableAbilities();
					auto& Items = ActivatableAbilities->GetItems();
					LOG_INFO(LogDev, "Searching for Siphon ability in {} abilities", Items.Num());

					for (size_t i = 0; i < Items.Num(); ++i)
					{
						auto& Item = Items.At(i, FGameplayAbilitySpec::GetStructSize());
						auto Ability = Item.GetAbility();

						if (Ability && Ability->ClassPrivate)
						{
							auto abilityName = Ability->ClassPrivate->GetName();
							if (abilityName.contains("Siphon"))
							{
								LOG_INFO(LogDev, "Found Siphon ability: {}", abilityName);

								FGameplayTag Tag{};
								Tag.TagName = UKismetStringLibrary::Conv_StringToName(TEXT("GameplayCue.Shield.PotionConsumed"));

								auto NetMulticast_InvokeGameplayCueAdded = FindObject<UFunction>(L"/Script/GameplayAbilities.AbilitySystemComponent.NetMulticast_InvokeGameplayCueAdded");
								auto NetMulticast_InvokeGameplayCueExecuted = FindObject<UFunction>(L"/Script/GameplayAbilities.AbilitySystemComponent.NetMulticast_InvokeGameplayCueExecuted");

								if (!NetMulticast_InvokeGameplayCueAdded || !NetMulticast_InvokeGameplayCueExecuted)
								{
									LOG_INFO(LogDev, "WARNING: GameplayCue functions not found!");
									break;
								}

								static auto GameplayCueTagOffsetAdded = NetMulticast_InvokeGameplayCueAdded->GetOffsetFunc("GameplayCueTag");
								static auto GameplayCueTagOffsetExecuted = NetMulticast_InvokeGameplayCueExecuted->GetOffsetFunc("GameplayCueTag");
								static auto PredictionKeyOffsetAdded = NetMulticast_InvokeGameplayCueAdded->GetOffsetFunc("PredictionKey");

								auto AddedParams = Alloc<void>(NetMulticast_InvokeGameplayCueAdded->GetPropertiesSize());
								auto ExecutedParams = Alloc<void>(NetMulticast_InvokeGameplayCueExecuted->GetPropertiesSize());

								if (!AddedParams || !ExecutedParams)
								{
									LOG_INFO(LogDev, "WARNING: Failed to allocate params!");
									break;
								}

								*(FGameplayTag*)(int64(AddedParams) + GameplayCueTagOffsetAdded) = Tag;
								*(FGameplayTag*)(int64(ExecutedParams) + GameplayCueTagOffsetExecuted) = Tag;

								KillerAbilityComp->ProcessEvent(NetMulticast_InvokeGameplayCueAdded, AddedParams);
								KillerAbilityComp->ProcessEvent(NetMulticast_InvokeGameplayCueExecuted, ExecutedParams);
								LOG_INFO(LogDev, "GameplayCue events invoked");

								break;
							}
						}
					}
				}
			}
		}

		// Apply health siphon to killer pawn
		if (AmountOfHealthSiphon > 0)
		{
			if (KillerPawn && KillerPawn != DeadPawn)
			{
				float Health = KillerPawn->GetHealth();
				float Shield = KillerPawn->GetShield();
				LOG_INFO(LogDev, "Killer stats before siphon - Health: {}, Shield: {}", Health, Shield);

				int MaxHealth = 100;
				int MaxShield = 100;
				int AmountGiven = 0;

				// Give health first
				if ((MaxHealth - Health) > 0)
				{
					int AmountToGive = MaxHealth - Health >= AmountOfHealthSiphon ? AmountOfHealthSiphon : MaxHealth - Health;
					KillerPawn->SetHealth(Health + AmountToGive);
					AmountGiven += AmountToGive;
					LOG_INFO(LogDev, "Gave {} health (total given: {})", AmountToGive, AmountGiven);
				}

				// Give shield if needed
				if ((MaxShield - Shield) > 0 && AmountGiven < AmountOfHealthSiphon)
				{
					int AmountToGive = MaxShield - Shield >= AmountOfHealthSiphon ? AmountOfHealthSiphon : MaxShield - Shield;
					AmountToGive -= AmountGiven;

					if (AmountToGive > 0)
					{
						KillerPawn->SetShield(Shield + AmountToGive);
						AmountGiven += AmountToGive;
						LOG_INFO(LogDev, "Gave {} shield (total given: {})", AmountToGive, AmountGiven);
					}
				}

				LOG_INFO(LogDev, "Siphon complete - Total given: {}", AmountGiven);
				LOG_INFO(LogDev, "Killer stats after siphon - Health: {}, Shield: {}", KillerPawn->GetHealth(), KillerPawn->GetShield());
			}
		}
	}

	// Check respawn
	bool bIsRespawningAllowed = GameState->IsRespawningAllowed(DeadPlayerState);
	LOG_INFO(LogDev, "bIsRespawningAllowed: {}", bIsRespawningAllowed);

	bool bDropInventory = true;

	LoopMutators([&](AFortAthenaMutator* Mutator)
		{
			if (auto FortAthenaMutator_InventoryOverride = Cast<AFortAthenaMutator_InventoryOverride>(Mutator))
			{
				if (FortAthenaMutator_InventoryOverride->GetDropAllItemsOverride(DeadPlayerState->GetTeamIndex()) == EAthenaLootDropOverride::ForceKeep)
				{
					bDropInventory = false;
					LOG_INFO(LogDev, "Mutator set bDropInventory to false");
				}
			}
		}
	);

	LOG_INFO(LogDev, "bDropInventory: {}", bDropInventory);

	// Drop inventory
	if (bDropInventory && !bIsRespawningAllowed)
	{
		LOG_INFO(LogDev, "Processing inventory drop");
		auto WorldInventory = PlayerController->GetWorldInventory();
		LOG_INFO(LogDev, "WorldInventory: {}", __int64(WorldInventory));

		if (WorldInventory)
		{
			auto& ItemInstances = WorldInventory->GetItemList().GetItemInstances();
			LOG_INFO(LogDev, "ItemInstances count: {}", ItemInstances.Num());

			std::vector<std::pair<FGuid, int>> GuidAndCountsToRemove;

			for (int i = 0; i < ItemInstances.Num(); ++i)
			{
				auto ItemInstance = ItemInstances.at(i);
				LOG_INFO(LogDev, "[{}/{}] Processing ItemInstance: {}", i, ItemInstances.Num(), __int64(ItemInstance));

				if (!ItemInstance)
				{
					LOG_INFO(LogDev, "[{}/{}] Skipping null ItemInstance", i, ItemInstances.Num());
					continue;
				}

				auto ItemEntry = ItemInstance->GetItemEntry();
				auto WorldItemDefinition = Cast<UFortWorldItemDefinition>(ItemEntry->GetItemDefinition());

				if (!WorldItemDefinition)
				{
					LOG_INFO(LogDev, "[{}/{}] Skipping - no WorldItemDefinition", i, ItemInstances.Num());
					continue;
				}

				LOG_INFO(LogDev, "[{}/{}] Item: {}", i, ItemInstances.Num(), WorldItemDefinition->GetFullName());

				auto ShouldBeDropped = WorldItemDefinition->CanBeDropped();
				LOG_INFO(LogDev, "[{}/{}] ShouldBeDropped: {}", i, ItemInstances.Num(), ShouldBeDropped);

				if (!ShouldBeDropped)
					continue;

				PickupCreateData CreateData;
				CreateData.PawnOwner = DeadPawn;
				CreateData.bToss = true;
				CreateData.ItemEntry = ItemEntry;
				CreateData.SourceType = EFortPickupSourceTypeFlag::GetPlayerValue();
				CreateData.Source = EFortPickupSpawnSource::GetPlayerEliminationValue();
				CreateData.SpawnLocation = DeathLocation;

				auto Pickup = AFortPickup::SpawnPickup(CreateData);
				LOG_INFO(LogDev, "[{}/{}] Spawned pickup: {}", i, ItemInstances.Num(), __int64(Pickup));

				GuidAndCountsToRemove.push_back({ ItemEntry->GetItemGuid(), ItemEntry->GetCount() });
			}

			LOG_INFO(LogDev, "Removing {} items from inventory", GuidAndCountsToRemove.size());
			for (auto& Pair : GuidAndCountsToRemove)
			{
				WorldInventory->RemoveItem(Pair.first, nullptr, Pair.second, true);
			}

			WorldInventory->Update();
			LOG_INFO(LogDev, "Inventory update complete");
		}
	}

	// Handle death and game state
	if (!bIsRespawningAllowed)
	{
		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		LOG_INFO(LogDev, "GameMode: {}", __int64(GameMode));
		LOG_INFO(LogDev, "PlayersLeft: {}, IsDBNO: {}", GameState->GetPlayersLeft(), DeadPawn->IsDBNO());

		if (!DeadPawn->IsDBNO())
		{
			if (bHandleDeath)
			{
				LOG_INFO(LogDev, "Handling death (bHandleDeath = true)");

				if (Fortnite_Version > 1.8 || Fortnite_Version == 1.11)
				{
					static void (*RemoveFromAlivePlayers)(AFortGameModeAthena * GameMode, AFortPlayerController * PlayerController, APlayerState * PlayerState, APawn * FinisherPawn,
						UFortWeaponItemDefinition * FinishingWeapon, uint8_t DeathCause, char a7)
						= decltype(RemoveFromAlivePlayers)(Addresses::RemoveFromAlivePlayers);

					AActor* DamageCauser = *(AActor**)(__int64(DeathReport) + MemberOffsets::DeathReport::DamageCauser);
					LOG_INFO(LogDev, "DamageCauser: {}", __int64(DamageCauser));

					UFortWeaponItemDefinition* KillerWeaponDef = nullptr;

					static auto FortProjectileBaseClass = FindObject<UClass>(L"/Script/FortniteGame.FortProjectileBase");

					if (DamageCauser)
					{
						if (DamageCauser->IsA(FortProjectileBaseClass))
						{
							auto Owner = Cast<AFortWeapon>(DamageCauser->GetOwner());
							KillerWeaponDef = Owner->IsValidLowLevel() ? Owner->GetWeaponData() : nullptr;
							LOG_INFO(LogDev, "DamageCauser is projectile, weapon: {}", __int64(KillerWeaponDef));
						}
						if (auto Weapon = Cast<AFortWeapon>(DamageCauser))
						{
							KillerWeaponDef = Weapon->GetWeaponData();
							LOG_INFO(LogDev, "DamageCauser is weapon: {}", __int64(KillerWeaponDef));
						}
					}

					LOG_INFO(LogDev, "Calling RemoveFromAlivePlayers");
					RemoveFromAlivePlayers(GameMode, PlayerController, KillerPlayerState == DeadPlayerState ? nullptr : KillerPlayerState, KillerPawn, KillerWeaponDef, DeathCause, 0);
					LOG_INFO(LogDev, "RemoveFromAlivePlayers completed");
				}

				LOG_INFO(LogDev, "TeamsLeft: {}", GameState->GetTeamsLeft());
			}
		}

		// Spectating (early versions)
		if (Fortnite_Version < 6)
		{
			if (GameState->GetGamePhase() > EAthenaGamePhase::Warmup)
			{
				static auto bAllowSpectateAfterDeathOffset = GameMode->GetOffset("bAllowSpectateAfterDeath");
				bool bAllowSpectate = GameMode->Get<bool>(bAllowSpectateAfterDeathOffset);
				LOG_INFO(LogDev, "bAllowSpectate: {}", bAllowSpectate);

				if (bAllowSpectate)
				{
					LOG_INFO(LogDev, "Setting up spectating");
					static auto PlayerToSpectateOnDeathOffset = PlayerController->GetOffset("PlayerToSpectateOnDeath");
					PlayerController->Get<APawn*>(PlayerToSpectateOnDeathOffset) = KillerPawn;
					UKismetSystemLibrary::K2_SetTimer(PlayerController, L"SpectateOnDeath", 5.f, false);
					LOG_INFO(LogDev, "Spectating timer set");
				}
			}
		}

		// Set spectating state (later versions)
		if (Fortnite_Version >= 15)
		{
			PlayerController->GetStateName() = UKismetStringLibrary::Conv_StringToName(L"Spectating");
			LOG_INFO(LogDev, "Set player state to Spectating");
		}

		// Check win condition
		if (GameState->GetGamePhase() > EAthenaGamePhase::Warmup)
		{
			int PlayersLeft = GameState->GetPlayersLeft();
			int TeamsLeft = GameState->GetTeamsLeft();

			LOG_INFO(LogDev, "Checking win condition - PlayersLeft: {}, TeamsLeft: {}", PlayersLeft, TeamsLeft);

			// Win condition: Only 1 team/player remaining
			bool bDidSomeoneWin = false;

			if (TeamsLeft <= 1)
			{
				LOG_INFO(LogDev, "Win condition met! TeamsLeft: {}", TeamsLeft);
				bDidSomeoneWin = true;
			}
			else if (PlayersLeft <= 1 && TeamsLeft <= 1)
			{
				LOG_INFO(LogDev, "Win condition met! PlayersLeft: {}", PlayersLeft);
				bDidSomeoneWin = true;
			}

			// Additional validation: Check if any player has Place == 1
			if (!bDidSomeoneWin)
			{
				auto AllPlayerStates = UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFortPlayerStateAthena::StaticClass());
				LOG_INFO(LogDev, "Checking places for {} PlayerStates", AllPlayerStates.Num());

				for (int i = 0; i < AllPlayerStates.Num(); ++i)
				{
					auto CurrentPlayerState = (AFortPlayerStateAthena*)AllPlayerStates.at(i);
					int place = CurrentPlayerState->GetPlace();
					LOG_INFO(LogDev, "PlayerState[{}] place: {}", i, place);

					if (place == 1)
					{
						bDidSomeoneWin = true;
						LOG_INFO(LogDev, "Win condition met! PlayerState {} has winning place 1", i);
						break;
					}
				}
			}

			LOG_INFO(LogDev, "bDidSomeoneWin: {}", bDidSomeoneWin);

			if (bDidSomeoneWin)
			{
				LOG_INFO(LogDev, "Game over - removing server and exiting");
				RemoveServer();
				ShutdownAfter40Sec();
			}
		}
	}

	// Bot cleanup
	if (DeadPlayerState->IsBot())
	{
		LOG_INFO(LogDev, "DeadPlayerState is bot - cleanup needed");
	}

	DeadPlayerState->EndDBNOAbilities();
	LOG_INFO(LogDev, "EndDBNOAbilities called");

	LOG_INFO(LogDev, "=== ClientOnPawnDiedHook END - Calling original ===");
	return ClientOnPawnDiedOriginal(PlayerController, DeathReport);
}

void AFortPlayerController::ServerBeginEditingBuildingActorHook(AFortPlayerController* PlayerController, ABuildingSMActor* BuildingActorToEdit)
{
	DEBUG_LOG_INFO(LogDev, "\n[Begin START]!");

	if (!BuildingActorToEdit || !BuildingActorToEdit->IsPlayerPlaced()) // We need more checks.
		return;

	auto Pawn = PlayerController->GetMyFortPawn();

	if (!Pawn)
		return;

	auto PlayerState = PlayerController->GetPlayerState();

	if (!PlayerState)
		return;

	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return;

	static auto EditToolDef = FindObject<UFortWeaponItemDefinition>(L"/Game/Items/Weapons/BuildingTools/EditTool.EditTool");

	auto EditToolInstance = WorldInventory->FindItemInstance(EditToolDef);

	if (!EditToolInstance)
		return;

	Pawn->EquipWeaponDefinition(EditToolDef, EditToolInstance->GetItemEntry()->GetItemGuid());

	auto EditTool = Cast<AFortWeap_EditingTool>(Pawn->GetCurrentWeapon());
	DEBUG_LOG_INFO(LogDev, "[Begin {}] EditTool: {}!", BuildingActorToEdit->GetFullName(), __int64(EditTool));

	if (!EditTool)
		return;

	EditTool->GetEditActor() = BuildingActorToEdit;
	EditTool->OnRep_EditActor();

	BuildingActorToEdit->SetEditingPlayer(PlayerState);

	DEBUG_LOG_INFO(LogDev, "[Begin] Updating Editing player to: {}!", __int64(PlayerState));
}

void AFortPlayerController::ServerEditBuildingActorHook(UObject* Context, FFrame& Stack, void* Ret)
{
	DEBUG_LOG_INFO(LogDev, "\n[Edit START]!");

	auto PlayerController = (AFortPlayerController*)Context;

	auto PlayerState = (AFortPlayerState*)PlayerController->GetPlayerState();

	auto Params = Stack.Locals;

	static auto RotationIterationsOffset = FindOffsetStruct("/Script/FortniteGame.FortPlayerController.ServerEditBuildingActor", "RotationIterations");
	static auto NewBuildingClassOffset = FindOffsetStruct("/Script/FortniteGame.FortPlayerController.ServerEditBuildingActor", "NewBuildingClass");
	static auto BuildingActorToEditOffset = FindOffsetStruct("/Script/FortniteGame.FortPlayerController.ServerEditBuildingActor", "BuildingActorToEdit");
	static auto bMirroredOffset = FindOffsetStruct("/Script/FortniteGame.FortPlayerController.ServerEditBuildingActor", "bMirrored");

	auto BuildingActorToEdit = *(ABuildingSMActor**)(__int64(Params) + BuildingActorToEditOffset);
	auto NewBuildingClass = *(UClass**)(__int64(Params) + NewBuildingClassOffset);
	int RotationIterations = Fortnite_Version < 8.30 ? *(int*)(__int64(Params) + RotationIterationsOffset) : (int)(*(uint8*)(__int64(Params) + RotationIterationsOffset));
	auto bMirrored = *(char*)(__int64(Params) + bMirroredOffset);

	// LOG_INFO(LogDev, "RotationIterations: {}", RotationIterations);

	if (!BuildingActorToEdit || !NewBuildingClass || BuildingActorToEdit->IsDestroyed() || BuildingActorToEdit->GetEditingPlayer() != PlayerState)
	{
		// LOG_INFO(LogDev, "Cheater?");
		// LOG_INFO(LogDev, "BuildingActorToEdit->GetEditingPlayer(): {} PlayerState: {} NewBuildingClass: {} BuildingActorToEdit: {}", BuildingActorToEdit ? __int64(BuildingActorToEdit->GetEditingPlayer()) : -1, __int64(PlayerState), __int64(NewBuildingClass), __int64(BuildingActorToEdit));
		return ServerEditBuildingActorOriginal(Context, Stack, Ret);
	}

	// if (!PlayerState || PlayerState->GetTeamIndex() != BuildingActorToEdit->GetTeamIndex()) 
		//return ServerEditBuildingActorOriginal(Context, Frame, Ret);

	if (Fortnite_Version >= 8 && Fortnite_Version < 11) // uhhmmm
	  BuildingActorToEdit->SetEditingPlayer(nullptr);

	static ABuildingSMActor* (*BuildingSMActorReplaceBuildingActor)(ABuildingSMActor*, __int64, UClass*, int, int, uint8_t, AFortPlayerController*) =
		decltype(BuildingSMActorReplaceBuildingActor)(Addresses::ReplaceBuildingActor);

	if (auto BuildingActor = BuildingSMActorReplaceBuildingActor(BuildingActorToEdit, 1, NewBuildingClass,
		BuildingActorToEdit->GetCurrentBuildingLevel(), RotationIterations, bMirrored, PlayerController))
	{
		BuildingActor->SetPlayerPlaced(true);
	}

	if (Fortnite_Version >= 11)
	{
		BuildingActorToEdit->SetEditingPlayer(nullptr);
		auto Pawn = PlayerController->GetMyFortPawn();

		if (!Pawn)
			return ServerEditBuildingActorOriginal(Context, Stack, Ret);

		static auto EditToolDef = FindObject<UFortWeaponItemDefinition>(L"/Game/Items/Weapons/BuildingTools/EditTool.EditTool");

		auto WorldInventory = PlayerController->GetWorldInventory();

		if (!WorldInventory)
			return ServerEditBuildingActorOriginal(Context, Stack, Ret);

		auto EditToolInstance = WorldInventory->FindItemInstance(EditToolDef);

		if (!EditToolInstance)
			return ServerEditBuildingActorOriginal(Context, Stack, Ret);

		Pawn->EquipWeaponDefinition(EditToolDef, EditToolInstance->GetItemEntry()->GetItemGuid());

		auto EditTool = Cast<AFortWeap_EditingTool>(Pawn->GetCurrentWeapon());
		DEBUG_LOG_INFO(LogDev, "[Edit] New Equipped EditTool: {}", __int64(EditTool));

		if (EditTool)
		{
			EditTool->GetEditActor() = nullptr;
		}
	}

	return ServerEditBuildingActorOriginal(Context, Stack, Ret);
}

void AFortPlayerController::ServerEndEditingBuildingActorHook(AFortPlayerController* PlayerController, ABuildingSMActor* BuildingActorToStopEditing)
{
	DEBUG_LOG_INFO(LogDev, "\n[End START] [{}] ServerEndEditingBuildingActorHook EditiNgplAyer: {}!", BuildingActorToStopEditing ? BuildingActorToStopEditing->GetName() : "NULL", BuildingActorToStopEditing ? __int64(BuildingActorToStopEditing->GetEditingPlayer()) : -1);
	auto Pawn = PlayerController->GetMyFortPawn();

	if (!BuildingActorToStopEditing || !Pawn
		|| BuildingActorToStopEditing->GetEditingPlayer() != PlayerController->GetPlayerState()
		|| BuildingActorToStopEditing->IsDestroyed())
		return;

	BuildingActorToStopEditing->SetEditingPlayer(nullptr);

	static auto EditToolDef = FindObject<UFortWeaponItemDefinition>(L"/Game/Items/Weapons/BuildingTools/EditTool.EditTool");

	auto WorldInventory = PlayerController->GetWorldInventory();

	if (!WorldInventory)
		return;

	auto EditToolInstance = WorldInventory->FindItemInstance(EditToolDef);

	if (!EditToolInstance)
		return;

	auto OldWep = Pawn->GetCurrentWeapon();
	DEBUG_LOG_INFO(LogDev, "[End] EditTool Equipped BEFORE: {} (name: {})", __int64(Cast<AFortWeap_EditingTool>(OldWep)), OldWep ? OldWep->GetFullName() : "NULL");

	if (Fortnite_Version >= 11)
	{
		Pawn->EquipWeaponDefinition(EditToolDef, EditToolInstance->GetItemEntry()->GetItemGuid());
	}

	auto EditTool = Cast<AFortWeap_EditingTool>(Pawn->GetCurrentWeapon());
	DEBUG_LOG_INFO(LogDev, "[End] EditTool Equipped AFTER: {}", __int64(EditTool));

	if (EditTool)
	{
		EditTool->GetEditActor() = nullptr;
		EditTool->OnRep_EditActor();
	}
}