#include "Managers/PersistentStateManager_DataLayers.h"

#include "PersistentStateModule.h"
#include "PersistentStateSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

DECLARE_DWORD_COUNTER_STAT(TEXT("Tracked Data Layers"),	STAT_PersistentState_NumDataLayers,	STATGROUP_PersistentState);

FDataLayerPersistentState::FDataLayerPersistentState(AWorldDataLayers* WorldDataLayers, const FPersistentStateObjectId& InHandle)
	: DataLayerAssetHandle(InHandle)
{
	Save(WorldDataLayers);
}

UDataLayerAsset* FDataLayerPersistentState::GetDataLayerAsset() const
{
	return DataLayerAssetHandle.ResolveObject<UDataLayerAsset>();
}

void FDataLayerPersistentState::Save(AWorldDataLayers* WorldDataLayers)
{
	UDataLayerAsset* DataLayerAsset = GetDataLayerAsset();
	check(DataLayerAsset);

	if (const UDataLayerInstance* DataLayerInstance = WorldDataLayers->GetDataLayerInstance(DataLayerAsset))
	{
		CurrentState = DataLayerInstance->GetRuntimeState();
	}
}

void FDataLayerPersistentState::Load(AWorldDataLayers* WorldDataLayers)
{
	UDataLayerAsset* DataLayerAsset = GetDataLayerAsset();
	check(DataLayerAsset);

	if (const UDataLayerInstance* DataLayerInstance = WorldDataLayers->GetDataLayerInstance(DataLayerAsset))
	{
		WorldDataLayers->SetDataLayerRuntimeState(DataLayerInstance, CurrentState);
	}
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

	CurrentWorld = Subsystem.GetWorld();
	check(CurrentWorld->IsInitialized() && !CurrentWorld->AreActorsInitialized());
	check(CurrentWorld->GetWorldPartition() && !CurrentWorld->GetWorldPartition()->IsInitialized());

	FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ThisClass::OnLevelAdded);
	FWorldDelegates::PreLevelRemovedFromWorld.AddUObject(this, &ThisClass::OnLevelRemoved);
}

void UPersistentStateManager_DataLayers::Cleanup(UPersistentStateSubsystem& InSubsystem)
{
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	FWorldDelegates::PreLevelRemovedFromWorld.RemoveAll(this);
	
	Super::Cleanup(InSubsystem);
}

void UPersistentStateManager_DataLayers::NotifyActorsInitialized()
{
	Super::NotifyActorsInitialized();
	
	LoadGameState();
}

void UPersistentStateManager_DataLayers::OnLevelAdded(ULevel* Level, UWorld* World)
{
	if (Level == nullptr)
	{
		return;
	}
	
	UWorld* OuterWorld = Level->GetTypedOuter<UWorld>();
	const FString PackageName = OuterWorld->GetPackage()->GetName();
	// level is persistent level (loaded once), and owning world is different from outer world - runtime level instance
	if (Level && OuterWorld->PersistentLevel == Level && CurrentWorld != OuterWorld && !FPackageName::IsMemoryPackage(PackageName))
	{
		if (AWorldDataLayers* DataLayers = OuterWorld->GetWorldDataLayers())
		{
			FPersistentStateObjectId WorldId = FPersistentStateObjectId::CreateStaticObjectId(OuterWorld);
			check(WorldId.IsValid());

			auto& Container = WorldMap.FindOrAdd(WorldId);
			LoadDataLayerContainer(OuterWorld, Container);
		}
	}
}

void UPersistentStateManager_DataLayers::OnLevelRemoved(ULevel* Level, UWorld* World)
{
	if (Level == nullptr)
	{
		return;
	}
	
	UWorld* OuterWorld = Level->GetTypedOuter<UWorld>();
	const FString PackageName = OuterWorld->GetPackage()->GetName();
	// level is persistent level (loaded once), and owning world is different from outer world - runtime level instance
	if (Level && OuterWorld->PersistentLevel == Level && CurrentWorld != OuterWorld && !FPackageName::IsMemoryPackage(PackageName))
	{
		if (AWorldDataLayers* DataLayers = OuterWorld->GetWorldDataLayers())
		{
			FPersistentStateObjectId WorldId = FPersistentStateObjectId::CreateStaticObjectId(OuterWorld);
			check(WorldId.IsValid());

			// save data layer that is being unloaded
			if (auto* ContainerPtr = WorldMap.Find(WorldId))
			{
				SaveDataLayerContainer(OuterWorld, *ContainerPtr);
			}
		}

	}
}

void UPersistentStateManager_DataLayers::LoadGameState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);

	UWorld* World = GetWorld();
	check(World);
	
	FPersistentStateObjectId WorldId = FPersistentStateObjectId::CreateStaticObjectId(World);
	check(WorldId.IsValid());

	// remove outdated data layers
	auto& MainContainer = WorldMap.FindOrAdd(WorldId);
	LoadDataLayerContainer(World, MainContainer);
}

void UPersistentStateManager_DataLayers::SaveDataLayerContainer(UWorld* InWorld, FPersistentStateDataLayerContainer& Container)
{
	AWorldDataLayers* WorldDataLayers = InWorld->GetWorldDataLayers();
	check(WorldDataLayers);

	IDataLayerInstanceProvider* DataLayerProvider = WorldDataLayers;
	for (UDataLayerInstance* Instance: DataLayerProvider->GetDataLayerInstances())
	{
		FPersistentStateObjectId DataLayerAssetId = FPersistentStateObjectId::CreateStaticObjectId(Instance->GetAsset());
		check(DataLayerAssetId.IsValid());
		
		if (Instance->GetInitialRuntimeState() != Instance->GetRuntimeState())
		{
			// if instance runtime state is different from initial state, find existing state and save it
			if (FDataLayerPersistentState* StatePtr = Container.DataLayers.FindByKey(DataLayerAssetId))
			{
				StatePtr->Save(WorldDataLayers);
			}
			else
			{
				Container.DataLayers.Add(FDataLayerPersistentState{WorldDataLayers, DataLayerAssetId});
			}
		}
		else if (int32 ExistingIndex = Container.DataLayers.IndexOfByKey(DataLayerAssetId); ExistingIndex != INDEX_NONE)
		{
			// if initial state equals runtime state, remove data layer state
			Container.DataLayers.RemoveAtSwap(ExistingIndex);
		}
	}
}

void UPersistentStateManager_DataLayers::LoadDataLayerContainer(UWorld* InWorld, FPersistentStateDataLayerContainer& Container)
{
	for (auto It = Container.DataLayers.CreateIterator(); It; ++It)
	{
		if (It->GetDataLayerAsset() == nullptr)
		{
#if WITH_EDITOR
			UE_LOG(LogPersistentState, Error, TEXT("%s: Failed to find data layer asset %s"),
				*FString(__FUNCTION__),  *It->DataLayerAssetHandle.GetObjectName());
#endif
			It.RemoveCurrentSwap();
		}
	}
	
	AWorldDataLayers* WorldDataLayers = InWorld->GetWorldDataLayers();
	check(WorldDataLayers);

	for (FDataLayerPersistentState& State: Container.DataLayers)
	{
		State.Load(WorldDataLayers);
	}
}

void UPersistentStateManager_DataLayers::SaveState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	Super::SaveState();
	
	for (auto& [WorldId, Container]: WorldMap)
	{
		UWorld* World = WorldId.ResolveObject<UWorld>();
		if (World == nullptr)
		{
			return;
		}

		SaveDataLayerContainer(World, Container);
	}
}

void UPersistentStateManager_DataLayers::UpdateStats() const
{
#if STATS
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(__FUNCTION__, PersistentStateChannel);
	int32 NumDataLayers{0};
	for (auto& [WorldId, Container]: WorldMap)
	{
		NumDataLayers += Container.DataLayers.Num();
	}
	SET_DWORD_STAT(STAT_PersistentState_NumDataLayers, NumDataLayers);
#endif
}

uint32 UPersistentStateManager_DataLayers::GetAllocatedSize() const
{
	uint32 TotalMemory = Super::GetAllocatedSize();
#if STATS
	TotalMemory += WorldMap.GetAllocatedSize();
	
	for (auto& [WorldId, Container]: WorldMap)
	{
		TotalMemory += Container.DataLayers.GetAllocatedSize();
	}
#endif
	
	return TotalMemory;
}
