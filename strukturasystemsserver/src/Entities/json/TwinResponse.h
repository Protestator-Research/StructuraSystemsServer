//
// Created by Moritz Herzog on 24.02.26.
//

#pragma once

#include <sysmlv2/rest/entities/Tag.h>

namespace StructuraSystems::Server {
    class TwinResponse  : public SysMLv2::REST::Tag {
    public:
        explicit TwinResponse(std::string name, std::shared_ptr<SysMLv2::REST::Project> owningProject, std::shared_ptr<SysMLv2::REST::Commit> referencedCommit);
        explicit TwinResponse(const std::string &jsonStringOrName);
        virtual ~TwinResponse() = default;
    };
} // StructuraSystems::Server

