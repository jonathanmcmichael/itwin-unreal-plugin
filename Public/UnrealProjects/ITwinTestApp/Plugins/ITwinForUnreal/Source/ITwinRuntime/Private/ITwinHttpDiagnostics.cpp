/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinHttpDiagnostics.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinLogCategory.h>

#include <HttpModule.h>
#include <Interfaces/IHttpRequest.h>
#include <Interfaces/IHttpResponse.h>
#include <Misc/EngineVersionComparison.h>
#include <Misc/Optional.h>
#include <HAL/IConsoleManager.h>

namespace
{
FString DescribeProbeResult(FHttpRequestPtr const& Request, FHttpResponsePtr const& Response,
	bool const bConnectedSuccessfully)
{
	TArray<FString> Details;
	Details.Reserve(9);
	Details.Emplace(FString::Printf(TEXT("connected=%s"), bConnectedSuccessfully ? TEXT("true") : TEXT("false")));
	if (Request)
	{
		Details.Emplace(FString::Printf(TEXT("verb=%s"), *Request->GetVerb()));
		Details.Emplace(FString::Printf(TEXT("url=%s"), *Request->GetURL()));
		Details.Emplace(FString::Printf(TEXT("status=%s"), EHttpRequestStatus::ToString(Request->GetStatus())));
		Details.Emplace(FString::Printf(TEXT("elapsed=%.3fs"), Request->GetElapsedTime()));
		if (TOptional<float> const Timeout = Request->GetTimeout())
		{
			Details.Emplace(FString::Printf(TEXT("requestTimeout=%.3fs"), Timeout.GetValue()));
		}
#if !UE_VERSION_OLDER_THAN(5, 4, 0)
		if (Request->GetStatus() == EHttpRequestStatus::Failed)
		{
			Details.Emplace(FString::Printf(TEXT("reason=%s"), LexToString(Request->GetFailureReason())));
		}
#endif
	}
	if (Response.IsValid())
	{
		Details.Emplace(FString::Printf(TEXT("code=%d"), Response->GetResponseCode()));
	}
	else
	{
		Details.Emplace(TEXT("response=invalid"));
	}
	return FString::Join(Details, TEXT(", "));
}

FString NormalizeProbeUrlArg(FString Url)
{
	Url.TrimStartAndEndInline();
	if (Url.Len() >= 2)
	{
		TCHAR const First = Url[0];
		TCHAR const Last = Url[Url.Len() - 1];
		if ((First == TEXT('"') && Last == TEXT('"'))
			|| (First == TEXT('\'') && Last == TEXT('\'')))
		{
			Url = Url.Mid(1, Url.Len() - 2);
			Url.TrimStartAndEndInline();
		}
	}
	return Url;
}
}

static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinHttpProbe(
	TEXT("ITwin.HttpProbe"),
	TEXT("Issue a direct Unreal HTTP GET request and log timeout/failure details. Args: [url] [timeoutSecs]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		FString const Url = Args.IsValidIndex(0) && !Args[0].IsEmpty()
			? NormalizeProbeUrlArg(Args[0])
			: TEXT("https://api.bentley.com");

		TOptional<float> RequestTimeout;
		if (Args.IsValidIndex(1))
		{
			double ParsedTimeout = 0.;
			if (LexTryParseString(ParsedTimeout, *Args[1]))
			{
				RequestTimeout = static_cast<float>(ParsedTimeout);
			}
			else
			{
				UE_LOG(LogITwin, Warning, TEXT("[ITwinHttpProbe] Ignoring invalid timeout '%s'."), *Args[1]);
			}
		}

		FHttpModule& HttpModule = FHttpModule::Get();
		UE_LOG(LogITwin, Display,
			TEXT("[ITwinHttpProbe] Starting GET %s with module timeouts total=%.3fs connection=%.3fs activity=%.3fs maxConnectionsPerServer=%d%s"),
			*Url,
			HttpModule.GetHttpTotalTimeout(),
			HttpModule.GetHttpConnectionTimeout(),
			HttpModule.GetHttpActivityTimeout(),
			HttpModule.GetHttpMaxConnectionsPerServer(),
			RequestTimeout ? *FString::Printf(TEXT(", requestTimeoutOverride=%.3fs"), RequestTimeout.GetValue()) : TEXT(""));

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = HttpModule.CreateRequest();
		Request->SetURL(Url);
		Request->SetVerb(TEXT("GET"));
		Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		if (RequestTimeout)
		{
			Request->SetTimeout(RequestTimeout.GetValue());
		}
		Request->OnStatusCodeReceived().BindLambda([](FHttpRequestPtr RequestPtr, int32 StatusCode)
		{
			UE_LOG(LogITwin, Display,
				TEXT("[ITwinHttpProbe] Status code received for %s: %d"),
				RequestPtr.IsValid() ? *RequestPtr->GetURL() : TEXT("<invalid request>"),
				StatusCode);
		});
		Request->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			UE_LOG(LogITwin, Warning,
				TEXT("[ITwinHttpProbe] Completed: %s"),
				*DescribeProbeResult(RequestPtr, Response, bConnectedSuccessfully));
		});

		if (!Request->ProcessRequest())
		{
			UE_LOG(LogITwin, Error,
				TEXT("[ITwinHttpProbe] Failed to start request for %s"),
				*Url);
		}
	}));
