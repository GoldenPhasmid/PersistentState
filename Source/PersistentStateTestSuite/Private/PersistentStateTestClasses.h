#pragma once

#include "PersistentStateActorBase.h"
#include "PersistentStateActorComponent.h"
#include "PersistentStateGameMode.h"
#include "PersistentStateGameState.h"
#include "PersistentStateInterface.h"
#include "PersistentStateModule.h"
#include "PersistentStateSlotStorage.h"
#include "PersistentStateSlot.h"
#include "Managers/PersistentStateManager.h"

#include "PersistentStateTestClasses.generated.h"

namespace UE::PersistentState
{
	extern FGameStateSharedRef CurrentGameState;
	extern FWorldStateSharedRef CurrentWorldState;
	extern FWorldStateSharedRef PrevWorldState;
	extern FPersistentStateSlotHandle ExpectedSlot;
	constexpr int32 AutomationFlags = EAutomationTestFlags::CriticalPriority | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask;
}

USTRUCT(meta = (HiddenByDefault, Hidden))
struct FPersistentStateTestData
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name = NAME_None;
};


UINTERFACE()
class UPersistentStateCallbackListener: public UPersistentStateObject
{
	GENERATED_BODY()	
};

class IPersistentStateCallbackListener: public IPersistentStateObject
{
	GENERATED_BODY()
public:
	virtual bool ShouldSaveState() const override { return bShouldSave; }
	virtual void PreSaveState() override;
	virtual void PreLoadState() override;
	virtual void PostSaveState() override;
	virtual void PostLoadState() override;

	virtual void LoadCustomObjectState(FConstStructView State) override;
	virtual FConstStructView SaveCustomObjectState() override;

	void Reset();
	FString GetClassName() const;

	/** test required interface */
	FORCEINLINE void SetInstanceName(FName InObjectName) { ObjectName = InObjectName; }
	FORCEINLINE FName GetInstanceName() const { return ObjectName; }
	
	uint8 bShouldSave: 1 = true;
	uint8 bPreSaveStateCalled: 1	= false;
	uint8 bPostSaveStateCalled: 1	= false;
	uint8 bPreLoadStateCalled: 1	= false;
	uint8 bPostLoadStateCalled: 1	= false;
	uint8 bCustomStateSaved: 1		= false;
	uint8 bCustomStateLoaded: 1		= false;
	FInstancedStruct CustomState;
	FName ObjectName = NAME_None;
};

struct FPersistentStateSubsystemCallbackListener
{
	using ThisClass = FPersistentStateSubsystemCallbackListener;

	FPersistentStateSubsystemCallbackListener() {}

	void SetSubsystem(UPersistentStateSubsystem& InSubsystem);

	void Clear();

	~FPersistentStateSubsystemCallbackListener();

	FPersistentStateSlotHandle LoadSlot;
	FPersistentStateSlotHandle SaveSlot;
	bool bLoadStarted = false;
	bool bLoadFinished = false;
	bool bSaveStarted = false;
	bool bSaveFinished = false;
	UPersistentStateSubsystem* Subsystem = nullptr;
	
private:

	void OnLoadStarted(const FPersistentStateSlotHandle& Slot)	{ bLoadStarted = true;	LoadSlot = Slot; }
	void OnLoadFinished(const FPersistentStateSlotHandle& Slot) { bLoadFinished = true; LoadSlot = Slot; }
	void OnSaveStarted(const FPersistentStateSlotHandle& Slot)	{ bSaveStarted = true;	SaveSlot = Slot; }
	void OnSaveFinished(const FPersistentStateSlotHandle& Slot) { bSaveFinished = true; SaveSlot = Slot; }
};


UCLASS(HideDropdown)
class UPersistentStateFakeStorage: public UPersistentStateStorage
{
	GENERATED_BODY()
public:

	//~Begin PersistentStateStorage interface
	virtual void Init() override {}
	virtual void Shutdown() override {}
	virtual uint32 GetAllocatedSize() const override;
	virtual void WaitUntilTasksComplete() const override {}
	virtual FGraphEventRef SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate) override;
	virtual FGraphEventRef LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName, FLoadCompletedDelegate CompletedDelegate) override;
	virtual void SaveStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle) override {}
	virtual bool HasScreenshotForStateSlot(const FPersistentStateSlotHandle& TargetSlotHandle) override { return false; }
	virtual bool LoadStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle, FLoadScreenshotCompletedDelegate CompletedDelegate) override { return false; }
	virtual FGraphEventRef UpdateAvailableStateSlots(FSlotUpdateCompletedDelegate CompletedDelegate) override { return {}; }
	virtual void GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly) override;
	virtual UPersistentStateSlotDescriptor* GetStateSlotDescriptor(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual FPersistentStateSlotHandle CreateStateSlot(const FName& SlotName, const FText& Title, TSubclassOf<UPersistentStateSlotDescriptor> DescriptorClass) override;
	virtual FPersistentStateSlotHandle GetStateSlotByName(FName SlotName) const override;
	virtual bool CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const override;
	virtual bool CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const override { return true; }
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle) override;
	//~End PersistentStateStorage interface

	TArray<FName> SlotNames;
};

UCLASS(HideDropdown)
class UPersistentStateSlotMockStorage: public UPersistentStateSlotStorage
{
	GENERATED_BODY()
public:
	FPersistentStateSlotSharedRef GetSlotUnsafe(FName SlotName) const
	{
		return FindSlot(SlotName);
	}
};

UCLASS(HideDropdown)
class UPersistentStateTestObject: public UObject, public IPersistentStateObject
{
	GENERATED_BODY()
};

UCLASS(HideDropdown)
class UPersistentStateTestObject_NoInterface: public UObject
{
	GENERATED_BODY()
};

UCLASS(HideDropdown, BlueprintType)
class UPersistentStateEmptyTestComponent: public UActorComponent, public IPersistentStateCallbackListener
{
	GENERATED_BODY()
public:
	UPersistentStateEmptyTestComponent();

	virtual void PostLoad() override;
	virtual void InitializeComponent() override;
};


UCLASS(HideDropdown, BlueprintType)
class APersistentStateEmptyTestActor: public AActor, public IPersistentStateCallbackListener
{
	GENERATED_BODY()
public:
	APersistentStateEmptyTestActor();
	
	virtual void PostLoad() override;
	virtual void PostInitializeComponents() override;

	UPROPERTY()
	UPersistentStateEmptyTestComponent* Component = nullptr;
};

UCLASS(HideDropdown)
class UPersistentStateTestComponent: public UPersistentStateActorComponent
{
	GENERATED_BODY()
public:
	
	virtual void LoadCustomObjectState(FConstStructView State) override { CustomStateData = State.Get<const FPersistentStateTestData>(); }
	virtual FConstStructView SaveCustomObjectState() override { return FConstStructView::Make(CustomStateData); }

	FORCEINLINE void SetInstanceName(FName InObjectName) { CustomStateData.Name = InObjectName; }
	FORCEINLINE FName GetInstanceName() const { return CustomStateData.Name; }

	/** stored int, expected to match previously set value after load */
	UPROPERTY(SaveGame)
	int32 StoredInt = 0;

	/** stored string, expected to match previously set value after load */
	UPROPERTY(SaveGame)
	FString StoredString{};

	/**
	 * stored name, expected to match previously set value after load.
	 * Differentiate between strings and names, because name in theory can be serialized as an string table index
	 */
	UPROPERTY(SaveGame)
	FName StoredName = NAME_None;

	/** reference to map stored actor, set at runtime */
	UPROPERTY(SaveGame)
	AActor* StoredStaticActor = nullptr;

	/** reference to runtime created actor, set at runtime */
	UPROPERTY(SaveGame)
	AActor* StoredDynamicActor = nullptr;

	/** reference to a statically created component, owned by another actor */
	UPROPERTY(SaveGame)
	UActorComponent* StoredStaticComponent = nullptr;

	/** reference to a dynamically created component, owned by another actor */
	UPROPERTY(SaveGame)
	UActorComponent* StoredDynamicComponent = nullptr;
	
	UPROPERTY()
	FPersistentStateTestData CustomStateData;
};

UCLASS(HideDropdown)
class UPersistentStateSceneTestComponent: public USceneComponent, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	virtual void InitializeComponent() override;
};

UCLASS(HideDropdown, BlueprintType)
class APersistentStateTestActor: public APersistentStateActorBase
{
	GENERATED_BODY()
public:
	APersistentStateTestActor();

	virtual void LoadCustomObjectState(FConstStructView State) override { CustomStateData = State.Get<const FPersistentStateTestData>(); }
	virtual FConstStructView SaveCustomObjectState() override { return FConstStructView::Make(CustomStateData); }

	FORCEINLINE void SetInstanceName(FName InObjectName) { CustomStateData.Name = InObjectName; }
	FORCEINLINE FName GetInstanceName() const { return CustomStateData.Name; }
	
	UPROPERTY(VisibleAnywhere)
	UPersistentStateTestComponent* StaticComponent;

	UPROPERTY(VisibleAnywhere)
	UPersistentStateSceneTestComponent* SceneComponent;
	
	UPROPERTY(SaveGame)
	int32 StoredInt = 0;

	UPROPERTY(SaveGame)
	FString StoredString{};

	UPROPERTY(SaveGame)
	FName StoredName = NAME_None;

	/** reference to dynamically created component, owned by this actor */
	UPROPERTY(SaveGame)
    UPersistentStateTestComponent* DynamicComponent;

	/** reference to map stored actor, set at runtime */
	UPROPERTY(SaveGame)
	AActor* StoredStaticActor = nullptr;

	/** reference to runtime created actor, set at runtime */
	UPROPERTY(SaveGame)
	AActor* StoredDynamicActor = nullptr;

	/** reference to a statically created component, owned by another actor */
	UPROPERTY(SaveGame)
	UActorComponent* StoredStaticComponent = nullptr;

	/** reference to a dynamically created component, owned by another actor */
	UPROPERTY(SaveGame)
	UActorComponent* StoredDynamicComponent = nullptr;

	UPROPERTY()
	FPersistentStateTestData CustomStateData;
};

UCLASS(HideDropdown)
class UPersistentStateTestWorldSubsystem: public UWorldSubsystem, public IPersistentStateCallbackListener
{
	GENERATED_BODY()
public:

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** stored int, expected to match previously set value after load */
	UPROPERTY(SaveGame)
	int32 StoredInt = 0;

	/** stored string, expected to match previously set value after load */
	UPROPERTY(SaveGame)
	FString StoredString{};

	/**
	 * stored name, expected to match previously set value after load.
	 * Differentiate between strings and names, because name in theory can be serialized as an string table index
	 */
	UPROPERTY(SaveGame)
	FName StoredName = NAME_None;

	/** reference to map stored actor, set at runtime */
	UPROPERTY(SaveGame)
	AActor* StoredStaticActor = nullptr;

	/** reference to runtime created actor, set at runtime */
	UPROPERTY(SaveGame)
	AActor* StoredDynamicActor = nullptr;

	/** reference to a statically created component, owned by another actor */
	UPROPERTY(SaveGame)
	UActorComponent* StoredStaticComponent = nullptr;

	/** reference to a dynamically created component, owned by another actor */
	UPROPERTY(SaveGame)
	UActorComponent* StoredDynamicComponent = nullptr;
};

UCLASS(HideDropdown)
class UPersistentStateTestGameSubsystem: public UGameInstanceSubsystem, public IPersistentStateCallbackListener
{
	GENERATED_BODY()
public:
	
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	
	virtual void PreSaveState() override;
	virtual void PreLoadState() override;
	virtual void PostSaveState() override;
	virtual void PostLoadState() override;
	
	/** stored int, expected to match previously set value after load */
	UPROPERTY(SaveGame)
	int32 StoredInt = 0;

	/** stored string, expected to match previously set value after load */
	UPROPERTY(SaveGame)
	FString StoredString{};

	/**
	 * stored name, expected to match previously set value after load.
	 * Differentiate between strings and names, because name in theory can be serialized as an string table index
	 */
	UPROPERTY(SaveGame)
	FName StoredName = NAME_None;

	/** reference to map stored actor, set at runtime */
	UPROPERTY(SaveGame)
	AActor* StoredStaticActor = nullptr;

	/** reference to runtime created actor, set at runtime */
	UPROPERTY(SaveGame)
	AActor* StoredDynamicActor = nullptr;

	/** reference to a statically created component, owned by another actor */
	UPROPERTY(SaveGame)
	UActorComponent* StoredStaticComponent = nullptr;

	/** reference to a dynamically created component, owned by another actor */
	UPROPERTY(SaveGame)
	UActorComponent* StoredDynamicComponent = nullptr;
};

UCLASS(HideDropdown)
class APersistentStateTestGameMode: public APersistentStateGameModeBase
{
	GENERATED_BODY()
public:
	APersistentStateTestGameMode(const FObjectInitializer& Initializer);

	UPROPERTY(SaveGame)
	AActor* StoredStaticActor = nullptr;
	
	UPROPERTY(SaveGame)
	AActor* StoredDynamicActor = nullptr;
};

UCLASS(HideDropdown)
class APersistentStateTestGameState: public APersistentStateGameStateBase
{
	GENERATED_BODY()
public:

	UPROPERTY(SaveGame)
	AActor* StoredStaticActor = nullptr;
	
	UPROPERTY(SaveGame)
	AActor* StoredDynamicActor = nullptr;
};

UCLASS(HideDropdown)
class APersistentStateTestPlayerController: public APlayerController, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	
	virtual void PostInitializeComponents() override;
	virtual FName GetStableName() const override { return GetClass()->GetFName(); }
	
	UPROPERTY(SaveGame)
	AActor* StoredStaticActor = nullptr;
	
	UPROPERTY(SaveGame)
	AActor* StoredDynamicActor = nullptr;
};

UCLASS(HideDropdown)
class UPersistentStateTestManager: public UPersistentStateManager
{
	GENERATED_BODY()
public:
	
	virtual bool ShouldCreateManager(UPersistentStateSubsystem& InSubsystem) const override;
	virtual void Init(UPersistentStateSubsystem& InSubsystem) override;
	virtual void Cleanup(UPersistentStateSubsystem& InSubsystem) override;
	virtual void SaveState() override;
	virtual void PreLoadState() override;
	virtual void PostLoadState() override;

	void ResetDebugState()
	{
		bInitCalled = bCleanupCalled = bSaveStateCalled = bPreLoadStateCalled = bPostLoadStateCalled = false;
	}

	uint8 bInitCalled: 1 = false;
	uint8 bCleanupCalled: 1 = false;
	uint8 bSaveStateCalled: 1 = false;
	uint8 bPreLoadStateCalled: 1 = false;
	uint8 bPostLoadStateCalled: 1 = false;
};

UCLASS(HideDropdown)
class UPersistentStateTestWorldManager: public UPersistentStateTestManager
{
	GENERATED_BODY()
public:
	UPersistentStateTestWorldManager();
};

UCLASS(HideDropdown)
class UPersistentStateTestGameManager: public UPersistentStateTestManager
{
	GENERATED_BODY()
public:
	UPersistentStateTestGameManager();
};