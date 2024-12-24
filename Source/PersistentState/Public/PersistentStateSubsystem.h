#pragma once

#include "CoreMinimal.h"
#include "PersistentStateStorage.h"
#include "Managers/PersistentStateManager.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "PersistentStateSubsystem.generated.h"

enum class EManagerStorageType : uint8;
struct FPersistentStateSlotHandle;
class UPersistentStateStorage;
class UPersistentStateManager;
struct FPersistentStorageHandle;

DECLARE_MULTICAST_DELEGATE_OneParam(FStateChangeDelegate, const FPersistentStateSlotHandle&);

struct FSaveGamePendingRequest
{
	FPersistentStateSlotHandle TargetSlot;
};

struct FLoadGamePendingRequest
{
	FLoadGamePendingRequest(FPersistentStateSlotHandle InTargetSlot, FName InMapName)
		: TargetSlot(InTargetSlot), MapName(InMapName)
	{}
	
	FPersistentStateSlotHandle TargetSlot;
	/** map name to load */
	FName MapName = NAME_None;
	/** travel options, used only by pending request */
	FString TravelOptions;
	/** load task handle */
	UE::Tasks::FTask LoadTask;
	/** loaded game state, set after load task is completed */
	FGameStateSharedRef LoadedGameState;
	/** loaded world state, set after load task is completed */
	FWorldStateSharedRef LoadedWorldState;
};

/**
 * Persistent State Subsystem
 * @todo: remove FTickableGameObject, managers do not require ticking by default
 */
UCLASS()
class PERSISTENTSTATE_API UPersistentStateSubsystem: public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	UPersistentStateSubsystem();

	static UPersistentStateSubsystem* Get(const UObject* WorldContextObject);
	static UPersistentStateSubsystem* Get(const UWorld* World);

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	//~Begin Subsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	//~End Subsystem interface

	//~Begin TickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsAllowedToTick() const override final;
	virtual bool IsTickableWhenPaused() const override;
	virtual TStatId GetStatId() const override;
	//~End TickableGameObject interface 

	/** @return state manager object of a specified type */
	UFUNCTION(BlueprintCallable)
	UPersistentStateManager* GetStateManager(TSubclassOf<UPersistentStateManager> ManagerClass) const;

	/** @return state manager object of a specified type */
	template <typename TManagerType = UPersistentStateManager>
	TManagerType* GetStateManager() const
	{
		return CastChecked<TManagerType>(GetStateManager(TManagerType::StaticClass()), ECastCheckedType::NullAllowed);
	}

	/**
	 * Load game state from a specified target slot 
	 * LoadGame always means absolute world travel, in this case it is going to be last saved world in a target slot
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	bool LoadGameFromSlot(const FPersistentStateSlotHandle& TargetSlot, FString TravelOptions = FString(TEXT("")));

	/**
	 * Load game state from a specified target slot 
	 * LoadGame always means absolute world travel, in this case to the specified @World, with world state loaded from target slot
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	bool LoadGameWorldFromSlot(const FPersistentStateSlotHandle& TargetSlot, TSoftObjectPtr<UWorld> World, FString TravelOptions = FString(TEXT("")));

	/**
	 * Save game state to the current slot
	 * Does nothing if active slot has not been established. To create a new save, call @CreateSaveGameSlot first
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	bool SaveGame();

	/** Save game state to the specified target slot. @ActiveSlot is automatically updated to a @TargetSlot value if save is successful */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	bool SaveGameToSlot(const FPersistentStateSlotHandle& TargetSlot);

	/**
	 * @return a list of available state slots.
	 * @param bUpdate
	 * @param bOnDiskOnly
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	void GetSaveGameSlots(TArray<FPersistentStateSlotHandle>& OutSlots, bool bUpdate = false, bool bOnDiskOnly = false) const;

	/**
	 * create a new save game slot with a specified @SlotName and @Title
	 * If slot with SlotName already exists its handle is returned, otherwise new slot is created
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	FPersistentStateSlotHandle CreateSaveGameSlot(FName SlotName, FText Title);

	/** @return state slot identified by @SlotName */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	FPersistentStateSlotHandle FindSaveGameSlotByName(FName SlotName) const;

	/** remove save game slot and associated slot data */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	void RemoveSaveGameSlot(const FPersistentStateSlotHandle& Slot) const;
	
	/** @return current slot */
	UFUNCTION(BlueprintPure, Category = "Persistent State")
	FPersistentStateSlotHandle GetActiveSaveGameSlot() const { return ActiveSlot; }

	/** Triggered after save game operation has started */
	FStateChangeDelegate OnSaveStateStarted;
	/** Triggered after save game operation has completed */
	FStateChangeDelegate OnSaveStateFinished;
	/** Triggered after load game operation has started. Always followed by the world transition. When broadcasted, old world is still valid */
	FStateChangeDelegate OnLoadStateStarted;
	/** Triggered after load game operation has completed. When broadcasted new world is already loaded */
	FStateChangeDelegate OnLoadStateFinished;

	void NotifyObjectInitialized(UObject& Object);

protected:
	
	/** @return manager collection by type */
	TConstArrayView<UPersistentStateManager*> GetManagerCollectionByType(EManagerStorageType ManagerType) const;

	/** iterate over each manager, optionally filter by manager type */
	void CreateManagerState(EManagerStorageType TypeFilter);
	void ResetManagerState(EManagerStorageType TypeFilter);
	
	void ForEachManager(EManagerStorageType TypeFilter, TFunctionRef<void(UPersistentStateManager*)> Callback) const;
	bool ForEachManagerWithBreak(EManagerStorageType TypeFilter,TFunctionRef<bool(UPersistentStateManager*)> Callback) const;
	bool HasManagerState(EManagerStorageType TypeFilter) const;

	void OnPreLoadMap(const FWorldContext& WorldContext, const FString& MapName);
	void OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS);
	void OnWorldInitActors(const FActorsInitializedParams& Params);
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void OnWorldSeamlessTravel(UWorld* World);
#if WITH_EDITOR
	void OnEndPlay(const bool bSimulating);
#endif
	
	void OnSaveStateCompleted(FPersistentStateSlotHandle TargetSlot);
	void OnLoadStateCompleted(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, TSharedPtr<FLoadGamePendingRequest> LoadRequest);

	void CreateActiveLoadRequest(FName MapName);
	void ProcessSaveRequests();
	void UpdateStats() const;

	UPROPERTY(Transient)
	TObjectPtr<UPersistentStateStorage> StateStorage = nullptr;

	/** pending load request, processed each frame at the end of the frame */
	TSharedPtr<FLoadGamePendingRequest> PendingLoadRequest;
	/** active load request, alive until world state is initialized. Stores pre-loaded world state */
	TSharedPtr<FLoadGamePendingRequest> ActiveLoadRequest;
	/** Pending save game requests, processed each frame at the end of the frame */
	TArray<TSharedPtr<FSaveGamePendingRequest>> SaveGameRequests;

	/** map from manager type to a list of active managers */
	TMap<EManagerStorageType, TArray<TObjectPtr<UPersistentStateManager>>> ManagerMap;
	/** map from manager type to a list of manager classes */
	TMap<EManagerStorageType, TArray<UClass*>> ManagerTypeMap;
	/** manager state */
	EManagerStorageType ManagerState = EManagerStorageType::None;

	/** current slot, either fully loaded or in progress (@see ActiveLoadRequest) */
	FPersistentStateSlotHandle ActiveSlot;
	/** subsystem is initialized */
	uint8 bInitialized : 1 = false;
};
