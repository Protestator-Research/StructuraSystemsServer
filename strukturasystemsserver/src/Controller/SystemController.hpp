#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace StructuraSystems::Server
{
	class SystemController : public drogon::HttpController<SystemController>
	{
	public:
		virtual ~SystemController() = default;

		METHOD_LIST_BEGIN
		ADD_METHOD_TO(SystemController::getVersionInfo,"/version",drogon::Get);
		METHOD_LIST_END

		void getVersionInfo(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK,drogon::CT_TEXT_PLAIN);
			response->setBody("3.0.alpha");
			callback(response);
		}
	};
}