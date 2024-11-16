#include "Managers/WorldPersistentStateManager_WorldSubsystems.h"

#include "PersistentStateDefines.h"
#include "PersistentStateInterface.h"
#include "PersistentStateStatics.h"

FWorldSubsystemPersistentState::FWorldSubsystemPersistentState(const UWorldSubsystem* Subsystem)
	: Handle(FPersistentStateObjectId::CreateStaticObjectId(Subsystem))
{
	check(Subsystem->Implements<UPersistentStateObject>());
}

void FWorldSubsystemPersistentState::Load()
{
	if (bStateSaved == false)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FWorldSubsystemPersistentState_Load, PersistentStateChannel);
	
	UWorldSubsystem* Subsystem = Handle.ResolveObject<UWorldSubsystem>();
	check(Subsystem);

	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Subsystem);
	State->PreLoadState();

	UE::PersistentState::LoadObjectSaveGameProperties(*Subsystem, SaveGameBunch);
	if (InstanceState.IsValid())
	{
		State->LoadCustomObjectState(InstanceState);	
	}

	State->PostLoadState();
}

void FWorldSubsystemPersistentState::Save()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FWorldSubsystemPersistentState_Save, PersistentStateChannel);

	UWorldSubsystem* Subsystem = Handle.ResolveObject<UWorldSubsystem>();
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

	UE::PersistentState::SaveObjectSaveGameProperties(*Subsystem, SaveGameBunch);
	InstanceState = State->SaveCustomObjectState();	

	State->PostSaveState();
}

void UWorldPersistentStateManager_WorldSubsystems::Init(UWorld* World)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_WorldSubsystems_Init, PersistentStateChannel);
	Super::Init(World);
	check(World->bIsWorldInitialized && !World->bActorsInitialized);

	// map and initialize current world subsystems to existing state
	for (UWorldSubsystem* Subsystem: CurrentWorld->GetSubsystemArray<UWorldSubsystem>())
	{
		if (Subsystem && Subsystem->Implements<UPersistentStateObject>())
		{
			// create IDs for world subsystem
			FPersistentStateObjectId Handle = FPersistentStateObjectId::CreateStaticObjectId(Subsystem);
			check(Handle.IsValid());

			if (FWorldSubsystemPersistentState* State = Subsystems.FindByKey(Handle))
			{
				State->Load();
			}
			else
			{
				Subsystems.Add(FWorldSubsystemPersistentState{Handle});
			}
		}
	}

	// remove outdated subsystems
	for (auto It = Subsystems.CreateIterator(); It; ++It)
	{
		UWorldSubsystem* Subsystem = It->Handle.ResolveObject<UWorldSubsystem>();
		if (Subsystem == nullptr)
		{
			// removing a full subsystem is never a good idea
			UE_LOG(LogPersistentState, Error, TEXT("%s: Failed to find world subsystem %s"), *FString(__FUNCTION__),  *It->Handle.GetObjectName());
			It.RemoveCurrentSwap();
		}
	}
}

void UWorldPersistentStateManager_WorldSubsystems::SaveGameState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_WorldSubsystems_SaveGameState, PersistentStateChannel);
	Super::SaveGameState();
	
	for (auto& State: Subsystems)
	{
		State.Save();
	}
}


void UWorldPersistentStateManager_WorldSubsystems::Cleanup(UWorld* World)
{
	Super::Cleanup(World);
	// do nothing
}
