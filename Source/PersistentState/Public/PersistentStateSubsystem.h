#pragma once

#include "CoreMinimal.h"
#include "PersistentStateStorage.h"
#include "Managers/PersistentStateManager.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "PersistentStateSubsystem.generated.h"

enum class EManagerStorageType : uint8;
class UPersistentStateStorage;
class UPersistentStateManager;
class UPersistentStateSlotDescriptor;
struct FPersistentStateSlotHandle;

DECLARE_MULTICAST_DELEGATE_OneParam(FStateChangeDelegate, const FPersistentStateSlotHandle&);

struct FSaveGamePendingRequest
{
	FPersistentStateSlotHandle TargetSlot;
};

struct FLoadGamePendingRequest
{
	FLoadGamePendingRequest(const FPersistentStateSlotHandle& InActiveSlot, const FPersistentStateSlotHandle& InTargetSlot, FName InMapName, bool bInCreatedByUser, bool bInInitialLoad)
		: TargetSlot(InTargetSlot)
		, MapName(InMapName)
		, bCreatedByUser(bInCreatedByUser)
		, bInitialLoad(bInInitialLoad)
	{
		bTravelingToNewSlot = bInitialLoad || InActiveSlot != InTargetSlot;
	}

	FORCEINLINE bool CreatedByUser() const { return bCreatedByUser; }
	
	/** target slot to load */
	FPersistentStateSlotHandle TargetSlot;
	/** map name to load */
	FName MapName = NAME_None;
	/** travel options, used only by a pending request */
	FString TravelOptions;
	/** load task handle */
	FGraphEventRef LoadEventRef;
	/** loaded game state, set after load task is completed */
	FGameStateSharedRef LoadedGameState;
	/** loaded world state, set after load task is completed */
	FWorldStateSharedRef LoadedWorldState;
	/** true if was created as a user request, otherwise an automatic request created by persistent state system */
	bool bCreatedByUser = false;
	/** true if loading was caused by initial game load (e.g. subsystem initialization), otherwise caused by world traveling */
	bool bInitialLoad = false;
	/** true if target slot is different from (currently loaded) active slot. Calculated when load request is created */
	bool bTravelingToNewSlot = false;
};

/**
 * Persistent State Subsystem
 * End point for persistent state queries for save, load, world travel, screenshots and more.
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
	TManagerType* GetStateManager() const requires std::is_base_of_v<UPersistentStateManager, TManagerType>
	{
		return CastChecked<TManagerType>(GetStateManager(TManagerType::StaticClass()), ECastCheckedType::NullAllowed);
	}

	/**
	 * Load game state from a specified target slot 
	 * LoadGame always means absolute world travel, in this case it is going to be last saved world in a target slot
	 */
	bool LoadGameFromSlot(const FPersistentStateSlotHandle& TargetSlot, FString TravelOptions = FString(TEXT("")));

	/**
	 * Load game state from a specified target slot 
	 * LoadGame always means absolute world travel, in this case to the specified @World, with world state loaded from target slot
	 */
	bool LoadGameWorldFromSlot(const FPersistentStateSlotHandle& TargetSlot, TSoftObjectPtr<UWorld> World, FString TravelOptions = FString(TEXT("")));

	/**
	 * Load screenshot from a provided slot
	 * @param TargetSlot slot to load a screenshot from
	 * @param CompletedDelegate fired on game thread when texture resource initialization is complete
	 * @return 
	 */
	bool LoadScreenshotFromSlot(const FPersistentStateSlotHandle& TargetSlot, FLoadScreenshotCompletedDelegate CompletedDelegate);

	/**
	 * Save game state to the current slot
	 * Does nothing if active slot has not been established. To create a new save, call @CreateSaveGameSlot first
	 */
	bool SaveGame();

	/** Save game state to the specified target slot. @ActiveSlot is automatically updated to a @TargetSlot value if save is successful */
	bool SaveGameToSlot(const FPersistentStateSlotHandle& TargetSlot);

	/** update a list of save game slots */
	void UpdateSaveGameSlots(FSlotUpdateCompletedDelegate OnUpdateCompleted);

	/**
	 * @return a list of available state slots.
	 * @param OutSlots currently available slots
	 * @param bOnDiskOnly counts only slots that have a physical file representation e.g. default named slots without file path are discarded
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	void GetSaveGameSlots(TArray<FPersistentStateSlotHandle>& OutSlots, bool bOnDiskOnly = false) const;
	
	/**
	 * Create a new save game slot and return a handle to it.
	 * 
	 * @param SlotName logical slot name, used to uniquely identify the slot between other existing slots.
	 * If slot with @SlotName already exists its handle is returned, otherwise new slot is created.
	 * @param SlotTitle displayed slot title. It can either duplicate SlotName, but in beautified name (AutoSave vs "Auto Save")
	 * or be a user-defined name for a slot.
	 * @param DescriptorClass custom descriptor class that will hold persistent save information.
	 * If not specified, default descriptor from Project Settings is used.
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	FPersistentStateSlotHandle CreateSaveGameSlot(FName SlotName, const FText& SlotTitle, TSubclassOf<UPersistentStateSlotDescriptor> DescriptorClass = nullptr);

	/** @return state slot identified by @SlotName */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	FPersistentStateSlotHandle FindSaveGameSlotByName(FName SlotName) const;

	/** remove save game slot and associated slot data */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	void RemoveSaveGameSlot(const FPersistentStateSlotHandle& Slot) const;

	/** @return save game slot descriptor that stores persistent information about the save game */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	UPersistentStateSlotDescriptor* GetSaveGameSlotDescriptor(const FPersistentStateSlotHandle& Slot) const;
	
	/**
	 * @return currently used slot. In case world travel is happening, new loaded slot is set only after world
	 * has been initialized with new game state/world state data.
	 */
	UFUNCTION(BlueprintPure, Category = "Persistent State")
	FPersistentStateSlotHandle GetActiveSaveGameSlot() const { return ActiveSlot; }

	/**
	 * Capture screenshot for a state slot, without saving any game data.
	 * Screenshots are captured automatically during save op if enabled
	 * Does nothing if screenshots are disabled with @UPersistentStateSettings::bCaptureScreenshot
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistent State")
	void CaptureScreenshotForSlot(const FPersistentStateSlotHandle& Slot) const;

	/** @return true if state slot has an associated screenshot */
	UFUNCTION(BlueprintPure, Category = "Persistent State")
	bool HasScreenshotForSlot(const FPersistentStateSlotHandle& Slot) const;

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
	bool CanCreateManagerState(EManagerStorageType ManagerType) const;

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

	void CreateAutoLoadRequest(FName MapName, bool bInitialLoad);
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
	/** flags that describe a set of currently active managers */
	EManagerStorageType ManagerState = EManagerStorageType::None;
	/** flags that describe a set of managers that can be created by subsystem. Initialized once during startup */
	EManagerStorageType CachedCanCreateManagerState = EManagerStorageType::None;

	/** current slot, either fully loaded or in progress (@see ActiveLoadRequest) */
	FPersistentStateSlotHandle ActiveSlot;
	/** subsystem is initialized */
	uint8 bInitialized : 1 = false;
};
