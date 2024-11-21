
#include "PersistentStateModule.h"

#include "SaveGameSystem.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogPersistentState);
UE_TRACE_CHANNEL_DEFINE(PersistentStateChannel);

class FPersistentStateModule: public IPersistentStateModule
{
public:
	virtual ISaveGameSystem* GetSaveGameSystem() override;
};

ISaveGameSystem* FPersistentStateModule::GetSaveGameSystem()
{
	static FGenericSaveGameSystem SaveGameSystem{};
	return &SaveGameSystem;
}

IMPLEMENT_MODULE(FPersistentStateModule, PersistentState)
