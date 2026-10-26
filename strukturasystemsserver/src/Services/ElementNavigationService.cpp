#include "ElementNavigationService.h"

#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/Data.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Project.h>
#include <kerml/root/elements/Element.h>
#include <kerml/root/elements/Relationship.h>
#include <sysmlv2/rest/entities/Direction.h>
#include <sysmlv2/rest/versioning/ElementReferences.h>

#include <algorithm>
#include <set>

#include "ProjectVersioningService.h"

namespace StructuraSystems::Server
{
	std::shared_ptr<ElementNavigationService> ElementNavigationService::getInstance()
	{
		struct concreteElementService : public  ElementNavigationService {};
		// A function-local static is initialised exactly once, even with concurrent first calls.
		static const std::shared_ptr<ElementNavigationService> instance = std::make_shared<concreteElementService>();
		return instance;
	}

	namespace
	{
		std::shared_ptr<KerML::Entities::Element> asElement(const std::shared_ptr<SysMLv2::REST::DataVersion>& version)
		{
			return version != nullptr ? std::dynamic_pointer_cast<KerML::Entities::Element>(version->getPayload()) : nullptr;
		}

		bool contains(const std::vector<boost::uuids::uuid>& ids, const boost::uuids::uuid& id)
		{
			return std::find(ids.begin(), ids.end(), id) != ids.end();
		}
	}

	// The elements at a commit are the Data of its versionedData (7.1.2) that are KerML elements: everything that was created
	// or changed in the commit or in an earlier one and was not deleted since.
	std::vector<std::shared_ptr<KerML::Entities::Element>> ElementNavigationService::getElements(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit)
	{
		const auto state = ProjectVersioningService::getInstance()->getVersionedData(project, commit);
		std::vector<std::shared_ptr<KerML::Entities::Element>> elements;
		for (const auto& [identity, version] : *state) {
			if (const auto element = asElement(version))
				elements.push_back(element);
		}
		return elements;
	}

	std::shared_ptr<KerML::Entities::Element> ElementNavigationService::getElementById(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid elementId){
		const auto state = ProjectVersioningService::getInstance()->getVersionedData(project, commit);
		const auto found = state->find(elementId);
		return found != state->end() ? asElement(found->second) : nullptr;
	}

	// The ids are compared as written in the JSON of the relationship: a payload is deserialized on its own, so source() and
	// target() of a stored relationship are not resolved.
	std::vector<std::shared_ptr<KerML::Entities::Relationship>> ElementNavigationService::getRelationshipsByRelatedElement(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid elementId, int direction)
	{
		const auto state = ProjectVersioningService::getInstance()->getVersionedData(project, commit);
		std::vector<std::shared_ptr<KerML::Entities::Relationship>> relationships;
		for (const auto& [identity, version] : *state) {
			const auto relationship = version != nullptr ? std::dynamic_pointer_cast<KerML::Entities::Relationship>(version->getPayload()) : nullptr;
			if (relationship == nullptr)
				continue;
			const auto ends = SysMLv2::REST::Versioning::relationshipEnds(version->getPayload());
			const bool isOut = contains(ends.source, elementId);
			const bool isIn = contains(ends.target, elementId);
			bool matches = false;
			switch (direction) {
				case SysMLv2::REST::OUT: matches = isOut; break;
				case SysMLv2::REST::IN: matches = isIn; break;
				default: matches = isOut || isIn || contains(ends.relatedElement, elementId); break;
			}
			if (matches)
				relationships.push_back(relationship);
		}
		return relationships;
	}

	// A root element has no owner: its JSON names neither an owner nor an owning relationship, and no relationship of the
	// commit owns it (ownedRelatedElement).
	std::vector<std::shared_ptr<KerML::Entities::Element>> ElementNavigationService::getRootElements(std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit)
	{
		const auto state = ProjectVersioningService::getInstance()->getVersionedData(project, commit);
		std::set<boost::uuids::uuid> owned;
		for (const auto& [identity, version] : *state) {
			const auto ends = SysMLv2::REST::Versioning::relationshipEnds(version->getPayload());
			owned.insert(ends.ownedRelatedElement.begin(), ends.ownedRelatedElement.end());
		}

		std::vector<std::shared_ptr<KerML::Entities::Element>> roots;
		for (const auto& [identity, version] : *state) {
			const auto element = asElement(version);
			if (element != nullptr && owned.find(identity) == owned.end() && !SysMLv2::REST::Versioning::declaresOwner(version->getPayload()))
				roots.push_back(element);
		}
		return roots;
	}
}
