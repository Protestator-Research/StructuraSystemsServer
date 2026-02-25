//
// Created by Moritz Herzog on 24.02.26.
//

#pragma once

#include <string>
#include <memory>
#include <sysmlv2/rest/entities/IEntity.h>
#include <sysmlv2/rest/entities/Commit.h>

namespace StructuraSystems::Server {
    class TwinRequest : public SysMLv2::REST::IEntity{
    public:
        explicit TwinRequest(std::string name, std::shared_ptr<SysMLv2::REST::Commit> referencedCommit);
        explicit TwinRequest(std::string jsonString);
        virtual ~TwinRequest() = default;

        std::string serializeToJson() override;

        std::string name();
        std::shared_ptr<SysMLv2::REST::Commit> referencedCommit();
    protected:
        void deserializeAndPopulate(const std::string &jsonString) override;

    private:
        std::string Name;
        std::string Type;
        std::shared_ptr<SysMLv2::REST::Commit> ReferencedCommit;
    };
} // StructuraSystems::Server

