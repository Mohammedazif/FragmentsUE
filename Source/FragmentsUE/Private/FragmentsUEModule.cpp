// Copyright Azif. All Rights Reserved.

#include "FragmentsUEModule.h"

DEFINE_LOG_CATEGORY(LogFragmentsUE);

#define LOCTEXT_NAMESPACE "FFragmentsUEModule"

void FFragmentsUEModule::StartupModule()
{
	UE_LOG(LogFragmentsUE, Log, TEXT("FragmentsUE v0.1.0: Module loaded"));
}

void FFragmentsUEModule::ShutdownModule()
{
	UE_LOG(LogFragmentsUE, Log, TEXT("FragmentsUE: Module unloaded"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFragmentsUEModule, FragmentsUE)
