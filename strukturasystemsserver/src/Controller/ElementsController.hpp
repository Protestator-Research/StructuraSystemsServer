#pragma once

#include <drogon/HttpController.h>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <kerml/root/elements/Element.h>
#include <kerml/root/elements/Relationship.h>
#include <sysmlv2/rest/entities/Direction.h>

#include "SystemController.hpp"
#include "../Services/ProjectVersioningService.h"
#include "../Services/ServerProjectService.h"
#include "../Services/ElementNavigationService.h"
#include "../Filter/JwtFilter.hpp"
#include "Helper/RequestHelper.hpp"
#include "Helper/ProjectAccessHelper.hpp"


namespace StructuraSystems::Server
{
	class ElementsController : public drogon::HttpController<StructuraSystems::Server::ElementsController>
	{
	public:
		METHOD_LIST_BEGIN
		ADD_METHOD_TO(ElementsController::getElements,"/projects/{1:projectId}/commits/{2:commitId}/elements", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ElementsController::getElementWithId,"/projects/{1:projectId}/commits/{2:commitId}/elements/{3:elementId}", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ElementsController::getRelationshipsByRelatedElement,"/projects/{1:projectId}/commits/{2:commitId}/elements/{3:elementId}/relationships", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ElementsController::getRootElements,"/projects/{1:projectId}/commits/{2:commitId}/roots", drogon::Get, "StructuraSystems::Server::JwtFilter");
		// Alias of /roots
		ADD_METHOD_TO(ElementsController::getRootElements,"/projects/{1:projectId}/commits/{2:commitId}/root", drogon::Get, "StructuraSystems::Server::JwtFilter");
		METHOD_LIST_END


		void getElements(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& commitId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedCommitId = parseUuid(commitId);
			if (!parsedCommitId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return;
			}

			const auto commit = ProjectVerService->getCommitById(project, *parsedCommitId);
			if (commit==nullptr)
            {
                callback(jsonError(drogon::k404NotFound, "Commit not found."));
                return;
            }

			const auto elements = ElementNavService->getElements(project, commit);
			
			respondWithArray(request, elements, callback);
		}

		void getElementWithId(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string projectId, std::string commitId, std::string elementId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedCommitId = parseUuid(commitId);
			if (!parsedCommitId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return;
			}

			const auto commit = ProjectVerService->getCommitById(project, *parsedCommitId);
			if (commit==nullptr)
            {
                callback(jsonError(drogon::k404NotFound, "Commit not found."));
                return;
            }


			const auto parsedElementId = parseUuid(elementId);
			if (!parsedElementId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid element id."));
				return;
			}

			const auto element = ElementNavService->getElementById(project,commit,*parsedElementId);
			if (element==nullptr)
            {
                callback(jsonError(drogon::k404NotFound, "Element not found."));
                return;
            }


			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(element->serializeToJson());
			callback(response);
		}

		void getRelationshipsByRelatedElement(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string projectId, std::string commitId, std::string elementId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedCommitId = parseUuid(commitId);
			if (!parsedCommitId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return;
			}
			const auto parsedElementId = parseUuid(elementId);
			if (!parsedElementId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid element id."));
				return;
			}

			// Default is both.
			auto direction = SysMLv2::REST::BOTH;
			const auto directions = queryValues(request, "direction");
			if (!directions.empty())
			{
				try {
					direction = SysMLv2::REST::directionFromString(directions.front());
				}
				catch (const std::invalid_argument&) {
					callback(jsonError(drogon::k400BadRequest, "Invalid direction. Expected in, out or both."));
					return;
				}
			}

			const auto commit = ProjectVerService->getCommitById(project, *parsedCommitId);
			if (commit == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Commit not found."));
				return;
			}
			if (ElementNavService->getElementById(project, commit, *parsedElementId) == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Element not found."));
				return;
			}

			const auto relationships = ElementNavService->getRelationshipsByRelatedElement(project, commit, *parsedElementId, direction);
			respondWithArray(request, relationships, callback);
		}

		void getRootElements(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string projectId, std::string commitId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedCommitId = parseUuid(commitId);
			if (!parsedCommitId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return;
			}
			const auto commit = ProjectVerService->getCommitById(project, *parsedCommitId);
			if (commit == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Commit not found."));
				return;
			}

			respondWithArray(request, ElementNavService->getRootElements(project, commit), callback);
		}

	private:
		/// The elements and relationships of a commit are listed in the order of their identity.
		template<typename T>
		static void respondWithArray(const drogon::HttpRequestPtr& request, const std::vector<std::shared_ptr<T>>& items,
			const std::function<void(const drogon::HttpResponsePtr&)>& callback)
		{
			callback(paginate(request, items,
				[](const std::shared_ptr<T>& item) { return boost::uuids::to_string(item->getId()); },
				[](const std::shared_ptr<T>& item) { return item->serializeToJson(); }));
		}

		std::shared_ptr<ElementNavigationService> ElementNavService = ElementNavigationService::getInstance();
		std::shared_ptr<ServerProjectService> ProjectService = ServerProjectService::getInstance();
		std::shared_ptr<ProjectVersioningService> ProjectVerService = ProjectVersioningService::getInstance();
	};
}