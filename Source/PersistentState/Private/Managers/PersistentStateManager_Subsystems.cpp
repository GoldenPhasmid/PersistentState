#include "Managers/PersistentStateManager_Subsystems.h"

#include "PersistentStateModule.h"
#include "PersistentStateInterface.h"
#include "PersistentStateStatics.h"
#include "PersistentStateSubsystem.h"

FSubsystemPersistentState::FSubsystemPersistentState(const USubsystem* Subsystem)
	: Handle(FPersistentStateObjectId::CreateStaticObjectId(Subsystem))
{
	check(Subsystem && Subsystem->Implements<UPersistentStateObject>());
}

void FSubsystemPersistentState::Load()
{
	if (bStateSaved == false)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FSubsystemPersistentState_Load, PersistentStateChannel);
	
	USubsystem* Subsystem = Handle.ResolveObject<USubsystem>();
	check(Subsystem);

	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Subsystem);
	State->PreLoadState();

	// @todo: track and pre-load hard references
	UE::PersistentState::LoadObjectSaveGameProperties(*Subsystem, SaveGameBunch);
	if (InstanceState.IsValid())
	{
		State->LoadCustomObjectState(InstanceState);	
	}

	State->PostLoadState();
}

void FSubsystemPersistentState::Save()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FSubsystemPersistentState_Save, PersistentStateChannel);

	USubsystem* Subsystem = Handle.ResolveObject<USubsystem>();
	check(Subsystem);

	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Subsystem);

	// PersistentState object can't transition from Saveable to not Saveable
	ensureAlwaysMsgf(static_cast<int32>(State->ShouldSaveState()) >= static_cast<int32>(bStateSaved), TEXT("%s: component %s transitioned from Saveable to NotSaveable."),
		*FString(__FUNCTION__), *GetNameSafe(Subsystem));
	bStateSaved = bStateSaved || State->ShouldSaveState();
	if (bStateSaved == false)
	{
		return;
	}

	State->PreSaveState();

	// @todo: track and save hard references
	UE::PersistentState::SaveObjectSaveGameProperties(*Subsystem, SaveGameBunch);
	InstanceState = State->SaveCustomObjectState();	

	State->PostSaveState();
}

UPersistentStateManager_Subsystems::UPersistentStateManager_Subsystems()
{
	ManagerType = EManagerStorageType::World;
}

void UPersistentStateManager_Subsystems::Init(UPersistentStateSubsystem& InSubsystem)
{

}


void UPersistentStateManager_Subsystems::SaveState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_WorldSubsystems_SaveGameState, PersistentStateChannel);
	Super::SaveState();
	
	for (FSubsystemPersistentState& State: SubsystemState)
	{
		State.Save();
	}
}

void UPersistentStateManager_Subsystems::LoadSubsystems(TConstArrayView<USubsystem*> Subsystems)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*FString::Printf(TEXT("%s:Init"), *GetClass()->GetName()), PersistentStateChannel);
	
	// map and initialize subsystems to existing state
	for (USubsystem* Subsystem: Subsystems)
	{
		if (Subsystem && Subsystem->Implements<UPersistentStateObject>())
		{
			// create IDs for world subsystem
			FPersistentStateObjectId Handle = FPersistentStateObjectId::CreateStaticObjectId(Subsystem);
			checkf(Handle.IsValid(), TEXT("Subsystem handle is required to be Static. Implement IPersistentStateObject and give subsystem's outer a stable name."));

			if (FSubsystemPersistentState* State = SubsystemState.FindByKey(Handle))
			{
				State->Load();
			}
			else
			{
				SubsystemState.Add(FSubsystemPersistentState{Handle});
			}
		}
	}

	// remove outdated subsystems
	for (auto It = SubsystemState.CreateIterator(); It; ++It)
	{
		USubsystem* Subsystem = It->Handle.ResolveObject<USubsystem>();
		if (Subsystem == nullptr)
		{
			// removing a full subsystem is never a good idea
			UE_LOG(LogPersistentState, Error, TEXT("%s: Failed to find world subsystem %s"), *FString(__FUNCTION__),  *It->Handle.GetObjectName());
			It.RemoveCurrentSwap();
		}
	}
}

UPersistentStateManager_WorldSubsystems::UPersistentStateManager_WorldSubsystems()
{
	ManagerType = EManagerStorageType::World;
}

void UPersistentStateManager_WorldSubsystems::Init(UPersistentStateSubsystem& InSubsystem)
{
	Super::Init(InSubsystem);

	UWorld* World = InSubsystem.GetWorld();
	check(World->bIsWorldInitialized && !World->bActorsInitialized);
	
	const TArray<USubsystem*>& Subsystems = static_cast<TArray<USubsystem*>>(World->GetSubsystemArray<UWorldSubsystem>());
	LoadSubsystems(Subsystems);
}

UPersistentStateManager_GameInstanceSubsystems::UPersistentStateManager_GameInstanceSubsystems()
{
	ManagerType = EManagerStorageType::Game;
}

void UPersistentStateManager_GameInstanceSubsystems::Init(UPersistentStateSubsystem& InSubsystem)
{
	Super::Init(InSubsystem);

	UGameInstance* GameInstance = InSubsystem.GetGameInstance();
	check(GameInstance);
	
	const TArray<USubsystem*>& Subsystems = static_cast<TArray<USubsystem*>>(GameInstance->GetSubsystemArray<UGameInstanceSubsystem>());
	LoadSubsystems(Subsystems);
}

UPersistentStateManager_PlayerSubsystems::UPersistentStateManager_PlayerSubsystems()
{
	ManagerType = EManagerStorageType::Persistent;
}

void UPersistentStateManager_PlayerSubsystems::Init(UPersistentStateSubsystem& InSubsystem)
{
	Super::Init(InSubsystem);

	UGameInstance* GameInstance = InSubsystem.GetGameInstance();
	check(GameInstance);

	
	if (GameInstance->GetNumLocalPlayers() > 0)
	{
		ULocalPlayer* LocalPlayer = InSubsystem.GetGameInstance()->GetFirstGamePlayer();
		check(LocalPlayer);

		// @todo: track subsystems for each local player
		LoadPrimaryPlayer(LocalPlayer);
	}
	else
	{
		GameInstance->OnLocalPlayerAddedEvent.AddUObject(this, &ThisClass::HandleLocalPlayerAdded);
	}
}

void UPersistentStateManager_PlayerSubsystems::HandleLocalPlayerAdded(ULocalPlayer* LocalPlayer)
{
	UPersistentStateSubsystem* Subsystem = GetStateSubsystem();
	check(Subsystem);
	
	UGameInstance* GameInstance = Subsystem->GetGameInstance();
	check(GameInstance);

	GameInstance->OnLocalPlayerAddedEvent.RemoveAll(this);
	// @todo: track subsystems for each local player
	LoadPrimaryPlayer(LocalPlayer);
}

void UPersistentStateManager_PlayerSubsystems::LoadPrimaryPlayer(ULocalPlayer* LocalPlayer)
{
	const TArray<USubsystem*>& Subsystems = static_cast<TArray<USubsystem*>>(LocalPlayer->GetSubsystemArray<ULocalPlayerSubsystem>());
	LoadSubsystems(Subsystems);
}


