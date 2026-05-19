/*--------------------------------------------------------------------------------------+
|
|     $Source: httpCprImpl.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <cpr/cpr.h>

#include <string>
#include "http.h"

namespace AdvViz::SDK
{
#ifdef WITH_HTTPCPR
	namespace Impl
	{
		class HttpCpr : public Http, Tools::TypeId<HttpCpr>
		{
		public:
			HttpCpr() {}
			void SetBasicAuth(const char* login, const char* passwd) override;
			bool DecodeBase64(const std::string& src, RawData& buffer) const override;
			Http::Response DoPut(const std::string& url, const BodyParams& body = {}, const Headers& headers = {}) override;
			Http::Response DoPutBinaryFile(const std::string& url, const std::string& filePath, const Headers& headers = {}) override;
			Http::Response DoPatch(const std::string& url, const BodyParams& body = {}, const Headers& headers = {}) override;
			void DoAsyncPatch(const std::function<void(Response&)>& callback, const std::string& url,
				const BodyParams& body, const Headers& headers, EAsyncCallbackExecutionMode asyncCBExecMode) override;
			Http::Response DoPost(const std::string& url, const BodyParams& body = {}, const Headers& headers = {}) override;
			void DoAsyncPost(const std::function<void(Response&)>& callback, const std::string& url,
				const BodyParams& body, const Headers& headers, EAsyncCallbackExecutionMode asyncCBExecMode) override;
			void DoAsyncPut(const std::function<void(Response&)>& callback, const std::string& url,
				const BodyParams& body, const Headers& headers, EAsyncCallbackExecutionMode asyncCBExecMode) override;
			Http::Response DoPostFile(const std::string& url, const std::string& fileParamName, const std::string& filePath,
				const KeyValueVector& extraParams = {}, const Headers& h = {}) override;
			void DoAsyncPostFile(const std::function<void(Response&)>& callback, const std::string& url,
				const std::string& fileParamName, const std::string& filePath,
				const KeyValueVector& extraParams = {}, const Headers& h = {},
				EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default) override;
			Http::Response DoGet(const std::string& url, const Headers& headers = {}, bool isFullUrl = false) override;
			void DoAsyncGet(const std::function<void(Response&)>& callback, const std::string& url,
				const Headers& headers = {}, bool isFullUrl = false,
				EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default) override;
			Http::Response DoDelete(const std::string& url, const BodyParams& body = {}, const Headers& headers = {}) override;
			void DoAsyncDelete(const std::function<void(Response&)>& callback, const std::string& url,
				const BodyParams& body = {}, const Headers& headers = {},
				EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default) override;

			using Tools::TypeId<HttpCpr>::GetTypeId;
			std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
			bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || Http::IsTypeOf(i); }

			std::string EncodeForUrl(const std::string& str) const override;

			bool SupportsExecuteAsyncCallbackInMainThread() const override { return false; }

		private:
			std::unique_ptr<cpr::Authentication> auth_;

			std::string GetBaseUrlStr() const;

			

		};
	};
#endif
}
