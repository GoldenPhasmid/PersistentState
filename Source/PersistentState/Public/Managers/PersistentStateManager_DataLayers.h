#pragma once

#include "CoreMinimal.h"
#include "PersistentStateManager.h"
#include "PersistentStateObjectId.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

#include "PersistentStateManager_DataLayers.generated.h"

enum class EDataLayerRuntimeState : uint8;

/**
 * State that describes Data Layer Asset state for a particular UWorld
 */
USTRUCT()
struct PERSISTENTSTATE_API FDataLayerPersistentState
{
	GENERATED_BODY()
public:
	FDataLayerPersistentState() = default;
	FDataLayerPersistentState(AWorldDataLayers* WorldDataLayers, const FPersistentStateObjectId& InHandle);

	void Load(AWorldDataLayers* WorldDataLayers);
	void Save(AWorldDataLayers* WorldDataLayers);

	UDataLayerAsset* GetDataLayerAsset() const;
	
	UPROPERTY(meta = (AlwaysLoaded))
	FPersistentStateObjectId DataLayerAssetHandle;
	
	UPROPERTY(meta = (AlwaysLoaded))
	EDataLayerRuntimeState CurrentState = EDataLayerRuntimeState::Unloaded;
};

FORCEINLINE bool operator==(const FDataLayerPersistentState& State, const FPersistentStateObjectId& Handle)
{
	return State.DataLayerAssetHandle == Handle;
}

USTRUCT()
struct FPersistentStateDataLayerContainer
{
	GENERATED_BODY()

	UPROPERTY(meta = (AlwaysLoaded))
	TArray<FDataLayerPersistentState> DataLayers;
};

/**
 * Data Layer Persistent State Manager
 * Responsible for storing data layer asset states for main world and any dynamically created level instances
 */
UCLASS()
class PERSISTENTSTATE_API UPersistentStateManager_DataLayers: public UPersistentStateManager
{
	GENERATED_BODY()
public:
	UPersistentStateManager_DataLayers();

	//~Begin PersistentStateManager interface
	virtual bool ShouldCreateManager(UPersistentStateSubsystem& Subsystem) const override;
	virtual void Init(UPersistentStateSubsystem& Subsystem) override;
	virtual void Cleanup(UPersistentStateSubsystem& InSubsystem) override;
	virtual void NotifyActorsInitialized() override;
	virtual void SaveState() override;
	virtual void UpdateStats() const override;
	virtual uint32 GetAllocatedSize() const override;
	//~End PersistentStateManager interface
	
protected:
	
	void LoadGameState();
	void LoadDataLayerContainer(UWorld* InWorld, FPersistentStateDataLayerContainer& Container);
	void SaveDataLayerContainer(UWorld* InWorld, FPersistentStateDataLayerContainer& Container);
	
	void OnLevelAdded(ULevel* Level, UWorld* World);
	void OnLevelRemoved(ULevel* Level, UWorld* World);

	/** data layer assets stored per world */
	UPROPERTY(meta = (AlwaysLoaded))
	TMap<FPersistentStateObjectId, FPersistentStateDataLayerContainer> WorldMap;

	UPROPERTY(Transient)
	UWorld* CurrentWorld = nullptr;
};
