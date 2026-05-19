/*--------------------------------------------------------------------------------------+
|
|     $Source: PaginatedIModelRowsQuerying.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinWebServices/ITwinWebServices.h>
#include <Network/JsonQueriesCache.h>

#include <Interfaces/IHttpResponse.h>
#include <Templates/SharedPointer.h>

#include <functional>
#include <memory>
#include <variant>

class AITwinIModel;
class FJsonObject;

enum class EElementsMetadata : uint8 {
	/// A single query now combines parent-child relationships, bounding boxes, Source ID's, and FederatedGuid's
	Combined,
	/// Construction detailing Elements' parents need an different kind of request that must be executed separately
	ConstructionDetailing
};

class FPaginatedIModelRowsQueries : public std::enable_shared_from_this<FPaginatedIModelRowsQueries>
{
public:
	enum class EState {
		NotStarted, Running, NeedRestart, Finished, StoppedOnError, Cancelled
	};

	using FOnLoadProgressUpdated = std::function<void()>;

	FPaginatedIModelRowsQueries(AITwinIModel& InIModel, EElementsMetadata InKindOfMetadata,
		ITwinHttp::FMutex& InMutex, FOnLoadProgressUpdated InOnLoadProgressUpdated);

	void Cancel();
	double PercentComplete() const;
	size_t GetRequestsFromRemote() const;
	size_t GetRequestsFromCache() const;
	EHttpResponseCodes::Type GetFirstErrorCode() const;
	FString GetFirstErrorString() const;
	EState GetState() const;
	void Restart();
	void UninitializeCache();
	bool ClearCacheOnDisk();
	void OnIModelUninit();

private:
	void QueryNextPage();
	/// \return Whether the reply was to a request emitted by this instance of metadata requester, and was
	///			thus parsed here.
	bool OnQueryCompleted(bool bSuccess, std::variant<FString, TSharedPtr<FJsonObject>> const& QueryResult,
		std::shared_ptr<AdvViz::SDK::ITwinAPIRequestInfo> RequestInfo, bool const bTableCountReply,
		bool const bIgnoreError);
	void DoRestart();

	AITwinIModel& IModel;
	EElementsMetadata const KindOfMetadata;
	FString const ECSQLQueryString;
	FString const ECSQLQueryCount;
	std::string const Description;
	FString LastCacheFolderUsed;
	FJsonQueriesCache Cache;
	ITwinHttp::FMutex& Mutex;
	FOnLoadProgressUpdated OnLoadProgressUpdated;

	EState State = EState::NotStarted;
	EHttpResponseCodes::Type FirstErrorCode = EHttpResponseCodes::Ok;
	FString FirstErrorString;
	int QueryRowStart = 0, TotalRowsParsed = 0, TotalRowsExpected = -1;
	HttpRequestID CurrentRequestID;
	/// Down from 50K to 32K to accommodate BBoxes in "Combined" metadata, because server reply is capped to 8MB!
	static constexpr int QueryRowCount = 32000;

	int NumPageInProgress = 0;
	size_t RequestsFromCache = 0, RequestsFromRemote = 0;
	const int MaxNumPageInProgress = 4;
	bool lastPageReached = false;
	bool bQueryTableCount = true;
};