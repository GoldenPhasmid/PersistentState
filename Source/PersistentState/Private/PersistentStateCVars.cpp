#include "PersistentStateCVars.h"
#include "PersistentStateModule.h"
#include "PersistentStateStorage.h"
#include "PersistentStateSubsystem.h"

namespace UE::PersistentState
{
	bool GPersistentState_Enabled = true;
	FAutoConsoleVariableRef PersistentState_Enabled(
		TEXT("PersistentState.Enabled"),
		GPersistentState_Enabled,
		TEXT("Values true/false, true by default."),
		ECVF_Default
	);
	
	bool GPersistentState_StatsEnabled = true;
	FAutoConsoleVariableRef PersistentState_StatsEnabled(
		TEXT("PersistentState.StatsEnabled"),
		GPersistentState_StatsEnabled,
		TEXT("Values true/false, true by default."),
		ECVF_Default
	);
	
	bool GPersistentStateStorage_ForceGameThread = false;
	FAutoConsoleVariableRef PersistentStateStorage_ForceGameThread(
		TEXT("PersistentState.ForceGameThread"),
		GPersistentStateStorage_ForceGameThread,
		TEXT("Values true/false, false by default."),
		ECVF_Default
	);

	bool GPersistentState_CanCreateProfileState = true;
	FAutoConsoleVariableRef PersistentState_ShouldCreateProfileState(
		TEXT("PersistentState.CanCreateProfileState"),
		GPersistentState_CanCreateProfileState,
		TEXT("Values true/false, true by default."),
		ECVF_Default
	);

	bool GPersistentState_CanCreateGameState = true;
	FAutoConsoleVariableRef PersistentState_ShouldCreateGameState(
		TEXT("PersistentState.CanCreateGameState"),
		GPersistentState_CanCreateProfileState,
		TEXT("Values true/false, true by default."),
		ECVF_Default
	);

	bool GPersistentState_CanCreateWorldState = true;
	FAutoConsoleVariableRef PersistentState_ShouldCreateWorldState(
		TEXT("PersistentState.CanCreateWorldState"),
		GPersistentState_CanCreateProfileState,
		TEXT("Values true/false, true by default."),
		ECVF_Default
	);
	
#if !UE_BUILD_SHIPPING
	FAutoConsoleCommandWithWorldAndArgs SaveGameToSlotCmd(
		TEXT("PersistentState.SaveGame"),
		TEXT("[SlotName]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InParams, UWorld* World)
		{
			if (InParams.Num() < 1)
			{
				return;
			}
		
			if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(World))
			{
				const FName SlotName = *InParams[0];
				FPersistentStateSlotHandle SlotHandle = Subsystem->FindSaveGameSlotByName(SlotName);
				if (!SlotHandle.IsValid())
				{
					SlotHandle = Subsystem->CreateSaveGameSlot(SlotName, FText::FromName(SlotName));
				}

				check(SlotHandle.IsValid());
				const bool bResult = Subsystem->SaveGameToSlot(SlotHandle);
				UE_CLOG(bResult == false, LogPersistentState, Error, TEXT("Failed to SaveGame to a slot %s"), *SlotName.ToString());
			}
		})
	);

	FAutoConsoleCommandWithWorldAndArgs LoadGameFromSlotCmd(
		TEXT("PersistentState.LoadGame"),
		TEXT("[SlotName]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InParams, UWorld* World)
		{
			if (InParams.Num() < 1)
			{
				return;
			}
		
			if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(World))
			{
				const FName SlotName = *InParams[0];
				FPersistentStateSlotHandle SlotHandle = Subsystem->FindSaveGameSlotByName(SlotName);
				if (SlotHandle.IsValid())
				{
					const bool bResult = Subsystem->LoadGameFromSlot(SlotHandle);
					UE_CLOG(bResult == false, LogPersistentState, Error, TEXT("Failed to LoadGame from slot %s"), *SlotName.ToString());
				}
			}
		})
	);

	FAutoConsoleCommandWithWorldAndArgs CreateSlotCmd(
		TEXT("PersistentState.CreateSlot"),
		TEXT("[SlotName]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InParams, UWorld* World)
		{
			if (InParams.Num() < 1)
			{
				return;
			}
			
			if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(World))
			{
				const FName SlotName = *InParams[0];
				if (FPersistentStateSlotHandle SlotHandle = Subsystem->FindSaveGameSlotByName(SlotName); !SlotHandle.IsValid())
				{
					Subsystem->CreateSaveGameSlot(SlotName, FText::FromName(SlotName));
				}
			}
		})
	);
	
	FAutoConsoleCommandWithWorldAndArgs DeleteSlotCmd(
		TEXT("PersistentState.DeleteSlot"),
		TEXT("[SlotName]. Remove save game slot and associated save data"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InParams, UWorld* World)
		{
			if (InParams.Num() < 1)
			{
				return;
			}
			
			if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(World))
			{
				const FName SlotName = *InParams[0];
				if (FPersistentStateSlotHandle SlotHandle = Subsystem->FindSaveGameSlotByName(SlotName); SlotHandle.IsValid())
				{
					Subsystem->RemoveSaveGameSlot(SlotHandle);
				}
			}
		})
	);

	FAutoConsoleCommandWithWorldAndArgs DeleteAllSlotsCmd(
		TEXT("PersistentState.DeleteAllSlots"),
		TEXT("Remove all save game slots and associated save data"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InParams, UWorld* World)
		{
			if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(World))
			{
				TArray<FPersistentStateSlotHandle> SlotHandles;
				Subsystem->GetSaveGameSlots(SlotHandles);

				for (const FPersistentStateSlotHandle& Slot: SlotHandles)
				{
					Subsystem->RemoveSaveGameSlot(Slot);
				}
			}
		})
	);
	
	FAutoConsoleCommandWithWorld UpdateSlotsCmd(
		TEXT("PersistentState.UpdateSlots"),
		TEXT("Update save game slots"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(World))
			{
				Subsystem->UpdateSaveGameSlots();
			}
		})
	);

	FAutoConsoleCommandWithWorld ListSlotsCmd(
		TEXT("PersistentState.ListSlots"),
		TEXT("Output available state slots"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(World))
			{
				TArray<FPersistentStateSlotHandle> SlotHandles;
				Subsystem->GetSaveGameSlots(SlotHandles, true);

				for (const FPersistentStateSlotHandle& Slot: SlotHandles)
				{
					UE_LOG(LogPersistentState, Display, TEXT("%s"), *Subsystem->GetSaveGameSlot(Slot).ToString());
				}
			}
		})
	);
#endif
}
