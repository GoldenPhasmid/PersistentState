#include "PersistentStateSubsystem.h"

#include "PersistentStateModule.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"
#include "PersistentStateStorage.h"
#include "Kismet/GameplayStatics.h"
#include "Managers/GamePersistentStateManager.h"
#include "Managers/WorldPersistentStateManager.h"

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
#endif

UPersistentStateSubsystem::UPersistentStateSubsystem()
{
	
}

UPersistentStateSubsystem* UPersistentStateSubsystem::Get(UObject* WorldContextObject)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<ThisClass>();
		}
	}

	return nullptr;
}

UPersistentStateSubsystem* UPersistentStateSubsystem::Get(UWorld* World)
{
	if (World != nullptr)
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<ThisClass>();
		}
	}

	return nullptr;
}

void UPersistentStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	check(!bInitialized);
	bInitialized = true;

	TArray<UClass*> ManagerClasses;
	GetDerivedClasses(UGamePersistentStateManager::StaticClass(), ManagerClasses, true);
	GetDerivedClasses(UWorldPersistentStateManager::StaticClass(), WorldManagerClasses, true);

	StateStorage = NewObject<UPersistentStateStorage>(this, UPersistentStateSettings::Get()->StateStorageClass);
	StateStorage->Init();

	if (FName StartupSlot = UPersistentStateSettings::Get()->StartupSlotName; StartupSlot != NAME_None)
	{
		ActiveSlot = StateStorage->GetStateSlotByName(StartupSlot);
	}
	
	FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::OnWorldInit);
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
	FWorldDelegates::OnSeamlessTravelTransition.AddUObject(this, &ThisClass::OnWorldSeamlessTravel);

#if WITH_EDITOR
	FEditorDelegates::PrePIEEnded.AddUObject(this, &ThisClass::OnEndPlay);
#endif
}

void UPersistentStateSubsystem::Deinitialize()
{
	ResetWorldState();

	FWorldDelegates::OnPostWorldInitialization.RemoveAll(this);
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);
	FWorldDelegates::OnSeamlessTravelTransition.RemoveAll(this);
	
	StateStorage->Shutdown();
	StateStorage->MarkAsGarbage();
	StateStorage = nullptr;

	check(bInitialized);
	bInitialized = false;
	
	Super::Deinitialize();
}


bool UPersistentStateSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	TArray<UClass*> DerivedSubsystems;
	GetDerivedClasses(ThisClass::StaticClass(), DerivedSubsystems);

	// allow derived subsystem to override default implementation
	for (UClass* DerivedClass: DerivedSubsystems)
	{
		if (UGameInstanceSubsystem* Subsystem = DerivedClass->GetDefaultObject<UGameInstanceSubsystem>();
			Subsystem && Subsystem->ShouldCreateSubsystem(Outer))
		{
			return false;
		}
	}

	// only create subsystem if it is enabled in Project Settings
	auto Settings = UPersistentStateSettings::Get();
	return Settings->bEnabled && Settings->StateStorageClass != nullptr;
}


void UPersistentStateSubsystem::Tick(float DeltaTime)
{
	check(StateStorage);
}

ETickableTickType UPersistentStateSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

bool UPersistentStateSubsystem::IsAllowedToTick() const
{
	return bInitialized && !IsTemplate();
}

bool UPersistentStateSubsystem::IsTickableWhenPaused() const
{
	return true;
}

TStatId UPersistentStateSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPersistentStateSubsystem, STATGROUP_Tickables);
}

UPersistentStateManager* UPersistentStateSubsystem::GetStateManager(TSubclassOf<UPersistentStateManager> ManagerClass) const
{
	if (ManagerClass->IsChildOf<UWorldPersistentStateManager>())
	{
		for (UPersistentStateManager* StateManager: WorldManagers)
		{
			if (StateManager->GetClass() == ManagerClass)
			{
				return StateManager;
			}
		}
	}
	
	return nullptr;
}

bool UPersistentStateSubsystem::SaveGame()
{
	if (!ActiveSlot.IsValid())
	{
		// no active slot. User should create one before calling SaveGame
		return false;
	}

	return SaveGameToSlot(ActiveSlot);
}

bool UPersistentStateSubsystem::SaveGameToSlot(const FPersistentStateSlotHandle& TargetSlot)
{
	check(StateStorage);
	if (!TargetSlot.IsValid())
	{
		return false;
	}

	if (!StateStorage->CanSaveToStateSlot(TargetSlot))
	{
		return false;
	}
	
	FPersistentStateSlotHandle SourceSlot = ActiveSlot.IsValid() ? ActiveSlot : TargetSlot;
	ActiveSlot = TargetSlot;

	SaveWorldState(GetWorld(), SourceSlot, TargetSlot);
	return true;
}

bool UPersistentStateSubsystem::LoadGameFromSlot(const FPersistentStateSlotHandle& TargetSlot, FString TravelOptions)
{
	check(StateStorage);
	if (!TargetSlot.IsValid())
	{
		return false;
	}

	if (!StateStorage->CanLoadFromStateSlot(TargetSlot))
	{
		return false;
	}

	FName WorldToLoad = StateStorage->GetWorldFromStateSlot(TargetSlot);
	if (WorldToLoad == NAME_None)
	{
		return false;
	}
	
	ActiveSlot = TargetSlot;
	
	ResetWorldState();
	UGameplayStatics::OpenLevel(this, WorldToLoad, true, TravelOptions);

	return true;
}

bool UPersistentStateSubsystem::LoadGameWorldFromSlot(const FPersistentStateSlotHandle& TargetSlot, TSoftObjectPtr<UWorld> World, FString TravelOptions)
{
	check(StateStorage);
    if (!TargetSlot.IsValid())
    {
    	return false;
    }

    if (!StateStorage->CanLoadFromStateSlot(TargetSlot))
    {
    	return false;
    }
    	
	ActiveSlot = TargetSlot;
	
	ResetWorldState();
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, World, true, TravelOptions);

	return true;
}

FPersistentStateSlotHandle UPersistentStateSubsystem::FindSaveGameSlotByName(FName SlotName) const
{
	check(StateStorage);
	return StateStorage->GetStateSlotByName(SlotName);
}

void UPersistentStateSubsystem::RemoveSaveGameSlot(const FPersistentStateSlotHandle& Slot) const
{
	check(StateStorage);
	return StateStorage->RemoveStateSlot(Slot);
}

void UPersistentStateSubsystem::GetSaveGameSlots(TArray<FPersistentStateSlotHandle>& OutSlots, bool bUpdate, bool bOnDiskOnly) const
{
	check(StateStorage);
	if (bUpdate)
	{
		StateStorage->UpdateAvailableStateSlots();
	}
	StateStorage->GetAvailableStateSlots(OutSlots, bOnDiskOnly);
}

FPersistentStateSlotHandle UPersistentStateSubsystem::CreateSaveGameSlot(FName SlotName, FText Title)
{
	check(StateStorage);
	return StateStorage->CreateStateSlot(SlotName, Title);
}

void UPersistentStateSubsystem::NotifyObjectInitialized(UObject& Object)
{
	check(Object.Implements<UPersistentStateObject>());
	for (UPersistentStateManager* StateManager: WorldManagers)
	{
		StateManager->NotifyObjectInitialized(Object);
	}
}

void UPersistentStateSubsystem::LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSubsystem_LoadWorldState, PersistentStateChannel);
	
	check(StateStorage);

	UWorld* World = GetOuterUGameInstance()->GetWorld();
	check(World);
	
	FWorldStateSharedRef WorldState = StateStorage->LoadWorldState(TargetSlotHandle, World->GetFName());
	if (WorldState.IsValid())
	{
		UE::PersistentState::LoadWorldState(WorldManagers, WorldState);
	}
}

void UPersistentStateSubsystem::OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS)
{
	if (World && World == GetOuterUGameInstance()->GetWorld())
	{
		check(WorldManagers.IsEmpty());
		AWorldSettings* WorldSettings = World->GetWorldSettings();
		check(WorldSettings);
		
		if (IPersistentStateWorldSettings::ShouldStoreWorldState(*WorldSettings))
		{
			for (UClass* ManagerClass: WorldManagerClasses)
			{
				if (ManagerClass->GetDefaultObject<UWorldPersistentStateManager>()->ShouldCreateManager(World))
				{
					UWorldPersistentStateManager* Manager = NewObject<UWorldPersistentStateManager>(this, ManagerClass);
					WorldManagers.Add(Manager);
				}
			}

			LoadWorldState(World, ActiveSlot);

			for (UPersistentStateManager* StateManager: WorldManagers)
			{
				CastChecked<UWorldPersistentStateManager>(StateManager)->Init(World);
			}
		}
	}
}

void UPersistentStateSubsystem::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (World && World == GetOuterUGameInstance()->GetWorld())
	{
		if (ActiveSlot.IsValid() && HasWorldState())
		{
			SaveWorldState(World, ActiveSlot, ActiveSlot);
		}

		ResetWorldState();
	}
}

void UPersistentStateSubsystem::OnWorldSeamlessTravel(UWorld* World)
{
	if (World == GetOuterUGameInstance()->GetWorld())
	{
		UE_LOG(LogPersistentState, Display, TEXT("Map SeamlessTravel: %s"), *UGameplayStatics::GetCurrentLevelName(World));
		OnWorldCleanup(World, false, true);
	}
}

#if WITH_EDITOR
void UPersistentStateSubsystem::OnEndPlay(const bool bSimulating)
{
	// do not save world cleanup caused by PIE end
	ResetWorldState();
}
#endif

void UPersistentStateSubsystem::ResetWorldState()
{
	UGameInstance* GameInstance = GetOuterUGameInstance();

	UWorld* World = GameInstance->GetWorld();
	for (UPersistentStateManager* Manager: WorldManagers)
	{
		CastChecked<UWorldPersistentStateManager>(Manager)->Cleanup(World);
	}
	WorldManagers.Empty();

	bHasWorldState = false;
}

void UPersistentStateSubsystem::LoadWorldState(UWorld* World, const FPersistentStateSlotHandle& TargetSlot)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSubsystem_LoadWorldState, PersistentStateChannel);
	check(World);
	
	bHasWorldState = true;
	// load requested state into state managers
	if (TargetSlot.IsValid())
	{
		check(TargetSlot == ActiveSlot);
		OnLoadStateStarted.Broadcast(TargetSlot);
	
		FWorldStateSharedRef WorldState = StateStorage->LoadWorldState(TargetSlot, World->GetFName());
		if (WorldState.IsValid())
		{
			UE::PersistentState::LoadWorldState(WorldManagers, WorldState);
		}
		LoadWorldState(TargetSlot);
		OnLoadStateFinished.Broadcast(TargetSlot);
	}
}

void UPersistentStateSubsystem::SaveWorldState(UWorld* World, const FPersistentStateSlotHandle& SourceSlot, const FPersistentStateSlotHandle& TargetSlot)
{
	if (HasWorldState() == false)
	{
		// nothing to save
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSubsystem_SaveWorldState, PersistentStateChannel);
	check(World && StateStorage);
	
	OnSaveStateStarted.Broadcast(TargetSlot);
	
	for (UPersistentStateManager* Manager: WorldManagers)
	{
		Manager->SaveGameState();
	}

	FWorldStateSharedRef WorldState = UE::PersistentState::SaveWorldState(World->GetFName(), World->GetPackage()->GetFName(), WorldManagers);
	check(WorldState.IsValid());
	
	StateStorage->SaveWorldState(WorldState, SourceSlot, TargetSlot);
	
	OnSaveStateFinished.Broadcast(TargetSlot);
}

