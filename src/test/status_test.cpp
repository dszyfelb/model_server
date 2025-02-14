//*****************************************************************************
// Copyright 2020 Intel Corporation
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
#include <unordered_set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../status.hpp"
#include "test_utils.hpp"

namespace ovms {
StatusCode& operator++(StatusCode& statusCode) {
    if (statusCode == StatusCode::STATUS_CODE_END) {
        throw std::out_of_range("for E& operator ++ (E&)");
    }
    statusCode = StatusCode(static_cast<std::underlying_type<StatusCode>::type>(statusCode) + 1);
    return statusCode;
}

TEST(StatusCodeTest, AllStatusCodesMapped) {
    for (auto statusCode = StatusCode::OK; statusCode != StatusCode::STATUS_CODE_END; ++statusCode) {
        Status status = Status(statusCode);
        ASSERT_NE(status.string(), "Undefined error");
    }
}
}  // namespace ovms
