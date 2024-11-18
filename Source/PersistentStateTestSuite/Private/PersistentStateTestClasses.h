#pragma once

#include "PersistentStateInterface.h"
#include "PersistentStateSlotStorage.h"
#include "PersistentStateSlot.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"

#include "PersistentStateTestClasses.generated.h"

namespace UE::PersistentState
{
	extern FWorldStateSharedRef CurrentWorldState;
	extern FWorldStateSharedRef PrevWorldState;
	extern FPersistentStateSlotHandle ExpectedSlot;
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
	virtual void PreSaveState() override	{ bPreSaveStateCalled = true; }
	virtual void PreLoadState() override	{ bPreLoadStateCalled = true; }
	virtual void PostSaveState() override	{ bPostSaveStateCalled = true; }
	virtual void PostLoadState() override	{ bPostLoadStateCalled = true; }

	virtual void LoadCustomObjectState(FConstStructView State) override
	{
		CustomState = State;
		bCustomStateLoaded = true;
	}
	
	virtual FConstStructView SaveCustomObjectState() override
	{
		CustomState = FInstancedStruct::Make<FPersistentStateTestData>(ObjectName);
		bCustomStateSaved = true;
		
		return FConstStructView{CustomState};
	}

	void ResetCallbacks()
	{
		bPreSaveStateCalled = bPostSaveStateCalled = bPreLoadStateCalled = bPostLoadStateCalled = false;
		bCustomStateSaved = bCustomStateLoaded = false;
		CustomState.Reset();
	}
	
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


UCLASS()
class UPersistentStateMockStorage: public UPersistentStateStorage
{
	GENERATED_BODY()
public:

	//~Begin PersistentStateStorage interface
	virtual void Init() override {}
	virtual void Shutdown() override {}
	virtual void SaveWorldState(const FWorldStateSharedRef& WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle) override;
	virtual FWorldStateSharedRef LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName) override;
	virtual void UpdateAvailableStateSlots() override {}
	virtual void GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates) override;
	virtual FPersistentStateSlotHandle CreateStateSlot(const FString& SlotName, const FText& Title) override;
	virtual FPersistentStateSlotHandle GetStateSlotByName(FName SlotName) const override;
	virtual FPersistentStateSlotSharedRef GetStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual FName GetWorldFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual bool CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override { return true; }
	virtual bool CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override { return true; }
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle) override { return; }
	//~End PersistentStateStorage interface
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
class UPersistentStateTestComponent: public UActorComponent, public IPersistentStateObject
{
	GENERATED_BODY()
public:

	virtual void InitializeComponent() override;
};

UCLASS(HideDropdown)
class UPersistentStateSceneTestComponent: public USceneComponent, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	virtual void InitializeComponent() override;
};

UCLASS(HideDropdown, BlueprintType)
class APersistentStateTestActor: public AActor, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	APersistentStateTestActor();

	virtual void PostInitializeComponents() override;

	virtual void LoadCustomObjectState(FConstStructView State) override { CustomStateData = State.Get<const FPersistentStateTestData>(); }
	virtual FConstStructView SaveCustomObjectState() override { return FConstStructView::Make(CustomStateData); }

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

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
};

UCLASS(HideDropdown)
class APersistentStateTestGameMode: public AGameModeBase, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	APersistentStateTestGameMode();
	
	virtual void PostInitializeComponents() override;
	virtual FName GetStableName() const override { return GetClass()->GetFName(); }

	UPROPERTY(SaveGame)
	AActor* StoredStaticActor = nullptr;
	
	UPROPERTY(SaveGame)
	AActor* StoredDynamicActor = nullptr;
};

UCLASS(HideDropdown)
class APersistentStateTestGameState: public AGameStateBase, public IPersistentStateObject
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