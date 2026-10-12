// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "Platform.h"
#include <vector>

namespace Graphing
{
    struct IBitmap
    {
        virtual std::vector<BYTE> GetData() const = 0;
    };
}
