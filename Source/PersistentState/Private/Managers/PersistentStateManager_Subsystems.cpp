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
	ManagerType = EPersistentStateManagerType::World;
}

void UPersistentStateManager_Subsystems::Init(UPersistentStateSubsystem& InSubsystem)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*FString::Printf(TEXT("%s:Init"), *GetClass()->GetName()), PersistentStateChannel);
	Super::Init(InSubsystem);

	UWorld* World = InSubsystem.GetWorld();
	check(World->bIsWorldInitialized && !World->bActorsInitialized);

	// map and initialize current world subsystems to existing state
	for (USubsystem* Subsystem: GetSubsystems(InSubsystem))
	{
		if (Subsystem && Subsystem->Implements<UPersistentStateObject>())
		{
			// create IDs for world subsystem
			FPersistentStateObjectId Handle = FPersistentStateObjectId::CreateStaticObjectId(Subsystem);
			check(Handle.IsValid());

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


void UPersistentStateManager_Subsystems::SaveState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_WorldSubsystems_SaveGameState, PersistentStateChannel);
	Super::SaveState();
	
	for (FSubsystemPersistentState& State: Subsystems)
	{
		State.Save();
	}
}


