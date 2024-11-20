#include "Managers/WorldPersistentStateManager_LevelActors.h"

#include "PersistentStateModule.h"
#include "PersistentStateInterface.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateStatics.h"
#include "Streaming/LevelStreamingDelegates.h"

FComponentPersistentState::FComponentPersistentState(UActorComponent* Component, const FPersistentStateObjectId& InComponentHandle)
	: ComponentHandle(InComponentHandle)
{
	InitWithComponentHandle(Component, InComponentHandle);
}

void FComponentPersistentState::InitWithComponentHandle(UActorComponent* Component, const FPersistentStateObjectId& InComponentHandle) const
{
	check(!bStateInitialized);
	check(ComponentHandle == InComponentHandle);

	bStateInitialized = true;
	ComponentHandle = InComponentHandle;
	if (ComponentHandle.IsStatic())
	{
		UE::PersistentState::MarkComponentStatic(*Component);
	}
	else
	{
		UE::PersistentState::MarkComponentDynamic(*Component);
	}
}

UActorComponent* FComponentPersistentState::CreateDynamicComponent(AActor* OwnerActor) const
{
	check(ComponentHandle.IsValid());
	// verify that persistent state is valid for creating a dynamic component
	check(!bStateInitialized && bComponentSaved && ComponentHandle.IsDynamic());
	check(ComponentClass.Get() != nullptr);
	
	UActorComponent* Component = NewObject<UActorComponent>(OwnerActor, ComponentClass.Get());
	// @todo: use FUObjectArray::AddUObjectCreateListener to assign serialized ID as early as possible
	// dynamic components should be spawned early enough to go into PostRegisterAllComponents
	// @todo: maybe we have an issue here? If dynamically created component is attached to the SCS created component
	// although we directly resolve attachments and after all components have been registered and initialized
	check(Component && !Component->IsRegistered());

	FPersistentStateObjectId::AssignSerializedObjectId(Component, ComponentHandle);
	InitWithComponentHandle(Component, ComponentHandle);

	return Component;
}

void FComponentPersistentState::LoadComponent(UWorldPersistentStateManager_LevelActors& StateManager)
{
	check(bStateInitialized);
	if (bComponentSaved == false)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FComponentPersistentState_LoadComponent, PersistentStateChannel);
	
	UActorComponent* Component = ComponentHandle.ResolveObject<UActorComponent>();
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

void FComponentPersistentState::SaveComponent(UWorldPersistentStateManager_LevelActors& StateManager, bool bFromLevelStreaming)
{
	check(bStateInitialized);
	UActorComponent* Component = ComponentHandle.ResolveObject<UActorComponent>();
	check(Component);
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Component);
	
	// PersistentState object can't transition from Saveable to not Saveable
	ensureAlwaysMsgf(static_cast<int32>(State->ShouldSaveState()) >= static_cast<int32>(bComponentSaved), TEXT("%s: component %s transitioned from Saveable to NotSaveable."),
		*FString(__FUNCTION__), *GetNameSafe(Component));

	ON_SCOPE_EXIT
	{
		if (bFromLevelStreaming)
		{
			// reset StateInitialized flag if it is caused by level streaming
			// otherwise next time level is loaded back, it will encounter actor/component state that is already "initialized"
			bStateInitialized = false;
		}
	};
	
	// ensure that we won't transition from true to false
	bComponentSaved = bComponentSaved || State->ShouldSaveState();
	if (bComponentSaved == false)
	{
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FComponentPersistentState_SaveComponent, PersistentStateChannel);
	
	State->PreSaveState();

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

FActorPersistentState::FActorPersistentState(AActor* InActor, const FPersistentStateObjectId& InActorHandle)
	: ActorHandle(InActorHandle)
{
	InitWithActorHandle(InActor, InActorHandle);
}

void FActorPersistentState::InitWithActorHandle(AActor* Actor, const FPersistentStateObjectId& InActorHandle) const
{
	check(!bStateInitialized);
	check(ActorHandle == InActorHandle);

	bStateInitialized = true;
	ActorHandle = InActorHandle;
	if (ActorHandle.IsStatic())
	{
		UE::PersistentState::MarkActorStatic(*Actor);
	}
	else
	{
		UE::PersistentState::MarkActorDynamic(*Actor);
	}
}

AActor* FActorPersistentState::CreateDynamicActor(UWorld* World, FActorSpawnParameters& SpawnParams) const
{
	check(ActorHandle.IsValid());
	// verify that persistent state can create a dynamic actor
	check(!bStateInitialized && bActorSaved && ActorHandle.IsDynamic());
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
		FPersistentStateObjectId::AssignSerializedObjectId(Actor, ActorHandle);
		InitWithActorHandle(Actor, ActorHandle);

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

void FActorPersistentState::LoadActor(UWorldPersistentStateManager_LevelActors& StateManager)
{
	check(bStateInitialized);
	if (bActorSaved == false)
	{
		return;
	}
	
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FActorPersistentState_LoadActor, PersistentStateChannel);
	
	AActor* Actor = ActorHandle.ResolveObject<AActor>();
	check(Actor != nullptr && Actor->IsActorInitialized() && !Actor->HasActorBegunPlay());
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Actor);
	State->PreLoadState();

	// load owner and instigator for a dynamic actor
	if (ActorHandle.IsDynamic())
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

void FActorPersistentState::SaveActor(UWorldPersistentStateManager_LevelActors& StateManager, bool bFromLevelStreaming)
{
	check(bStateInitialized);
	AActor* Actor = ActorHandle.ResolveObject<AActor>();
	check(Actor);
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Actor);
	
	// PersistentState object can't transition from Saveable to not Saveable
	ensureAlwaysMsgf(static_cast<int32>(State->ShouldSaveState()) >= static_cast<int32>(bActorSaved), TEXT("%s: actor %s transitioned from Saveable to NotSaveable."),
		*FString(__FUNCTION__), *GetNameSafe(Actor));

	ON_SCOPE_EXIT
	{
		if (bFromLevelStreaming)
		{
			// reset StateInitialized flag if it is caused by level streaming
			// otherwise next time level is loaded back, it will encounter actor/component state that is already "initialized"
			bStateInitialized = false;
		}
	};

	// ensure that we won't transition from true to false
	bActorSaved = bActorSaved || State->ShouldSaveState();

	if (bActorSaved == false)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FActorPersistentState_SaveActor, PersistentStateChannel);

	State->PreSaveState();

	// update list of actor components
	UpdateActorComponents(StateManager, *Actor);

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
}

const FComponentPersistentState* FActorPersistentState::GetComponentState(const FPersistentStateObjectId& ComponentHandle) const
{
	return const_cast<FActorPersistentState*>(this)->GetComponentState(ComponentHandle);
}

FComponentPersistentState* FActorPersistentState::GetComponentState(const FPersistentStateObjectId& ComponentHandle)
{
	return Components.FindByPredicate([&ComponentHandle](const FComponentPersistentState& ComponentState)
	{
		return ComponentState.GetHandle() == ComponentHandle;
	});
}

FComponentPersistentState* FActorPersistentState::CreateComponentState(UActorComponent* Component, const FPersistentStateObjectId& ComponentHandle)
{
	check(GetComponentState(ComponentHandle) == nullptr);
	FComponentPersistentState* ComponentState = &Components.Add_GetRef(FComponentPersistentState{Component, ComponentHandle});
	
	return ComponentState;
}

void FActorPersistentState::UpdateActorComponents(UWorldPersistentStateManager_LevelActors& StateManager, const AActor& Actor)
{
	TInlineComponentArray<UActorComponent*> OwnedComponents;
	Actor.GetComponents(OwnedComponents);
	
	// we process dynamically destroyed components during actor save due to lack of events for destroying actor components
	// Detect destroyed components - remove component state and mark static components as destroyed
	for (int32 ComponentStateIndex = Components.Num() - 1; ComponentStateIndex >= 0; --ComponentStateIndex)
	{
		FComponentPersistentState& ComponentState = Components[ComponentStateIndex];
		FPersistentStateObjectId ComponentId = ComponentState.GetHandle();
		check(ComponentId.IsValid());

		UActorComponent* ActorComponent = ComponentId.ResolveObject<UActorComponent>();
		if (!IsValid(ActorComponent))
		{
			if (ComponentState.IsStatic())
			{
				// mark static component as destroyed
				StateManager.AddDestroyedObject(ComponentId);
			}

			// remove destroyed component from the component list
			Components.RemoveAtSwap(ComponentStateIndex);
		}
	}
}

FLevelPersistentState::FLevelPersistentState(const ULevel* Level)
	: LevelHandle(FPersistentStateObjectId::CreateStaticObjectId(Level))
{
	check(LevelHandle.IsValid());
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
			return ComponentState.GetHandle() == ComponentId;
		}))
		{
			return true;
		}
	}

	return false;
}

const FActorPersistentState* FLevelPersistentState::GetActorState(const FPersistentStateObjectId& ActorHandle) const
{
	return Actors.Find(ActorHandle);
}

FActorPersistentState* FLevelPersistentState::GetActorState(const FPersistentStateObjectId& ActorHandle)
{
	return Actors.Find(ActorHandle);
}

FActorPersistentState* FLevelPersistentState::CreateActorState(AActor* Actor, const FPersistentStateObjectId& ActorHandle)
{
	check(GetActorState(ActorHandle) == nullptr);
	FActorPersistentState* ActorState = &Actors.Add(ActorHandle, FActorPersistentState{Actor, ActorHandle});
	
	return ActorState;
}

void UWorldPersistentStateManager_LevelActors::Init(UWorld* World)
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

void UWorldPersistentStateManager_LevelActors::Cleanup(UWorld* World)
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FLevelStreamingDelegates::OnLevelBeginMakingVisible.Remove(LevelVisibleHandle);
	FLevelStreamingDelegates::OnLevelBeginMakingInvisible.Remove(LevelInvisibleHandle);

	World->OnActorsInitialized.Remove(ActorsInitializedHandle);
	World->RemoveOnActorDestroyededHandler(ActorDestroyedHandle);

	Super::Cleanup(World);
}

void UWorldPersistentStateManager_LevelActors::NotifyObjectInitialized(UObject& Object)
{
	Super::NotifyObjectInitialized(Object);

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
			ensureAlwaysMsgf(false, TEXT("%s: component %s that supports persistent state created on the actor %s that doesn't. "),
				*FString(__FUNCTION__), *Object.GetClass()->GetName(), *OwnerActor->GetClass()->GetName());
		}
#endif
		return;
	}
	
	if (!OwnerActor->Implements<UPersistentStateObject>())
	{
		// runtime created component added to the actor that doesn't implement persistent state interface, which means component won't be saved/loaded.
		// notify user about it, as it is definitely not an expected behavior.
		ensureAlwaysMsgf(false, TEXT("%s: component %s that supports persistent state created on the actor %s that doesn't. "),
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
	if (!ComponentId.IsValid())
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
	ActorState->CreateComponentState(Component, ComponentId);
}

void UWorldPersistentStateManager_LevelActors::FLevelRestoreContext::AddCreatedActor(const FActorPersistentState& ActorState)
{
	check(ActorState.IsDynamic() && ActorState.IsInitialized());
	CreatedActors.Add(ActorState.GetHandle());
}

void UWorldPersistentStateManager_LevelActors::FLevelRestoreContext::AddCreatedComponent(const FComponentPersistentState& ComponentState)
{
	check(ComponentState.IsDynamic() && ComponentState.IsInitialized());
	CreatedComponents.Add(ComponentState.GetHandle());
}

void UWorldPersistentStateManager_LevelActors::LoadGameState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_LevelActors_LoadGameState, PersistentStateChannel);

	FLevelRestoreContext Context{};

	constexpr bool bFromLevelStreaming = false;
	InitializeLevel(CurrentWorld->PersistentLevel, Context, bFromLevelStreaming);
	for (ULevelStreaming* LevelStreaming: CurrentWorld->GetStreamingLevels())
	{
		if (ULevel* Level = LevelStreaming->GetLoadedLevel())
		{
			InitializeLevel(Level, Context, bFromLevelStreaming);
		}
	}
}

void UWorldPersistentStateManager_LevelActors::SaveGameState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_LevelActors_SaveGameState, PersistentStateChannel);
	
	Super::SaveGameState();
	
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

void UWorldPersistentStateManager_LevelActors::AddDestroyedObject(const FPersistentStateObjectId& ObjectId)
{
	check(ObjectId.IsValid());

#if WITH_EDITOR
	UObject* Object = ObjectId.ResolveObject();
	check(Object);

	ULevel* Level = Object->GetTypedOuter<ULevel>();
	check(Level && GetLevelState(Level) != nullptr);
#endif
	
	DestroyedObjects.Add(ObjectId);
}

void UWorldPersistentStateManager_LevelActors::SaveLevel(FLevelPersistentState& LevelState, bool bFromLevelStreaming)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_LevelActors_SaveLevel, PersistentStateChannel);
	check(LoadedLevels.Contains(LevelState.LevelHandle));
	
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

void UWorldPersistentStateManager_LevelActors::InitializeLevel(ULevel* Level, FLevelRestoreContext& Context, bool bFromLevelStreaming)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_LevelActors_InitializeLevel, PersistentStateChannel);
	// we should not process level if actor initialization/registration/loading is currently going on
	check(Level && CanInitializeState());
	// verify that we don't process the same level twice
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

		TGuardValue ActorScope{CurrentlyProcessedActor, Actor};
		
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
			// level is being re-added to the world
			check(ActorId.ResolveObject<AActor>() == Actor);
			FActorPersistentState* ActorState = LevelState.GetActorState(ActorId);
			check(ActorState != nullptr && !ActorState->IsInitialized());

			ActorState->InitWithActorHandle(Actor, ActorId);
			RestoreActorComponents(*Actor, *ActorState, Context);
		}
		else
		{
			// new level is being added to the world
			ActorId = FPersistentStateObjectId::CreateStaticObjectId(Actor);
			check(ActorId.IsValid());
		
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
				check(ActorId == ActorState->GetHandle());

				// re-initialize actor state with a static actor
				ActorState->InitWithActorHandle(Actor, ActorId);
			}
			else
			{
				// create actor state for the static actor for the first time is it loaded
				ActorState = LevelState.CreateActorState(Actor, ActorId);
			}

			RestoreActorComponents(*Actor, *ActorState, Context);
		}
	}
	
	for (AActor* Actor: PendingDestroyActors)
	{
		TGuardValue ActorScope{CurrentlyProcessedActor, Actor};
		Actor->Destroy();
	}
	
	RestoreDynamicActors(Level, LevelState, Context);
}

void UWorldPersistentStateManager_LevelActors::RestoreDynamicActors(ULevel* Level, FLevelPersistentState& LevelState, FLevelRestoreContext& Context)
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

		AActor* DynamicActor = ActorState.GetHandle().ResolveObject<AActor>();
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
}

void UWorldPersistentStateManager_LevelActors::RestoreActorComponents(AActor& Actor, FActorPersistentState& ActorState, FLevelRestoreContext& Context)
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

			ComponentState->InitWithComponentHandle(Component, ComponentId);
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
			check(ComponentId == ComponentState->GetHandle());
			if (!ComponentState->IsInitialized())
			{
				check(ComponentState->IsStatic());
				ComponentState->InitWithComponentHandle(Component, ComponentId);
			}
		}
		else
		{
			// create component state for the static component for the first time it is loaded
			ComponentState = ActorState.CreateComponentState(Component, ComponentId);
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
			OutdatedObjects.Add(ComponentState.GetHandle());
			It.RemoveCurrent();
			continue;
		}
		
		UActorComponent* Component = ComponentState.CreateDynamicComponent(&Actor);
		check(Component);

		Context.AddCreatedComponent(ComponentState);
	}
}

const FLevelPersistentState* UWorldPersistentStateManager_LevelActors::GetLevelState(ULevel* Level) const
{
	return Levels.Find(FPersistentStateObjectId::FindObjectId(Level));
}

FLevelPersistentState* UWorldPersistentStateManager_LevelActors::GetLevelState(ULevel* Level)
{
	return Levels.Find(FPersistentStateObjectId::FindObjectId(Level));
}

FLevelPersistentState& UWorldPersistentStateManager_LevelActors::GetOrCreateLevelState(ULevel* Level)
{
	const FPersistentStateObjectId LevelId = FPersistentStateObjectId::CreateStaticObjectId(Level);
	return Levels.FindOrAdd(LevelId, FLevelPersistentState{LevelId});
}

void UWorldPersistentStateManager_LevelActors::OnWorldInitializedActors(const FActorsInitializedParams& InitParams)
{
	if (InitParams.World == CurrentWorld)
	{
		bWorldInitializedActors = true;
	}
}

void UWorldPersistentStateManager_LevelActors::OnLevelLoaded(ULevel* LoadedLevel, UWorld* World)
{
	if (World == CurrentWorld)
	{
		
	}
}

#if 0
void UWorldPersistentStateManager_LevelActors::ProcessPendingRegisterActors(FLevelRestoreContext& Context)
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
#endif

void UWorldPersistentStateManager_LevelActors::OnLevelBecomeVisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel)
{
	if (World == CurrentWorld)
	{
		constexpr bool bFromLevelStreaming = true;
		
		FLevelRestoreContext Context{};
		InitializeLevel(LoadedLevel, Context, bFromLevelStreaming);
	}
}

void UWorldPersistentStateManager_LevelActors::OnLevelBecomeInvisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel)
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

FActorPersistentState* UWorldPersistentStateManager_LevelActors::InitializeActor(AActor* Actor, FLevelRestoreContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UWorldPersistentStateManager_LevelActors_RegisterActor, PersistentStateChannel);
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
		// re-init existing actor state
		check(ActorState->IsStatic() == ActorId.IsStatic());
		ActorState->InitWithActorHandle(Actor, ActorId);
	}
	else
	{
		// create persistent state for a new actor, either static or dynamic
		ActorState = LevelState->CreateActorState(Actor, ActorId);
	}

	// do a full component discovery
	RestoreActorComponents(*Actor, *ActorState, Context);
	return ActorState;
}

void UWorldPersistentStateManager_LevelActors::OnActorInitialized(AActor* Actor)
{
	check(Actor != nullptr && Actor->IsActorInitialized() && Actor->Implements<UPersistentStateObject>());
	check(CanInitializeState());
	
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

void UWorldPersistentStateManager_LevelActors::OnActorDestroyed(AActor* Actor)
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
