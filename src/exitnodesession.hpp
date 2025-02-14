//*****************************************************************************
// Copyright 2021 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************
#pragma once

#include <string>

#include <openvino/openvino.hpp>

#include "nodesession.hpp"
#include "nodesessionmetadata.hpp"
#include "status.hpp"
#include "tensormap.hpp"

namespace ovms {

class Node;
class TensorInfo;

class ExitNodeSession : public NodeSession {
public:
    ExitNodeSession(const NodeSessionMetadata& metadata, const std::string& nodeName, uint32_t inputsCount, const CollapseDetails& collapsingDetails);
    ExitNodeSession(const NodeSessionMetadata&& metadata, const std::string& nodeName, uint32_t inputsCount, const CollapseDetails& collapsingDetails);
    virtual ~ExitNodeSession();

    const TensorMap& getInputTensors() const;
    void release() override;
};
}  // namespace ovms
