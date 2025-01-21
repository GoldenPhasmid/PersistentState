#include "Managers/PersistentStateManager_Subsystems.h"

#include "PersistentStateModule.h"
#include "PersistentStateInterface.h"
#include "PersistentStateStatics.h"
#include "PersistentStateSubsystem.h"

DECLARE_DWORD_COUNTER_STAT(TEXT("Tracked Subsystems"),	STAT_PersistentState_NumSubsystems,	STATGROUP_PersistentState);

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
	UE::PersistentState::LoadObject(*Subsystem, SaveGameBunch);
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
	ensureAlwaysMsgf(static_cast<int32>(State->ShouldSaveState()) >= static_cast<int32>(bStateSaved), TEXT("%s: subsystem %s transitioned from Saveable to NotSaveable."),
		*FString(__FUNCTION__), *GetNameSafe(Subsystem));
	bStateSaved = bStateSaved || State->ShouldSaveState();
	if (bStateSaved == false)
	{
		return;
	}

	State->PreSaveState();

	// @todo: track and save hard references
	UE::PersistentState::SaveObject(*Subsystem, SaveGameBunch);
	InstanceState = State->SaveCustomObjectState();	

	State->PostSaveState();
}

UPersistentStateManager_Subsystems::UPersistentStateManager_Subsystems()
{
	ManagerType = EManagerStorageType::World;
}


void UPersistentStateManager_Subsystems::SaveState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_WorldSubsystems_SaveGameState, PersistentStateChannel);
	Super::SaveState();
	
	for (FSubsystemPersistentState& State: Subsystems)
	{
		State.Save();
	}
}

void UPersistentStateManager_Subsystems::UpdateStats() const
{
#if STATS
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	SET_DWORD_STAT(STAT_PersistentState_NumSubsystems, Subsystems.Num());
	INC_DWORD_STAT_BY(STAT_PersistentState_NumObjects, Subsystems.Num());
#endif
}

uint32 UPersistentStateManager_Subsystems::GetAllocatedSize() const
{
	uint32 TotalMemory = Super::GetAllocatedSize();
	TotalMemory += Subsystems.GetAllocatedSize();

	for (const FSubsystemPersistentState& State: Subsystems)
	{
		TotalMemory += State.GetAllocatedSize();
	}
	
	return TotalMemory;
}

void UPersistentStateManager_Subsystems::LoadGameState(TConstArrayView<USubsystem*> SubsystemArray)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*FString::Printf(TEXT("%s:Init"), *GetClass()->GetName()), PersistentStateChannel);
	
	// map and initialize subsystems to existing state
	for (USubsystem* Subsystem: SubsystemArray)
	{
		if (Subsystem && Subsystem->Implements<UPersistentStateObject>())
		{
			// create IDs for world subsystem
			FPersistentStateObjectId Handle = FPersistentStateObjectId::CreateStaticObjectId(Subsystem);
			checkf(Handle.IsValid(), TEXT("Subsystem handle is required to be Static. Implement IPersistentStateObject and give subsystem's outer a stable name."));

			if (FSubsystemPersistentState* State = Subsystems.FindByKey(Handle))
			{
				State->Load();
			}
			else
			{
				Subsystems.Add(FSubsystemPersistentState{Handle});
			}
		}
	}

	// remove outdated subsystems
	for (auto It = Subsystems.CreateIterator(); It; ++It)
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

void UPersistentStateManager_WorldSubsystems::NotifyActorsInitialized()
{
	Super::NotifyActorsInitialized();
	
	const TArray<USubsystem*>& SubsystemArray = static_cast<TArray<USubsystem*>>(GetWorld()->GetSubsystemArray<UWorldSubsystem>());
	LoadGameState(SubsystemArray);
}

UPersistentStateManager_GameInstanceSubsystems::UPersistentStateManager_GameInstanceSubsystems()
{
	ManagerType = EManagerStorageType::Game;
}

void UPersistentStateManager_GameInstanceSubsystems::NotifyActorsInitialized()
{
	Super::NotifyActorsInitialized();

	const TArray<USubsystem*>& SubsystemArray = static_cast<TArray<USubsystem*>>(GetGameInstance()->GetSubsystemArray<UGameInstanceSubsystem>());
	LoadGameState(SubsystemArray);
}

UPersistentStateManager_PlayerSubsystems::UPersistentStateManager_PlayerSubsystems()
{
	ManagerType = EManagerStorageType::Profile;
}

void UPersistentStateManager_PlayerSubsystems::NotifyActorsInitialized()
{
	Super::NotifyActorsInitialized();

	UGameInstance* GameInstance = GetGameInstance();
	check(GameInstance);
	
	if (GameInstance->GetNumLocalPlayers() > 0)
	{
		ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer();
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
	const TArray<USubsystem*>& SubsystemArray = static_cast<TArray<USubsystem*>>(LocalPlayer->GetSubsystemArray<ULocalPlayerSubsystem>());
	LoadGameState(SubsystemArray);
}


