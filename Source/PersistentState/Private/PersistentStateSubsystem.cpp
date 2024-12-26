#include "PersistentStateSubsystem.h"

#include "PersistentStateCVars.h"
#include "PersistentStateInterface.h"
#include "PersistentStateModule.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"
#include "PersistentStateStorage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet/GameplayStatics.h"
#include "Managers/PersistentStateManager.h"

DECLARE_MEMORY_STAT(TEXT("World State Memory"),		STAT_PersistentState_WorldStateMemory,		STATGROUP_PersistentState);
DECLARE_MEMORY_STAT(TEXT("Game State Memory"),		STAT_PersistentState_GameStateMemory,		STATGROUP_PersistentState);
DECLARE_MEMORY_STAT(TEXT("Profile State Memory"),	STAT_PersistentState_ProfileStateMemory,	STATGROUP_PersistentState);
DECLARE_MEMORY_STAT(TEXT("State Storage Memory"),	STAT_PersistentState_StateStorageMemory,	STATGROUP_PersistentState);

UPersistentStateSubsystem::UPersistentStateSubsystem()
{
	
}

UPersistentStateSubsystem* UPersistentStateSubsystem::Get(const UObject* WorldContextObject)
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

UPersistentStateSubsystem* UPersistentStateSubsystem::Get(const UWorld* World)
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

void UPersistentStateSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UPersistentStateSubsystem* This = CastChecked<UPersistentStateSubsystem>(InThis);

	for (auto& [ManagerType, Managers]: This->ManagerMap)
	{
		Collector.AddStableReferenceArray(&Managers);
	}
}

void UPersistentStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	check(!bInitialized);
	bInitialized = true;

	TArray<UClass*> Classes;
	GetDerivedClasses(UPersistentStateManager::StaticClass(), Classes, true);

	// gather non-abstract manager classes and place them in containers divided by manager type
	for (UClass* ManagerClass: Classes)
	{
		if (ManagerClass && !ManagerClass->HasAnyClassFlags(CLASS_Abstract))
		{
			ManagerTypeMap.FindOrAdd(ManagerClass->GetDefaultObject<UPersistentStateManager>()->GetManagerType()).Add(ManagerClass);
		}
	}

	// create state storage
	StateStorage = NewObject<UPersistentStateStorage>(this, UPersistentStateSettings::Get()->StateStorageClass);
	StateStorage->Init();

	auto Settings = UPersistentStateSettings::Get();
	if (FName StartupSlot = Settings->StartupSlotName; StartupSlot != NAME_None)
	{
		ActiveSlot = StateStorage->GetStateSlotByName(StartupSlot);
	}

	CachedCanCreateManagerState = Settings->CanCreateManagerState();

	if (CanCreateManagerState(EManagerStorageType::Profile))
	{
		// create profile managers
		CreateManagerState(EManagerStorageType::Profile);
	}
	
	if (CanCreateManagerState(EManagerStorageType::Game))
	{
		// create game managers
		CreateManagerState(EManagerStorageType::Game);
	}

	check(!ActiveLoadRequest.IsValid() && !PendingLoadRequest.IsValid());
	if (ActiveSlot.IsValid())
	{
		// start loading world state, if active slot is set and last saved world is currently being loaded
		if (FName LastWorld = StateStorage->GetWorldFromStateSlot(ActiveSlot); LastWorld == GetWorld()->GetFName())
		{
			CreateAutoLoadRequest(LastWorld);
		}
	}

	FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::OnPreLoadMap);
	FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::OnWorldInit);
	FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &ThisClass::OnWorldInitActors);
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
	FWorldDelegates::OnSeamlessTravelTransition.AddUObject(this, &ThisClass::OnWorldSeamlessTravel);

#if WITH_EDITOR
	FEditorDelegates::PrePIEEnded.AddUObject(this, &ThisClass::OnEndPlay);
#endif
}

void UPersistentStateSubsystem::CreateAutoLoadRequest(FName MapName)
{
	check(bInitialized && !ActiveLoadRequest.IsValid());
	ActiveLoadRequest = MakeShared<FLoadGamePendingRequest>(ActiveSlot, ActiveSlot, FName{MapName}, false);
	OnLoadStateStarted.Broadcast(ActiveLoadRequest->TargetSlot);
	// request world state via state storage interface
	ActiveLoadRequest->LoadTask = StateStorage->LoadState(ActiveLoadRequest->TargetSlot, ActiveLoadRequest->MapName,
		FLoadCompletedDelegate::CreateUObject(this, &ThisClass::OnLoadStateCompleted, ActiveLoadRequest));
}

void UPersistentStateSubsystem::OnPreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
	if (WorldContext.OwningGameInstance != GetGameInstance())
	{
		return;
	}

	if (!ActiveSlot.IsValid())
	{
		// nothing to load, no active slot
		return;
	}
	
	const FName WorldName = FPackageName::GetShortFName(MapName);
	ensureAlwaysMsgf(!ActiveLoadRequest.IsValid() || ActiveLoadRequest->MapName == WorldName, TEXT("Unexpected PreLoadMap callback."));
	
	// pre-load world state for map that initialized loading
	// if load request is already active, load map request probably instigated by LoadGameFromSlot
	if (!ActiveLoadRequest.IsValid())
	{
		CreateAutoLoadRequest(WorldName);
	}
}

void UPersistentStateSubsystem::OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS)
{
	if (World == nullptr || World != GetOuterUGameInstance()->GetWorld())
	{
		return;
	}

	CacheSourcePackageName(World);
	
	AWorldSettings* WorldSettings = World->GetWorldSettings();
	check(WorldSettings);
	check(!ManagerMap.Contains(EManagerStorageType::World));

	const bool bStoreWorldState = IPersistentStateWorldStateController::ShouldStoreWorldState(*WorldSettings);
	UE_CLOG(!bStoreWorldState, LogPersistentState, Verbose, TEXT("%s: %s world state creation is disabled via World Settings."), *FString(__FUNCTION__), *GetNameSafe(World));
	
	if (bStoreWorldState && CanCreateManagerState(EManagerStorageType::World))
	{
		// create and initialize world managers
		CreateManagerState(EManagerStorageType::World);
	}
	
	if (CanCreateManagerState(EManagerStorageType::Game) && !HasManagerState(EManagerStorageType::Game))
	{
		// create and initialize game managers if we don't have them yet	
		CreateManagerState(EManagerStorageType::Game);
	}

	// finalize loading the world state. We try to load state from disk asynchronously before or during map load request, so that
	// we don't waste time during world initialization.
	// Currently, load can happen in one of the following places:
	// * LoadGameFromSlot - load is issued before requesting map loading, everything is handled via persistent state system
	// * OnPreLoadMap - catches any loads issued outside persistent state system
	// * UPersistentStateSubsystem::Initialize(), if we have a StartupSlot and a last saved world.
	// ActiveLoadRequest is created once loading begins and cleaned up after managers are initialized with world state
	if (ActiveLoadRequest.IsValid())
	{
		// wait for load task to complete. This is no-op if task has already been completed or loaded on GT
		UE::PersistentState::WaitForTask(ActiveLoadRequest->LoadTask);
		// it is ok to not have any world state e.g. the world was never saved to the current state slot
		if (ActiveLoadRequest->LoadedWorldState.IsValid())
		{
			check(ActiveLoadRequest->LoadedWorldState->Header.WorldName == World->GetName());
			// load requested state into state managers
			UE::PersistentState::LoadWorldState(GetManagerCollectionByType(EManagerStorageType::World), ActiveLoadRequest->LoadedWorldState);
		}
		// load game state
		if (ActiveLoadRequest->LoadedGameState.IsValid() && ActiveLoadRequest->TargetSlot != ActiveLoadRequest->CurrentSlot)
		{
			UE::PersistentState::LoadGameState(GetManagerCollectionByType(EManagerStorageType::Game), ActiveLoadRequest->LoadedGameState);
		}
		
		OnLoadStateFinished.Broadcast(ActiveLoadRequest->TargetSlot);
		// reset active request
		ActiveLoadRequest.Reset();
	}

	// route world initialized callback
	ForEachManager(EManagerStorageType::All, [](UPersistentStateManager* StateManager)
	{
		StateManager->NotifyWorldInitialized();
	});
}

bool UPersistentStateSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// only create subsystem if it is enabled in Project Settings or not explicitly disabled via CVar
	auto Settings = UPersistentStateSettings::Get();
	if (!Settings->bEnabled || Settings->StateStorageClass == nullptr || !UE::PersistentState::GPersistentState_Enabled)
	{
		return false;
	}

	if (Settings->CanCreateManagerState() == EManagerStorageType::None)
	{
		// all state (profile, game, world) is disabled
		return false;
	}
	
	TArray<UClass*> DerivedSubsystems;
	GetDerivedClasses(StaticClass(), DerivedSubsystems);

	// allow derived subsystem to override default implementation
	for (UClass* DerivedClass: DerivedSubsystems)
	{
		if (UGameInstanceSubsystem* Subsystem = DerivedClass->GetDefaultObject<UGameInstanceSubsystem>();
			Subsystem && Subsystem->ShouldCreateSubsystem(Outer))
		{
			return false;
		}
	}
	
	return true;
}

void UPersistentStateSubsystem::Deinitialize()
{
	ResetManagerState(EManagerStorageType::All);

	FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
	FWorldDelegates::OnPostWorldInitialization.RemoveAll(this);
	FWorldDelegates::OnWorldInitializedActors.RemoveAll(this);
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);
	FWorldDelegates::OnSeamlessTravelTransition.RemoveAll(this);
	
	StateStorage->Shutdown();
	StateStorage->MarkAsGarbage();
	StateStorage = nullptr;

	check(bInitialized);
	bInitialized = false;
	
	Super::Deinitialize();
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

void UPersistentStateSubsystem::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(StateStorage);
	
	ProcessSaveRequests();

	if (PendingLoadRequest.IsValid())
	{
		ActiveLoadRequest = MoveTemp(PendingLoadRequest);

		// always reset world state
		ResetManagerState(EManagerStorageType::World);
		if (ActiveSlot != ActiveLoadRequest->TargetSlot)
		{
			// reset game state if we're loading to a different state slot
			ResetManagerState(EManagerStorageType::Game);
		}
		
		ActiveSlot = ActiveLoadRequest->TargetSlot;
		// request world state via state storage interface
		OnLoadStateStarted.Broadcast(ActiveLoadRequest->TargetSlot);
		ActiveLoadRequest->LoadTask = StateStorage->LoadState(ActiveLoadRequest->TargetSlot, ActiveLoadRequest->MapName,
			FLoadCompletedDelegate::CreateUObject(this, &ThisClass::OnLoadStateCompleted, ActiveLoadRequest));

		// request open level
		UGameplayStatics::OpenLevel(this, ActiveLoadRequest->MapName, true, ActiveLoadRequest->TravelOptions);
	}

	UpdateStats();
}

void UPersistentStateSubsystem::ProcessSaveRequests()
{
	if (!SaveGameRequests.IsEmpty())
	{
		ForEachManager(EManagerStorageType::Game | EManagerStorageType::World, [](UPersistentStateManager* StateManager)
		{
			StateManager->SaveState();
		});

		const UWorld* World = GetWorld();
		check(World);

		FGameStateSharedRef	GameState = UE::PersistentState::CreateGameState(GetManagerCollectionByType(EManagerStorageType::Game));
		FWorldStateSharedRef WorldState = UE::PersistentState::CreateWorldState(World->GetName(), GetSourcePackageName(World), GetManagerCollectionByType(EManagerStorageType::World));
		
		// create a local copy of save game requests
		// any new requests are processed on the next update
		FPersistentStateSlotHandle LastActiveSlot = ActiveSlot;
		auto PendingRequests = MoveTemp(SaveGameRequests);
		for (TSharedPtr<FSaveGamePendingRequest> Request: PendingRequests)
		{
			// schedule save state requests
			const FPersistentStateSlotHandle TargetSlot = Request->TargetSlot;
			OnSaveStateStarted.Broadcast(TargetSlot);
	
			const FPersistentStateSlotHandle& SourceSlot = LastActiveSlot.IsValid() ? LastActiveSlot : TargetSlot;
			StateStorage->SaveState(GameState, WorldState, SourceSlot, TargetSlot, FSaveCompletedDelegate::CreateUObject(this, &ThisClass::OnSaveStateCompleted, TargetSlot));
		}
		
		ActiveSlot = PendingRequests.Last()->TargetSlot;
	}
}

void UPersistentStateSubsystem::UpdateStats() const
{
#if STATS
	if (!UE::PersistentState::GPersistentState_StatsEnabled)
	{
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	// reset NumObjects stat. It should be incremented by each manager separately
	SET_MEMORY_STAT(STAT_PersistentState_NumObjects, 0);
	ForEachManager(EManagerStorageType::All, [](UPersistentStateManager* StateManager)
	{
		StateManager->UpdateStats();
	});
	
	int32 WorldMemory{0}, GameMemory{0}, ProfileMemory{0};
	ForEachManager(EManagerStorageType::World, [&WorldMemory](UPersistentStateManager* StateManager)
	{
		WorldMemory += StateManager->GetAllocatedSize();
	});
	ForEachManager(EManagerStorageType::Game, [&GameMemory](UPersistentStateManager* StateManager)
	{
		GameMemory += StateManager->GetAllocatedSize();
	});
	ForEachManager(EManagerStorageType::Profile, [&ProfileMemory](UPersistentStateManager* StateManager)
	{
		ProfileMemory += StateManager->GetAllocatedSize();
	});

	SET_MEMORY_STAT(STAT_PersistentState_WorldStateMemory, WorldMemory);
	SET_MEMORY_STAT(STAT_PersistentState_GameStateMemory, GameMemory);
	SET_MEMORY_STAT(STAT_PersistentState_ProfileStateMemory, ProfileMemory);
	SET_MEMORY_STAT(STAT_PersistentState_StateStorageMemory, StateStorage->GetAllocatedSize());
#endif
}

TStatId UPersistentStateSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPersistentStateSubsystem, STATGROUP_Tickables);
}

void UPersistentStateSubsystem::CreateManagerState(EManagerStorageType TypeFilter)
{
	check(CanCreateManagerState(TypeFilter) && !HasManagerState(TypeFilter));
	for (auto& [ManagerType, ManagerClasses]: ManagerTypeMap)
	{
		if (!!(TypeFilter & ManagerType))
		{
			check(!ManagerMap.Contains(ManagerType));

			auto& StateManagers = ManagerMap.Add(ManagerType);
			StateManagers.Reserve(ManagerClasses.Num());
			
			for (UClass* ManagerClass: ManagerClasses)
			{
				if (ManagerClass->GetDefaultObject<UPersistentStateManager>()->ShouldCreateManager(*this))
				{
					UPersistentStateManager* StateManager = NewObject<UPersistentStateManager>(this, ManagerClass);
					StateManagers.Add(StateManager);
				}
			}

			for (UPersistentStateManager* StateManager: StateManagers)
			{
				StateManager->Init(*this);
			}
		}
	}

	ManagerState |= TypeFilter;
}

void UPersistentStateSubsystem::ResetManagerState(EManagerStorageType TypeFilter)
{
	for (auto It = ManagerMap.CreateIterator(); It; ++It)
	{
		auto& [ManagerType, Managers] = *It;
		if (!!(TypeFilter & ManagerType) && HasManagerState(ManagerType))
		{
			for (UPersistentStateManager* StateManager: Managers)
			{
				StateManager->Cleanup(*this);
			}

			It.RemoveCurrent();
		}
	}
	
	ManagerState &= ~TypeFilter;
}

void UPersistentStateSubsystem::ForEachManager(EManagerStorageType TypeFilter, TFunctionRef<void(UPersistentStateManager*)> Callback) const
{
	for (auto& [ManagerType, Managers]: ManagerMap)
	{
		if (!!(TypeFilter & ManagerType))
		{
			for (UPersistentStateManager* StateManager: Managers)
			{
				check(StateManager);
				Callback(StateManager);
			}
		}
	}
}

bool UPersistentStateSubsystem::ForEachManagerWithBreak(EManagerStorageType TypeFilter, TFunctionRef<bool(UPersistentStateManager*)> Callback) const
{
	for (auto& [ManagerType, Managers]: ManagerMap)
	{
		if (!!(TypeFilter & ManagerType))
		{
			for (UPersistentStateManager* StateManager: Managers)
			{
				check(StateManager);
				if (Callback(StateManager))
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool UPersistentStateSubsystem::HasManagerState(EManagerStorageType TypeFilter) const
{
	return !!(ManagerState & TypeFilter);
}

bool UPersistentStateSubsystem::CanCreateManagerState(EManagerStorageType ManagerType) const
{
	return !!(CachedCanCreateManagerState & ManagerType);
}

TConstArrayView<UPersistentStateManager*> UPersistentStateSubsystem::GetManagerCollectionByType(EManagerStorageType ManagerType) const
{
	if (auto* CollectionPtr = ManagerMap.Find(ManagerType))
	{
		auto& Collection = *CollectionPtr;
		return ObjectPtrDecay(Collection);
	}
	
	return {};
}


UPersistentStateManager* UPersistentStateSubsystem::GetStateManager(TSubclassOf<UPersistentStateManager> ManagerClass) const
{
	if (ManagerClass == nullptr)
	{
		return nullptr;
	}
	
	UPersistentStateManager* OutManager = nullptr;
	ForEachManagerWithBreak(ManagerClass->GetDefaultObject<UPersistentStateManager>()->GetManagerType(), [&OutManager, ManagerClass](UPersistentStateManager* StateManager)
	{
		if (StateManager->GetClass() == ManagerClass)
		{
			OutManager = StateManager;
			return true;
		}

		return false;
	});

	return OutManager;
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
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(StateStorage);
	
	if (!bInitialized)
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Failed save game request - subsystem is not initialized yet."), *FString(__FUNCTION__));
		return false;
	}

	if (ActiveLoadRequest.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: Failed save game request - map transition is already active."), *FString(__FUNCTION__));
		return false;
	}

	if (!HasManagerState(EManagerStorageType::Game | EManagerStorageType::World))
	{
		// world and game state managers are explicitly disabled
		UE_LOG(LogPersistentState, Verbose, TEXT("%s: No state to save."), *FString(__FUNCTION__));
		return false;
	}
	
	if (!TargetSlot.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: invalid target slot"), *FString(__FUNCTION__));
		return false;
	}
	
	if (!StateStorage->CanSaveToStateSlot(TargetSlot))
	{
		// can't save to the slot
		return false;
	}

	for (TSharedPtr<FSaveGamePendingRequest> Request: SaveGameRequests)
	{
		if (TargetSlot == Request->TargetSlot)
		{
			// save to a target slot already requested
			return true;
		}
	}

	SaveGameRequests.Add(MakeShared<FSaveGamePendingRequest>(TargetSlot));
	return true;
}

bool UPersistentStateSubsystem::LoadGameFromSlot(const FPersistentStateSlotHandle& TargetSlot, FString TravelOptions)
{
	check(StateStorage);
	return LoadGameWorldFromSlot(TargetSlot, {}, TravelOptions);
}

bool UPersistentStateSubsystem::LoadGameWorldFromSlot(const FPersistentStateSlotHandle& TargetSlot, TSoftObjectPtr<UWorld> World, FString TravelOptions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	check(StateStorage);

	if (ActiveLoadRequest.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: trying to issue load request during map transition."), *FString(__FUNCTION__));
		return false;
	}
	
	if (!TargetSlot.IsValid())
	{
		UE_LOG(LogPersistentState, Error, TEXT("%s: invalid target slot"), *FString(__FUNCTION__));
		return false;
	}

	if (!StateStorage->CanLoadFromStateSlot(TargetSlot))
	{
		return false;
	}

	FName WorldToLoad{World.GetAssetName()};
	if (WorldToLoad == NAME_None)
	{
		WorldToLoad = StateStorage->GetWorldFromStateSlot(TargetSlot);
		if (WorldToLoad == NAME_None)
		{
			UE_LOG(LogPersistentState, Error, TEXT("%s: can't find last saved world from slot %s"), *FString(__FUNCTION__), *TargetSlot.ToString());
			return false;
		}
	}

	if (PendingLoadRequest.IsValid())
	{
		ensureAlwaysMsgf(PendingLoadRequest->TargetSlot == TargetSlot, TEXT("%s: multiple LoadGameFromSlot attempts in a single frame with a different target slot: %s, %s"),
			*FString(__FUNCTION__), *TargetSlot.ToString(),*PendingLoadRequest->TargetSlot.ToString());
		return true;
	}
	
	check(!PendingLoadRequest.IsValid() && TargetSlot.IsValid());
	constexpr bool bCreatedByUser = true;
	PendingLoadRequest = MakeShared<FLoadGamePendingRequest>(ActiveSlot, TargetSlot, WorldToLoad, bCreatedByUser);
	PendingLoadRequest->TravelOptions = TravelOptions;

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

FPersistentStateSlotDesc UPersistentStateSubsystem::GetSaveGameSlot(const FPersistentStateSlotHandle& Slot) const
{
	check(StateStorage);
	return StateStorage->GetStateSlotDesc(Slot);
}

void UPersistentStateSubsystem::UpdateSaveGameSlots()
{
	check(StateStorage);
	StateStorage->UpdateAvailableStateSlots();
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
	ForEachManager(EManagerStorageType::All, [&Object](UPersistentStateManager* StateManager)
	{
		StateManager->NotifyObjectInitialized(Object);
	});
}

FString UPersistentStateSubsystem::GetSourcePackageName(const UWorld* InWorld) const
{
	if (const FName* PackageName = WorldPackageMap.Find(InWorld))
	{
		return PackageName->ToString();
	}

	return {};
}

void UPersistentStateSubsystem::CacheSourcePackageName(const UWorld* InWorld)
{
	if (WorldPackageMap.Contains(InWorld))
	{
		return;
	}

	const FName WorldName = InWorld->GetFName();
	// Look up in the AssetRegistry
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	check(FPackageName::IsShortPackageName(WorldName));
	
	const FName SourcePackageName = AssetRegistry.GetFirstPackageByName(WorldName.ToString());
#if WITH_EDITOR_COMPATIBILITY
	if (SourcePackageName.IsNone())
	{
		// world is created on the fly - use world name as a package name
		WorldPackageMap.Add(InWorld, WorldName);
	}
	else
	{
		WorldPackageMap.Add(InWorld, SourcePackageName);
	}
#else
	check(!SourcePackageName.IsNone());
	WorldPackageMap.Add(InWorld, SourcePackageName);
#endif
}

void UPersistentStateSubsystem::OnWorldInitActors(const FActorsInitializedParams& Params)
{
	if (Params.World == nullptr || Params.World != GetOuterUGameInstance()->GetWorld())
	{
		return;
	}
	
	// route actors initialized callback
	ForEachManager(EManagerStorageType::All, [](UPersistentStateManager* StateManager)
	{
		StateManager->NotifyActorsInitialized();
	});
}

void UPersistentStateSubsystem::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	ON_SCOPE_EXIT
	{
		WorldPackageMap.Remove(World);
	};
	
	if (World && World == GetOuterUGameInstance()->GetWorld())
	{
		// route world cleanup callback
		ForEachManager(EManagerStorageType::All, [](UPersistentStateManager* StateManager)
		{
			StateManager->NotifyWorldCleanup();
		});

		// automatic load request may have been created in OnPreLoadMap
		if (ActiveLoadRequest.IsValid() && ActiveLoadRequest->CreatedByUser())
		{
			// if it is user-created load request, we should not save any state
			return;
		}
		
		// if world is being cleaned up and there's still a world or game state,
		// it is probably caused by OpenLevel request outside of persistent state system
		// expected behavior would be to save current state before transitioning to a new map
		// @todo: for LoadMap scenario, maybe use PreLevelRemovedFromWorld delegate instead of OnWorldCleanup, as EndPlay for actors has already been called
		// @todo: investigate all cases when OnWorldCleanup is called
		if (ActiveSlot.IsValid() && HasManagerState(EManagerStorageType::World | EManagerStorageType::Game))
		{
			SaveGameRequests.Add(MakeShared<FSaveGamePendingRequest>(ActiveSlot));
			ProcessSaveRequests();
		}

		// reset only world state
		// game state is being explicitly reset if we're loading into a different state slot
		ResetManagerState(EManagerStorageType::World);
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
	ResetManagerState(EManagerStorageType::All);
}
#endif

void UPersistentStateSubsystem::OnSaveStateCompleted(FPersistentStateSlotHandle TargetSlot)
{
	OnSaveStateFinished.Broadcast(TargetSlot);
}

void UPersistentStateSubsystem::OnLoadStateCompleted(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, TSharedPtr<FLoadGamePendingRequest> LoadRequest)
{
	check(LoadRequest.IsValid());
	// cache game and world state and wait until load map has completed
	LoadRequest->LoadedGameState = GameState;
	LoadRequest->LoadedWorldState = WorldState;
}
