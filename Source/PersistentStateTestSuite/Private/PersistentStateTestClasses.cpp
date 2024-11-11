#include "PersistentStateTestClasses.h"

#include "PersistentStateSubsystem.h"

namespace UE::PersistentState
{
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

void UPersistentStateMockStorage::Init()
{
	Super::Init();
}

void UPersistentStateMockStorage::SaveWorldState(const FWorldStateSharedRef& WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle)
{
	check(UE::PersistentState::ExpectedSlot == TargetSlotHandle);
	UE::PersistentState::CurrentWorldState = WorldState;
	
	TSharedPtr<FPersistentStateSlot> Slot = FindSlot(TargetSlotHandle.GetSlotName());
	check(Slot.IsValid());

	Slot->SetLastSavedWorld(WorldState->World);
}

FWorldStateSharedRef UPersistentStateMockStorage::LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName)
{
	check(UE::PersistentState::ExpectedSlot == TargetSlotHandle);
	return UE::PersistentState::CurrentWorldState;
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

void UPersistentStateEmptyTestComponent::OnRegister()
{
	Super::OnRegister();

	IPersistentStateObject::NotifyInitialized(*this);
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

void APersistentStateEmptyTestActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	
	IPersistentStateObject::NotifyInitialized(*this);
}

APersistentStateTestActor::APersistentStateTestActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Scene Root"));
	SceneComponent = CreateDefaultSubobject<UPersistentStateSceneTestComponent>(TEXT("Test Scene Component"));
	SceneComponent->SetupAttachment(RootComponent);

	StaticComponent = CreateDefaultSubobject<UPersistentStateTestComponent>(TEXT("Test Component"));
}

void APersistentStateTestActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	IPersistentStateObject::NotifyInitialized(*this);
}

APersistentStateTestGameMode::APersistentStateTestGameMode()
{
	GameStateClass = APersistentStateTestGameState::StaticClass();
	PlayerControllerClass = APersistentStateTestPlayerController::StaticClass();
}

void APersistentStateTestGameMode::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	IPersistentStateObject::NotifyInitialized(*this);
}

void APersistentStateTestGameState::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	IPersistentStateObject::NotifyInitialized(*this);
}

void APersistentStateTestPlayerController::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	IPersistentStateObject::NotifyInitialized(*this);
}

void UPersistentStateTestComponent::OnRegister()
{
	Super::OnRegister();

	IPersistentStateObject::NotifyInitialized(*this);
}

void UPersistentStateSceneTestComponent::OnRegister()
{
	Super::OnRegister();
	
	IPersistentStateObject::NotifyInitialized(*this);
}
