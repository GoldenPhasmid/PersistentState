#include "Managers/PersistentStateManager_DataLayers.h"

#include "PersistentStateModule.h"
#include "PersistentStateSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

FDataLayerPersistentState::FDataLayerPersistentState(const FPersistentStateObjectId& InHandle)
	: Handle(InHandle)
{

}

void FDataLayerPersistentState::Load(UDataLayerManager* DataLayerManager)
{
	if (bStateSaved == false)
	{
		return;
	}

	UDataLayerInstance* DataLayerInstance = Handle.ResolveObject<UDataLayerInstance>();
	check(DataLayerInstance);
	
	InitialState = DataLayerInstance->GetInitialRuntimeState();
	CurrentState = DataLayerManager->GetDataLayerInstanceRuntimeState(DataLayerInstance);
}

void FDataLayerPersistentState::Save(UDataLayerManager* DataLayerManager)
{
	bStateSaved = true;

	UDataLayerInstance* DataLayerInstance = Handle.ResolveObject<UDataLayerInstance>();
	check(DataLayerInstance);

	DataLayerManager->SetDataLayerRuntimeState(DataLayerInstance->GetAsset(), CurrentState);
}

UPersistentStateManager_DataLayers::UPersistentStateManager_DataLayers()
{
	ManagerType = EManagerStorageType::World;
}

bool UPersistentStateManager_DataLayers::ShouldCreateManager(UPersistentStateSubsystem& Subsystem) const
{
	if (!Super::ShouldCreateManager(Subsystem))
	{
		return false;
	}

	if (UWorld* World = Subsystem.GetWorld())
	{
		return World->IsPartitionedWorld();
	}

	return false;
}

void UPersistentStateManager_DataLayers::Init(UPersistentStateSubsystem& Subsystem)
{
	Super::Init(Subsystem);

	UWorld* World = Subsystem.GetWorld();
	check(World->bIsWorldInitialized && !World->bActorsInitialized);
	check(World->GetWorldPartition() && !World->GetWorldPartition()->IsInitialized());

	InitializedActorsHandle = World->OnActorsInitialized.AddUObject(this, &ThisClass::LoadGameState);
}

void UPersistentStateManager_DataLayers::LoadGameState(const FActorsInitializedParams& Params)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_DataLayers_LoadGameState, PersistentStateChannel);

	UWorld* CurrentWorld = Params.World;
	CurrentWorld->OnActorsInitialized.Remove(InitializedActorsHandle);
	
	// @todo: for each loaded static/dynamic level instance, track data layer state as well
	UDataLayerManager* Manager = CurrentWorld->GetDataLayerManager();

	// map data layer instances to existing state or create new state
	for (UDataLayerInstance* Instance: Manager->GetDataLayerInstances())
	{
		FPersistentStateObjectId Handle = FPersistentStateObjectId::CreateStaticObjectId(Instance);
		check(Handle.IsValid());

		if (FDataLayerPersistentState* State = DataLayers.FindByKey(Handle))
		{
			State->Load(Manager);
		}
		else
		{
			DataLayers.Add(FDataLayerPersistentState{Handle});
		}
	}

	// remove outdated data layers
	for (auto It = DataLayers.CreateIterator(); It; ++It)
	{
		UWorldSubsystem* Subsystem = It->Handle.ResolveObject<UWorldSubsystem>();
		if (Subsystem == nullptr)
		{
#if WITH_EDITOR
			UE_LOG(LogPersistentState, Error, TEXT("%s: Failed to find data layer instance %s"), *FString(__FUNCTION__),  *It->Handle.GetObjectName());
#endif
			It.RemoveCurrentSwap();
		}
	}
}

void UPersistentStateManager_DataLayers::SaveState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_DataLayers_SaveGameState, PersistentStateChannel);
	Super::SaveState();

	UDataLayerManager* Manager = GetWorld()->GetDataLayerManager();
	check(Manager);

	// @todo: for each loaded static/dynamic level instance, track data layer state as well
	for (auto& State: DataLayers)
	{
		State.Save(Manager);
	}
}
