#include "PersistentStateSubsystem.h"

#include "PersistentStateDefines.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStorage.h"
#include "Kismet/GameplayStatics.h"
#include "Managers/GamePersistentStateManager.h"
#include "Managers/WorldPersistentStateManager.h"

bool IPersistentStateWorldSettings::ShouldStoreWorldState(AWorldSettings& WorldSettings)
{
	return !WorldSettings.Implements<UPersistentStateWorldSettings>() || CastChecked<IPersistentStateWorldSettings>(&WorldSettings)->ShouldStoreWorldState();
}

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
		CurrentSlot = StateStorage->GetStateSlotByName(StartupSlot);
	}
	
	FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::OnWorldInit);
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
	FWorldDelegates::OnSeamlessTravelTransition.AddUObject(this, &ThisClass::OnWorldSeamlessTravel);
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
	return UPersistentStateSettings::Get()->bEnabled && UPersistentStateSettings::Get()->StateStorageClass != nullptr;
}


void UPersistentStateSubsystem::Tick(float DeltaTime)
{
	check(StateStorage);
	StateStorage->Tick(DeltaTime);
	
	for (UPersistentStateManager* Manager: WorldManagers)
	{
		Manager->Tick(DeltaTime);
	}
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
	if (!CurrentSlot.IsValid())
	{
		// no active slot. User should create one before calling SaveGame
		return false;
	}

	return SaveGameToSlot(CurrentSlot);
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
	
	FPersistentStateSlotHandle SourceSlot = CurrentSlot.IsValid() ? CurrentSlot : TargetSlot;
	CurrentSlot = TargetSlot;

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
	
	CurrentSlot = TargetSlot;
	
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
    	
	CurrentSlot = TargetSlot;
	
	ResetWorldState();
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, World, true, TravelOptions);

	return true;
}

FPersistentStateSlotHandle UPersistentStateSubsystem::FindSaveGameSlotByName(FName SlotName) const
{
	check(StateStorage);
	return StateStorage->GetStateSlotByName(SlotName);
}

void UPersistentStateSubsystem::GetSaveGameSlots(TArray<FPersistentStateSlotHandle>& OutSlots, bool bUpdate) const
{
	check(StateStorage);
	if (bUpdate)
	{
		StateStorage->RefreshSlots();
	}
	StateStorage->GetAvailableSlots(OutSlots);
}

FPersistentStateSlotHandle UPersistentStateSubsystem::CreateSaveGameSlot(const FString& SlotName, const FText& Title)
{
	check(StateStorage);
	return StateStorage->CreateStateSlot(SlotName, Title);
}

void UPersistentStateSubsystem::NotifyInitialized(UObject& Object)
{
	check(Object.Implements<UPersistentStateObject>());
	for (UPersistentStateManager* StateManager: WorldManagers)
	{
		StateManager->NotifyObjectInitialized(Object);
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

			LoadWorldState(World, CurrentSlot);

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
		if (CurrentSlot.IsValid() && HasWorldState())
		{
			SaveWorldState(World, CurrentSlot, CurrentSlot);
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

void UPersistentStateSubsystem::LoadWorldState(UWorld* World, const FPersistentStateSlotHandle& SourceSlot)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSubsystem_LoadWorldState, PersistentStateChannel);

	bHasWorldState = true;
	// load requested state into state managers
	if (SourceSlot.IsValid())
	{
		OnLoadStateStarted.Broadcast(SourceSlot);
		StateStorage->LoadWorldState(SourceSlot, World->GetFName(), WorldManagers);
		OnLoadStateFinished.Broadcast(SourceSlot);
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
	
	OnSaveStateStarted.Broadcast(TargetSlot);
	
	for (UPersistentStateManager* Manager: WorldManagers)
	{
		Manager->SaveGameState();
	}
	StateStorage->SaveWorldState(SourceSlot, TargetSlot, World, WorldManagers);
	
	OnSaveStateFinished.Broadcast(TargetSlot);
}

