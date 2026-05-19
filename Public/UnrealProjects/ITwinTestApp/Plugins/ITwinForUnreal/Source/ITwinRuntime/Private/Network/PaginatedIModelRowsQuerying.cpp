/*--------------------------------------------------------------------------------------+
|
|     $Source: PaginatedIModelRowsQuerying.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "PaginatedIModelRowsQuerying.h"

#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinServerConnection.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <ITwinSceneMapping.h>
#include <Network/JsonQueriesCache.h>

#include <HAL/FileManager.h>
#include <HAL/PlatformFileManager.h>
#include <Tasks/Task.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <Core/Tools/Log.h>
#	include <Core/Tools/DelayedCall.h>
#	include <SDK/Core/Tools/Tools.h>
#	include <SDK/Core/ITwinAPI/ITwinTypes.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace {

FString GetMetadataQueryString(EElementsMetadata const KindOfMetadata)
{
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
		return FString(
			TEXT("SELECT e.ECInstanceId, b.BBoxLow, b.BBoxHigh, e.Parent.Id, e.FederationGuid, a.Identifier"))
			+ TEXT(" FROM bis.Element e")
			+ TEXT(" LEFT JOIN bis.ExternalSourceAspect a ON a.Element.Id = e.ECInstanceId")
			+ TEXT(" LEFT JOIN bis.GeometricElement3d b ON b.ECInstanceId = e.ECInstanceId");
	case EElementsMetadata::ConstructionDetailing:
		return FString(TEXT("SELECT DISTINCT TargetECInstanceId"))
			+ TEXT(" FROM Construction.ConstructionDetailingElementSplitsGeometricElement3d");
	default:
		ensure(false);
		return {};
	}
}

FString GetMetadataQueryCountString(EElementsMetadata const KindOfMetadata)
{
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
		return TEXT("SELECT COUNT(*) FROM bis.Element");
	case EElementsMetadata::ConstructionDetailing:
		//QueryCountString = TEXT("SELECT DISTINCT COUNT(*) FROM ..."); <= not possible (and/or not efficient)
		return {};
	default:
		ensure(false);
		return {};
	}
}

std::string GetMetadataQueryDescription(EElementsMetadata const KindOfMetadata)
{
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
		return "Elements metadata";
	case EElementsMetadata::ConstructionDetailing:
		return "Construction detailing";
	default:
		ensure(false);
		return "<INVAL>";
	}
}

FString GetCacheFolder(EElementsMetadata const KindOfMetadata, AITwinIModel const& IModel)
{
	QueriesCache::ESubtype Type;
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
		Type = QueriesCache::ESubtype::ElementsMetadataCombined;
		break;
	case EElementsMetadata::ConstructionDetailing:
		Type = QueriesCache::ESubtype::ConstructionDetailing;
		break;
	default:
		ensure(false);
		return {};
	}
	return QueriesCache::GetCacheFolder(Type, IModel.ServerConnection->Environment, IModel.ITwinId, IModel.IModelId,
										IModel.ResolvedChangesetId);
}

}

FPaginatedIModelRowsQueries::FPaginatedIModelRowsQueries(
	AITwinIModel& InIModel, EElementsMetadata const InKindOfMetadata,
	ITwinHttp::FMutex& InMutex, FOnLoadProgressUpdated InOnLoadProgressUpdated)
	: IModel(InIModel)
	, KindOfMetadata(InKindOfMetadata)
	, ECSQLQueryString(GetMetadataQueryString(InKindOfMetadata))
	, ECSQLQueryCount(GetMetadataQueryCountString(InKindOfMetadata))
	, Description(GetMetadataQueryDescription(InKindOfMetadata) + " queries for " + TCHAR_TO_UTF8(*InIModel.IModelId)
		+ " (\""  + TCHAR_TO_UTF8(*InIModel.GetActorNameOrLabel()) + "\")")
	, Cache(InIModel)
	, Mutex(InMutex)
	, OnLoadProgressUpdated(std::move(InOnLoadProgressUpdated))
{
	bQueryTableCount = !ECSQLQueryCount.IsEmpty();
}

void FPaginatedIModelRowsQueries::Cancel()
{
	ITwinHttp::FLock Lock(Mutex);
	State = EState::Cancelled;
}

double FPaginatedIModelRowsQueries::PercentComplete() const
{
	ITwinHttp::FLock Lock(Mutex);
	switch (State)
	{
	case EState::NotStarted:
	case EState::NeedRestart:
		return 0.;
	case EState::Finished:
		return 100.;
	case EState::StoppedOnError:
	case EState::Running:
	case EState::Cancelled:
		break;
	}
	switch (KindOfMetadata)
	{
	case EElementsMetadata::Combined:
		if (TotalRowsExpected > 0)
			// Do not return 100% before all queries are actually finished!
			return std::min(95., (100. * QueryRowStart) / TotalRowsExpected);
		else
			return 0.;
	case EElementsMetadata::ConstructionDetailing:
	default:
		// ConstructionDetailing querying progress is not handled because of the SELECT DISTINCT query
		return 0.;
	}
}

size_t FPaginatedIModelRowsQueries::GetRequestsFromRemote() const
{
	ITwinHttp::FLock Lock(Mutex);
	return RequestsFromRemote;
}

size_t FPaginatedIModelRowsQueries::GetRequestsFromCache() const
{
	ITwinHttp::FLock Lock(Mutex);
	return RequestsFromCache;
}

EHttpResponseCodes::Type FPaginatedIModelRowsQueries::GetFirstErrorCode() const
{
	ITwinHttp::FLock Lock(Mutex);
	return FirstErrorCode;
}

FString FPaginatedIModelRowsQueries::GetFirstErrorString() const
{
	ITwinHttp::FLock Lock(Mutex);
	return FirstErrorString;
}

FPaginatedIModelRowsQueries::EState FPaginatedIModelRowsQueries::GetState() const
{
	ITwinHttp::FLock Lock(Mutex);
	return State;
}

void FPaginatedIModelRowsQueries::DoRestart()
{
	ITwinHttp::FLock Lock(Mutex);
	QueryRowStart = TotalRowsParsed = 0;
	RequestsFromCache = RequestsFromRemote = 0;
	TotalRowsExpected = -1;
	OnLoadProgressUpdated();
	FString const CacheFolder = GetCacheFolder(KindOfMetadata, IModel);
	if (LastCacheFolderUsed != CacheFolder && ensure(!CacheFolder.IsEmpty()))
	{
		if (!Cache.Initialize(CacheFolder, IModel.ServerConnection->Environment, UTF8_TO_TCHAR(Description.c_str())))
		{
			BE_LOGW("ITwinQuery", "Something went wrong while setting up the local http cache for Elements metadata queries - cache will NOT be used!");
		}
		LastCacheFolderUsed = CacheFolder;
	}
	State = EState::Running;
	bQueryTableCount = !ECSQLQueryCount.IsEmpty();
	NumPageInProgress = 0;
	lastPageReached = false;
	for (int i = 0; i < MaxNumPageInProgress; ++i) // read MaxNumPageInProgress pages in //
	{
		std::weak_ptr<FPaginatedIModelRowsQueries> wptr = shared_from_this();
		UE::Tasks::Launch(UE_SOURCE_LOCATION,
			[wptr]()
			{
				auto pThis = wptr.lock();
				if (!pThis)
					return;
				pThis->QueryNextPage();
			},
			UE::Tasks::ETaskPriority::BackgroundLow);
	}
}

void FPaginatedIModelRowsQueries::Restart()
{
	if (EState::Running != State && EState::NeedRestart != State)
	{
		UninitializeCache(); // reinit, we may have a new changesetId for example
		BE_LOGI("ITwinAPI", Description << ": queries (re)starting...");
		DoRestart();
	}
	else
	{
		State = EState::NeedRestart;
	}
}

void FPaginatedIModelRowsQueries::QueryNextPage()
{
	if (EState::Cancelled == State)
	{
		BE_LOGI("ITwinAPI", Description << ": queries cancelled.");
		return;
	}
	std::shared_ptr<AdvViz::SDK::ITwinAPIRequestInfo> RequestInfo;
	int currentQueryRowStart = -1; // will stay "-1" for 'bQueryTableCount'
	{
		ITwinHttp::FLock Lock(Mutex);
		NumPageInProgress++;
		RequestInfo = std::make_shared<AdvViz::SDK::ITwinAPIRequestInfo>(
			IModel.GetMutableWebServices()->InfosToQueryIModel(
				IModel.ITwinId, IModel.IModelId, IModel.ResolvedChangesetId,
				bQueryTableCount ? ECSQLQueryCount : ECSQLQueryString, QueryRowStart, QueryRowCount));
		if (!bQueryTableCount)
		{
			currentQueryRowStart = QueryRowStart;
			QueryRowStart += QueryRowCount;
		}
		bQueryTableCount = false;
	}

	auto const Hit = Cache.IsValid() ? Cache.LookUp(*RequestInfo, Mutex) : std::nullopt;
	if (Hit)
	{
		BE_LOGD("ITwinAPI", Description << ": start query page in cache begin rowstart:" << currentQueryRowStart << " count:" << QueryRowCount << " RequestInfoId:" << RequestInfo.get());
		OnQueryCompleted(true, Cache.Read(*Hit), RequestInfo, currentQueryRowStart == -1, false);
	}
	else
	{
		BE_LOGD("ITwinAPI", Description << ": start query with http begin: rowstart:" << currentQueryRowStart << " count:" << QueryRowCount << " RequestInfoId:" << RequestInfo.get());
		std::weak_ptr< FPaginatedIModelRowsQueries> wptr(shared_from_this());
		AdvViz::SDK::FilterErrorFunc funcFilterError;
		AdvViz::SDK::FilterErrorFunc funcStoreFirstErrorCode =
			[this](long statusCode, std::string const& requestError, bool&, bool&)
			{
				if (!EHttpResponseCodes::IsOk(statusCode))
				{
					ITwinHttp::FLock Lock(Mutex);
					if (EHttpResponseCodes::IsOk(FirstErrorCode))
					{
						FirstErrorCode = EHttpResponseCodes::Type(statusCode);
						FirstErrorString = UTF8_TO_TCHAR(requestError.c_str());
					}
				}
			};
		std::shared_ptr<bool> bIgnoreMissingConstructionDetailingECClass;
		if (EElementsMetadata::ConstructionDetailing == KindOfMetadata)
		{
			bIgnoreMissingConstructionDetailingECClass = std::make_shared<bool>(false);
			funcFilterError = [this, funcStoreFirstErrorCode, bIgnoreMissingConstructionDetailingECClass]
				(long statusCode, std::string const& requestError, bool& bAllowRetry, bool& bLogError)
				{
					if (requestError.find("ECClass 'Construction.ConstructionDetailingElementSplitsGeometricElement3d' does not exist")
						!= std::string::npos)
					{
						bAllowRetry = false;
						bLogError = false;
						// Flag the error to be ignored in OnQueryCompleted so that finalization can happen
						// and 4D actually become available!
						{
							ITwinHttp::FLock Lock(Mutex);
							*bIgnoreMissingConstructionDetailingECClass = true;
						}
					}
					else
					{
						funcStoreFirstErrorCode(statusCode, requestError, bAllowRetry, bLogError);
					}
				};
		}
		else
		{
			funcFilterError = std::move(funcStoreFirstErrorCode);
		}
		IModel.GetMutableWebServices()->QueryIModelRows({}, {}, {}, {}, 0, 0, // everything's in RequestInfo
			{},
			[wptr, RequestInfo, currentQueryRowStart, bIgnoreMissingConstructionDetailingECClass]
			(const AdvViz::expected<AdvViz::SDK::Http::Response, std::string>& exp)
			{
				std::shared_ptr<FPaginatedIModelRowsQueries> pThis = wptr.lock();
				if (!pThis)
					return;

				BE_LOGD("ITwinAPI", pThis->Description << ": http query end rowstart:" << currentQueryRowStart << " count:" << pThis->QueryRowCount << " RequestInfoId:" << RequestInfo.get());

				bool bSuccess = true;
				FString QueryResult;
				if (!exp)
				{
					BE_LOGE("ITwinAPI", "iModel request Failed: " << exp.error());
					bSuccess = false;
				}
				else
				{
					QueryResult = UTF8_TO_TCHAR(exp->second.c_str());
				}
				if (!pThis->OnQueryCompleted(bSuccess, QueryResult, RequestInfo, currentQueryRowStart == -1,
					bIgnoreMissingConstructionDetailingECClass && (*bIgnoreMissingConstructionDetailingECClass)))
				{
					BE_LOGE("ITwinAPI", "iModel request not recognized");
				}
			},
			AdvViz::SDK::Http::EAsyncCallbackExecutionMode::WorkerThread,
			&(*RequestInfo), std::move(funcFilterError));
	}
}

bool FPaginatedIModelRowsQueries::OnQueryCompleted(bool const bSuccess,
	std::variant<FString, TSharedPtr<FJsonObject>> const& QueryResult,
	std::shared_ptr<AdvViz::SDK::ITwinAPIRequestInfo> RequestInfo,
	bool const bTableCountReply, bool const bIgnoreError)
{
	// cleanup() must be called before fctFinish(), otherwise it will be done automatically
	// when going out of scope
	Be::CleanUpGuard PageDecrementer([this]()
		{
			ITwinHttp::FLock Lock(Mutex);
			NumPageInProgress--;
		});

	bool const bFromCache = (QueryResult.index() == 1);
	auto& IModelInternals = GetInternals(IModel);
	TSceneMappingPtr sceneMapping = IModelInternals.SceneMapping;
	std::weak_ptr<FPaginatedIModelRowsQueries> wptr = shared_from_this();

	auto fctFinish = [wptr, sceneMapping, bFromCache]() {
		auto pThis = wptr.lock();
		if (!pThis)
			return;

		{
			ITwinHttp::FLock Lock(pThis->Mutex);
			if (pThis->State == EState::Finished)
				return;

			BE_LOGD("ITwinAPI", pThis->Description << " Check finished NumPageInProgress:"
								<< pThis->NumPageInProgress << " lastPageReached:" << pThis->lastPageReached);

			if (pThis->NumPageInProgress != 0 || !pThis->lastPageReached)
				return;

			BE_LOGI("ITwinAPI", pThis->Description << ": page query finished\n Total retrieved from "
								// likely all retrieved from same source...
								<< (bFromCache ? "cache: " : "remote: ") << pThis->TotalRowsParsed);
		}

		// This call will release hold of the cache folder, which will "often" allow reuse by cloned
		// actor when entering PIE (unless it was not yet finished downloading, of course)
		{
			ITwinHttp::FLock Lock(pThis->Mutex);
			pThis->UninitializeCache();
			BE_LOGD("ITwinAPI", pThis->Description << " final preparation started");
		}

		FITwinSceneMapping::CheckParentChildGraph(sceneMapping);
		{
			auto SceneMappingLock = sceneMapping->GetAutoLock();
			SceneMappingLock->FinishedParsingIModelMetadata();
		}
		{
			ITwinHttp::FLock Lock(pThis->Mutex);
			ensure(EState::NotStarted != pThis->State);
			if (EState::Running == pThis->State)
				pThis->State = EState::Finished;
			//else: Cancelled or StoppedOnError, don't change!
			BE_LOGD("ITwinAPI", pThis->Description << " final preparation finished");
		}
	};

	{
		ITwinHttp::FLock Lock(Mutex);
		if (EState::Cancelled == State || EState::StoppedOnError == State)
		{
			BE_LOGI("ITwinAPI", Description << ": queries cancelled"
								<< (EState::StoppedOnError == State) ? " (on error)" : "");
			PageDecrementer.cleanup();
			fctFinish();
			return true;
		}
		if (EState::NeedRestart == State)
		{
			BE_LOGI("ITwinAPI", Description << ": queries interrupted, will restart...");
			DoRestart();
			return true;
		}
		if (!bSuccess && !bIgnoreError)
		{
			State = EState::StoppedOnError;
			PageDecrementer.cleanup();
			fctFinish();
			if (OnLoadProgressUpdated)
				OnLoadProgressUpdated();
			// ResetSchedules will cancel the existing 4D querying process, which will not restart because
			// of the error state just flagged. It will broadcast OnScheduleQueryingStatusChanged(false)
			// instead and thus notify Carrot's MainPanel and the iTS part if present...
			if (IsValid(IModel.Synchro4DSchedules))
				IModel.Synchro4DSchedules->ResetSchedules();
			return true;
		}
	}
	int RowsParsed = 0;
	bool bHasReceivedTableCount = false;
	TSharedPtr<FJsonObject> JsonObj;
	if (bFromCache)
	{
		JsonObj = std::get<1>(QueryResult);
	}
	else
	{
		BE_LOGD("ITwinAPI", Description << ": Deserialize json RequestInfoId:" << RequestInfo.get() << " started");
		if (Cache.IsValid())
			Cache.Write(*RequestInfo, std::get<0>(QueryResult), true, Mutex);
		auto Reader = TJsonReaderFactory<TCHAR>::Create(std::get<0>(QueryResult));
		if (!FJsonSerializer::Deserialize(Reader, JsonObj))
			JsonObj.Reset();
		BE_LOGD("ITwinAPI", Description << ": Deserialize json RequestInfoId:" << RequestInfo.get() << " finished");
	}

	TArray<TSharedPtr<FJsonValue>> const* JsonRows = nullptr;
	if (JsonObj.IsValid() && JsonObj->TryGetArrayField(TEXT("data"), JsonRows))
	{
		if (bTableCountReply)
		{
			if (ensure(JsonRows->Num() == 1))
			{
				auto const& Entries = (*JsonRows)[0]->AsArray();
				if (ensure(!Entries.IsEmpty() && Entries[0]->TryGetNumber(TotalRowsExpected)))
				{
					bHasReceivedTableCount = true;
					if (TotalRowsExpected > 0)
					{
						auto SceneMappingLock = IModelInternals.SceneMapping->GetAutoLock();
						SceneMappingLock->ReserveIModelMetadata(TotalRowsExpected);
					}
				}
			}
		}
		else
		{
			std::stringstream log;
			log << Description << ": parsing for RequestInfoId:" << RequestInfo.get();

			PageDecrementer.release(); // why can't I move it :-(
			UE::Tasks::Launch(UE_SOURCE_LOCATION,
				[JsonRows, wptr, sceneMapping, JsonObj, strlog = log.str(), fctFinish, bFromCache
				// Moving does not work, I got compilation error :/
				/*MovedPageDecrementer = std::move(PageDecrementer)*/]() mutable
				{
					auto pThis = wptr.lock();
					if (!pThis)
						return;

					BE_LOGD("ITwinAPI", strlog << " started");
					int RowsParsed = 0;
					switch (pThis->KindOfMetadata)
					{
					case EElementsMetadata::Combined:
						RowsParsed = FITwinSceneMapping::ParseIModelMetadata(sceneMapping, *JsonRows);
						break;
					case EElementsMetadata::ConstructionDetailing:
					{
						auto SceneMappingLock = sceneMapping->GetAutoLock();
						RowsParsed = SceneMappingLock->ParseConstructionDetailingParentIDs(*JsonRows);
						break;
					}
					default: ensure(false); break;
					}
					BE_LOGD("ITwinAPI", strlog << " finished");

					{
						ITwinHttp::FLock Lock(pThis->Mutex);
						pThis->NumPageInProgress--;
						pThis->TotalRowsParsed += RowsParsed;
						if (RowsParsed > 0)
							if (bFromCache)
								pThis->RequestsFromCache++;
							else
								pThis->RequestsFromRemote++;
					}

					fctFinish();
				},
				UE::Tasks::ETaskPriority::Normal);
		}
	}

	if ((JsonRows && JsonRows->Num() > 0) || bHasReceivedTableCount)
	{
		if (bHasReceivedTableCount)
		{
			BE_LOGI("ITwinAPI", Description << ": table count retrieved from " << (bFromCache ? "cache: " : "remote: ")
								<< TotalRowsExpected);
		}
		else
		{
			BE_LOGD("ITwinAPI", Description << ": " << TotalRowsParsed << " rows retrieved from "
				<< (bFromCache ? "cache" : "remote") << ", asking for more...");
			if (TotalRowsExpected != -1)
			{
				if (OnLoadProgressUpdated)
					OnLoadProgressUpdated();
			}
		}
		int CurrentNumPageInProgress;
		{
			ITwinHttp::FLock Lock(Mutex);
			CurrentNumPageInProgress = NumPageInProgress;
		}
		// "<" instead of former "<=" because now the counter is decremented after this (except in the unlikely
		// case where the task above has already executed when we reach this).
		if (CurrentNumPageInProgress < MaxNumPageInProgress)
		{
			QueryNextPage();
		}
		else
		{
			static std::atomic_int taskcounter{ 0 };
			AdvViz::SDK::UniqueDelayedCall("OnQueryCompleted.QueryNextPage" + std::to_string(taskcounter++),
				[wptr]()
				{
					auto pThis = wptr.lock();
					if (!pThis)
						return AdvViz::SDK::DelayedCall::EReturnedValue::Done;
					{
						ITwinHttp::FLock Lock(pThis->Mutex);
						if (pThis->NumPageInProgress > pThis->MaxNumPageInProgress) // still too many tasks
						{
							return AdvViz::SDK::DelayedCall::EReturnedValue::Repeat;
						}
					}
					UE::Tasks::Launch(UE_SOURCE_LOCATION,
						[wptr]()
						{
							auto pThis = wptr.lock();
							if (!pThis)
								return;
							pThis->QueryNextPage();
						},
						UE::Tasks::ETaskPriority::BackgroundLow);
					return AdvViz::SDK::DelayedCall::EReturnedValue::Done;
				},
				0.064f);
		}
	}
	else
	{
		// Current page queried returned no result => signal completion.
		lastPageReached = true;
		// Must decrement before fctFinish, in case we are the last one.
		if (!PageDecrementer.isClean())
			PageDecrementer.cleanup();
		fctFinish();
	}
	return true;
}

void FPaginatedIModelRowsQueries::UninitializeCache()
{
	Cache.Uninitialize();
	LastCacheFolderUsed = {};// otw Cache is never re-init!! see azdev#1621189, Investigation Notes
}

bool FPaginatedIModelRowsQueries::ClearCacheOnDisk()
{
	if (EState::Finished != GetState())
		return false;
	FString const CacheFolder = GetCacheFolder(KindOfMetadata, IModel);
	if (ensure(!CacheFolder.IsEmpty()))
	{
		return IFileManager::Get().DeleteDirectory(*CacheFolder, /*requireExists*/false, /*recurse*/true);
	}
	return false;
}

void FPaginatedIModelRowsQueries::OnIModelUninit()
{
	UninitializeCache();
}
