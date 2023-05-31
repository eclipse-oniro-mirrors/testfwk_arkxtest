/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd.
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
 */

#ifndef SCREEN_COPY_H
#define SCREEN_COPY_H

#include <functional>
#include <cstddef>

namespace OHOS::uitest {
    using ScreenCopyHandler = std::function<void (uint8_t *, std::size_t)>;
    bool StartScreenCopy(float scale, ScreenCopyHandler handler);
    void StopScreenCopy();
    void NotifyScreenCopyFrameConsumed();
}

#endif