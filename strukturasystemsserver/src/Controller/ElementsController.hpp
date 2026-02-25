#pragma once

#include <drogon/HttpController.h>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <kerml/root/elements/Element.h>

#include "SystemController.hpp"
#include "../Services/ProjectVersioningService.h"
#include "../Services/ServerProjectService.h"
#include "../Services/ElementNavigationService.h"
#include "../Services/JwtFilter.hpp"


namespace StructuraSystems::Server
{
	class ElementsController : public drogon::HttpController<StructuraSystems::Server::ElementsController>
	{
	public:
		METHOD_LIST_BEGIN
		ADD_METHOD_TO(ElementsController::getElements,"/projects/{1:projectId}/commits/{2:commitId}/elements", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ElementsController::getElementWithId,"/projects/{1:projectId}/commits/{2:commitId}/elements/{3:elementId}", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ElementsController::getRelationshipsByRelatedElement,"/projects/{1:projectId}/commits/{2:commitId}/elements/{3:elementId}/relationships", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ElementsController::getRootElements,"/projects/{1:projectId}/commits/{2:commitId}/root", drogon::Get, "StructuraSystems::Server::JwtFilter");
		METHOD_LIST_END


		void getElements(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& commitId)
		{
			auto string_generator = boost::uuids::string_generator();
			const auto project = ProjectService->getProjectById(string_generator(projectId));
			const auto commit = ProjectVerService->getCommitById(project, string_generator(commitId));
			const auto elements = ElementNavService->getElements(project, commit);
			
			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < elements.size(); i++)
			{
				returnValue += elements[i]->serializeToJson();
				if (i < (elements.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(returnValue);
			callback(response);
		}

		void getElementWithId(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string projectId, std::string commitId, std::string elementId)
		{
			auto string_generator = boost::uuids::string_generator();

			const auto project = ProjectService->getProjectById(string_generator(projectId));
			const auto commit = ProjectVerService->getCommitById(project, string_generator(commitId));
			const auto element = ElementNavService->getElementById(project,commit,boost::uuids::string_generator()(elementId));

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(element->serializeToJson());
			callback(response);
		}

		void getRelationshipsByRelatedElement(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string , std::string , std::string )
		{
			//TODO Not yet implemented.
			callback(drogon::HttpResponse::newHttpResponse(drogon::k501NotImplemented,drogon::CT_TEXT_PLAIN));
		}

		void getRootElements(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string , std::string)
		{
			//TODO Not yet implemented.
			callback(drogon::HttpResponse::newHttpResponse(drogon::k501NotImplemented,drogon::CT_TEXT_PLAIN));
		}

	private:
		std::shared_ptr<ElementNavigationService> ElementNavService = ElementNavigationService::getInstance();
		std::shared_ptr<ServerProjectService> ProjectService = ServerProjectService::getInstance();
		std::shared_ptr<ProjectVersioningService> ProjectVerService = ProjectVersioningService::getInstance();
	};
}