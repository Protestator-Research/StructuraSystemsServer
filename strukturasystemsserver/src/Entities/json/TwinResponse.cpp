//
// Created by Moritz Herzog on 24.02.26.
//

#include "TwinResponse.h"

namespace StructuraSystems {
    namespace Server {
        TwinResponse::TwinResponse(std::string name, std::shared_ptr<SysMLv2::REST::Project> owningProject,
            std::shared_ptr<SysMLv2::REST::Commit> referencedCommit) : Tag(name,owningProject,referencedCommit) {
            Type = "Twin";
        }

        TwinResponse::TwinResponse(const std::string &jsonStringOrName) : Tag(jsonStringOrName) {
            Type = "Twin";
        }
    } // Server
} // StructuraSystems