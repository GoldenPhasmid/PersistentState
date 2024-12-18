#pragma once

#include "CoreMinimal.h"
#include "PersistentStateStorage.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "PersistentStateSubsystem.generated.h"

enum class EManagerStorageType : uint8;
struct FPersistentStateSlotHandle;
class UPersistentStateStorage;
class UPersistentStateManager;
struct FPersistentStorageHandle;

DECLARE_MULTICAST_DELEGATE_OneParam(FStateChangeDelegate, const FPersistentStateSlotHandle&);



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

	static UPersistentStateSubsystem* Get(UObject* WorldContextObject);
	static UPersistentStateSubsystem* Get(UWorld* World);

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
	UFUNCTION(BlueprintCallable)
	bool LoadGameFromSlot(const FPersistentStateSlotHandle& TargetSlot, FString TravelOptions = FString(TEXT("")));

	/**
	 * Load game state from a specified target slot 
	 * LoadGame always means absolute world travel, in this case to the specified @World, with world state loaded from target slot
	 */
	UFUNCTION(BlueprintCallable)
	bool LoadGameWorldFromSlot(const FPersistentStateSlotHandle& TargetSlot, TSoftObjectPtr<UWorld> World, FString TravelOptions = FString(TEXT("")));

	/**
	 * Save game state to the current slot
	 * Does nothing if slot has not been established. To create a new save, call @CreateSaveGameSlot first
	 */
	UFUNCTION(BlueprintCallable)
	bool SaveGame();

	/** Save game state to the specified target slot. @ActiveSlot is automatically updated to a @TargetSlot value if save is successful */
	UFUNCTION(BlueprintCallable)
	bool SaveGameToSlot(const FPersistentStateSlotHandle& TargetSlot);

	/**
	 * @return a list of available state slots.
	 * @param bUpdate
	 * @param bOnDiskOnly
	 */
	UFUNCTION(BlueprintCallable)
	void GetSaveGameSlots(TArray<FPersistentStateSlotHandle>& OutSlots, bool bUpdate = false, bool bOnDiskOnly = false) const;

	/**
	 * create a new save game slot with a specified @SlotName and @Title
	 * If slot with SlotName already exists its handle is returned, otherwise new slot is created
	 */
	UFUNCTION(BlueprintCallable)
	FPersistentStateSlotHandle CreateSaveGameSlot(FName SlotName, FText Title);

	/** @return state slot identified by @SlotName */
	UFUNCTION(BlueprintCallable)
	FPersistentStateSlotHandle FindSaveGameSlotByName(FName SlotName) const;

	/** remove save game slot and associated slot data */
	UFUNCTION(BlueprintCallable)
	void RemoveSaveGameSlot(const FPersistentStateSlotHandle& Slot) const;
	
	/** @return current slot */
	UFUNCTION(BlueprintPure)
	FPersistentStateSlotHandle GetActiveSaveGameSlot() const { return ActiveSlot; }

	/** */
	FStateChangeDelegate OnSaveStateStarted;
	/** */
	FStateChangeDelegate OnSaveStateFinished;
	/** */
	FStateChangeDelegate OnLoadStateStarted;
	/** */
	FStateChangeDelegate OnLoadStateFinished;

	void NotifyObjectInitialized(UObject& Object);

protected:
	/** @return manager collection by type */
	TConstArrayView<UPersistentStateManager*> GetManagerCollectionByType(EManagerStorageType ManagerType) const;

	/** iterate over each manager, optionally filter by manager type */
	void ForEachManager(EManagerStorageType TypeFilter, TFunctionRef<void(UPersistentStateManager*)> Callback) const;
	bool ForEachManagerWithBreak(EManagerStorageType TypeFilter,TFunctionRef<bool(UPersistentStateManager*)> Callback) const;
	void LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle);
	
	void OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS);
	void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void OnWorldSeamlessTravel(UWorld* World);
#if WITH_EDITOR
	void OnEndPlay(const bool bSimulating);
#endif
	
	void LoadWorldState(UWorld* World, const FPersistentStateSlotHandle& TargetSlot);
	void SaveWorldState(UWorld* World, const FPersistentStateSlotHandle& SourceSlot, const FPersistentStateSlotHandle& TargetSlot);
	void ResetWorldState();
	FORCEINLINE bool HasWorldState() const { return bHasWorldState; }

	UPROPERTY(Transient)
	UPersistentStateStorage* StateStorage = nullptr;
	
	TMap<EManagerStorageType, TArray<TObjectPtr<UPersistentStateManager>>> ManagerMap;
	TMap<EManagerStorageType, TArray<UClass*>> ManagerTypeMap;
	
	FPersistentStateSlotHandle ActiveSlot;
	bool bInitialized = false;
	bool bHasWorldState = false;
};
