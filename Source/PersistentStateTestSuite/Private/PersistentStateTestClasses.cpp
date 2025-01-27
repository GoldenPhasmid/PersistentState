#include "PersistentStateTestClasses.h"

#include "AutomationWorld.h"
#include "PersistentStateModule.h"
#include "PersistentStateSettings.h"
#include "PersistentStateSubsystem.h"

namespace UE::PersistentState
{
	FGameStateSharedRef CurrentGameState;
	FWorldStateSharedRef CurrentWorldState;
	FWorldStateSharedRef PrevWorldState;
	FPersistentStateSlotHandle ExpectedSlot;
}

FString IPersistentStateCallbackListener::GetClassName() const
{
	return *Cast<UObject>(this)->GetClass()->GetName();
}

void IPersistentStateCallbackListener::PreSaveState()
{
	UE_CLOG(bPreSaveStateCalled, LogPersistentState, Error, TEXT("%s: PreSaveState already called"), *GetClassName());
	UE_CLOG(bPostSaveStateCalled, LogPersistentState, Error, TEXT("%s: PreSaveState called after PostSaveState"), *GetClassName());
	bPreSaveStateCalled = true;
}

void IPersistentStateCallbackListener::PostSaveState()
{
	UE_CLOG(bPostSaveStateCalled, LogPersistentState, Error, TEXT("%s: PostSaveState already called"), *GetClassName());
	UE_CLOG(!bPreSaveStateCalled, LogPersistentState, Error, TEXT("%s: PostSaveState called before PreSaveState"), *GetClassName());
	bPostSaveStateCalled = true;
}

void IPersistentStateCallbackListener::PreLoadState()
{
	UE_CLOG(bPreLoadStateCalled, LogPersistentState, Error, TEXT("%s: PreLoadState already called"), *GetClassName());
	UE_CLOG(bPostLoadStateCalled, LogPersistentState, Error, TEXT("%s: PreLoadState called after PostLoadState"), *GetClassName());
	bPreLoadStateCalled = true;
}

void IPersistentStateCallbackListener::PostLoadState()
{
	UE_CLOG(bPostLoadStateCalled, LogPersistentState, Error, TEXT("%s: PostLoadState already called"), *GetClassName());
	UE_CLOG(!bPreLoadStateCalled, LogPersistentState, Error, TEXT("%s: PostLoadState called before PreLoadState"), *GetClassName());
	bPostLoadStateCalled = true;
}

void IPersistentStateCallbackListener::LoadCustomObjectState(FConstStructView State)
{
	CustomState = State;
	bCustomStateLoaded = true;

	if (CustomState.IsValid() && CustomState.GetScriptStruct() == FPersistentStateTestData::StaticStruct())
	{
		ObjectName = CustomState.Get<FPersistentStateTestData>().Name;
	}
}

FConstStructView IPersistentStateCallbackListener::SaveCustomObjectState()
{
	CustomState = FInstancedStruct::Make<FPersistentStateTestData>(ObjectName);
	bCustomStateSaved = true;
		
	return FConstStructView{CustomState};
}

void IPersistentStateCallbackListener::Reset()
{
	bPreSaveStateCalled = bPostSaveStateCalled = bPreLoadStateCalled = bPostLoadStateCalled = false;
	bCustomStateSaved = bCustomStateLoaded = false;
	CustomState.Reset();
}

void FPersistentStateSubsystemCallbackListener::SetSubsystem(UPersistentStateSubsystem& InSubsystem)
{
	Subsystem = &InSubsystem;
	Subsystem->OnLoadStateStarted.AddRaw(this, &ThisClass::OnLoadStarted);
	Subsystem->OnLoadStateFinished.AddRaw(this, &ThisClass::OnLoadFinished);
	Subsystem->OnSaveStateStarted.AddRaw(this, &ThisClass::OnSaveStarted);
	Subsystem->OnSaveStateFinished.AddRaw(this, &ThisClass::OnSaveFinished);
}

void FPersistentStateSubsystemCallbackListener::Clear()
{
	LoadSlot = SaveSlot = {};
	bLoadStarted = bLoadFinished = bSaveStarted = bSaveFinished = false;
}

FPersistentStateSubsystemCallbackListener::~FPersistentStateSubsystemCallbackListener()
{
	if (Subsystem != nullptr)
	{
		Subsystem->OnLoadStateStarted.RemoveAll(this);
		Subsystem->OnLoadStateFinished.RemoveAll(this);
		Subsystem->OnSaveStateStarted.RemoveAll(this);
		Subsystem->OnSaveStateFinished.RemoveAll(this);
	}
}

FPersistentStateSlotHandle UPersistentStateMockStorage::CreateStateSlot(const FName& SlotName, const FText& Title)
{
	SlotNames.Add(SlotName);
	return FPersistentStateSlotHandle{*this, SlotName};
}

void UPersistentStateMockStorage::GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly)
{
	OutStates.Reset();
	for (const FPersistentSlotEntry& Entry: UPersistentStateSettings::Get()->DefaultNamedSlots)
	{
		OutStates.Add(FPersistentStateSlotHandle{*this, Entry.SlotName});
	}

	for (const FName& SlotName: SlotNames)
	{
		OutStates.Add(FPersistentStateSlotHandle{*this, SlotName});
	}
}

FPersistentStateSlotDesc UPersistentStateMockStorage::GetStateSlotDesc(const FPersistentStateSlotHandle& SlotHandle) const
{
	FPersistentStateSlotDesc Desc{};
	Desc.SlotName = SlotHandle.GetSlotName();
	if (UE::PersistentState::CurrentWorldState.IsValid())
	{
		Desc.LastSavedWorld = UE::PersistentState::CurrentWorldState->GetWorld();
	}
	
	return Desc;
}

FPersistentStateSlotHandle UPersistentStateMockStorage::GetStateSlotByName(FName SlotName) const
{
	return FPersistentStateSlotHandle{*this, SlotName};
}

bool UPersistentStateMockStorage::CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const
{
	return UE::PersistentState::CurrentWorldState.IsValid() && (World == NAME_None || World == UE::PersistentState::CurrentWorldState->GetWorld());
}

uint32 UPersistentStateMockStorage::GetAllocatedSize() const
{
	uint32 TotalMemory = GetClass()->GetStructureSize();
	TotalMemory += SlotNames.GetAllocatedSize();
	
	if (UE::PersistentState::CurrentWorldState.IsValid())
	{
		TotalMemory += UE::PersistentState::CurrentWorldState->GetAllocatedSize();
	}
	
	return TotalMemory;
}

FGraphEventRef UPersistentStateMockStorage::SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate)
{
	check(UE::PersistentState::ExpectedSlot == TargetSlotHandle);
	UE::PersistentState::CurrentWorldState = WorldState;
	UE::PersistentState::CurrentGameState = GameState;

	(void)CompletedDelegate.ExecuteIfBound();
	return {};
}

FGraphEventRef UPersistentStateMockStorage::LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName, FLoadCompletedDelegate CompletedDelegate)
{
	check(UE::PersistentState::ExpectedSlot == TargetSlotHandle);
	(void)CompletedDelegate.ExecuteIfBound(UE::PersistentState::CurrentGameState, UE::PersistentState::CurrentWorldState);

	return {};
}

void UPersistentStateMockStorage::RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle)
{
	SlotNames.RemoveSingleSwap(SlotHandle.GetSlotName());
}

UPersistentStateEmptyTestComponent::UPersistentStateEmptyTestComponent()
{
	ObjectName = TEXT("DynamicComponent");
}

void UPersistentStateEmptyTestComponent::PostLoad()
{
	Super::PostLoad();
	ObjectName = TEXT("StaticComponent");
}

void UPersistentStateEmptyTestComponent::InitializeComponent()
{
	Super::InitializeComponent();

	IPersistentStateObject::NotifyObjectInitialized(*this);
}

APersistentStateEmptyTestActor::APersistentStateEmptyTestActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	Component = CreateDefaultSubobject<UPersistentStateEmptyTestComponent>(TEXT("PersistentStateEmptyTestComponent"));
	ObjectName = TEXT("DynamicActor");
}

void APersistentStateEmptyTestActor::PostLoad()
{
	Super::PostLoad();
	ObjectName = TEXT("StaticActor");
}

void APersistentStateEmptyTestActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	IPersistentStateObject::NotifyObjectInitialized(*this);
}

void UPersistentStateSceneTestComponent::InitializeComponent()
{
	Super::InitializeComponent();

	IPersistentStateObject::NotifyObjectInitialized(*this);
}

APersistentStateTestActor::APersistentStateTestActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	SceneComponent = CreateDefaultSubobject<UPersistentStateSceneTestComponent>(TEXT("Test Scene Component"));
	SceneComponent->SetupAttachment(RootComponent);

	StaticComponent = CreateDefaultSubobject<UPersistentStateTestComponent>(TEXT("Test Component"));
}

bool UPersistentStateTestWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && FAutomationWorld::Exists();
}

bool UPersistentStateTestGameSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && FAutomationWorld::Exists();
}

void UPersistentStateTestGameSubsystem::PreSaveState()
{
	bPreSaveStateCalled = true;
}

void UPersistentStateTestGameSubsystem::PostSaveState()
{
	bPostSaveStateCalled = true;
}

void UPersistentStateTestGameSubsystem::PreLoadState()
{
	bPreLoadStateCalled = true;
}

void UPersistentStateTestGameSubsystem::PostLoadState()
{
	bPostLoadStateCalled = true;
}

APersistentStateTestGameMode::APersistentStateTestGameMode(const FObjectInitializer& Initializer): Super(Initializer)
{
	GameStateClass = APersistentStateTestGameState::StaticClass();
	PlayerControllerClass = APersistentStateTestPlayerController::StaticClass();
}

void APersistentStateTestPlayerController::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	
	IPersistentStateObject::NotifyObjectInitialized(*this);
}

bool UPersistentStateTestManager::ShouldCreateManager(UPersistentStateSubsystem& InSubsystem) const
{
	return Super::ShouldCreateManager(InSubsystem) && FAutomationWorld::Exists();
}

void UPersistentStateTestManager::Init(UPersistentStateSubsystem& InSubsystem)
{
	Super::Init(InSubsystem);

	bInitCalled = true;
	UE_CLOG(bCleanupCalled, LogPersistentState, Error, TEXT("%s: Init called after Cleanup"), *GetClass()->GetName());
}

void UPersistentStateTestManager::Cleanup(UPersistentStateSubsystem& InSubsystem)
{
	Super::Cleanup(InSubsystem);

	bCleanupCalled = true;
	UE_CLOG(!bInitCalled, LogPersistentState, Error, TEXT("%s: Cleanup called before Init"), *GetClass()->GetName());
}

void UPersistentStateTestManager::SaveState()
{
	Super::SaveState();

	bSaveStateCalled = true;
	
	UE_CLOG(!bInitCalled,	LogPersistentState, Error, TEXT("%s: SaveState called before Init"), *GetClass()->GetName());
	UE_CLOG(bCleanupCalled, LogPersistentState, Error, TEXT("%s: SaveState called after Cleanup"), *GetClass()->GetName());
}

void UPersistentStateTestManager::PreLoadState()
{
	Super::PreLoadState();

	bPreLoadStateCalled = true;
	UE_CLOG(!bInitCalled,	LogPersistentState, Error, TEXT("%s: PreLoadState called before Init"), *GetClass()->GetName());
	UE_CLOG(bCleanupCalled, LogPersistentState, Error, TEXT("%s: PreLoadState called after Cleanup"), *GetClass()->GetName());
	UE_CLOG(bPostLoadStateCalled, LogPersistentState, Error, TEXT("%s: PreLoadState called after PostLoadState"), *GetClass()->GetName());
}

void UPersistentStateTestManager::PostLoadState()
{
	Super::PostLoadState();

	bPostLoadStateCalled = true;
	UE_CLOG(!bInitCalled,	LogPersistentState, Error, TEXT("%s: PostLoadState called before Init"), *GetClass()->GetName());
	UE_CLOG(bCleanupCalled, LogPersistentState, Error, TEXT("%s: PostLoadState called after Cleanup"), *GetClass()->GetName());
	UE_CLOG(!bPreLoadStateCalled, LogPersistentState, Error, TEXT("%s: PostLoadState called before PreLoadState"), *GetClass()->GetName());
}

UPersistentStateTestWorldManager::UPersistentStateTestWorldManager()
{
	ManagerType = EManagerStorageType::World;
}

UPersistentStateTestGameManager::UPersistentStateTestGameManager()
{
	ManagerType = EManagerStorageType::Game;
}
