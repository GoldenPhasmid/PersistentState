#pragma once

#include "CoreMinimal.h"
#include "PersistentStateObjectId.h"
#include "WorldPersistentStateManager.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"

#include "WorldPersistentStateManager_DataLayers.generated.h"

enum class EDataLayerRuntimeState : uint8;

USTRUCT()
struct PERSISTENTSTATE_API FDataLayerPersistentState
{
	GENERATED_BODY()
public:
	FDataLayerPersistentState() = default;
	explicit FDataLayerPersistentState(const FPersistentStateObjectId& InHandle);

	void Load(UDataLayerManager* DataLayerManager);
	void Save(UDataLayerManager* DataLayerManager);
	
	UPROPERTY(meta = (AlwaysLoaded))
	FPersistentStateObjectId Handle;

	UPROPERTY(meta = (AlwaysLoaded))
	EDataLayerRuntimeState InitialState = EDataLayerRuntimeState::Unloaded;
	
	UPROPERTY(meta = (AlwaysLoaded))
	EDataLayerRuntimeState CurrentState = EDataLayerRuntimeState::Unloaded;

	UPROPERTY(meta = (AlwaysLoaded))
	bool bStateSaved = false;
};

FORCEINLINE bool operator==(const FDataLayerPersistentState& State, const FPersistentStateObjectId& Handle)
{
	return State.Handle == Handle;
}

UCLASS()
class PERSISTENTSTATE_API UWorldPersistentStateManager_DataLayers: public UWorldPersistentStateManager
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateManager(UWorld* InWorld) const override;
	virtual void Init(UWorld* World) override;
	virtual void SaveGameState() override;

protected:
	
	void LoadGameState(const FActorsInitializedParams& Params);

	UPROPERTY()
	TArray<FDataLayerPersistentState> DataLayers;

	FDelegateHandle InitializedActorsHandle;
};
