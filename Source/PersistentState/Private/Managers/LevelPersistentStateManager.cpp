#include "Managers/LevelPersistentStateManager.h"

#include "PersistentStateInterface.h"
#include "PersistentStateStatics.h"
#include "Streaming/LevelStreamingDelegates.h"

template <typename T = UObject>
T* ResolveObject(const FUniqueObjectId& Object)
{
	return CastChecked<T>(Object.GetUniqueID().ResolveObject(), ECastCheckedType::NullAllowed);
}

FGuid GetObjectId(const FUniqueObjectId& Object)
{
	return Object.GetUniqueID().GetGuid();
}

FComponentPersistentState::FComponentPersistentState(UActorComponent* Component, FGuid ComponentId, bool bStatic)
{
	WeakComponent = UE::PersistentState::AssignObjectId(*Component, ComponentId);
	bComponentStatic = bStatic;
}

void FComponentPersistentState::InitWithStaticComponent(UActorComponent* Component, FGuid ComponentId) const
{
	check(!bStateInitialized);
	check(GetObjectId(WeakComponent) == ComponentId && bComponentStatic == true);

	bStateInitialized = true;
	WeakComponent = UE::PersistentState::AssignObjectId(*Component, ComponentId);
	UE::PersistentState::MarkComponentStatic(*Component);
}

void FComponentPersistentState::InitWithDynamicComponent(UActorComponent* Component, FGuid ComponentId) const
{
	check(!bStateInitialized);
	check(GetObjectId(WeakComponent) == ComponentId);

	bStateInitialized = true;
	WeakComponent = UE::PersistentState::AssignObjectId(*Component, ComponentId);
	UE::PersistentState::MarkComponentDynamic(*Component);
}

UActorComponent* FComponentPersistentState::CreateDynamicComponent(AActor* OwnerActor) const
{
	check(GetObjectId(WeakComponent).IsValid());
	check(ComponentClass.Get() != nullptr && bComponentStatic == false);
	
	UActorComponent* Component = NewObject<UActorComponent>(OwnerActor, ComponentClass.Get());
	check(!Component->IsRegistered());

	InitWithDynamicComponent(Component, GetObjectId(WeakComponent));

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
	
	UActorComponent* Component = ResolveObject<UActorComponent>(WeakComponent);
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
			USceneComponent* AttachParent = ResolveObject<USceneComponent>(AttachParentId);
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
	
	UE::PersistentState::SaveObjectSaveGameProperties(*Component, SaveGameBunch);

	if (InstanceState.IsValid())
	{
		State->LoadCustomObjectState(InstanceState);	
	}

	State->PostLoadState();
}

void FComponentPersistentState::SaveComponent(ULevelPersistentStateManager& StateManager)
{
	check(bStateInitialized);
	UActorComponent* Component = ResolveObject<UActorComponent>(WeakComponent);
	check(Component);
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Component);
	bComponentSaved = State->ShouldSaveState();
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
			// @todo: SaveComponent saves and serializes attachment information for any Saveable scene component
			// which does not seem reasonable for a lot of cases
			// statically created components almost never detached/reattached to another component, so it makes sense (in general)
			// to store attachment information for dynamic components only
			// HOWEVER, we still want to know whether component is attached to something or not, so that we can determine
			// whether to save relative or absolute transform
			AttachSocketName = SceneComponent->GetAttachSocketName();
			AttachParentId = UE::PersistentState::FindUniqueIdFromObject(AttachParent);
			ensureAlwaysMsgf(AttachParentId.IsValid(), TEXT("%s: saveable component [%s:%s] is attached to component [%s;%s], which does not have a stable id"),
				*FString(__FUNCTION__), *GetNameSafe(Component->GetOwner()), *Component->GetName(), *GetNameSafe(AttachParent->GetOwner()), *AttachParent->GetName());

			ComponentTransform = SceneComponent->GetRelativeTransform();
		}
		else
		{
			ComponentTransform = SceneComponent->GetComponentTransform();
		}
	}

	UE::PersistentState::LoadObjectSaveGameProperties(*Component, SaveGameBunch);
	InstanceState = State->SaveCustomObjectState();

	State->PostSaveState();
}

FGuid FComponentPersistentState::GetComponentId() const
{
	return GetObjectId(WeakComponent);
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
		ActorOwnerId = UE::PersistentState::FindUniqueIdFromObject(OwnerActor);
		check(ActorOwnerId.IsValid());
	}
	if (APawn* Instigator = InActor->GetInstigator())
	{
		ActorInstigatorId = UE::PersistentState::FindUniqueIdFromObject(Instigator);
		check(ActorInstigatorId.IsValid());
	}
}

FActorPersistentState::FActorPersistentState(AActor* InActor, FGuid InActorId, bool bStatic)
{
	WeakActor = UE::PersistentState::AssignObjectId(*InActor, InActorId);
	bActorStatic = bStatic;
}

void FActorPersistentState::InitWithStaticActor(AActor* Actor, FGuid ActorId) const
{
	check(!bStateInitialized);
	check(GetObjectId(WeakActor) == ActorId);

	bStateInitialized = true;
	WeakActor = UE::PersistentState::AssignObjectId(*Actor, ActorId);
	UE::PersistentState::MarkActorStatic(*Actor);
}

void FActorPersistentState::InitWithDynamicActor(AActor* Actor, FGuid ActorId) const
{
	check(!bStateInitialized);
	check(GetObjectId(WeakActor) == ActorId);

	bStateInitialized = true;
	WeakActor = UE::PersistentState::AssignObjectId(*Actor, ActorId);
	UE::PersistentState::MarkActorDynamic(*Actor);
}

AActor* FActorPersistentState::CreateDynamicActor(UWorld* World, FActorSpawnParameters& SpawnParams) const
{
	check(GetObjectId(WeakActor).IsValid());
	check(SpawnData.IsValid() && bActorStatic == false);

	UClass* ActorClass = SpawnData.ActorClass.Get();
	check(ActorClass);

	SpawnParams.Name = SpawnData.ActorName;
	SpawnParams.CustomPreSpawnInitalization = [this, Callback = SpawnParams.CustomPreSpawnInitalization](AActor* Actor)
	{
		// assign actor id before actor is fully spawned
		InitWithDynamicActor(Actor, GetObjectId(WeakActor));
		if (Callback)
		{
			Callback(Actor);
		}
	};

	// actor transform is going to be overriden later by LoadActor call
	AActor* Actor = World->SpawnActor(ActorClass, &ActorTransform, SpawnParams);
	check(!Actor->HasActorBegunPlay());

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
	
	AActor* Actor = ResolveObject<AActor>(WeakActor);
	check(Actor != nullptr && Actor->HasActorRegisteredAllComponents() && !Actor->HasActorBegunPlay());
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Actor);
	State->PreLoadState();

	// load owner and instigator for a dynamic actor
	if (bActorStatic == false)
	{
		if (SpawnData.HasOwner())
		{
			AActor* Owner = UE::PersistentState::FindObjectByUniqueId<AActor>(SpawnData.ActorOwnerId);
			check(Owner);
			Actor->SetOwner(Owner);
		}
		if (SpawnData.HasInstigator())
		{
			APawn* Instigator = UE::PersistentState::FindObjectByUniqueId<APawn>(SpawnData.ActorInstigatorId);
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
			USceneComponent* AttachParent = ResolveObject<USceneComponent>(AttachParentId);
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
	
	UE::PersistentState::SaveObjectSaveGameProperties(*Actor, SaveGameBunch);

	if (InstanceState.IsValid())
	{
		State->LoadCustomObjectState(InstanceState);
	}

	State->PostLoadState();
}

void FActorPersistentState::SaveActor(ULevelPersistentStateManager& StateManager)
{
	check(bStateInitialized);
	AActor* Actor = ResolveObject<AActor>(WeakActor);
	check(Actor);
	
	IPersistentStateObject* State = CastChecked<IPersistentStateObject>(Actor);
	bActorSaved = State->ShouldSaveState();

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
		ComponentState.SaveComponent(StateManager);
	}
	
	SpawnData = FDynamicActorSpawnData{Actor};
	// some actors don't have a root component
	if (USceneComponent* RootComponent = Actor->GetRootComponent())
	{
		bHasTransform = true;
		if (USceneComponent* AttachParent = RootComponent->GetAttachParent())
		{
			AttachSocketName = RootComponent->GetAttachSocketName();
			AttachParentId = UE::PersistentState::FindUniqueIdFromObject(AttachParent);
			ensureAlwaysMsgf(AttachParentId.IsValid(), TEXT("%s: saveable actor [%s] is attached to component [%s;%s], which does not have a stable id"),
				*FString(__FUNCTION__), *Actor->GetName(), *GetNameSafe(AttachParent->GetOwner()), *AttachParent->GetName());
			
			ActorTransform = RootComponent->GetRelativeTransform();
		}
		else
		{
			ActorTransform = Actor->GetActorTransform();
		}
	}

	UE::PersistentState::LoadObjectSaveGameProperties(*Actor, SaveGameBunch);
	InstanceState = State->SaveCustomObjectState();

	State->PostSaveState();
}

FGuid FActorPersistentState::GetActorId() const
{
	return GetObjectId(WeakActor);
}

const FComponentPersistentState* FActorPersistentState::GetComponentState(const FGuid& ComponentId) const
{
	return const_cast<FActorPersistentState*>(this)->GetComponentState(ComponentId);
}

FComponentPersistentState* FActorPersistentState::GetComponentState(const FGuid& ComponentId)
{
	return Components.FindByPredicate([&ComponentId](const FComponentPersistentState& ComponentState)
	{
		return GetObjectId(ComponentState.WeakComponent) == ComponentId;
	});
}

FComponentPersistentState* FActorPersistentState::CreateComponentState(UActorComponent* Component, const FGuid& ComponentId, bool bStatic)
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
	: LevelId(UE::PersistentState::CreateUniqueIdFromStableName(*Level))
{
	
}

bool FLevelPersistentState::HasActor(const FGuid& ActorId) const
{
	return Actors.Contains(ActorId);
}

bool FLevelPersistentState::HasComponent(const FGuid& ActorId, const FGuid& ComponentId) const
{
	if (const FActorPersistentState* ActorState = Actors.Find(ActorId))
	{
		if (ActorState->Components.ContainsByPredicate([&ComponentId](const FComponentPersistentState& ComponentState)
		{
			return GetObjectId(ComponentState.WeakComponent) == ComponentId;
		}))
		{
			return true;
		}
	}

	return false;
}

const FActorPersistentState* FLevelPersistentState::GetActorState(const FGuid& ActorId) const
{
	return Actors.Find(ActorId);
}

FActorPersistentState* FLevelPersistentState::GetActorState(const FGuid& ActorId)
{
	return Actors.Find(ActorId);
}

FActorPersistentState* FLevelPersistentState::CreateActorState(AActor* Actor, const FGuid& ActorId, bool bStatic)
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
	
	LevelVisibleHandle = FLevelStreamingDelegates::OnLevelBeginMakingVisible.AddUObject(this, &ThisClass::OnLevelBecomeVisible);
	LevelInvisibleHandle = FLevelStreamingDelegates::OnLevelBeginMakingInvisible.AddUObject(this, &ThisClass::OnLevelBecomeInvisible);

	ActorsInitializedHandle = World->OnActorsInitialized.AddUObject(this, &ThisClass::OnWorldActorsInitialized);
	ActorDestroyedHandle = World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &ThisClass::OnActorDestroyed));
	ActorRegisteredHandle = World->AddOnPostRegisterAllActorComponentsHandler(FOnPostRegisterAllActorComponents::FDelegate::CreateUObject(this, &ThisClass::OnActorRegistered));
	
	LoadGameState();
}

void ULevelPersistentStateManager::Cleanup(UWorld* World)
{
	FLevelStreamingDelegates::OnLevelBeginMakingVisible.Remove(LevelVisibleHandle);
	FLevelStreamingDelegates::OnLevelBeginMakingInvisible.Remove(LevelInvisibleHandle);
	
	World->RemoveOnPostRegisterAllActorComponentsHandler(ActorRegisteredHandle);
	World->RemoveOnActorDestroyededHandler(ActorDestroyedHandle);

	Super::Cleanup(World);
}

void ULevelPersistentStateManager::NotifyInitialized(UObject& Object)
{
	Super::NotifyInitialized(Object);

	// @note: this function purpose is to catch dynamic objects created at runtime, that are not visible to state system
	// via existing engine callbacks.
	// It is triggered by user explicitly calling IPersistentStateObject::NotifyInitialized from the object itself
	// at the appropriate time.
	// Currently, it is used to catch runtime created components, both on static and dynamic actors

	UActorComponent* Component = Cast<UActorComponent>(&Object);
	if (Component == nullptr)
	{
		return;
	}

	AActor* OwnerActor = Component->GetOwner();
	check(OwnerActor != nullptr);
	
	FGuid ComponentId = UE::PersistentState::FindUniqueIdFromObject(Component);
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
	
	FGuid ActorId = UE::PersistentState::FindUniqueIdFromObject(OwnerActor);
	if (!ActorId.IsValid())
	{
		ensureAlwaysMsgf(false, TEXT("%s: actor %s implements persistent state interface but was not discovered by state system."),
			*FString(__FUNCTION__), *OwnerActor->GetClass()->GetName());
		return;
	}
	
	ComponentId = UE::PersistentState::CreateUniqueIdFromStableName(*Component);
	const bool bStatic = ComponentId.IsValid();
	if (!bStatic)
	{
		ComponentId = FGuid::NewGuid();
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
	
	RestoreLevel(CurrentWorld->PersistentLevel, Context);
	for (ULevelStreaming* LevelStreaming: CurrentWorld->GetStreamingLevels())
	{
		if (ULevel* Level = LevelStreaming->GetLoadedLevel())
		{
			RestoreLevel(Level, Context);
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
			SaveLevel(LevelState);
		}
	}
}

void ULevelPersistentStateManager::SaveLevel(FLevelPersistentState& LevelState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(ULevelPersistentStateManager_SaveLevel, PersistentStateChannel);

	TArray<FGuid, TInlineAllocator<16>> OutdatedActors;
	for (auto& [ActorId, ActorState]: LevelState.Actors)
	{
		// @todo: what do we do with static actors, that were not found?
		// For PIE this is understandable, because level changes between sessions which causes old save to accumulate
		// static actors that doesn't exist.
		// In packaged game it might be a bug/issue with a state system, although game can remove static actors from the level
		// between updates and doesn't care about state of those actors
		if (ActorState.IsInitialized())
		{
			ActorState.SaveActor(*this);
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
	for (const FGuid& ActorId: OutdatedActors)
	{
		LevelState.Actors.Remove(ActorId);
	}
}

void ULevelPersistentStateManager::RestoreLevel(ULevel* Level, FLevelRestoreContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(ULevelPersistentStateManager_RestoreLevel, PersistentStateChannel);
	// we should not process level if actor initialization/registration/loading is currently going on
	check(Level && CanRestoreLevel());
	// verify that we don't process the same level twice
	// @todo: stable name has collision for runtime created level instances
	const FGuid LevelId = UE::PersistentState::CreateUniqueIdFromStableName(*Level);
	check(!LoadedLevels.Contains(LevelId));
	LoadedLevels.Add(LevelId);
	
	FLevelPersistentState& LevelState = GetOrCreateLevelState(Level);
	
	static TArray<AActor*> PendingDestroyActors;
	PendingDestroyActors.Reset();
	
	// create object identifiers for level static actors
	for (AActor* StaticActor: Level->Actors)
	{
		if (StaticActor == nullptr)
		{
			continue;
		}
		
		// create and assign actor id from stable name for static actors, so that persistent state system can indirectly track actors (like attachment)
		// @todo: create id only for actors that implement interface
		FGuid ActorId = UE::PersistentState::CreateUniqueIdFromStableName(*StaticActor);
		check(ActorId.IsValid());
		
		if (!StaticActor->Implements<UPersistentStateObject>())
		{
			UE::PersistentState::AssignObjectId(*StaticActor, ActorId);
			continue;
		}
		
		check(!StaticActor->HasActorRegisteredAllComponents());
		
		TGuardValue ActorScope{CurrentlyProcessedActor, StaticActor};
		
		if (IsDestroyedObject(ActorId))
		{
			// actor has been destroyed, verify that actor state doesn't exist and skip processing components
			check(LevelState.GetActorState(ActorId) == nullptr);
			PendingDestroyActors.Add(StaticActor);
			continue;
		}
		
		FActorPersistentState* ActorState = LevelState.GetActorState(ActorId);
		if (ActorState != nullptr)
		{
			check(ActorState->IsStatic());
			check(ActorId == GetObjectId(ActorState->WeakActor));

			// re-initialize actor state with a static actor
			ActorState->InitWithStaticActor(StaticActor, ActorId);
		}
		else
		{
			// create actor state for the static actor for the first time is it loaded
			constexpr bool bStatic = true;
			ActorState = LevelState.CreateActorState(StaticActor, ActorId, bStatic);
		}

		RestoreActorComponents(*StaticActor, *ActorState, Context);
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

void ULevelPersistentStateManager::RestoreDynamicActors(ULevel* Level,
	FLevelPersistentState& LevelState, FLevelRestoreContext& Context)
{
	UWorld* World = Level->GetWorld();
	
	FActorSpawnParameters SpawnParams{};
	SpawnParams.bNoFail = true;
	SpawnParams.OverrideLevel = Level;
#if 0
	// defer construction for levels streamed via AddToWorld flow
	SpawnParams.bDeferConstruction = Level->bIsAssociatingLevel;
#endif
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	
	FGuardValue_Bitfield(bRestoringDynamicActors, true);

	TArray<FGuid, TInlineAllocator<16>> OutdatedActors;
	for (auto& [ActorId, ActorState]: LevelState.Actors)
	{
		if (ActorState.IsStatic() || ActorState.IsInitialized())
		{
			continue;
		}

		// invalid dynamic actor, probably caused by a cpp/blueprint class being renamed or removed
		if (ActorState.GetActorClass() == nullptr)
		{
			OutdatedActors.Add(ActorId);
			continue;
		}

		AActor* DynamicActor = ResolveObject<AActor>(ActorState.WeakActor);
		if (DynamicActor == nullptr)
		{
			SpawnParams.CustomPreSpawnInitalization = [&ActorState, &Context, this](AActor* DynamicActor)
			{
				CurrentlyProcessedActor = DynamicActor;
				RestoreActorComponents(*DynamicActor, ActorState, Context);
			};
			// dynamically spawned actors have fully registered components after spawned regardless of the owning world state
			// we process static and dynamic components before actor initialization in PreSpawnInitialization callback
			// static and dynamic components are processed in OnActorSpawned
			DynamicActor = ActorState.CreateDynamicActor(World, SpawnParams);
			check(DynamicActor);

			Context.AddCreatedActor(ActorState);
			CurrentlyProcessedActor = nullptr;
		}
	}
	CurrentlyProcessedActor = nullptr;

	// remove outdated actors
	OutdatedObjects.Append(OutdatedActors);
	for (const FGuid& ActorId: OutdatedActors)
	{
		LevelState.Actors.Remove(ActorId);
	}

	if (CanRegisterActors())
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
	for (UActorComponent* StaticComponent: Actor.GetComponents())
	{
		if (StaticComponent == nullptr)
		{
			continue;
		}
		
		// create and assign component id from a stable name so persistent state system can track stable actor components (for attachment and other purposes)
		// @todo: create id only for components that implement interface
		FGuid ComponentId = UE::PersistentState::CreateUniqueIdFromStableName(*StaticComponent);
		if (!ComponentId.IsValid())
		{
			ensureAlwaysMsgf(false, TEXT("%s: found dynamic component %s on actor %s created during actor initialization.")
				TEXT("PersistentState currently doesn't support saveable components created during registration."),
				 *FString(__FUNCTION__), *StaticComponent->GetName(), *Actor.GetName());
			continue;
		}
		
		if (!StaticComponent->Implements<UPersistentStateObject>())
		{
			UE::PersistentState::AssignObjectId(*StaticComponent, ComponentId);
			continue;
		}
		
		if (IsDestroyedObject(ComponentId))
		{
			// static component has been explicitly destroyed, verify that component state doesn't exist
			check(ActorState.GetComponentState(ComponentId) == nullptr);
			PendingDestroyComponents.Add(StaticComponent);
			continue;
		}
			
		const FComponentPersistentState* ComponentState = ActorState.GetComponentState(ComponentId);
		if (ComponentState != nullptr && !ComponentState->IsInitialized())
		{
			check(ComponentState->IsStatic());
			check(ComponentId == GetObjectId(ComponentState->WeakComponent));
				
			ComponentState->InitWithStaticComponent(StaticComponent, ComponentId);
		}
		else
		{
			// create component state for the static component for the first time it is loaded
			constexpr bool bStatic = true;
			ComponentState = ActorState.CreateComponentState(StaticComponent, ComponentId, bStatic);
		}
	}

	// process pending destroy components
	for (UActorComponent* Component: PendingDestroyComponents)
	{
		Component->DestroyComponent();
	}

	// spawn dynamic components created on a static actor during runtime
	for (int32 Index = ActorState.Components.Num() - 1; Index >= 0; --Index)
	{
		const FComponentPersistentState& ComponentState = ActorState.Components[Index];
		// skip static and already initialized components
		if (ComponentState.IsStatic() || ComponentState.IsInitialized())
		{
			continue;
		}

		// outdated component, probably caused by cpp/blueprint class being renamed or removed
		if (ComponentState.GetComponentClass() == nullptr)
		{
			// remove outdated component
			OutdatedObjects.Add(ComponentState.GetComponentId());
			ActorState.Components.RemoveAtSwap(Index);
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
		FGuid ComponentId = ComponentState.GetComponentId();
		check(ComponentId.IsValid());

		UActorComponent* ActorComponent = ResolveObject<UActorComponent>(ComponentState.WeakComponent);
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
			FGuid ComponentId = UE::PersistentState::FindUniqueIdFromObject(ActorComponent);
			if (!ComponentId.IsValid())
			{
				ComponentId = FGuid::NewGuid();
				check(UE::PersistentState::CreateUniqueIdFromStableName(*ActorComponent).IsValid() == false);
				
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
	return Levels.Find(UE::PersistentState::CreateUniqueIdFromStableName(*Level));
}

FLevelPersistentState* ULevelPersistentStateManager::GetLevelState(ULevel* Level)
{
	return Levels.Find(UE::PersistentState::CreateUniqueIdFromStableName(*Level));
}

FLevelPersistentState& ULevelPersistentStateManager::GetOrCreateLevelState(ULevel* Level)
{
	const FGuid LevelId = UE::PersistentState::CreateUniqueIdFromStableName(*Level);
	return Levels.FindOrAdd(LevelId, FLevelPersistentState{LevelId});
}

void ULevelPersistentStateManager::OnWorldActorsInitialized(const FActorsInitializedParams& InitParams)
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

void ULevelPersistentStateManager::ProcessPendingRegisterActors(FLevelRestoreContext& Context)
{
	check(CanRegisterActors() == true);
	
	TArray<FActorPersistentState*> ActorStateList;

	{
		FGuardValue_Bitfield(bRegisteringActors, true);
		for (int32 Index = 0; Index < PendingRegisterActors.Num(); ++Index)
        {
        	// process pending register actors until it is empty, because loading actors may cause new actors to spawn
			FActorPersistentState* ActorState = RegisterActor(PendingRegisterActors[Index], Context);
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
		FLevelRestoreContext Context{};
		RestoreLevel(LoadedLevel, Context);
	}
}

void ULevelPersistentStateManager::OnLevelBecomeInvisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel)
{
	if (World == CurrentWorld)
	{
		if (FLevelPersistentState* LevelState = GetLevelState(LoadedLevel))
		{
			SaveLevel(*LevelState);
		}

		const FGuid LevelId = UE::PersistentState::CreateUniqueIdFromStableName(*LoadedLevel);
		check(LoadedLevels.Contains(LevelId));
		LoadedLevels.Remove(LevelId);
	}
}

FActorPersistentState* ULevelPersistentStateManager::RegisterActor(AActor* Actor, FLevelRestoreContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(ULevelPersistentStateManager_RegisterActor, PersistentStateChannel);
	check(Actor->HasActorRegisteredAllComponents());
	check(!Actor->HasActorBegunPlay());
	
	FLevelPersistentState* LevelState = GetLevelState(Actor->GetLevel());
	check(LevelState);

	// @todo: tick based processing to resolve everything all at once?
	// @todo: Example: GameMode cached a reference to GameState and GameMode and is registered first. LoadActor will not restore reference to GameState
	// @todo: Example: GameState holds a reference to a PlayerController. Player controller IS NOT yet created
	// Global actors that spawn dynamically but "appear" as static (e.g. they have a stable name and state system doesn't respawn them) should primarily
	// live as a part of persistent level.
	
	FGuid ActorId = UE::PersistentState::FindUniqueIdFromObject(Actor);
	if (ActorId.IsValid())
	{
		FActorPersistentState* ActorState = ActorState = LevelState->GetActorState(ActorId);
		check(ActorState != nullptr);
		
		return ActorState;
	}

	// actor has not been discovered by state system in RestoreLevel, which means it was spawned at runtime
	// dynamically created actor outside of persistent state system scope can be anything: game mode, player controller, pawn, other dynamic actors, etc.
	// actor can also be spawned during level visibility request (AddToWorld) as part of another actor registration
	//
	// actor can be spawned dynamically, but if it has a stable name we consider it static.
	// Gameplay code is responsible for respawning static actors, not the state system
	ActorId = UE::PersistentState::CreateUniqueIdFromStableName(*Actor);
	const bool bActorStatic = ActorId.IsValid();
	
	if (!ActorId.IsValid())
	{
		// actor is fully dynamic
		ActorId = FGuid::NewGuid();
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

void ULevelPersistentStateManager::OnActorRegistered(AActor* Actor)
{
	check(Actor != nullptr && Actor->HasActorRegisteredAllComponents());
	if (!Actor->Implements<UPersistentStateObject>())
	{
		return;
	}
	
	if (CanRegisterActors() == false)
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
		ActorState = RegisterActor(Actor, Context);
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

	const FGuid ActorId = UE::PersistentState::FindUniqueIdFromObject(Actor);
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
