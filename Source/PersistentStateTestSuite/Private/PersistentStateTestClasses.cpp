#include "PersistentStateTestClasses.h"

#include "AutomationWorld.h"
#include "PersistentStateSettings.h"
#include "PersistentStateSubsystem.h"

namespace UE::PersistentState
{
	FGameStateSharedRef CurrentGameState;
	FWorldStateSharedRef CurrentWorldState;
	FWorldStateSharedRef PrevWorldState;
	FPersistentStateSlotHandle ExpectedSlot;
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
	return FAutomationWorld::Exists();
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
