/*
 * Copyright 2021 Arm Limited. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of PAF, the Physical Attack Framework.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "PAF/SCA/utils.h"

#include <cmath>

using std::fabs;
using std::vector;

namespace PAF {
namespace SCA {

double find_max(const vector<double> &data, size_t *index) {

    if (data.empty()) {
        *index = -1;
        return 0.0;
    }

    double max_v = data[0];
    *index = 0;

    for (size_t i = 1; i < data.size(); i++)
        if (fabs(data[i]) > fabs(max_v)) {
            max_v = data[i];
            *index = i;
        }

    return max_v;
}

} // namespace SCA
} // namespace PAF
