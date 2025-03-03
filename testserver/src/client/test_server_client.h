/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#ifndef TEST_SERVER_CLIENT_H
#define TEST_SERVER_CLIENT_H

#include "iremote_object.h"
#include "system_ability_load_callback_stub.h"
#include "itest_server_interface.h"
#include <common_event_manager.h>
#include <common_event_subscribe_info.h>

namespace OHOS::testserver {
    class TestServerClient {
    public:
        static TestServerClient &GetInstance();
        sptr<ITestServerInterface> LoadTestServer();
        int32_t SetPasteData(std::string text);
        bool PublishCommonEvent(const EventFwk::CommonEventData &event);
        void FrequencyLock();
        void SpDaemonProcess(int daemonCommand);
    private:
        TestServerClient() = default;
        ~TestServerClient() = default;
        void InitLoadState();
        bool WaitLoadStateChange(int32_t systemAbilityId);

        sptr<IRemoteObject> remoteObject_ = nullptr;
    };
} // namespace OHOS::testserver

#ifdef __cplusplus
extern "C" {
#endif
void FrequencyLockPlugin();
int32_t SpDaemonProcessPlugin(int32_t daemonCommand);
#ifdef __cplusplus
}
#endif

#endif // TEST_SERVER_CLIENT_H