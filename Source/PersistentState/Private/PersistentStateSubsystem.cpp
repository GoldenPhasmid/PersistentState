#include "PersistentStateSubsystem.h"

#include "PersistentStateCVars.h"
#include "PersistentStateInterface.h"
#include "PersistentStateModule.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"
#include "PersistentStateStorage.h"
#include "Kismet/GameplayStatics.h"
#include "Managers/PersistentStateManager.h"

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

	StateStorage = NewObject<UPersistentStateStorage>(this, UPersistentStateSettings::Get()->StateStorageClass);
	StateStorage->Init();

	if (FName StartupSlot = UPersistentStateSettings::Get()->StartupSlotName; StartupSlot != NAME_None)
	{
		ActiveSlot = StateStorage->GetStateSlotByName(StartupSlot);
	}

	check(!ActiveLoadRequest.IsValid() && !PendingLoadRequest.IsValid());
	if (ActiveSlot.IsValid())
	{
		// start loading world state, if active slot is set and last saved world is currently being loaded
		if (FName LastWorld = StateStorage->GetWorldFromStateSlot(ActiveSlot); LastWorld == GetWorld()->GetFName())
		{
			CreateActiveLoadRequest(LastWorld);
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

void UPersistentStateSubsystem::CreateActiveLoadRequest(FName MapName)
{
	check(bInitialized && !ActiveLoadRequest.IsValid());
	ActiveLoadRequest = MakeShared<FLoadGamePendingRequest>(ActiveSlot, FName{MapName});
	OnLoadStateStarted.Broadcast(ActiveLoadRequest->TargetSlot);
	// request world state via state storage interface
	ActiveLoadRequest->LoadTask = StateStorage->LoadWorldState(ActiveLoadRequest->TargetSlot, ActiveLoadRequest->MapName,
		FLoadCompletedDelegate::CreateUObject(this, &ThisClass::OnLoadStateCompleted, ActiveLoadRequest));
}

void UPersistentStateSubsystem::OnPreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
	if (WorldContext.OwningGameInstance != GetGameInstance())
	{
		return;
	}
	
	const FName WorldName = FPackageName::GetShortFName(MapName);
	ensureAlwaysMsgf(!ActiveLoadRequest.IsValid() || ActiveLoadRequest->MapName == WorldName, TEXT("Unexpected PreLoadMap callback."));
	
	// pre-load world state for map that initialized loading
	// if load request is already active, load map request probably instigated by LoadGameFromSlot
	if (!ActiveLoadRequest.IsValid())
	{
		CreateActiveLoadRequest(WorldName);
	}
}

void UPersistentStateSubsystem::OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS)
{
	if (World == nullptr || World != GetOuterUGameInstance()->GetWorld())
	{
		return;
	}
	
	AWorldSettings* WorldSettings = World->GetWorldSettings();
	check(WorldSettings);
	check(!ManagerMap.Contains(EManagerStorageType::World));
		
	if (!IPersistentStateWorldSettings::ShouldStoreWorldState(*WorldSettings))
	{
		return;
	}

	// create and initialize world managers
	if (const TArray<UClass*>* ManagerTypes = ManagerTypeMap.Find(EManagerStorageType::World))
	{
		auto& Collection = ManagerMap.Add(EManagerStorageType::World);
				
		for (UClass* ManagerType: *ManagerTypes)
		{
			if (ManagerType->GetDefaultObject<UPersistentStateManager>()->ShouldCreateManager(*this))
			{
				UPersistentStateManager* StateManager = NewObject<UPersistentStateManager>(this, ManagerType);
				Collection.Add(StateManager);
			}
		}

		bHasWorldManagerState = true;
		for (UPersistentStateManager* StateManager: Collection)
		{
			StateManager->Init(*this);
		}
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
			OnLoadStateFinished.Broadcast(ActiveLoadRequest->TargetSlot);
		}

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
	ResetWorldState();

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
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateSubsystem_Tick, PersistentStateChannel);
	check(StateStorage);
	
	ProcessSaveRequests();

	if (PendingLoadRequest.IsValid())
	{
		ActiveLoadRequest = MoveTemp(PendingLoadRequest);
		ActiveSlot = ActiveLoadRequest->TargetSlot;

		ResetWorldState();
		// request world state via state storage interface
		OnLoadStateStarted.Broadcast(ActiveLoadRequest->TargetSlot);
		ActiveLoadRequest->LoadTask = StateStorage->LoadWorldState(ActiveLoadRequest->TargetSlot, ActiveLoadRequest->MapName,
			FLoadCompletedDelegate::CreateUObject(this, &ThisClass::OnLoadStateCompleted, ActiveLoadRequest));

		// request open level
		UGameplayStatics::OpenLevel(this, ActiveLoadRequest->MapName, true, ActiveLoadRequest->TravelOptions);
	}
}

void UPersistentStateSubsystem::ProcessSaveRequests()
{
	if (!SaveGameRequests.IsEmpty())
	{
		ForEachManager(EManagerStorageType::World, [](UPersistentStateManager* StateManager)
		{
			StateManager->SaveState();
		});

		UWorld* World = GetWorld();
		check(World);

		const FName WorldName = World->GetFName();
		const FName WorldPackageName = World->GetPackage()->GetFName();
		FWorldStateSharedRef WorldState = UE::PersistentState::CreateWorldState(WorldName, WorldPackageName, GetManagerCollectionByType(EManagerStorageType::World));
		check(WorldState.IsValid());

		// create a local copy of save game requests
		// any new requests are processed on the next update
		auto LocalSaveGameRequests = MoveTemp(SaveGameRequests);
		FPersistentStateSlotHandle LastActiveSlot = ActiveSlot;
		for (TSharedPtr<FSaveGamePendingRequest> Request: LocalSaveGameRequests)
		{
			// schedule save state requests
			const FPersistentStateSlotHandle TargetSlot = Request->TargetSlot;
			OnSaveStateStarted.Broadcast(TargetSlot);
	
			const FPersistentStateSlotHandle& SourceSlot = LastActiveSlot.IsValid() ? LastActiveSlot : TargetSlot;
			StateStorage->SaveWorldState(WorldState, SourceSlot, TargetSlot, FSaveCompletedDelegate::CreateUObject(this, &ThisClass::OnSaveStateCompleted, TargetSlot));
		}
		
		ActiveSlot = LocalSaveGameRequests.Last()->TargetSlot;
	}
}

TStatId UPersistentStateSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPersistentStateSubsystem, STATGROUP_Tickables);
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
	check(StateStorage);
	
	if (!bInitialized || !bHasWorldManagerState || ActiveLoadRequest.IsValid())
	{
		// don't have any world state, transitioning to a new map or not yet initialized
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
	PendingLoadRequest = MakeShared<FLoadGamePendingRequest>(TargetSlot, WorldToLoad);
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
	if (World && World == GetOuterUGameInstance()->GetWorld())
	{
		if (ActiveSlot.IsValid() && bHasWorldManagerState)
		{
			SaveGameRequests.Add(MakeShared<FSaveGamePendingRequest>(ActiveSlot));
			ProcessSaveRequests();
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
	ForEachManager(EManagerStorageType::World, [this](UPersistentStateManager* StateManager)
	{
		StateManager->Cleanup(*this);
	});
	ManagerMap.Remove(EManagerStorageType::World);

	bHasWorldManagerState = false;
}

void UPersistentStateSubsystem::OnSaveStateCompleted(FPersistentStateSlotHandle TargetSlot)
{
	OnSaveStateFinished.Broadcast(TargetSlot);
}

void UPersistentStateSubsystem::OnLoadStateCompleted(FWorldStateSharedRef WorldState, TSharedPtr<FLoadGamePendingRequest> LoadRequest)
{
	check(LoadRequest.IsValid());
	// cache world state and wait until load map has completed
	LoadRequest->LoadedWorldState = WorldState;
}
