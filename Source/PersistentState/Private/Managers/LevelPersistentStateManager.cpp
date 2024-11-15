#include "Managers/LevelPersistentStateManager.h"

#include "PersistentStateInterface.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateStatics.h"
#include "Streaming/LevelStreamingDelegates.h"

FComponentPersistentState::FComponentPersistentState(UActorComponent* Component, FPersistentStateObjectId ComponentId, bool bStatic)
{
	WeakComponent = ComponentId;
	bComponentStatic = bStatic;
}

void FComponentPersistentState::InitWithStaticComponent(UActorComponent* Component, FPersistentStateObjectId ComponentId) const
{
	check(!bStateInitialized);
	check(WeakComponent == ComponentId && bComponentStatic == true);

	bStateInitialized = true;
	UE::PersistentState::MarkComponentStatic(*Component);
}

void FComponentPersistentState::InitWithDynamicComponent(UActorComponent* Component, FPersistentStateObjectId ComponentId) const
{
	check(!bStateInitialized);
	check(WeakComponent == ComponentId && bComponentStatic == false);

	bStateInitialized = true;
	UE::PersistentState::MarkComponentDynamic(*Component);
}

UActorComponent* FComponentPersistentState::CreateDynamicComponent(AActor* OwnerActor) const
{
	check(WeakComponent.IsValid());
	// verify that persistent state is valid for creating a dynamic component
	check(!bStateInitialized && bComponentStatic == false && bComponentSaved);
	check(ComponentClass.Get() != nullptr);
	
	UActorComponent* Component = NewObject<UActorComponent>(OwnerActor, ComponentClass.Get());
	// @todo: use FUObjectArray::AddUObjectCreateListener to assign serialized ID as early as possible
	// dynamic components should be spawned early enough to go into PostRegisterAllComponents
	// @todo: maybe we have an issue here? If dynamically created component is attached to the SCS created component
	// although we directly resolve attachments and after all components have been registered and initialized
	check(!Component->IsRegistered());

	FPersistentStateObjectId::AssignSerializedObjectId(Component, WeakComponent.GetObjectID());
	InitWithDynamicComponent(Component, WeakComponent);

	return Component;
}

void FComponentPersistentState::LoadComponent(ULevelPersistentStateManager& StateManager)
{
	check(bStateInitialized);
	if (bComponentSaved == false)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FComponentPersistentState_LoadComponent, PersistentStateChannel);
	
	UActorComponent* Component = WeakComponent.ResolveObject<UActorComponent>();
	check(Component && Component->IsRegistered());
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Component);
	State->PreLoadState();

	if (bHasTransform)
	{
		USceneComponent* SceneComponent = CastChecked<USceneComponent>(Component);
		
		if (AttachParentId.IsValid())
		{
			// @todo: LoadComponent loads and applies attachment information for any Saveable scene component
			// which does not seem reasonable for a lot of cases
			USceneComponent* AttachParent = AttachParentId.ResolveObject<USceneComponent>();
			check(AttachParent);
			
			SceneComponent->AttachToComponent(AttachParent, FAttachmentTransformRules::KeepWorldTransform, AttachSocketName);
			SceneComponent->SetRelativeTransform(ComponentTransform);
		}
		else
		{
			// component is not attached to anything, ComponentTransform is world transform
			SceneComponent->SetWorldTransform(ComponentTransform);
		}
	}
	
	UE::PersistentState::LoadObjectSaveGameProperties(*Component, SaveGameBunch);

	if (InstanceState.IsValid())
	{
		State->LoadCustomObjectState(InstanceState);	
	}

	State->PostLoadState();
}

void FComponentPersistentState::SaveComponent(ULevelPersistentStateManager& StateManager, bool bFromLevelStreaming)
{
	check(bStateInitialized);
	UActorComponent* Component = WeakComponent.ResolveObject<UActorComponent>();
	check(Component);
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Component);
	
	// PersistentState object can't transition from Saveable to not Saveable
	ensureAlwaysMsgf(static_cast<int32>(State->ShouldSaveState()) >= static_cast<int32>(bComponentSaved), TEXT("%s: component %s transitioned from Saveable to NotSaveable."),
		*FString(__FUNCTION__), *GetNameSafe(Component));

	// ensure that we won't transition from true to false
	bComponentSaved = bComponentSaved || State->ShouldSaveState();
	if (bComponentSaved == false)
	{
		return;
	}
	
	State->PreSaveState();
	
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FComponentPersistentState_SaveComponent, PersistentStateChannel);

	ComponentClass = Component->GetClass();
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
	{
		bHasTransform = true;
		if (USceneComponent* AttachParent = SceneComponent->GetAttachParent())
		{
			// @todo: SaveComponent saves and serializes attachment information for any scene component that implement IPersistentStateObject
			// which does not seem reasonable for a lot of cases
			// statically created components almost never detached/reattached to another component, so it makes sense (in general)
			// to store attachment information for dynamic components only
			// HOWEVER, we still want to know whether component is attached to something or not, so that we can determine
			// whether to save relative or absolute transform
			AttachSocketName = SceneComponent->GetAttachSocketName();
			AttachParentId = FPersistentStateObjectId::FindObjectId(AttachParent);
			ensureAlwaysMsgf(AttachParentId.IsValid(), TEXT("%s: saveable component [%s:%s] is attached to component [%s;%s], which does not have a stable id"),
				*FString(__FUNCTION__), *GetNameSafe(Component->GetOwner()), *Component->GetName(), *GetNameSafe(AttachParent->GetOwner()), *AttachParent->GetName());

			ComponentTransform = SceneComponent->GetRelativeTransform();
		}
		else
		{
			ComponentTransform = SceneComponent->GetComponentTransform();
		}
	}

	UE::PersistentState::SaveObjectSaveGameProperties(*Component, SaveGameBunch);
	InstanceState = State->SaveCustomObjectState();

	State->PostSaveState();

	if (bFromLevelStreaming)
	{
		// reset StateInitialized flag if it is caused by level streaming
		// otherwise next time level is loaded back, it will encounter actor/component state that is already "initialized"
		bStateInitialized = false;
	}
}

FPersistentStateObjectId FComponentPersistentState::GetComponentId() const
{
	return WeakComponent;
}

#if WITH_COMPONENT_CUSTOM_SERIALIZE
FArchive& operator<<(FArchive& Ar, const FComponentPersistentState& Value)
{
	Ar << Value.WeakComponent;
	Ar << Value.bComponentSaved;
	Ar << Value.bComponentStatic;
	Ar << Value.bSceneComponent;

	if (Value.bComponentStatic == false)
	{
		Ar << Value.ComponentClass;
		Ar << Value.ComponentTransform;
	}

	return Ar;
}

bool FComponentPersistentState::Serialize(FArchive& Ar)
{
	Ar << *this;
	return true;
}
#endif

FDynamicActorSpawnData::FDynamicActorSpawnData(AActor* InActor)
{
	check(InActor);
	ActorName = InActor->GetFName();
	ActorClass = InActor->GetClass();
	
	if (AActor* OwnerActor = InActor->GetOwner())
	{
		ActorOwnerId = FPersistentStateObjectId::FindObjectId(OwnerActor);
		check(ActorOwnerId.IsValid());
	}
	if (APawn* Instigator = InActor->GetInstigator())
	{
		ActorInstigatorId = FPersistentStateObjectId::FindObjectId(Instigator);
		check(ActorInstigatorId.IsValid());
	}
}

FActorPersistentState::FActorPersistentState(AActor* InActor, FPersistentStateObjectId InActorId, bool bStatic)
{
	WeakActor = InActorId;
	bActorStatic = bStatic;
}

void FActorPersistentState::InitWithStaticActor(AActor* Actor, FPersistentStateObjectId ActorId) const
{
	check(!bStateInitialized);
	check(WeakActor == ActorId);
	
	bStateInitialized = true;
	UE::PersistentState::MarkActorStatic(*Actor);
}

void FActorPersistentState::InitWithDynamicActor(AActor* Actor, FPersistentStateObjectId ActorId) const
{
	check(!bStateInitialized);
	check(WeakActor == ActorId);

	bStateInitialized = true;
	UE::PersistentState::MarkActorDynamic(*Actor);
}

AActor* FActorPersistentState::CreateDynamicActor(UWorld* World, FActorSpawnParameters& SpawnParams) const
{
	check(WeakActor.IsValid());
	// verify that persistent state can create a dynamic actor
	check(!bStateInitialized && bActorSaved && bActorStatic == false);
	check(SpawnData.IsValid());

	UClass* ActorClass = SpawnData.ActorClass.Get();
	check(ActorClass);

	check(SpawnParams.OverrideLevel);
	SpawnParams.Name = SpawnData.ActorName;
	// @todo: both Owner and Instigator should be resolved and applied BEFORE dynamic actor is spawned.
	// @todo: use FUObjectArray::AddUObjectCreateListener to assign serialized ID as early as possible
	SpawnParams.CustomPreSpawnInitalization = [this, Callback = SpawnParams.CustomPreSpawnInitalization](AActor* Actor)
	{
		// assign actor id before actor is fully spawned
		FPersistentStateObjectId::AssignSerializedObjectId(Actor, WeakActor.GetObjectID());
		InitWithDynamicActor(Actor, WeakActor);
		if (Callback)
		{
			Callback(Actor);
		}
	};
	
	// when dynamic actors are recreated for streaming levels, they're spawned before level is fully initialized and added to world via AddToWorld flow
	// actors spawned after level is initialized have a correct return value for IsNameStableForNetworking
	// however, before level is initialized, all actors in it "deemed" as network stable due to IsNetStartupActor implementation
	// This means that UE::PersistentState::GetStableName will give different values for actors spawned by gameplay
	// and actors spawned by state system which will mess up IDs for Native and SCS components
	// @see AActor::IsNetStartupActor()
	// this override ensures that newly created actor will return false when asked about whether its name is stable
	// @todo: MAJOR ISSUE: if anything else asks static actors whether its name is stable, it will return false. 
	FGuardValue_Bitfield(SpawnParams.OverrideLevel->bAlreadyInitializedNetworkActors, true);
	// actor transform is going to be overriden later by LoadActor call
	AActor* Actor = World->SpawnActor(ActorClass, &ActorTransform, SpawnParams);

	// @todo: GSpawnActorDeferredTransformCache is not cleared from a deferred spawned actor
	// PostActorConstruction() is executed as a part of level visibility request (via AddToWorld() flow)
	if (SpawnParams.bDeferConstruction)
	{
		FEditorScriptExecutionGuard ScriptGuard;
		Actor->ExecuteConstruction(ActorTransform, nullptr, nullptr, false);
	}
	
	check(Actor && Actor->HasActorRegisteredAllComponents() && !Actor->IsActorInitialized() && !Actor->HasActorBegunPlay());

	return Actor;
}

void FActorPersistentState::LoadActor(ULevelPersistentStateManager& StateManager)
{
	check(bStateInitialized);
	if (bActorSaved == false)
	{
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FActorPersistentState_LoadActor, PersistentStateChannel);
	
	AActor* Actor = WeakActor.ResolveObject<AActor>();
	check(Actor != nullptr && Actor->IsActorInitialized() && !Actor->HasActorBegunPlay());
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Actor);
	State->PreLoadState();

	// load owner and instigator for a dynamic actor
	if (bActorStatic == false)
	{
		// @todo: both owner and instigator should be resolved and applied BEFORE dynamic actor is spawned.
		if (SpawnData.HasOwner())
		{
			AActor* Owner = SpawnData.ActorOwnerId.ResolveObject<AActor>();
			check(Owner);
			
			Actor->SetOwner(Owner);
		}
		if (SpawnData.HasInstigator())
		{
			APawn* Instigator = SpawnData.ActorInstigatorId.ResolveObject<APawn>();
			check(Instigator);

			Actor->SetInstigator(Instigator);
		}
	}

	// restore actor components
	for (FComponentPersistentState& ComponentState: Components)
	{
		ComponentState.LoadComponent(StateManager);
	}

	// restore actor transform
	if (bHasTransform)
	{
		if (AttachParentId.IsValid())
		{
			// actor is attached to other scene component, it means actor transform is relative
			USceneComponent* AttachParent = AttachParentId.ResolveObject<USceneComponent>();
			check(AttachParent != nullptr);

			AttachSocketName = Actor->GetAttachParentSocketName();

			Actor->AttachToComponent(AttachParent, FAttachmentTransformRules::KeepWorldTransform, AttachSocketName);
			Actor->SetActorRelativeTransform(ActorTransform);
		}
		else
		{
			// actor is not attached to anything, transform is in world space
			Actor->SetActorTransform(ActorTransform);
		}
	}
	
	UE::PersistentState::LoadObjectSaveGameProperties(*Actor, SaveGameBunch);

	if (InstanceState.IsValid())
	{
		State->LoadCustomObjectState(InstanceState);
	}

	State->PostLoadState();
}

void FActorPersistentState::SaveActor(ULevelPersistentStateManager& StateManager, bool bFromLevelStreaming)
{
	check(bStateInitialized);
	AActor* Actor = WeakActor.ResolveObject<AActor>();
	check(Actor);
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Actor);
	
	// PersistentState object can't transition from Saveable to not Saveable
	ensureAlwaysMsgf(static_cast<int32>(State->ShouldSaveState()) >= static_cast<int32>(bActorSaved), TEXT("%s: actor %s transitioned from Saveable to NotSaveable."),
		*FString(__FUNCTION__), *GetNameSafe(Actor));

	// ensure that we won't transition from true to false
	bActorSaved = bActorSaved || State->ShouldSaveState();

	if (bActorSaved == false)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FActorPersistentState_SaveActor, PersistentStateChannel);

	State->PreSaveState();

	// update list of actor components
	StateManager.UpdateActorComponents(*Actor, *this);

	// save component states
	for (FComponentPersistentState& ComponentState: Components)
	{
		ComponentState.SaveComponent(StateManager, bFromLevelStreaming);
	}
	
	SpawnData = FDynamicActorSpawnData{Actor};
	// some actors don't have a root component
	if (USceneComponent* RootComponent = Actor->GetRootComponent())
	{
		bHasTransform = true;
		if (USceneComponent* AttachParent = RootComponent->GetAttachParent())
		{
			AttachSocketName = RootComponent->GetAttachSocketName();
			AttachParentId = FPersistentStateObjectId::FindObjectId(AttachParent);
			ensureAlwaysMsgf(AttachParentId.IsValid(), TEXT("%s: saveable actor [%s] is attached to component [%s;%s], which does not have a stable id"),
				*FString(__FUNCTION__), *Actor->GetName(), *GetNameSafe(AttachParent->GetOwner()), *AttachParent->GetName());
			
			ActorTransform = RootComponent->GetRelativeTransform();
		}
		else
		{
			ActorTransform = Actor->GetActorTransform();
		}
	}

	UE::PersistentState::SaveObjectSaveGameProperties(*Actor, SaveGameBunch);
	InstanceState = State->SaveCustomObjectState();

	State->PostSaveState();
	
	if (bFromLevelStreaming)
	{
		// reset StateInitialized flag if it is caused by level streaming
		// otherwise next time level is loaded back, it will encounter actor/component state that is already "initialized"
		bStateInitialized = false;
	}
}

FPersistentStateObjectId FActorPersistentState::GetActorId() const
{
	return WeakActor;
}

const FComponentPersistentState* FActorPersistentState::GetComponentState(const FPersistentStateObjectId& ComponentId) const
{
	return const_cast<FActorPersistentState*>(this)->GetComponentState(ComponentId);
}

FComponentPersistentState* FActorPersistentState::GetComponentState(const FPersistentStateObjectId& ComponentId)
{
	return Components.FindByPredicate([&ComponentId](const FComponentPersistentState& ComponentState)
	{
		return ComponentState.WeakComponent == ComponentId;
	});
}

FComponentPersistentState* FActorPersistentState::CreateComponentState(UActorComponent* Component, const FPersistentStateObjectId& ComponentId, bool bStatic)
{
	check(GetComponentState(ComponentId) == nullptr);
	FComponentPersistentState* ComponentState = &Components.Add_GetRef(FComponentPersistentState{Component, ComponentId, bStatic});
	if (bStatic)
	{
		ComponentState->InitWithStaticComponent(Component, ComponentId);
	}
	else
	{
		ComponentState->InitWithDynamicComponent(Component, ComponentId);
	}
	
	return ComponentState;
}

FLevelPersistentState::FLevelPersistentState(const ULevel* Level)
	: LevelId(FPersistentStateObjectId::CreateStaticObjectId(Level))
{
	check(LevelId.IsValid());
}

bool FLevelPersistentState::HasActor(const FPersistentStateObjectId& ActorId) const
{
	return Actors.Contains(ActorId);
}

bool FLevelPersistentState::HasComponent(const FPersistentStateObjectId& ActorId, const FPersistentStateObjectId& ComponentId) const
{
	if (const FActorPersistentState* ActorState = Actors.Find(ActorId))
	{
		if (ActorState->Components.ContainsByPredicate([&ComponentId](const FComponentPersistentState& ComponentState)
		{
			return ComponentState.WeakComponent == ComponentId;
		}))
		{
			return true;
		}
	}

	return false;
}

const FActorPersistentState* FLevelPersistentState::GetActorState(const FPersistentStateObjectId& ActorId) const
{
	return Actors.Find(ActorId);
}

FActorPersistentState* FLevelPersistentState::GetActorState(const FPersistentStateObjectId& ActorId)
{
	return Actors.Find(ActorId);
}

FActorPersistentState* FLevelPersistentState::CreateActorState(AActor* Actor, const FPersistentStateObjectId& ActorId, bool bStatic)
{
	check(GetActorState(ActorId) == nullptr);
	FActorPersistentState* ActorState = &Actors.Add(ActorId, FActorPersistentState{Actor, ActorId, bStatic});
	if (bStatic)
	{
		ActorState->InitWithStaticActor(Actor, ActorId);
	}
	else
	{
		ActorState->InitWithDynamicActor(Actor, ActorId);
	}

	return ActorState;
}

void ULevelPersistentStateManager::Init(UWorld* World)
{
	Super::Init(World);
	
	check(World->bIsWorldInitialized && !World->bActorsInitialized);

	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ThisClass::OnLevelLoaded);
	LevelVisibleHandle = FLevelStreamingDelegates::OnLevelBeginMakingVisible.AddUObject(this, &ThisClass::OnLevelBecomeVisible);
	LevelInvisibleHandle = FLevelStreamingDelegates::OnLevelBeginMakingInvisible.AddUObject(this, &ThisClass::OnLevelBecomeInvisible);
	
	ActorsInitializedHandle = World->OnActorsInitialized.AddUObject(this, &ThisClass::OnWorldInitializedActors);
	ActorDestroyedHandle = World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &ThisClass::OnActorDestroyed));
	
	LoadGameState();
}

void ULevelPersistentStateManager::Cleanup(UWorld* World)
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FLevelStreamingDelegates::OnLevelBeginMakingVisible.Remove(LevelVisibleHandle);
	FLevelStreamingDelegates::OnLevelBeginMakingInvisible.Remove(LevelInvisibleHandle);
	
	World->RemoveOnActorDestroyededHandler(ActorDestroyedHandle);

	Super::Cleanup(World);
}

void ULevelPersistentStateManager::NotifyInitialized(UObject& Object)
{
	Super::NotifyInitialized(Object);

	// @todo: update comment
	// @note: this function purpose is to catch dynamic objects created at runtime, that are not visible to state system
	// via existing engine callbacks.
	// It is triggered by user explicitly calling IPersistentStateObject::NotifyInitialized from the object itself
	// at the appropriate time.
	// Currently, it is used to catch runtime created components, both on static and dynamic actors
	
	if (AActor* Actor = Cast<AActor>(&Object))
	{
		OnActorInitialized(Actor);
		return;
	}

	UActorComponent* Component = Cast<UActorComponent>(&Object);
	if (Component == nullptr)
	{
		return;
	}

	check(Component->HasBeenInitialized());
	AActor* OwnerActor = Component->GetOwner();
	check(OwnerActor != nullptr);
	
	FPersistentStateObjectId ComponentId = FPersistentStateObjectId::FindObjectId(Component);
	if (ComponentId.IsValid())
	{
		check(UE::PersistentState::IsStaticComponent(*Component) || UE::PersistentState::IsDynamicComponent(*Component));
		// component is already located and initialized with persistent state system
#if WITH_EDITOR
		if (!OwnerActor->Implements<UPersistentStateObject>())
		{
			// static component added to the actor that doesn't implement persistent state interface, which means component won't be saved/loaded.
			// notify user about it, as it is definitely not an expected behavior.
			ensureAlwaysMsgf(false, TEXT("%s: static component %s that supports persistent state created on the actor %s that doesn't. "),
				*FString(__FUNCTION__), *Object.GetClass()->GetName(), *OwnerActor->GetClass()->GetName());
		}
#endif
		return;
	}
	
	if (!OwnerActor->Implements<UPersistentStateObject>())
	{
		// runtime created component added to the actor that doesn't implement persistent state interface, which means component won't be saved/loaded.
		// notify user about it, as it is definitely not an expected behavior.
		ensureAlwaysMsgf(false, TEXT("%s: dynamic component %s that supports persistent state created on the actor %s that doesn't. "),
			*FString(__FUNCTION__), *Object.GetClass()->GetName(), *OwnerActor->GetClass()->GetName());
		return;
	}

	if (!OwnerActor->IsActorInitialized())
	{
		// component will be initialized as a part of actor initialization, so we can skip it
		UE_LOG(LogPersistentState, Verbose, TEXT("%s: skipping initialized component because actor is not fully initialized"), *FString(__FUNCTION__));
		return;
	}
	
	FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(OwnerActor);
	if (!ActorId.IsValid())
	{
		ensureAlwaysMsgf(false, TEXT("%s: actor %s implements persistent state interface but was not discovered by state system."),
			*FString(__FUNCTION__), *OwnerActor->GetClass()->GetName());
		return;
	}
	
	ComponentId = FPersistentStateObjectId::CreateStaticObjectId(Component);
	const bool bStatic = ComponentId.IsValid();
	if (!bStatic)
	{
		// this is currently a bug trap - fully dynamic actor is spawned before AddToWorld flow is finished
		// we can fully rely on IsNameStableForNetworking after ULevel::InitializeNetworkActors
		// however, before level is initialized, all actors in it "deemed" as network stable due to IsNetStartupActor implementation
		// @see AActor::IsNetStartupActor()
		check(OwnerActor->GetLevel() && OwnerActor->GetLevel()->bAlreadyInitializedNetworkActors == true);
		ComponentId = FPersistentStateObjectId::CreateDynamicObjectId(Component);
	}

	FLevelPersistentState* LevelState = GetLevelState(OwnerActor->GetLevel());
	check(LevelState);
	FActorPersistentState* ActorState = LevelState->GetActorState(ActorId);
	check(ActorState);

	// create component state for a runtime created component
	ActorState->CreateComponentState(Component, ComponentId, bStatic);
}

void ULevelPersistentStateManager::FLevelRestoreContext::AddCreatedActor(const FActorPersistentState& ActorState)
{
	check(ActorState.IsDynamic() && ActorState.IsInitialized());
	CreatedActors.Add(ActorState.GetActorId());
}

void ULevelPersistentStateManager::FLevelRestoreContext::AddCreatedComponent(const FComponentPersistentState& ComponentState)
{
	check(ComponentState.IsDynamic() && ComponentState.IsInitialized());
	CreatedComponents.Add(ComponentState.GetComponentId());
}

void ULevelPersistentStateManager::LoadGameState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(ULevelPersistentStateManager_LoadGameState, PersistentStateChannel);

	FLevelRestoreContext Context{};

	constexpr bool bFromLevelStreaming = false;
	RestoreLevel(CurrentWorld->PersistentLevel, Context, bFromLevelStreaming);
	for (ULevelStreaming* LevelStreaming: CurrentWorld->GetStreamingLevels())
	{
		if (ULevel* Level = LevelStreaming->GetLoadedLevel())
		{
			RestoreLevel(Level, Context, bFromLevelStreaming);
		}
	}
}

void ULevelPersistentStateManager::SaveGameState()
{
	Super::SaveGameState();
	
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(ULevelPersistentStateManager_SaveGameState, PersistentStateChannel);
	
	for (auto& [LevelName, LevelState]: Levels)
	{
		if (LoadedLevels.Contains(LevelName))
		{
			// save only loaded levels
			constexpr bool bFromLevelStreaming = false;
			SaveLevel(LevelState, bFromLevelStreaming);
		}
	}
}

void ULevelPersistentStateManager::SaveLevel(FLevelPersistentState& LevelState, bool bFromLevelStreaming)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(ULevelPersistentStateManager_SaveLevel, PersistentStateChannel);
	check(LoadedLevels.Contains(LevelState.LevelId));
	
	TArray<FPersistentStateObjectId, TInlineAllocator<16>> OutdatedActors;
	for (auto& [ActorId, ActorState]: LevelState.Actors)
	{
		// @todo: what do we do with static actors, that were not found?
		// For PIE this is understandable, because level changes between sessions which causes old save to accumulate
		// static actors that doesn't exist.
		// In packaged game it might be a bug/issue with a state system, although game can remove static actors from the level
		// between updates and doesn't care about state of those actors
		if (ActorState.IsInitialized())
		{
			ActorState.SaveActor(*this, bFromLevelStreaming);
		}
		else
		{
			// @todo: dynamic actors are never outdated, we should provide some way to detect/remove them for game updates
			// only static actors can be "automatically" outdated due to level change
			// dynamically created actors should be always recreated by the state manager
			check(ActorState.IsStatic());
			OutdatedActors.Add(ActorId);
		}
	}

	// remove outdated actor states
	OutdatedObjects.Append(OutdatedActors);
	for (const FPersistentStateObjectId& ActorId: OutdatedActors)
	{
		LevelState.Actors.Remove(ActorId);
	}
}

void ULevelPersistentStateManager::RestoreLevel(ULevel* Level, FLevelRestoreContext& Context, bool bFromLevelStreaming)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(ULevelPersistentStateManager_RestoreLevel, PersistentStateChannel);
	// we should not process level if actor initialization/registration/loading is currently going on
	check(Level && CanRestoreLevel());
	// verify that we don't process the same level twice
	// @todo: stable name has collision for runtime created level instances
	const FPersistentStateObjectId LevelId = FPersistentStateObjectId::CreateStaticObjectId(Level);
	check(LevelId.IsValid() && !LoadedLevels.Contains(LevelId));
	LoadedLevels.Add(LevelId);
	
	FLevelPersistentState& LevelState = GetOrCreateLevelState(Level);
	
	static TArray<AActor*> PendingDestroyActors;
	PendingDestroyActors.Reset();
	
	// create object identifiers for level static actors
	for (AActor* Actor: Level->Actors)
	{
		if (Actor == nullptr)
		{
			continue;
		}
		check(!Actor->IsActorInitialized());
		
		// create and assign actor id from stable name for static actors, so that
		// persistent state system can indirectly track static actors and components
		// this is mostly required for things like attachment (scene root component) or ownership
		// @todo: create id only for actors that implement interface
		if (!Actor->Implements<UPersistentStateObject>())
		{
			// create static ID for actors present on the level. If the level is loaded first time
			// is doesn't have dynamically created actors, and InitializeNetworkActors has to be called only once
			FPersistentStateObjectId ActorId = FPersistentStateObjectId::CreateStaticObjectId(Actor);
			continue;
		}

		FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(Actor);
		if (ActorId.IsValid())
		{
			check(ActorId.ResolveObject<AActor>() == Actor);
			FActorPersistentState* ActorState = LevelState.GetActorState(ActorId);
			check(ActorState != nullptr && !ActorState->IsInitialized());

			if (ActorState->IsStatic())
			{
				ActorState->InitWithStaticActor(Actor, ActorId);
			}
			else
			{
				ActorState->InitWithDynamicActor(Actor, ActorId);
			}
			RestoreActorComponents(*Actor, *ActorState, Context);
			
			continue;
		}
		
		ActorId = FPersistentStateObjectId::CreateStaticObjectId(Actor);
		check(ActorId.IsValid());
		
		TGuardValue ActorScope{CurrentlyProcessedActor, Actor};
		
		if (IsDestroyedObject(ActorId))
		{
			// actor has been destroyed, verify that actor state doesn't exist and skip processing components
			check(LevelState.GetActorState(ActorId) == nullptr);
			PendingDestroyActors.Add(Actor);
			continue;
		}
		
		FActorPersistentState* ActorState = LevelState.GetActorState(ActorId);
		if (ActorState != nullptr)
		{
			check(ActorState->IsStatic());
			check(ActorId == ActorState->WeakActor);

			// re-initialize actor state with a static actor
			ActorState->InitWithStaticActor(Actor, ActorId);
		}
		else
		{
			// create actor state for the static actor for the first time is it loaded
			constexpr bool bStatic = true;
			ActorState = LevelState.CreateActorState(Actor, ActorId, bStatic);
		}

		RestoreActorComponents(*Actor, *ActorState, Context);
	}
	
	for (AActor* Actor: PendingDestroyActors)
	{
		TGuardValue ActorScope{CurrentlyProcessedActor, Actor};
		Actor->Destroy();
	}
	
	if (bWorldInitializedActors)
	{
		RestoreDynamicActors(Level, LevelState, Context);
	}
}

void ULevelPersistentStateManager::RestoreDynamicActors(ULevel* Level, FLevelPersistentState& LevelState, FLevelRestoreContext& Context)
{
	UWorld* World = Level->GetWorld();
	
	FActorSpawnParameters SpawnParams{};
	SpawnParams.bNoFail = true;
	SpawnParams.OverrideLevel = Level;
	// defer OnActorConstruction for dynamic actors spawned inside streamed levels (added via AddToWorld flow)
	// ExecuteConstruction() is called explicitly to spawn SCS components
	SpawnParams.bDeferConstruction = Level->bIsAssociatingLevel;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	
	FGuardValue_Bitfield(bRestoringDynamicActors, true);

	TArray<FPersistentStateObjectId, TInlineAllocator<16>> OutdatedActors;
	for (auto It = LevelState.Actors.CreateIterator(); It; ++It)
	{
		const FPersistentStateObjectId& ActorId = It.Key();
		FActorPersistentState& ActorState = It.Value();
		
		if (ActorState.IsStatic() || ActorState.IsInitialized())
		{
			continue;
		}
		
		if (!ActorState.IsSaved())
		{
			// remove dynamic actor state because it cannot be re-created
			// @todo: resolve such things in Load PostSerialize
			It.RemoveCurrent();
			return;
		}

		// invalid dynamic actor, probably caused by a cpp/blueprint class being renamed or removed
		if (ActorState.IsOutdated())
		{
			OutdatedActors.Add(ActorId);
			It.RemoveCurrent();
			continue;
		}

		AActor* DynamicActor = ActorState.WeakActor.ResolveObject<AActor>();
		if (DynamicActor == nullptr)
		{
			SpawnParams.CustomPreSpawnInitalization = [&ActorState, &Context, this](AActor* DynamicActor)
			{
				CurrentlyProcessedActor = DynamicActor;
				RestoreActorComponents(*DynamicActor, ActorState, Context);
			};
			// dynamically spawned actors have fully registered components after spawn regardless of the owning world state
			// we process static native components and spawn dynamically created components in PreSpawnInitialization callback
			// SCS spawned components are going to be processed right after actor initialization with NotifyInitialized() callback
			DynamicActor = ActorState.CreateDynamicActor(World, SpawnParams);
			check(DynamicActor);

			Context.AddCreatedActor(ActorState);
			CurrentlyProcessedActor = nullptr;
		}
	}
	CurrentlyProcessedActor = nullptr;
	
	OutdatedObjects.Append(OutdatedActors);

	if (CanInitializeActors())
	{
		ProcessPendingRegisterActors(Context);
	}
}

void ULevelPersistentStateManager::RestoreActorComponents(AActor& Actor, FActorPersistentState& ActorState, FLevelRestoreContext& Context)
{
	static TInlineComponentArray<UActorComponent*> PendingDestroyComponents;
	PendingDestroyComponents.Reset();

	// statically created components can live both on map loaded and runtime created actors
	// in both cases component name should be a stable name. For dynamic actors, stable name is a combination of
	// already created ActorId and component name (which is unique in the "context" of dynamic actor)
	// 
	// for map loaded actors, RestoreStaticComponents is called twice:
	// 1. During AddToWorld map flow, or after world initialization for persistent level. It picks up default components
	// 2. After full actor registration e.g. PostRegisterAllComponents is called. It picks up blueprint created components via SCS
	//
	// for runtime created actors, RestoreStaticComponents called once after actor registration.

	// process statically created components
	for (UActorComponent* Component: Actor.GetComponents())
	{
		if (Component == nullptr)
		{
			continue;
		}

		// create and assign component id from a stable name so persistent state system can track stable actor components (for attachment and other purposes)
		// @todo: create id only for components that implement interface
		if (!Component->Implements<UPersistentStateObject>())
		{
			FPersistentStateObjectId ComponentId = FPersistentStateObjectId::CreateStaticObjectId(Component);
			continue;
		}

		FPersistentStateObjectId ComponentId = FPersistentStateObjectId::FindObjectId(Component);
		if (ComponentId.IsValid())
		{
			check(ComponentId.ResolveObject<UActorComponent>() == Component);
			FComponentPersistentState* ComponentState = ActorState.GetComponentState(ComponentId);
			check(ComponentState != nullptr && !ComponentState->IsInitialized());

			if (ComponentState->IsStatic())
			{
				ComponentState->InitWithStaticComponent(Component, ComponentId);	
			}
			else
			{
				ComponentState->InitWithDynamicComponent(Component, ComponentId);	
			}
			
			continue;
		}
		
		ComponentId = FPersistentStateObjectId::CreateStaticObjectId(Component);
		if (!ComponentId.IsValid())
		{
			ensureAlwaysMsgf(false, TEXT("%s: found dynamic component %s on actor %s created during actor initialization.")
				TEXT("PersistentState currently doesn't support saveable components created during registration."),
				 *FString(__FUNCTION__), *Component->GetName(), *Actor.GetName());
			continue;
		}
		
		if (IsDestroyedObject(ComponentId))
		{
			// static component has been explicitly destroyed, verify that component state doesn't exist
			check(ActorState.GetComponentState(ComponentId) == nullptr);
			PendingDestroyComponents.Add(Component);
			continue;
		}
			
		const FComponentPersistentState* ComponentState = ActorState.GetComponentState(ComponentId);
		// RestoreActorComponents can be processed twice. Second time is designed to catch blueprint created components via SCS,
		// so it is ok if component state is already initialized with component
		if (ComponentState != nullptr)
		{
			if (!ComponentState->IsInitialized())
			{
				check(ComponentState->IsStatic());
				check(ComponentId == ComponentState->WeakComponent);
				
				ComponentState->InitWithStaticComponent(Component, ComponentId);
			}
			else
			{
				check(ComponentState->GetComponentId() == ComponentId);
			}
		}
		else
		{
			// create component state for the static component for the first time it is loaded
			constexpr bool bStatic = true;
			ComponentState = ActorState.CreateComponentState(Component, ComponentId, bStatic);
		}
	}

	// process pending destroy components
	for (UActorComponent* Component: PendingDestroyComponents)
	{
		Component->DestroyComponent();
	}

	// spawn dynamic components created on a static actor during runtime
	for (auto It = ActorState.Components.CreateIterator(); It; ++It)
	{
		const FComponentPersistentState& ComponentState = *It;
		// skip static and already initialized components
		if (ComponentState.IsStatic() || ComponentState.IsInitialized())
		{
			continue;
		}
		
		if (!ComponentState.IsSaved())
		{
			// remove dynamic component state because it cannot be re-created
			// @todo: resolve such things in Load PostSerialize
			It.RemoveCurrent();
			return;
		}

		// outdated component, probably caused by cpp/blueprint class being renamed or removed
		if (ComponentState.IsOutdated())
		{
			// remove outdated component
			OutdatedObjects.Add(ComponentState.GetComponentId());
			It.RemoveCurrent();
			continue;
		}
		
		UActorComponent* Component = ComponentState.CreateDynamicComponent(&Actor);
		check(Component);

		Context.AddCreatedComponent(ComponentState);
	}
}

void ULevelPersistentStateManager::UpdateActorComponents(AActor& Actor, FActorPersistentState& ActorState)
{
	TInlineComponentArray<UActorComponent*> OwnedComponents;
	Actor.GetComponents(OwnedComponents);
	
	// we process dynamic created/destroyed components during actor save due to lack of events for creating/destroying actor components
	// 1. Detect destroyed components - remove component state and mark static components as destroyed
	// 2. Detected newly created components - create dynamic component state for them
	// detect component states that reference destroyed components
	for (int32 ComponentStateIndex = ActorState.Components.Num() - 1; ComponentStateIndex >= 0; --ComponentStateIndex)
	{
		FComponentPersistentState& ComponentState = ActorState.Components[ComponentStateIndex];
		FPersistentStateObjectId ComponentId = ComponentState.GetComponentId();
		check(ComponentId.IsValid());

		UActorComponent* ActorComponent = ComponentState.WeakComponent.ResolveObject<UActorComponent>();
		if (!IsValid(ActorComponent))
		{
			if (ComponentState.IsStatic())
			{
				// mark static component as destroyed
				AddDestroyedObject(ComponentId);	
			}

			// remove destroyed component from the component list
			ActorState.Components.RemoveAtSwap(ComponentStateIndex);
		}
	}

	// detect dynamically created components
	for (UActorComponent* ActorComponent: OwnedComponents)
	{
		if (ActorComponent && ActorComponent->Implements<UPersistentStateObject>())
		{
			FPersistentStateObjectId ComponentId = FPersistentStateObjectId::FindObjectId(ActorComponent);
			if (!ComponentId.IsValid())
			{
				ComponentId = FPersistentStateObjectId::CreateDynamicObjectId(ActorComponent);
				check(ComponentId.IsValid());
				
				constexpr bool bStatic = false;
				ActorState.CreateComponentState(ActorComponent, ComponentId, bStatic);
			}
#if WITH_EDITOR
			else
			{
				// check that component guid corresponds to a valid component state
				check(ActorState.GetComponentState(ComponentId) != nullptr);
			}
#endif
		}
	}
}

const FLevelPersistentState* ULevelPersistentStateManager::GetLevelState(ULevel* Level) const
{
	return Levels.Find(FPersistentStateObjectId::CreateStaticObjectId(Level));
}

FLevelPersistentState* ULevelPersistentStateManager::GetLevelState(ULevel* Level)
{
	return Levels.Find(FPersistentStateObjectId::CreateStaticObjectId(Level));
}

FLevelPersistentState& ULevelPersistentStateManager::GetOrCreateLevelState(ULevel* Level)
{
	const FPersistentStateObjectId LevelId = FPersistentStateObjectId::CreateStaticObjectId(Level);
	return Levels.FindOrAdd(LevelId, FLevelPersistentState{LevelId});
}

void ULevelPersistentStateManager::OnWorldInitializedActors(const FActorsInitializedParams& InitParams)
{
	if (InitParams.World != CurrentWorld)
	{
		return;
	}
	
	FLevelRestoreContext Context{};
	
	// restore dynamic actors for always loaded levels, BEFORE settings that world initialized actors
	// this way both static and dynamic actors will end up in PendingRegisterActors list, and we can load them all at once
	RestoreDynamicActors(CurrentWorld->PersistentLevel, *GetLevelState(CurrentWorld->PersistentLevel), Context);
	for (ULevelStreaming* LevelStreaming: CurrentWorld->GetStreamingLevels())
	{
		if (ULevel* Level = LevelStreaming->GetLoadedLevel())
		{
			RestoreDynamicActors(Level, *GetLevelState(Level), Context);
		}
	}

	bWorldInitializedActors = true;
	ProcessPendingRegisterActors(Context);
}

void ULevelPersistentStateManager::OnLevelLoaded(ULevel* LoadedLevel, UWorld* World)
{
	if (World == CurrentWorld)
	{
		
	}
}

void ULevelPersistentStateManager::ProcessPendingRegisterActors(FLevelRestoreContext& Context)
{
	check(CanInitializeActors() == true);
	
	TArray<FActorPersistentState*> ActorStateList;

	{
		FGuardValue_Bitfield(bRegisteringActors, true);
		for (int32 Index = 0; Index < PendingRegisterActors.Num(); ++Index)
        {
        	// process pending register actors until it is empty, because loading actors may cause new actors to spawn
			FActorPersistentState* ActorState = InitializeActor(PendingRegisterActors[Index], Context);
			ActorStateList.Add(ActorState);
        }
		PendingRegisterActors.Reset();
	}

	{
		FGuardValue_Bitfield(bLoadingActors, true);
		for (FActorPersistentState* ActorState: ActorStateList)
		{
			ActorState->LoadActor(*this);
		}
	}
}

void ULevelPersistentStateManager::OnLevelBecomeVisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel)
{
	if (World == CurrentWorld)
	{
		constexpr bool bFromLevelStreaming = true;
		
		FLevelRestoreContext Context{};
		RestoreLevel(LoadedLevel, Context, bFromLevelStreaming);
		ProcessPendingRegisterActors(Context);
	}
}

void ULevelPersistentStateManager::OnLevelBecomeInvisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel)
{
	if (World == CurrentWorld)
	{
		if (FLevelPersistentState* LevelState = GetLevelState(LoadedLevel))
		{
			constexpr bool bFromLevelStreaming = true;
			SaveLevel(*LevelState, bFromLevelStreaming);
		}

		const FPersistentStateObjectId LevelId = FPersistentStateObjectId::CreateStaticObjectId(LoadedLevel);
		check(LevelId.IsValid() && LoadedLevels.Contains(LevelId));
		
		LoadedLevels.Remove(LevelId);
	}
}

FActorPersistentState* ULevelPersistentStateManager::InitializeActor(AActor* Actor, FLevelRestoreContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(ULevelPersistentStateManager_RegisterActor, PersistentStateChannel);
	check(Actor->IsActorInitialized() && !Actor->HasActorBegunPlay());
	
	FLevelPersistentState* LevelState = GetLevelState(Actor->GetLevel());
	check(LevelState);

	// @todo: tick based processing to resolve everything all at once?
	// @todo: Example: GameMode cached a reference to GameState and GameMode and is registered first. LoadActor will not restore reference to GameState
	// @todo: Example: GameState holds a reference to a PlayerController. Player controller IS NOT yet created
	// Global actors that spawn dynamically but "appear" as static (e.g. they have a stable name and state system doesn't respawn them) should primarily
	// live as a part of persistent level.
	
	FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(Actor);
	if (ActorId.IsValid())
	{
		FActorPersistentState* ActorState = ActorState = LevelState->GetActorState(ActorId);
		check(ActorState != nullptr && ActorState->IsInitialized());
		
		return ActorState;
	}

	// actor has not been discovered by state system in RestoreLevel, which means it was spawned at runtime
	// dynamically created actor outside of persistent state system scope can be anything: game mode, player controller, pawn, other dynamic actors, etc.
	// actor can also be spawned during level visibility request (AddToWorld) as part of another actor registration
	//
	// actor can be spawned dynamically, but if it has a stable name we consider it static.
	// Gameplay code is responsible for respawning static actors, not the state system
	ActorId = FPersistentStateObjectId::CreateStaticObjectId(Actor);
	const bool bActorStatic = ActorId.IsValid();
	
	if (!ActorId.IsValid())
	{
		// this is currently a bug trap - fully dynamic actor is spawned before AddToWorld flow is finished
		// actors spawned after level is initialized have a correct return value for IsNameStableForNetworking
		// however, before level is initialized, all actors in it "deemed" as network stable due to IsNetStartupActor implementation
		// @see AActor::IsNetStartupActor()
		check(Actor->GetLevel() && Actor->GetLevel()->bAlreadyInitializedNetworkActors == true);
		// actor is fully dynamic
		ActorId = FPersistentStateObjectId::CreateDynamicObjectId(Actor);
	}

	check(ActorId.IsValid());
	FActorPersistentState* ActorState = LevelState->GetActorState(ActorId);
	if (ActorState != nullptr)
	{
		check(bActorStatic == ActorState->IsStatic());
		// re-init existing actor state
		if (bActorStatic)
		{
			ActorState->InitWithStaticActor(Actor, ActorId);
		}
		else
		{
			ActorState->InitWithDynamicActor(Actor, ActorId);
		}
	}
	else
	{
		// create persistent state for a new actor, either static or dynamic
		ActorState = LevelState->CreateActorState(Actor, ActorId, bActorStatic);
	}

	// do a full component discovery
	RestoreActorComponents(*Actor, *ActorState, Context);
	return ActorState;
}

void ULevelPersistentStateManager::OnActorInitialized(AActor* Actor)
{
	check(Actor != nullptr && Actor->IsActorInitialized());
	if (!Actor->Implements<UPersistentStateObject>())
	{
		return;
	}
	
	if (CanInitializeActors() == false)
	{
		// actor registration is blocked, because persistent state system is processing level or waiting for
		// world actor initialization to complete
		// wait until world is fully initialized, otherwise premature load will break references between core game actors
		// wati for all dynamic actors to spawn for the current level, otherwise premature load will break references between core game actors
		PendingRegisterActors.Add(Actor);
		return;
	}
	
	FLevelRestoreContext Context{};
	FActorPersistentState* ActorState = nullptr;
	
	{
		FGuardValue_Bitfield(bRegisteringActors, true);
		ActorState = InitializeActor(Actor, Context);
	}
	
	{
		FGuardValue_Bitfield(bLoadingActors, true);
		// load actor state
		ActorState->LoadActor(*this);
	}
}

void ULevelPersistentStateManager::OnActorDestroyed(AActor* Actor)
{
	if (CurrentlyProcessedActor == Actor || !Actor->Implements<UPersistentStateObject>())
	{
		// do not handle callback if it is caused by state manager
		return;
	}

	const FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(Actor);
	check(ActorId.IsValid());

	if (FLevelPersistentState* LevelState = GetLevelState(Actor->GetLevel()))
	{
		FActorPersistentState* ActorState = LevelState->GetActorState(ActorId);
		if (ActorState->IsStatic())
		{
			// mark static actor as destroyed
			AddDestroyedObject(ActorId);
		}
		else
		{
			// remove dynamic actor. ActorState no longer valid after this call
			LevelState->Actors.Remove(ActorId);
		}
	}
}
