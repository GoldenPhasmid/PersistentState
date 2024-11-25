#include "Managers/PersistentStateManager_LevelActors.h"

#include "PersistentStateArchive.h"
#include "PersistentStateModule.h"
#include "PersistentStateInterface.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateStatics.h"
#include "PersistentStateSubsystem.h"
#include "Engine/AssetManager.h"
#include "Streaming/LevelStreamingDelegates.h"

void FLevelLoadContext::AddCreatedActor(const FActorPersistentState& ActorState)
{
	check(ActorState.IsDynamic() && ActorState.IsLinked());
	CreatedActors.Add(ActorState.GetHandle());
}

void FLevelLoadContext::AddCreatedComponent(const FComponentPersistentState& ComponentState)
{
	check(ComponentState.IsDynamic() && ComponentState.IsLinked());
	CreatedComponents.Add(ComponentState.GetHandle());
}

void FLevelSaveContext::ProcessActorState(const FActorPersistentState& State)
{
	if (State.IsDynamic())
	{
		const auto& Class = State.GetClass();
		check(Class.IsValid());
		DynamicClasses.Add(Class.ToSoftObjectPath());
	}
}

void FLevelSaveContext::ProcessComponentState(const FComponentPersistentState& State)
{
	if (State.IsDynamic())
	{
		const auto& Class = State.GetClass();
		check(Class.IsValid());
		DynamicClasses.Add(Class.ToSoftObjectPath());
	}
}

FPersistentStateObjectDesc FPersistentStateObjectDesc::Create(AActor& Actor, FPersistentStateObjectTracker& ObjectTracker)
{
	FPersistentStateObjectDesc Result{};

	Result.Name = Actor.GetFName();
	Result.Class = Actor.GetClass();

	if (const AActor* Owner = Actor.GetOwner())
	{
		Result.OwnerID = FPersistentStateObjectId::FindObjectId(Owner);
	}

	// some actors don't have a root component
	if (USceneComponent* RootComponent = Actor.GetRootComponent())
	{
		Result.bHasTransform = true;
		if (USceneComponent* AttachParent = RootComponent->GetAttachParent())
		{
			Result.AttachParentID = FPersistentStateObjectId::FindObjectId(AttachParent);
			Result.AttachSocketName = RootComponent->GetAttachSocketName();
			Result.Transform = RootComponent->GetRelativeTransform();
		}
		else
		{
			Result.Transform = RootComponent->GetComponentTransform();
		}
	}

	UE::PersistentState::SaveObjectSaveGameProperties(Actor, Result.SaveGameBunch, ObjectTracker);

	return Result;
}

FPersistentStateObjectDesc FPersistentStateObjectDesc::Create(UActorComponent& Component, FPersistentStateObjectTracker& ObjectTracker)
{
	FPersistentStateObjectDesc Result{};

	Result.Name = Component.GetFName();
	Result.Class = Component.GetClass();
	Result.OwnerID = FPersistentStateObjectId::FindObjectId(Component.GetOwner());
	
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(&Component))
	{
		Result.bHasTransform = true;
		if (USceneComponent* AttachParent = SceneComponent->GetAttachParent())
		{
			Result.AttachParentID = FPersistentStateObjectId::FindObjectId(AttachParent);
			Result.AttachSocketName = SceneComponent->GetAttachSocketName();
			// if component is attached to anything its transform is relative
			Result.Transform = SceneComponent->GetRelativeTransform();
		}
		else
		{
			Result.Transform = SceneComponent->GetComponentTransform();
		}
	}

	UE::PersistentState::SaveObjectSaveGameProperties(Component, Result.SaveGameBunch, ObjectTracker);

	return Result;
}

void FPersistentStateDescFlags::SerializeObjectState(FArchive& Ar, FPersistentStateObjectDesc& State)
{
	check(!Ar.IsSaving() || bStateSaved == true);

	Ar << TDeltaSerialize(State.Name, bHasInstanceName);
	Ar << TDeltaSerialize(State.Class, bHasInstanceClass);
	Ar << TDeltaSerialize(State.OwnerID, bHasInstanceOwner);
	Ar << TDeltaSerialize(State.Transform, bHasInstanceTransform);
	Ar << TDeltaSerialize(State.AttachParentID, bHasInstanceAttachment);
	Ar << TDeltaSerialize(State.AttachSocketName, bHasInstanceAttachment);
	Ar << TDeltaSerialize(State.SaveGameBunch, bHasInstanceSaveGameBunch);

	check(!Ar.IsLoading() || bStateSaved == true);
}

FPersistentStateDescFlags FPersistentStateDescFlags::GetFlagsForStaticObject(
	FPersistentStateDescFlags SourceFlags,
    const FPersistentStateObjectDesc& Default,
    const FPersistentStateObjectDesc& Current) const
{
	checkf(Default.Name == Current.Name, TEXT("renaming statically created objects is not supported."));
	checkf(Default.Class == Current.Class, TEXT("static object class should not change."));
	checkf(Default.bHasTransform == Current.bHasTransform, TEXT("transform property should not flip."));
	
	FPersistentStateDescFlags Result = SourceFlags;
	
	Result.bHasInstanceName				= false;
	Result.bHasInstanceClass			= false;
	Result.bHasInstanceOwner			= Default.OwnerID != Current.OwnerID;
	Result.bHasInstanceAttachment		= !(Default.AttachParentID == Current.AttachParentID && Default.AttachSocketName == Current.AttachSocketName);
	Result.bHasInstanceTransform		= Result.bHasInstanceAttachment || (Current.bHasTransform && !Default.Transform.Equals(Current.Transform));
	// @todo: equal save game is always different between Default and Current if it contains soft object references
	Result.bHasInstanceSaveGameBunch	= !Default.EqualSaveGame(Current);

	return Result;
}

FPersistentStateDescFlags FPersistentStateDescFlags::GetFlagsForDynamicObject(
	FPersistentStateDescFlags SourceFlags,
	const FPersistentStateObjectDesc& Current) const
{
	FPersistentStateDescFlags Result = SourceFlags;
	
	Result.bHasInstanceName				= true;
	Result.bHasInstanceClass			= true;
	Result.bHasInstanceOwner			= Current.OwnerID.IsValid();
	Result.bHasInstanceTransform		= Current.bHasTransform;
	Result.bHasInstanceAttachment		= Current.AttachParentID.IsValid();
	Result.bHasInstanceSaveGameBunch	= Current.SaveGameBunch.Num() > 0;

	return Result;
}

bool FPersistentStateDescFlags::Serialize(FArchive& Ar)
{
	Ar << *this;
	return true;
}

FArchive& operator<<(FArchive& Ar, FPersistentStateDescFlags& Value)
{
	FGuardValue_Bitfield(Value.bStateLinked, false);
	Ar.Serialize(&Value, sizeof(FPersistentStateDescFlags));
		
	return Ar;
}


FComponentPersistentState::FComponentPersistentState(UActorComponent* Component, const FPersistentStateObjectId& InComponentHandle)
	: ComponentHandle(InComponentHandle)
{
	LinkComponentHandle(Component, InComponentHandle);
}

void FComponentPersistentState::LinkComponentHandle(UActorComponent* Component, const FPersistentStateObjectId& InComponentHandle) const
{
	check(StateFlags.bStateLinked == false);
	check(ComponentHandle == InComponentHandle);

	StateFlags.bStateLinked = true;
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
	check(StateFlags.bStateLinked == false && StateFlags.bStateSaved && ComponentHandle.IsDynamic());

	UClass* Class = SavedComponentState.Class.Get();
	check(Class);

	UActorComponent* Component = nullptr;
	
	{
		FUObjectIDInitializer Initializer{ComponentHandle, SavedComponentState.Name, Class};
		Component = NewObject<UActorComponent>(OwnerActor, Class);
		// component is not be registered, dynamic components should be spawned early enough to go into PostRegisterAllComponents
		check(Component && !Component->IsRegistered());
	}
	
	LinkComponentHandle(Component, ComponentHandle);
	UE_LOG(LogPersistentState, Verbose, TEXT("created dynamic component %s"), *ToString());

	return Component;
}

void FComponentPersistentState::LoadComponent(FLevelLoadContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FComponentPersistentState_LoadComponent, PersistentStateChannel);
	check(StateFlags.bStateLinked);
		
	UActorComponent* Component = ComponentHandle.ResolveObject<UActorComponent>();
	check(Component && Component->IsRegistered());

	if (IsStatic())
	{
		FPersistentStateObjectTracker DummyTracker{};
		DefaultComponentState = FPersistentStateObjectDesc::Create(*Component, DummyTracker);
	}
	
	if (StateFlags.bStateSaved)
	{
		IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Component);
		State->PreLoadState();

		if (StateFlags.bHasInstanceTransform)
		{
			USceneComponent* SceneComponent = CastChecked<USceneComponent>(Component);

			if (StateFlags.bHasInstanceAttachment || SceneComponent->GetAttachParent())
			{
				if (StateFlags.bHasInstanceAttachment)
				{
					if (SavedComponentState.AttachParentID.IsValid())
					{
						USceneComponent* AttachParent = SavedComponentState.AttachParentID.ResolveObject<USceneComponent>();
						check(AttachParent);
						
						SceneComponent->AttachToComponent(AttachParent, FAttachmentTransformRules::KeepWorldTransform, SavedComponentState.AttachSocketName);
					}
					else
					{
						SceneComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
					}
					
				}

				SceneComponent->SetRelativeTransform(SavedComponentState.Transform);
			}
			else
			{
				// component is not attached to anything, ComponentTransform is world transform
				SceneComponent->SetWorldTransform(SavedComponentState.Transform);
			}
		}

		if (StateFlags.bHasInstanceSaveGameBunch)
		{
			UE::PersistentState::LoadObjectSaveGameProperties(*Component, SavedComponentState.SaveGameBunch, Context.ObjectTracker);
		}
		
		if (InstanceState.IsValid())
		{
			State->LoadCustomObjectState(InstanceState);	
		}

		State->PostLoadState();
	}
}

void FComponentPersistentState::SaveComponent(FLevelSaveContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FComponentPersistentState_SaveComponent, PersistentStateChannel);
	check(StateFlags.bStateLinked);
	UActorComponent* Component = ComponentHandle.ResolveObject<UActorComponent>();
	check(Component);
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Component);
	
	// PersistentState object can't transition from Saveable to not Saveable
	ensureAlwaysMsgf(static_cast<int32>(State->ShouldSaveState()) >= static_cast<int32>(StateFlags.bStateSaved), TEXT("%s: component %s transitioned from Saveable to NotSaveable."),
		*FString(__FUNCTION__), *GetNameSafe(Component));

	ON_SCOPE_EXIT
	{
		if (Context.IsLevelUnloading())
		{
			// reset StateInitialized flag if it is caused by level streaming
			// otherwise next time level is loaded back, it will encounter actor/component state that is already "initialized"
			StateFlags.bStateLinked = false;
		}
	};
	
	// ensure that we won't transition from true to false
	StateFlags.bStateSaved = StateFlags.bStateSaved || State->ShouldSaveState();
	if (StateFlags.bStateSaved == false)
	{
		return;
	}
	
	State->PreSaveState();

	SavedComponentState = FPersistentStateObjectDesc::Create(*Component, Context.ObjectTracker);
	if (IsStatic())
	{
		StateFlags = StateFlags.GetFlagsForStaticObject(StateFlags, DefaultComponentState, SavedComponentState);
	}
	else
	{
		StateFlags = StateFlags.GetFlagsForDynamicObject(StateFlags, SavedComponentState);
	}
	
	// process component state through save context
	Context.ProcessComponentState(*this);
	

	if (StateFlags.bHasInstanceOwner)
	{
		AActor* Owner = Component->GetOwner();
		ensureAlwaysMsgf(Owner == nullptr || SavedComponentState.OwnerID.IsValid(), TEXT("%s: saveable component [%s:%s] is owned by actor [%s] that does not have a stable id"),
        	*FString(__FUNCTION__), *GetNameSafe(Component->GetOwner()), *Component->GetName(), *GetNameSafe(Owner));
	}

	if (StateFlags.bHasInstanceAttachment)
	{
		USceneComponent* AttachParent = CastChecked<USceneComponent>(Component)->GetAttachParent();
		ensureAlwaysMsgf(AttachParent == nullptr || SavedComponentState.AttachParentID.IsValid(), TEXT("%s: saveable component [%s:%s] is attached to component [%s:%s] that does not have a stable id"),
			*FString(__FUNCTION__), *GetNameSafe(Component->GetOwner()), *Component->GetName(), *GetNameSafe(AttachParent->GetOwner()), *GetNameSafe(AttachParent));
	}
	
	InstanceState = State->SaveCustomObjectState();

	State->PostSaveState();
}

#if WITH_COMPONENT_CUSTOM_SERIALIZE
FArchive& operator<<(FArchive& Ar, FComponentPersistentState& Value)
{
	Ar << Value.ComponentHandle;
	Ar << Value.StateFlags;
	if (Value.StateFlags.bStateSaved)
	{
    	Value.StateFlags.SerializeObjectState(Ar, Value.SavedComponentState);
		Value.InstanceState.Serialize(Ar);
	}

	return Ar;
}

bool FComponentPersistentState::Serialize(FArchive& Ar)
{
	Ar << *this;
	return true;
}
#endif

FActorPersistentState::FActorPersistentState(AActor* InActor, const FPersistentStateObjectId& InActorHandle)
	: ActorHandle(InActorHandle)
{
	LinkActorHandle(InActor, InActorHandle);
}

void FActorPersistentState::LinkActorHandle(AActor* Actor, const FPersistentStateObjectId& InActorHandle) const
{
	check(!StateFlags.bStateLinked);
	check(ActorHandle == InActorHandle);

	StateFlags.bStateLinked = true;
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
	check(!StateFlags.bStateLinked && StateFlags.bStateSaved && ActorHandle.IsDynamic());

	UClass* ActorClass = SavedActorState.Class.Get();
	check(ActorClass);

	check(SpawnParams.OverrideLevel);
	SpawnParams.Name = SavedActorState.Name;
	// @todo: both Owner and Instigator should be resolved and applied BEFORE dynamic actor is spawned.
	SpawnParams.CustomPreSpawnInitalization = [this, Callback = SpawnParams.CustomPreSpawnInitalization](AActor* Actor)
	{
		// assign actor id before actor is fully spawned
		
		LinkActorHandle(Actor, ActorHandle);

		if (Callback)
		{
			Callback(Actor);
		}
	};

	AActor* Actor = nullptr;

	{
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
		FUObjectIDInitializer Initializer{ActorHandle, SpawnParams.Name, ActorClass};
		Actor = World->SpawnActor(ActorClass, &SavedActorState.Transform, SpawnParams);
	}

	// @todo: GSpawnActorDeferredTransformCache is not cleared from a deferred spawned actor
	// PostActorConstruction() is executed as a part of level visibility request (via AddToWorld() flow)
	if (SpawnParams.bDeferConstruction)
	{
		FEditorScriptExecutionGuard ScriptGuard;
		Actor->ExecuteConstruction(SavedActorState.Transform, nullptr, nullptr, false);
	}
	
	check(Actor && Actor->HasActorRegisteredAllComponents() && !Actor->IsActorInitialized() && !Actor->HasActorBegunPlay());
	UE_LOG(LogPersistentState, Verbose, TEXT("created dynamic actor %s"), *ToString());

	return Actor;
}

void FActorPersistentState::LoadActor(FLevelLoadContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FActorPersistentState_LoadActor, PersistentStateChannel);
	check(StateFlags.bStateLinked);
		
	AActor* Actor = ActorHandle.ResolveObject<AActor>();
	check(Actor != nullptr && Actor->IsActorInitialized() && !Actor->HasActorBegunPlay());

	if (IsStatic())
	{
		// save default sate for static actors to compared it with runtime state during save
		FPersistentStateObjectTracker DummyTracker{};
		DefaultActorState = FPersistentStateObjectDesc::Create(*Actor, DummyTracker);
	}

	// load components
	for (FComponentPersistentState& ComponentState: Components)
	{
		ComponentState.LoadComponent(Context);
	}
	
	if (StateFlags.bStateSaved)
	{
		IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Actor);
		State->PreLoadState();

		// @todo: owner has to be resolved and applied BEFORE dynamic actor is spawned.
		if (StateFlags.bHasInstanceOwner)
		{
			AActor* Owner = SavedActorState.OwnerID.ResolveObject<AActor>();
			Actor->SetOwner(Owner);
		}

		if (StateFlags.bHasInstanceTransform)
		{
			// actor is attached to other scene component, it means actor transform is relative
			if (Actor->GetAttachParentActor() || StateFlags.bHasInstanceAttachment)
			{
				if (StateFlags.bHasInstanceAttachment)
				{
					if (SavedActorState.AttachParentID.IsValid())
					{
						USceneComponent* AttachParent = SavedActorState.AttachParentID.ResolveObject<USceneComponent>();
						check(AttachParent);
						
						Actor->AttachToComponent(AttachParent, FAttachmentTransformRules::KeepWorldTransform, SavedActorState.AttachSocketName);
					}
					else
					{
						Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
					}
				}
				
				Actor->SetActorRelativeTransform(SavedActorState.Transform);
			}
			else
			{
				// actor is not attached to anything, transform is in world space
				Actor->SetActorTransform(SavedActorState.Transform);
			}
		}

		if (StateFlags.bHasInstanceSaveGameBunch)
		{
			UE::PersistentState::LoadObjectSaveGameProperties(*Actor, SavedActorState.SaveGameBunch, Context.ObjectTracker);
		}
		
		if (InstanceState.IsValid())
		{
			State->LoadCustomObjectState(InstanceState);
		}

		State->PostLoadState();
	}
}

void FActorPersistentState::SaveActor(FLevelSaveContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(FActorPersistentState_SaveActor, PersistentStateChannel);
	check(StateFlags.bStateLinked);
	AActor* Actor = ActorHandle.ResolveObject<AActor>();
	check(Actor);
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Actor);
	
	// PersistentState object can't transition from Saveable to not Saveable
	ensureAlwaysMsgf(static_cast<int32>(State->ShouldSaveState()) >= static_cast<int32>(StateFlags.bStateSaved), TEXT("%s: actor %s transitioned from Saveable to NotSaveable."),
		*FString(__FUNCTION__), *GetNameSafe(Actor));

	ON_SCOPE_EXIT
	{
		if (Context.IsLevelUnloading())
		{
			// reset StateInitialized flag if it is caused by level streaming
			// otherwise next time level is loaded back, it will encounter actor/component state that is already "initialized"
			StateFlags.bStateLinked = false;
		}
	};

	// ensure that we won't transition from true to false
	StateFlags.bStateSaved = StateFlags.bStateSaved || State->ShouldSaveState();

	if (StateFlags.bStateSaved == false)
	{
		return;
	}
	
	State->PreSaveState();

	// update list of actor components
	UpdateActorComponents(Context, *Actor);

	// save component states
	for (auto It = Components.CreateIterator(); It; ++It)
	{
		if (It->IsLinked())
		{
			It->SaveComponent(Context);
		}
		else
		{
			Context.AddOutdatedObject(It->GetHandle());
			// @todo: what do we do with static components that were not found?
			// @todo: dynamic components are never outdated, we should provide some way to detect/remove them for game updates
			// For PIE this is understandable, because level changes between sessions which causes old save to accumulate
			// static components that doesn't exist.
			// In packaged game it might be a bug/issue with a state system, although game can remove static components from the level
			// between updates and doesn't care about state of those actors.
			// only static components can be "automatically" outdated due to level change. Dynamically created components
			// are always recreated by the state manager, unless their class is explicitly deleted
			// remove outdated component state
			It.RemoveCurrentSwap();
		}
	}

	SavedActorState = FPersistentStateObjectDesc::Create(*Actor, Context.ObjectTracker);
	if (IsStatic())
	{
		StateFlags = StateFlags.GetFlagsForStaticObject(StateFlags, DefaultActorState, SavedActorState);
	}
	else
	{
		StateFlags = StateFlags.GetFlagsForDynamicObject(StateFlags, SavedActorState);
	}

	// process actor state through save context
	Context.ProcessActorState(*this);

	if (StateFlags.bHasInstanceOwner)
	{
		AActor* Owner = Actor->GetOwner();
		ensureAlwaysMsgf(Owner == nullptr || SavedActorState.OwnerID.IsValid(), TEXT("%s: saveable actor [%s] is owned by actor [%s] that does not have a stable id"),
        	*FString(__FUNCTION__), *Actor->GetName(), *GetNameSafe(Owner));
	}
	if (StateFlags.bHasInstanceAttachment)
	{
		AActor* AttachActor = Actor->GetAttachParentActor();
		ensureAlwaysMsgf(AttachActor == nullptr || SavedActorState.AttachParentID.IsValid(), TEXT("%s: saveable actor [%s] is attached to actor [%s], which does not have a stable id"),
			*FString(__FUNCTION__), *Actor->GetName(), *GetNameSafe(AttachActor));
	}
	
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

void FActorPersistentState::UpdateActorComponents(FLevelSaveContext& Context, const AActor& Actor)
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
				Context.AddDestroyedObject(ComponentId);
			}

			// remove destroyed component from the component list
			Components.RemoveAtSwap(ComponentStateIndex);
		}
	}
}

#if WITH_ACTOR_CUSTOM_SERIALIZE
bool FActorPersistentState::Serialize(FArchive& Ar)
{
	Ar << *this;
	return true;
}

FArchive& operator<<(FArchive& Ar, FActorPersistentState& Value)
{
	Ar << Value.ActorHandle;
	Ar << Value.StateFlags;
	if (Value.StateFlags.bStateSaved)
	{
        Value.StateFlags.SerializeObjectState(Ar, Value.SavedActorState);
		Value.InstanceState.Serialize(Ar);
        Ar << Value.Components;
	}

	return Ar;
}
#endif

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

FLevelLoadContext FLevelPersistentState::CreateLoadContext() const
{
	// @todo: unnecessary HardDependencies copy
	FPersistentStateObjectTracker ObjectTracker{HardDependencies};
	FLevelLoadContext Context{ObjectTracker, !!bStreamingLevel};

	return Context;
}

void FLevelPersistentState::PreLoadAssets(FStreamableDelegate LoadCompletedDelegate)
{
	check(!AssetHandle.IsValid());

	AssetHandle = UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(HardDependencies);
	// asset handle can be invalid if level state doesn't have any hard dependencies
	if (!AssetHandle.IsValid() || AssetHandle->HasLoadCompleted())
	{
		// do not use FStreamableDelegate::Execute because it is delayed one frame
		LoadCompletedDelegate.Execute();
	}
	else
	{
		AssetHandle->BindCompleteDelegate(LoadCompletedDelegate);
		AssetHandle->BindCancelDelegate(FStreamableDelegate::CreateLambda([]
		{
			checkf(false, TEXT("failed to load assets required by level state"));
		}));
	}
}

void FLevelPersistentState::FinishLoadAssets()
{
	if (AssetHandle.IsValid() && AssetHandle->IsLoadingInProgress())
	{
		AssetHandle->WaitUntilComplete();
	}
}

void FLevelPersistentState::ReleaseLevelAssets()
{
	if (AssetHandle.IsValid())
	{
		ensureAlwaysMsgf(!AssetHandle.IsValid() || AssetHandle->HasLoadCompleted(), TEXT("%s: level hasn't finished loading level assets"), *FString(__FUNCTION__));
		AssetHandle->ReleaseHandle();
		AssetHandle = nullptr;
	}
}

UPersistentStateManager_LevelActors::UPersistentStateManager_LevelActors()
{
	ManagerType = EManagerStorageType::World;
}

bool UPersistentStateManager_LevelActors::ShouldCreateManager(UPersistentStateSubsystem& Subsystem) const
{
	return Super::ShouldCreateManager(Subsystem) && Subsystem.GetWorld() != nullptr;
}

void UPersistentStateManager_LevelActors::Init(UPersistentStateSubsystem& Subsystem)
{
	Super::Init(Subsystem);

	CurrentWorld = Subsystem.GetWorld();
	check(CurrentWorld && CurrentWorld->IsGameWorld());
	check(CurrentWorld->bIsWorldInitialized && !CurrentWorld->bActorsInitialized);

	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ThisClass::OnLevelAddedToWorld);
	LevelVisibleHandle = FLevelStreamingDelegates::OnLevelBeginMakingVisible.AddUObject(this, &ThisClass::OnLevelBecomeVisible);
	LevelInvisibleHandle = FLevelStreamingDelegates::OnLevelBeginMakingInvisible.AddUObject(this, &ThisClass::OnLevelBecomeInvisible);
	
	ActorsInitializedHandle = CurrentWorld->OnActorsInitialized.AddUObject(this, &ThisClass::OnWorldInitializedActors);
	ActorDestroyedHandle = CurrentWorld->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &ThisClass::OnActorDestroyed));
	
	LoadGameState();
}

void UPersistentStateManager_LevelActors::Cleanup(UPersistentStateSubsystem& Subsystem)
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FLevelStreamingDelegates::OnLevelBeginMakingVisible.Remove(LevelVisibleHandle);
	FLevelStreamingDelegates::OnLevelBeginMakingInvisible.Remove(LevelInvisibleHandle);

	CurrentWorld->OnActorsInitialized.Remove(ActorsInitializedHandle);
	CurrentWorld->RemoveOnActorDestroyededHandler(ActorDestroyedHandle);
	
	Super::Cleanup(Subsystem);
}

void UPersistentStateManager_LevelActors::NotifyObjectInitialized(UObject& Object)
{
	Super::NotifyObjectInitialized(Object);
	
	// handler for actor/component initialization
	// for actors it should be called from AActor::PostInitializeComponents
	// for components it has to be called from UActorComponent::InitializeComponent
	
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


void UPersistentStateManager_LevelActors::LoadGameState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentStateLevelManager_LoadGameState, PersistentStateChannel);

	constexpr bool bFromLevelStreaming = false;
	InitializeLevel(CurrentWorld->PersistentLevel, bFromLevelStreaming);
	for (ULevelStreaming* LevelStreaming: CurrentWorld->GetStreamingLevels())
	{
		if (ULevel* Level = LevelStreaming->GetLoadedLevel())
		{
			InitializeLevel(Level, bFromLevelStreaming);
		}
	}
}

void UPersistentStateManager_LevelActors::SaveState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentStateLevelManager_SaveGameState, PersistentStateChannel);
	
	Super::SaveState();
	
	for (auto& [LevelName, LevelState]: Levels)
	{
		if (LevelState.bLevelInitialized && LevelState.bLevelAdded)
		{
			// save only fully added levels
			constexpr bool bFromLevelStreaming = false;
			SaveLevel(LevelState, bFromLevelStreaming);
		}
	}
}

void UPersistentStateManager_LevelActors::AddDestroyedObject(const FPersistentStateObjectId& ObjectId)
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

void UPersistentStateManager_LevelActors::SaveLevel(FLevelPersistentState& LevelState, bool bFromLevelStreaming)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentStateLevelManager_SaveLevel, PersistentStateChannel);
	check(LevelState.bLevelInitialized == true);

	FPersistentStateObjectTracker ObjectTracker{};
	FLevelSaveContext Context{ObjectTracker, bFromLevelStreaming};
	
	for (auto It = LevelState.Actors.CreateIterator(); It; ++It)
	{
		auto& [ActorId, ActorState] = *It;
		if (ActorState.IsLinked())
		{
			ActorState.SaveActor(Context);
		}
		else
		{
			// @todo: what do we do with static actors, that were not found?
			// @todo: dynamic actors are never outdated, we should provide some way to detect/remove them for game updates
			// For PIE this is understandable, because level changes between sessions which causes old save to accumulate
			// static actors that doesn't exist.
			// In packaged game it might be a bug/issue with a state system, although game can remove static actors from the level
			// between updates and doesn't care about state of those actors.
			// only static actors can be "automatically" outdated due to level change. Dynamically created actors
			// are always recreated by the state manager, unless their class is explicitly deleted
			check(ActorState.IsStatic());
			Context.AddOutdatedObject(ActorId);
			OutdatedObjects.Add(ActorId);
            			
			// remove outdated actor state
			It.RemoveCurrent();
		}
	}

	// override level hard dependencies
	LevelState.HardDependencies = ObjectTracker.GetReferences();
	LevelState.HardDependencies.Append(Context.DynamicClasses.Array());

	// append outdated objects
	OutdatedObjects.Append(Context.OutdatedObjects);
	// append destroyed objects
	DestroyedObjects.Append(Context.DestroyedObjects);

}

void UPersistentStateManager_LevelActors::InitializeLevel(ULevel* Level, bool bFromLevelStreaming)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentStateLevelManager_InitializeLevel, PersistentStateChannel);
	// we should not process level if actor initialization/registration/loading is currently going on
	check(Level && CanInitializeState());
	// verify that we don't process the same level twice
	const FPersistentStateObjectId LevelId = FPersistentStateObjectId::CreateStaticObjectId(Level);
	check(LevelId.IsValid());

	FLevelPersistentState& LevelState = GetOrCreateLevelState(Level);
	check(LevelState.bLevelAdded == false && LevelState.bLevelInitialized == false);

	// update level state flags
	LevelState.bLevelInitialized = true;
	LevelState.bLevelAdded = !bFromLevelStreaming;
	LevelState.bStreamingLevel = bFromLevelStreaming;
	
	static TArray<AActor*> PendingDestroyActors;
	PendingDestroyActors.Reset();

	
	FLevelLoadContext Context = LevelState.CreateLoadContext();
	
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
			check(ActorState != nullptr && !ActorState->IsLinked());

			ActorState->LinkActorHandle(Actor, ActorId);
			InitializeActorComponents(*Actor, *ActorState, Context);
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
				ActorState->LinkActorHandle(Actor, ActorId);
			}
			else
			{
				// create actor state for the static actor for the first time is it loaded
				ActorState = LevelState.CreateActorState(Actor, ActorId);
			}

			InitializeActorComponents(*Actor, *ActorState, Context);
		}
	}
	
	// actor classes and other asset dependencies may or may not be loaded when level becomes visible
	// LevelState requests async load for asset dependencies required to properly restore level state
	// if no loading required, dynamic actors are created right away, but AFTER we process static actors on the level
	LevelState.PreLoadAssets(FStreamableDelegate::CreateUObject(this, &ThisClass::CreateDynamicActors, Level));
	
	for (AActor* Actor: PendingDestroyActors)
	{
		TGuardValue ActorScope{CurrentlyProcessedActor, Actor};
		Actor->Destroy();
	}
}

void UPersistentStateManager_LevelActors::CreateDynamicActors(ULevel* Level)
{
	UWorld* World = Level->GetWorld();

	FLevelPersistentState& LevelState = GetLevelStateChecked(Level);
	FLevelLoadContext Context = LevelState.CreateLoadContext();
	
	FActorSpawnParameters SpawnParams{};
	SpawnParams.bNoFail = true;
	SpawnParams.OverrideLevel = Level;
	// defer OnActorConstruction for dynamic actors spawned inside streamed levels (added via AddToWorld flow)
	// ExecuteConstruction() is called explicitly to spawn SCS components
	SpawnParams.bDeferConstruction = Level->bIsAssociatingLevel;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	
	FGuardValue_Bitfield(bCreatingDynamicActors, true);
	
	TArray<FPersistentStateObjectId, TInlineAllocator<16>> OutdatedActors;
	for (auto It = LevelState.Actors.CreateIterator(); It; ++It)
	{
		FActorPersistentState& ActorState = It.Value();
		
		if (ActorState.IsStatic() || ActorState.IsLinked())
		{
			continue;
		}
		
		if (!ActorState.IsSaved())
		{
			// remove dynamic actor state because it cannot be re-created
			It.RemoveCurrent();
			return;
		}

#if 0
		// invalid dynamic actor, probably caused by a cpp/blueprint class being renamed or removed
		if (ActorState.IsOutdated())
		{
			OutdatedActors.Add(ActorId);
			It.RemoveCurrent();
			continue;
		}
#endif

		AActor* DynamicActor = ActorState.GetHandle().ResolveObject<AActor>();
		if (DynamicActor == nullptr)
		{
			SpawnParams.CustomPreSpawnInitalization = [&ActorState, &Context, this](AActor* DynamicActor)
			{
				CurrentlyProcessedActor = DynamicActor;
				InitializeActorComponents(*DynamicActor, ActorState, Context);
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

void UPersistentStateManager_LevelActors::InitializeActorComponents(AActor& Actor, FActorPersistentState& ActorState, FLevelLoadContext& Context)
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
			check(ComponentState != nullptr && !ComponentState->IsLinked());

			ComponentState->LinkComponentHandle(Component, ComponentId);
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
			if (!ComponentState->IsLinked())
			{
				check(ComponentState->IsStatic());
				ComponentState->LinkComponentHandle(Component, ComponentId);
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
		if (ComponentState.IsStatic() || ComponentState.IsLinked())
		{
			continue;
		}
		
		if (!ComponentState.IsSaved())
		{
			// remove dynamic component state because it cannot be re-created
			It.RemoveCurrent();
			return;
		}

#if 0
		// outdated component, probably caused by cpp/blueprint class being renamed or removed
		if (ComponentState.IsOutdated())
		{
			// remove outdated component
			OutdatedObjects.Add(ComponentState.GetHandle());
			It.RemoveCurrent();
			continue;
		}
#endif
		
		UActorComponent* Component = ComponentState.CreateDynamicComponent(&Actor);
		check(Component);

		Context.AddCreatedComponent(ComponentState);
	}
}

const FLevelPersistentState* UPersistentStateManager_LevelActors::GetLevelState(ULevel* Level) const
{
	return Levels.Find(FPersistentStateObjectId::FindObjectId(Level));
}

FLevelPersistentState* UPersistentStateManager_LevelActors::GetLevelState(ULevel* Level)
{
	return Levels.Find(FPersistentStateObjectId::FindObjectId(Level));
}

const FLevelPersistentState& UPersistentStateManager_LevelActors::GetLevelStateChecked(ULevel* Level) const
{
	return Levels.FindChecked(FPersistentStateObjectId::FindObjectId(Level));
}

FLevelPersistentState& UPersistentStateManager_LevelActors::GetLevelStateChecked(ULevel* Level)
{
	return Levels.FindChecked(FPersistentStateObjectId::FindObjectId(Level));
}

FLevelPersistentState& UPersistentStateManager_LevelActors::GetOrCreateLevelState(ULevel* Level)
{
	const FPersistentStateObjectId LevelId = FPersistentStateObjectId::CreateStaticObjectId(Level);
	return Levels.FindOrAdd(LevelId, FLevelPersistentState{LevelId});
}

void UPersistentStateManager_LevelActors::OnWorldInitializedActors(const FActorsInitializedParams& InitParams)
{
	if (InitParams.World == CurrentWorld)
	{
		bWorldInitializedActors = true;
	}
}

void UPersistentStateManager_LevelActors::OnLevelAddedToWorld(ULevel* LoadedLevel, UWorld* World)
{
	if (World == CurrentWorld)
	{
		FLevelPersistentState& LevelState = GetLevelStateChecked(LoadedLevel);
		check(LevelState.bLevelInitialized == true);
		LevelState.bLevelAdded = true;
	}
}

void UPersistentStateManager_LevelActors::OnLevelBecomeVisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel)
{
	if (World == CurrentWorld)
	{
		constexpr bool bFromLevelStreaming = true;
		InitializeLevel(LoadedLevel, bFromLevelStreaming);
	}
}

void UPersistentStateManager_LevelActors::OnLevelBecomeInvisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel)
{
	if (World == CurrentWorld)
	{
		if (FLevelPersistentState* LevelState = GetLevelState(LoadedLevel))
		{
			constexpr bool bFromLevelStreaming = true;
			SaveLevel(*LevelState, bFromLevelStreaming);

			// release level assets
			LevelState->ReleaseLevelAssets();

			LevelState->bLevelAdded = false;
			LevelState->bLevelInitialized = false;
		}
	}
}

FActorPersistentState* UPersistentStateManager_LevelActors::InitializeActor(AActor* Actor, FLevelPersistentState& LevelState, FLevelLoadContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(PersistentStateLevelManager_InitializeActor, PersistentStateChannel);
	check(Actor->IsActorInitialized() && !Actor->HasActorBegunPlay());
	
	// Global actors that spawn dynamically but "appear" as static (e.g. they have a stable name and state system doesn't respawn them) should primarily
	// live as a part of persistent level.
	
	FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(Actor);
	if (ActorId.IsValid())
	{
		FActorPersistentState* ActorState = ActorState = LevelState.GetActorState(ActorId);
		check(ActorState != nullptr && ActorState->IsLinked());
		
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
	FActorPersistentState* ActorState = LevelState.GetActorState(ActorId);
	if (ActorState != nullptr)
	{
		// re-init existing actor state
		check(ActorState->IsStatic() == ActorId.IsStatic());
		ActorState->LinkActorHandle(Actor, ActorId);
	}
	else
	{
		// create persistent state for a new actor, either static or dynamic
		ActorState = LevelState.CreateActorState(Actor, ActorId);
	}

	// do a full component discovery
	InitializeActorComponents(*Actor, *ActorState, Context);
	return ActorState;
}

void UPersistentStateManager_LevelActors::OnActorInitialized(AActor* Actor)
{
	check(Actor != nullptr && Actor->IsActorInitialized() && Actor->Implements<UPersistentStateObject>());
	check(CanInitializeState());

	FLevelPersistentState& LevelState = GetLevelStateChecked(Actor->GetLevel());
	// finish loading assets if it is not done yet
	LevelState.FinishLoadAssets();

	// @todo: unnecessary HardDependencies copy
	FPersistentStateObjectTracker ObjectTracker{LevelState.HardDependencies};
	FLevelLoadContext Context{ObjectTracker, !!LevelState.bStreamingLevel};
	
	FActorPersistentState* ActorState = nullptr;
	
	{
		FGuardValue_Bitfield(bInitializingActors, true);
		ActorState = InitializeActor(Actor, LevelState, Context);
	}
	
	{
		FGuardValue_Bitfield(bLoadingActors, true);
		// load actor state
		ActorState->LoadActor(Context);
	}
}

void UPersistentStateManager_LevelActors::OnActorDestroyed(AActor* Actor)
{
	if (CurrentlyProcessedActor == Actor || !Actor->Implements<UPersistentStateObject>())
	{
		// do not handle callback if it is caused by state manager
		return;
	}

	const FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(Actor);
	check(ActorId.IsValid());
	
	FLevelPersistentState& LevelState = GetLevelStateChecked(Actor->GetLevel());

	FActorPersistentState* ActorState = LevelState.GetActorState(ActorId);
	check(ActorState);
		
	if (ActorState->IsStatic())
	{
		// mark static actor as destroyed
		AddDestroyedObject(ActorId);
	}
		
	// remove ActorState for destroyed actor
	LevelState.Actors.Remove(ActorId);
}