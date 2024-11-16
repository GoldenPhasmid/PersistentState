#include "Managers/WorldPersistentStateManager_DataLayers.h"

#include "PersistentStateDefines.h"
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
	
	DataLayerManager->SetDataLayerRuntimeState(DataLayerInstance->GetAsset(), CurrentState);
}

void FDataLayerPersistentState::Save(UDataLayerManager* DataLayerManager)
{
	bStateSaved = true;

	UDataLayerInstance* DataLayerInstance = Handle.ResolveObject<UDataLayerInstance>();
	check(DataLayerInstance);

	InitialState = DataLayerInstance->GetInitialRuntimeState();
	CurrentState = DataLayerManager->GetDataLayerInstanceRuntimeState(DataLayerInstance);
}

bool UWorldPersistentStateManager_DataLayers::ShouldCreateManager(UWorld* InWorld) const
{
	return Super::ShouldCreateManager(InWorld) && InWorld->IsPartitionedWorld();
}

void UWorldPersistentStateManager_DataLayers::Init(UWorld* World)
{
	Super::Init(World);
	check(World->bIsWorldInitialized && !World->bActorsInitialized);
}

void UWorldPersistentStateManager_DataLayers::Cleanup(UWorld* World)
{
	Super::Cleanup(World);
}

void UWorldPersistentStateManager_DataLayers::SaveGameState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_DataLayers_SaveGameState, PersistentStateChannel);
	Super::SaveGameState();

	UDataLayerManager* Manager = CurrentWorld->GetDataLayerManager();
	check(Manager);

	// @todo: for each loaded static/dynamic level isntance, track data layer state as well

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
			UE_LOG(LogPersistentState, Error, TEXT("%s: Failed to find world subsystem %s"), *FString(__FUNCTION__),  *It->Handle.GetObjectName());
#endif
			It.RemoveCurrentSwap();
		}
	}

	for (auto& State: DataLayers)
	{
		State.Save(Manager);
	}
}
