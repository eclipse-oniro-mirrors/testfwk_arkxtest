/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#include <utility>

#include "gtest/gtest.h"
// For testing private method
#include "nlohmann/json.hpp"
#define private public
#include "frontend_api_handler.h"
#undef private
#include "dummy_controller.h"
#include "mock_element_node_iterator.h"
#include "mock_controller.h"
#include "widget_selector.h"
#include "ui_driver.h"

using namespace OHOS::uitest;
using namespace std;
using namespace nlohmann;

vector<shared_ptr<UiEventListener>> DummyEventMonitor::listeners_;

// test fixture
class FrontendApiHandlerTest : public testing::Test {
public:
    ~FrontendApiHandlerTest() override = default;
protected:
    void SetUp() override
    {
        auto dummyController = make_unique<DummyController>();
        dummyController->SetWorkable(true);
        UiDriver::RegisterController(move(dummyController));
    }

    void TearDown() override
    {
        // common-preprocessors works on all apiCall, delete after testing
        FrontendApiServer::Get().RemoveCommonPreprocessor("dummyProcessor");
    }
};

TEST_F(FrontendApiHandlerTest, basicJsonReadWrite)
{
    auto container = json::array();
    // write
    container.emplace_back("wyz");
    container.emplace_back(1);
    container.emplace_back(0.1);
    container.emplace_back(true);
    container.emplace_back(nullptr);
    // read
    ASSERT_EQ(5, container.size());
    ASSERT_EQ("wyz", container.at(INDEX_ZERO).get<string>());
    ASSERT_EQ(1, container.at(INDEX_ONE).get<uint8_t>());
    ASSERT_NEAR(0.1, container.at(INDEX_TWO).get<float>(), 1E-6);
    ASSERT_EQ(true, container.at(INDEX_THREE).get<bool>());
    ASSERT_EQ(nlohmann::detail::value_t::null, container.at(INDEX_FOUR).type());
}

static string GenerateUniqueId()
{
    return to_string(GetCurrentMicroseconds());
}

TEST_F(FrontendApiHandlerTest, noInvocationHandler)
{
    static auto apiId = GenerateUniqueId();
    auto call = ApiCallInfo {.apiId_ = "wyz"};
    auto reply = ApiReplyInfo();
    FrontendApiServer::Get().Call(call, reply);
    ASSERT_EQ(ERR_INTERNAL, reply.exception_.code_);
    ASSERT_TRUE(reply.exception_.message_.find("No handler found") != string::npos);
}

TEST_F(FrontendApiHandlerTest, addRemoveHandler)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    ASSERT_FALSE(server.HasHandlerFor(apiId));
    auto handler = [](const ApiCallInfo &in, ApiReplyInfo &out) {
        // nop
    };
    server.AddHandler(apiId, handler);
    ASSERT_TRUE(server.HasHandlerFor(apiId));

    json caller;
    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    server.Call(call, reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);

    server.RemoveHandler(apiId);
    ASSERT_FALSE(server.HasHandlerFor(apiId));
    server.Call(call, reply);
    ASSERT_EQ(ERR_INTERNAL, reply.exception_.code_) << "The handler should be unavailable after been removed";
}

TEST_F(FrontendApiHandlerTest, inOutDataTransfer)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    auto handler = [](const ApiCallInfo &in, ApiReplyInfo &out) {
        out.resultValue_ = in.paramList_.at(0).get<string>().length() + in.paramList_.at(1).get<uint8_t>();
    };
    server.AddHandler(apiId, handler);

    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    call.paramList_.emplace_back("wyz");
    call.paramList_.emplace_back(10);
    server.Call(call, reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_EQ(string("wyz").length() + 10, reply.resultValue_.get<uint8_t>());
}

TEST_F(FrontendApiHandlerTest, jsonExceptionDefance)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    auto handler = [](const ApiCallInfo &in, ApiReplyInfo &out) {
        out.resultValue_ = in.paramList_.at(100).get<uint8_t>();
    };
    server.AddHandler(apiId, handler);

    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    server.Call(call, reply);
    // json exception should be caught and reported properly
    ASSERT_EQ(ERR_INTERNAL, reply.exception_.code_);
    ASSERT_TRUE(reply.exception_.message_.find("json.exception.out_of_range") != string::npos);
}

TEST_F(FrontendApiHandlerTest, apiErrorDeliver)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    auto handler = [](const ApiCallInfo &in, ApiReplyInfo &out) { out.exception_.code_ = USAGE_ERROR; };
    server.AddHandler(apiId, handler);

    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    server.Call(call, reply);
    // api error should be delivered out to caller
    ASSERT_EQ(USAGE_ERROR, reply.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, commonPreprocessor)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    auto handlerCalled = false;
    auto handler = [&handlerCalled](const ApiCallInfo &in, ApiReplyInfo &out) { handlerCalled = true; };
    server.AddHandler(apiId, handler);

    auto processor = [](const ApiCallInfo &in, ApiReplyInfo &out) {
        if (in.paramList_.at(0).get<string>() == "oops") {
            out.exception_.code_ = ERR_OPERATION_UNSUPPORTED;
        }
    };
    server.AddCommonPreprocessor("dummyProcessor", processor);

    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    // handler should be called if preprocessing passed
    call.paramList_.emplace_back("nice");
    server.Call(call, reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(handlerCalled);
    // preprocessing failed, handler should not be called
    call.paramList_.clear();
    call.paramList_.emplace_back("oops");
    handlerCalled = false;
    server.Call(call, reply);
    ASSERT_EQ(ERR_OPERATION_UNSUPPORTED, reply.exception_.code_);
    ASSERT_FALSE(handlerCalled);
}

TEST_F(FrontendApiHandlerTest, checkAllHandlersRegisted)
{
    auto &server = FrontendApiServer::Get();
    // each frontend-api should have handler
    for (const auto &classDef : FRONTEND_CLASS_DEFS) {
        for (size_t idx = 0; idx < classDef->methodCount_; idx++) {
            const auto &methodDef = classDef->methods_[idx];
            if (methodDef.name_ == "UiWindow.isActived") {
                continue;
            }
            const auto message = "No handler registered for '" + string(methodDef.name_) + "'";
            ASSERT_TRUE(server.HasHandlerFor(methodDef.name_)) << message;
        }
    }
}

// API8 end to end call test
TEST_F(FrontendApiHandlerTest, callApiE2EOldAPi)
{
    const auto& server =  FrontendApiServer::Get();
    // create by1 with seed
    auto call0 = ApiCallInfo {.apiId_ = "By.text", .callerObjRef_ = string(REF_SEED_BY)};
    call0.paramList_.emplace_back("wyz");
    auto reply0 = ApiReplyInfo();
    server.Call(call0, reply0);
    // check result
    ASSERT_EQ(NO_ERROR, reply0.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply0.resultValue_.type()); // should return backend-by-ref
    const auto ref0 = reply0.resultValue_.get<string>();
    ASSERT_TRUE(ref0.find("By#") != string::npos);

    // go on creating combine by: isAfter (after ref0)
    auto call1 = ApiCallInfo {.apiId_ = "By.isAfter", .callerObjRef_ = string(REF_SEED_BY)};
    call1.paramList_.emplace_back(ref0);
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    ASSERT_EQ(NO_ERROR, reply1.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply1.resultValue_.type()); // should return backend-by-ref
    const auto ref1 = reply1.resultValue_.get<string>();
    ASSERT_TRUE(ref1.find("By#") != string::npos);
    // should always return a new By
    ASSERT_NE(ref0, ref1);

    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.create", .callerObjRef_ = string()};
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    ASSERT_EQ(NO_ERROR, reply2.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply2.resultValue_.type()); // should return backend-On-ref
    const auto ref2 = reply2.resultValue_.get<string>();
    ASSERT_TRUE(ref2.find("UiDriver#") != string::npos);

    auto call3 = ApiCallInfo {.apiId_ = "By.id", .callerObjRef_ = string(REF_SEED_BY)};
    call3.paramList_.emplace_back(1);
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    // check result
    ASSERT_EQ(NO_ERROR, reply3.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply3.resultValue_.type()); // should return backend-by-ref
    const auto ref3 = reply3.resultValue_.get<string>();
    ASSERT_TRUE(ref3.find("By#") != string::npos);

    auto call4 = ApiCallInfo {.apiId_ = "By.key", .callerObjRef_ = string(REF_SEED_BY)};
    call4.paramList_.emplace_back("1");
    auto reply4 = ApiReplyInfo();
    server.Call(call4, reply4);
    // check result
    ASSERT_EQ(NO_ERROR, reply4.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply4.resultValue_.type()); // should return backend-by-ref
    const auto ref4 = reply4.resultValue_.get<string>();
    ASSERT_TRUE(ref4.find("By#") != string::npos);
}

// API9+ end to end call test
TEST_F(FrontendApiHandlerTest, callApiE2E)
{
    const auto& server =  FrontendApiServer::Get();

    // create by1 with seed
    auto call0 = ApiCallInfo {.apiId_ = "On.text", .callerObjRef_ = string(REF_SEED_BY)};
    call0.paramList_.emplace_back("wyz");
    auto reply0 = ApiReplyInfo();
    server.Call(call0, reply0);
    // check result
    ASSERT_EQ(NO_ERROR, reply0.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply0.resultValue_.type()); // should return backend-On-ref
    const auto ref0 = reply0.resultValue_.get<string>();
    ASSERT_TRUE(ref0.find("On#") != string::npos);

    // go on creating combine by: isAfter (after ref0)
    auto call1 = ApiCallInfo {.apiId_ = "On.isAfter", .callerObjRef_ = string(REF_SEED_BY)};
    call1.paramList_.emplace_back(ref0);
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    // check result
    ASSERT_EQ(NO_ERROR, reply1.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply1.resultValue_.type()); // should return backend-On-ref
    const auto ref1 = reply1.resultValue_.get<string>();
    ASSERT_TRUE(ref1.find("On#") != string::npos);
    ASSERT_NE(ref0, ref1);

    // should always return a new By
    auto call2 = ApiCallInfo {.apiId_ = "Driver.create", .callerObjRef_ = string()};
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    ASSERT_EQ(NO_ERROR, reply2.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply2.resultValue_.type()); // should return backend-On-ref
    const auto ref2 = reply2.resultValue_.get<string>();
    ASSERT_TRUE(ref2.find("Driver#") != string::npos);
    
    auto call3 = ApiCallInfo {.apiId_ = "On.id", .callerObjRef_ = string(REF_SEED_BY)};
    call3.paramList_.emplace_back("1");
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    // check result
    ASSERT_EQ(NO_ERROR, reply3.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply3.resultValue_.type()); // should return backend-by-ref
    const auto ref3 = reply3.resultValue_.get<string>();
    ASSERT_TRUE(ref3.find("On#") != string::npos);
}

// FrontendAPiServer::ApiMapPre and ApiMapPost Test (Used for old api adaptation)
TEST_F(FrontendApiHandlerTest, apiMapTest) {
    const auto& server =  FrontendApiServer::Get();
    auto call0 = ApiCallInfo {.apiId_ = "UiDriver.findComponent", .callerObjRef_ = "UiDriver#0"};
    call0.paramList_.emplace_back("By#1");
    string oldApiName = server.ApiMapPre(call0);
    // Call_id and callerObjRef should be converted correctly.
    // Object parameters and object return value should be converted.
    ASSERT_EQ(call0.paramList_.at(0).get<string>(), "On#1");
    ASSERT_EQ(call0.apiId_, "Driver.findComponent");
    ASSERT_EQ(call0.callerObjRef_, "Driver#0");
    ASSERT_EQ(oldApiName, "UiDriver.findComponent");
    auto reply0 = ApiReplyInfo();
    reply0.resultValue_ = "Component#1";
    server.ApiMapPost(oldApiName, reply0);
    ASSERT_EQ(reply0.resultValue_.get<string>(), "UiComponent#1");

    oldApiName = "UiDriver.findComponents";
    auto reply1 = ApiReplyInfo();
    reply1.resultValue_ = nlohmann::json::array();
    reply1.resultValue_.emplace_back("Component#0");
    reply1.resultValue_.emplace_back("Component#1");
    server.ApiMapPost(oldApiName, reply1);
    // Object array return value should be converted;
    ASSERT_EQ(reply1.resultValue_.at(0).get<string>(), "UiComponent#0");
    ASSERT_EQ(reply1.resultValue_.at(1).get<string>(), "UiComponent#1");

    auto call1 = ApiCallInfo {.apiId_ = "By.text", .callerObjRef_ = "By#seed"};
    call1.paramList_.emplace_back("By#0");
    oldApiName = server.ApiMapPre(call1);
    // String parameters should NOT be converted;
    ASSERT_EQ(call1.apiId_, "On.text");
    ASSERT_EQ(call1.callerObjRef_, "On#seed");
    ASSERT_EQ(call1.paramList_.at(0).get<string>(), "By#0");
    ASSERT_EQ(oldApiName, "By.text");

    auto call2 = ApiCallInfo {.apiId_ = "UiComponent.getText", .callerObjRef_ = "UiComponent#0"};
    oldApiName = server.ApiMapPre(call2);
    // String return value should NOT be converted.
    ASSERT_EQ(call2.apiId_, "Component.getText");
    ASSERT_EQ(call2.callerObjRef_, "Component#0");
    ASSERT_EQ(oldApiName, "UiComponent.getText");
    auto reply2 = ApiReplyInfo();
    reply2.resultValue_ = "Component#0";
    server.ApiMapPost(oldApiName, reply2);
    ASSERT_EQ(reply2.resultValue_.get<string>(), "Component#0");

    auto call3 = ApiCallInfo {.apiId_ = "Component.getText", .callerObjRef_ = "Component#0"};
    oldApiName = server.ApiMapPre(call3);
    // New API should NOT be converted.
    ASSERT_EQ(call3.apiId_, "Component.getText");
    ASSERT_EQ(call3.callerObjRef_, "Component#0");
    ASSERT_EQ(oldApiName, "");
    
    // Test special apiId_ mapping
    ApiCallInfo calls[] = {
        {.apiId_ = "By.id", .callerObjRef_ = "By#0"},
        {.apiId_ = "By.key", .callerObjRef_ = "By#0"},
        {.apiId_ = "UiComponent.getId", .callerObjRef_ = "UiComponent#0"},
        {.apiId_ = "UiComponent.getKey", .callerObjRef_ = "UiComponent#0"}
    };
    string expectedResults[] = {
        "On.accessibilityId",
        "On.id",
        "Component.getAccessibilityId",
        "Component.getId"
    };
    for (size_t i = 0; i < sizeof(calls) / sizeof(ApiCallInfo); i++) {
        server.ApiMapPre(calls[i]);
        ASSERT_EQ(calls[i].apiId_, expectedResults[i]);
    }
}

TEST_F(FrontendApiHandlerTest, parameterPreChecks1)
{
    const auto& server =  FrontendApiServer::Get();
    // set the optional parameter as undefined.
    auto call0 = ApiCallInfo {.apiId_ = "On.text", .callerObjRef_ = string(REF_SEED_BY)};
    call0.paramList_.emplace_back("wyz");
    call0.paramList_.emplace_back(nullptr);
    auto reply0 = ApiReplyInfo();
    server.Call(call0, reply0);
    ASSERT_EQ(NO_ERROR, reply0.exception_.code_);
    // set the mandatory parameter as undefined.
    auto call1 = ApiCallInfo {.apiId_ = "On.type", .callerObjRef_ = string(REF_SEED_BY)};
    call1.paramList_.emplace_back(nullptr);
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    ASSERT_EQ(ERR_INVALID_INPUT, reply1.exception_.code_);
    ASSERT_TRUE(reply1.exception_.message_.find("failed: Expect string") != string::npos);
    // set the mandatory parameter of FRONTEND_JSON_DEFS as undefined.
    auto call3 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    auto call4 = ApiCallInfo {.apiId_ = "Driver.click", .callerObjRef_ = reply3.resultValue_.get<string>()};
    auto arg1 = json();
    arg1["x"] = 1;
    arg1["y"] = "null";
    call4.paramList_.emplace_back(arg1);
    auto reply4 = ApiReplyInfo();
    server.Call(call4, reply4);
    ASSERT_EQ(ERR_INVALID_INPUT, reply4.exception_.code_);
    ASSERT_TRUE(reply1.exception_.message_.find("failed: Expect string") != string::npos);
    // set the optional parameter of FRONTEND_JSON_DEFS as undefined.
    auto call5 = ApiCallInfo {.apiId_ = "Driver.findWindow", .callerObjRef_ = reply3.resultValue_.get<string>()};
    auto arg2 = json();
    arg2["bundleName"] = "xyz";
    arg2["title"] = "null";
    call5.paramList_.emplace_back(arg2);
    auto reply5 = ApiReplyInfo();
    server.Call(call5, reply5);
    // cheak parameter pass, but updateUi failed.
    ASSERT_EQ("Get window nodes failed", reply5.exception_.message_);
}

TEST_F(FrontendApiHandlerTest, parameterPreChecks2)
{
    const auto& server =  FrontendApiServer::Get();
    // call with argument missing
    auto call0 = ApiCallInfo {.apiId_ = "By.type", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply0 = ApiReplyInfo();
    server.Call(call0, reply0);
    ASSERT_EQ(USAGE_ERROR, reply0.exception_.code_);
    ASSERT_TRUE(reply0.exception_.message_.find("Illegal argument count") != string::npos);
    // call with argument redundant
    auto call1 = ApiCallInfo {.apiId_ = "By.type", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply1 = ApiReplyInfo();
    call1.paramList_.emplace_back("wyz");
    call1.paramList_.emplace_back(1);
    call1.paramList_.emplace_back(1);
    server.Call(call1, reply1);
    ASSERT_EQ(USAGE_ERROR, reply1.exception_.code_);
    ASSERT_TRUE(reply1.exception_.message_.find("Illegal argument count") != string::npos);
    // call with argument of wrong type
    auto call2 = ApiCallInfo {.apiId_ = "By.type", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply2 = ApiReplyInfo();
    call2.paramList_.emplace_back(1);
    server.Call(call2, reply2);
    ASSERT_EQ(USAGE_ERROR, reply2.exception_.code_);
    ASSERT_TRUE(reply2.exception_.message_.find("Expect string") != string::npos);
    // call with argument defaulted (bool=true)
    auto call3 = ApiCallInfo {.apiId_ = "By.enabled", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply3 = ApiReplyInfo();
    call3.paramList_.emplace_back(true); // no defaulted
    server.Call(call3, reply3);
    ASSERT_EQ(NO_ERROR, reply3.exception_.code_);

    auto call4 = ApiCallInfo {.apiId_ = "By.enabled", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply4 = ApiReplyInfo(); // defaulted
    server.Call(call4, reply4);
    ASSERT_EQ(NO_ERROR, reply4.exception_.code_);
    // call with bad object ref
    auto call5 = ApiCallInfo {.apiId_ = "By.isAfter", .callerObjRef_ = string(REF_SEED_BY)};
    call5.paramList_.emplace_back("By#100");
    auto reply5 = ApiReplyInfo();
    server.Call(call5, reply5);
    ASSERT_EQ(INTERNAL_ERROR, reply5.exception_.code_); // bad-object is internal_error
    ASSERT_TRUE(reply5.exception_.message_.find("Bad object ref") != string::npos);
    // call with json param with wrong property
    auto call6 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply6 = ApiReplyInfo();
    server.Call(call6, reply6);
    auto call7 = ApiCallInfo {.apiId_ = "UiDriver.findWindow", .callerObjRef_ = reply6.resultValue_.get<string>()};
    auto arg = json();
    arg["badProp"] = "wyz";
    call7.paramList_.emplace_back(arg);
    auto reply7 = ApiReplyInfo();
    server.Call(call7, reply7);
    ASSERT_EQ(USAGE_ERROR, reply7.exception_.code_);
    ASSERT_TRUE(reply7.exception_.message_.find("Illegal property") != string::npos);
}

TEST_F(FrontendApiHandlerTest, parameterPreChecks3)
{
    const auto& server =  FrontendApiServer::Get();
    auto call0 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply0 = ApiReplyInfo();
    server.Call(call0, reply0);

    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.delayMs", .callerObjRef_ = reply0.resultValue_.get<string>()};
    call1.paramList_.emplace_back(-100);
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    ASSERT_TRUE(reply1.exception_.message_.find("Expect integer which cannot be less than 0") != string::npos);
}

TEST_F(FrontendApiHandlerTest, parameterPreChecks4)
{
    const auto& server =  FrontendApiServer::Get();
    // call with argument missing
    auto call0 = ApiCallInfo {.apiId_ = "On.id", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply0 = ApiReplyInfo();
    server.Call(call0, reply0);
    ASSERT_EQ(ERR_INVALID_INPUT, reply0.exception_.code_);
    ASSERT_TRUE(reply0.exception_.message_.find("Illegal argument count") != string::npos);
    // call with argument redundant
    auto call1 = ApiCallInfo {.apiId_ = "On.id", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply1 = ApiReplyInfo();
    call1.paramList_.emplace_back("wyz");
    call1.paramList_.emplace_back(1);
    call1.paramList_.emplace_back(1);
    server.Call(call1, reply1);
    ASSERT_EQ(ERR_INVALID_INPUT, reply1.exception_.code_);
    ASSERT_TRUE(reply1.exception_.message_.find("Illegal argument count") != string::npos);
    // call with argument of wrong type
    auto call2 = ApiCallInfo {.apiId_ = "On.text", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply2 = ApiReplyInfo();
    call2.paramList_.emplace_back(1);
    server.Call(call2, reply2);
    ASSERT_EQ(ERR_INVALID_INPUT, reply2.exception_.code_);
    ASSERT_TRUE(reply2.exception_.message_.find("Expect string") != string::npos);
    // call with argument of wrong type
    auto call3 = ApiCallInfo {.apiId_ = "On.text", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply3 = ApiReplyInfo();
    call3.paramList_.emplace_back("text");
    call3.paramList_.emplace_back("REG_EXP");
    server.Call(call3, reply3);
    ASSERT_EQ(ERR_INVALID_INPUT, reply3.exception_.code_);
    ASSERT_TRUE(reply3.exception_.message_.find("Expect integer") != string::npos);
}

TEST_F(FrontendApiHandlerTest, pointerMatrixparameterPreChecks)
{
    const auto& server =  FrontendApiServer::Get();
    // call with argument llegal fingers
    auto call0 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply0 = ApiReplyInfo();
    call0.paramList_.emplace_back(11);
    call0.paramList_.emplace_back(3);
    server.Call(call0, reply0);
    ASSERT_EQ(ERR_INVALID_INPUT, reply0.exception_.code_);
    ASSERT_TRUE(reply0.exception_.message_.find("Number of illegal fingers") != string::npos);
    // call with argument illegal steps
    auto call2 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply2 = ApiReplyInfo();
    call2.paramList_.emplace_back(2);
    call2.paramList_.emplace_back(1001);
    server.Call(call2, reply2);
    ASSERT_EQ(ERR_INVALID_INPUT, reply2.exception_.code_);
    ASSERT_TRUE(reply2.exception_.message_.find("Number of illegal steps") != string::npos);
    // call with argument illegal steps
    auto call4 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply4 = ApiReplyInfo();
    call4.paramList_.emplace_back(5);
    call4.paramList_.emplace_back(0);
    server.Call(call4, reply4);
    ASSERT_EQ(ERR_INVALID_INPUT, reply4.exception_.code_);
    ASSERT_TRUE(reply4.exception_.message_.find("Number of illegal steps") != string::npos);
    // call with argument illegal fingers
    auto call5 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply5 = ApiReplyInfo();
    call5.paramList_.emplace_back(0);
    call5.paramList_.emplace_back(5);
    server.Call(call5, reply5);
    ASSERT_EQ(ERR_INVALID_INPUT, reply5.exception_.code_);
    ASSERT_TRUE(reply5.exception_.message_.find("Number of illegal fingers") != string::npos);
    // call with argument illegal fingers
    auto call6 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply6 = ApiReplyInfo();
    call6.paramList_.emplace_back(0);
    call6.paramList_.emplace_back(5);
    server.Call(call6, reply6);
    ASSERT_EQ(ERR_INVALID_INPUT, reply6.exception_.code_);
    ASSERT_TRUE(reply6.exception_.message_.find("Number of illegal fingers") != string::npos);
}

TEST_F(FrontendApiHandlerTest, pointerMatrixparameterPreChecksOne)
{
    const auto& server =  FrontendApiServer::Get();
    // call with argument illegal steps
    auto call7 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply7 = ApiReplyInfo();
    call7.paramList_.emplace_back(5);
    call7.paramList_.emplace_back(0);
    server.Call(call7, reply7);
    ASSERT_EQ(ERR_INVALID_INPUT, reply7.exception_.code_);
    ASSERT_TRUE(reply7.exception_.message_.find("Number of illegal steps") != string::npos);
    // call with argument illegal fingers
    auto call10 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply10 = ApiReplyInfo();
    call10.paramList_.emplace_back(6);
    call10.paramList_.emplace_back(10);
    server.Call(call10, reply10);
    ASSERT_EQ(NO_ERROR, reply10.exception_.code_);
    auto call11 = ApiCallInfo {.apiId_ = "PointerMatrix.setPoint", .callerObjRef_ = reply10.resultValue_.get<string>()};
    call11.paramList_.emplace_back(6);
    call11.paramList_.emplace_back(1);
    auto arg1 = json();
    arg1["x"] = 9;
    arg1["y"] = 10;
    call11.paramList_.emplace_back(arg1);
    auto reply11 = ApiReplyInfo();
    server.Call(call11, reply11);
    ASSERT_EQ(ERR_INVALID_INPUT, reply11.exception_.code_);
    ASSERT_TRUE(reply11.exception_.message_.find("Number of illegal fingers") != string::npos);
    // call with argument illegal steps
    auto call12 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply12 = ApiReplyInfo();
    call12.paramList_.emplace_back(6);
    call12.paramList_.emplace_back(10);
    server.Call(call12, reply12);
    ASSERT_EQ(NO_ERROR, reply12.exception_.code_);
    auto call13 = ApiCallInfo {.apiId_ = "PointerMatrix.setPoint", .callerObjRef_ = reply12.resultValue_.get<string>()};
    call13.paramList_.emplace_back(5);
    call13.paramList_.emplace_back(11);
    auto arg2 = json();
    arg2["x"] = 9;
    arg2["y"] = 10;
    call13.paramList_.emplace_back(arg2);
    auto reply13 = ApiReplyInfo();
    server.Call(call13, reply13);
    ASSERT_EQ(ERR_INVALID_INPUT, reply13.exception_.code_);
    ASSERT_TRUE(reply13.exception_.message_.find("Number of illegal steps") != string::npos);
}

TEST_F(FrontendApiHandlerTest, injectMultiPointerActionparameterPreChecks)
{
    const auto& server =  FrontendApiServer::Get();
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    auto call3 = ApiCallInfo {.apiId_ = "UiDriver.fling", .callerObjRef_ = reply2.resultValue_.get<string>()};
    auto from = json();
    from["x"] = 30;
    from["y"] = 40;
    auto to = json();
    to["x"] = 300;
    to["y"] = 400;
    call3.paramList_.emplace_back(from);
    call3.paramList_.emplace_back(to);
    call3.paramList_.emplace_back(0);
    call3.paramList_.emplace_back(4000);
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    ASSERT_EQ(USAGE_ERROR, reply3.exception_.code_);
    ASSERT_TRUE(reply3.exception_.message_.find("The stepLen is out of range") != string::npos);

    auto call4 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply4 = ApiReplyInfo();
    server.Call(call4, reply4);
    auto call5 = ApiCallInfo {.apiId_ = "UiDriver.fling", .callerObjRef_ = reply4.resultValue_.get<string>()};
    call5.paramList_.emplace_back(from);
    call5.paramList_.emplace_back(to);
    call5.paramList_.emplace_back(451);
    call5.paramList_.emplace_back(4000);
    auto reply5 = ApiReplyInfo();
    server.Call(call5, reply5);
    ASSERT_EQ(USAGE_ERROR, reply5.exception_.code_);
    ASSERT_TRUE(reply5.exception_.message_.find("The stepLen is out of range") != string::npos);

    auto call6 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply6 = ApiReplyInfo();
    server.Call(call6, reply6);
    auto call7 = ApiCallInfo {.apiId_ = "UiDriver.fling", .callerObjRef_ = reply6.resultValue_.get<string>()};
    auto from3 = json();
    from3["x"] = "";
    from3["y"] = "";
    call7.paramList_.emplace_back(from3);
    call7.paramList_.emplace_back(to);
    call7.paramList_.emplace_back(500);
    call7.paramList_.emplace_back(4000);
    auto reply7 = ApiReplyInfo();
    server.Call(call7, reply7);
    ASSERT_EQ(USAGE_ERROR, reply7.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, callback)
{
    auto& server = FrontendApiServer::Get();
    server.SetCallbackHandler(nullptr);
    ApiCallInfo call { .apiId_ = "wyz" };
    ApiReplyInfo reply {};
    server.Callback(call, reply);
    ASSERT_EQ(ERR_INTERNAL, reply.exception_.code_); // no callback handler

    auto handler = [](const ApiCallInfo& in, ApiReplyInfo& out) {
        out.resultValue_ = in.apiId_ + "Amm";
    };
    server.SetCallbackHandler(handler);
    reply.exception_ = ApiCallErr(NO_ERROR);
    server.Callback(call, reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_EQ("wyzAmm", reply.resultValue_.get<string>());
}

TEST_F(FrontendApiHandlerTest, onEventCallback)
{
    auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);

    auto call2 = ApiCallInfo {.apiId_ = "Driver.createUIEventObserver",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    ASSERT_EQ(NO_ERROR, reply2.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply2.resultValue_.type());
    const auto ref2 = reply2.resultValue_.get<string>();
    ASSERT_TRUE(ref2.find("UIEventObserver#") != string::npos);

    string result = "capture event: ";
    auto jsCallback = [&result](const ApiCallInfo& in, ApiReplyInfo& out) {
        auto &elementInfo = in.paramList_.at(INDEX_ZERO);
        result = result + string(elementInfo["type"]) + ",";
    };
    server.SetCallbackHandler(jsCallback);
    auto jsCbId = to_string(reinterpret_cast<uintptr_t>(&jsCallback));
    auto call3 = ApiCallInfo {.apiId_ = "UIEventObserver.once",
                              .callerObjRef_ = reply2.resultValue_.get<string>()};
    call3.paramList_.push_back("toastShow");
    call3.paramList_.push_back(jsCbId);
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    ASSERT_EQ(NO_ERROR, reply3.exception_.code_);
    auto monitor = DummyEventMonitor::GetInstance();
    ASSERT_EQ(1, monitor.GetListenerCount());
    monitor.OnEvent("toastShow");
    ASSERT_EQ("capture event: toastShow,", result);
    
    auto call4 = ApiCallInfo {.apiId_ = "UIEventObserver.once",
                              .callerObjRef_ = reply2.resultValue_.get<string>()};
    call4.paramList_.push_back("dialogShow");
    call4.paramList_.push_back(jsCbId);
    auto reply4 = ApiReplyInfo();
    server.Call(call4, reply4);
    ASSERT_EQ(NO_ERROR, reply4.exception_.code_);
    monitor.OnEvent("dialogShow");
    ASSERT_EQ("capture event: toastShow,dialogShow,", result);
    // Works once
    monitor.OnEvent("toastShow");
    ASSERT_EQ("capture event: toastShow,dialogShow,", result);
}

TEST_F(FrontendApiHandlerTest, touchPadMultiFingerSwipeParameterPreChecks)
{
    const auto& server =  FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.touchPadMultiFingerSwipe",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    call2.paramList_.emplace_back(2);
    call2.paramList_.emplace_back(0);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    ASSERT_EQ(USAGE_ERROR, reply2.exception_.code_);
    ASSERT_TRUE(reply2.exception_.message_.find("Number of illegal fingers") != string::npos);

    auto call3 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    auto call4 = ApiCallInfo {.apiId_ = "UiDriver.touchPadMultiFingerSwipe",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    call4.paramList_.emplace_back(5);
    call4.paramList_.emplace_back(0);
    auto reply4 = ApiReplyInfo();
    server.Call(call4, reply4);
    ASSERT_EQ(USAGE_ERROR, reply4.exception_.code_);
    ASSERT_TRUE(reply4.exception_.message_.find("Number of illegal fingers") != string::npos);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsValidSpeedTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validSpeed = 1000;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["speed"] = validSpeed;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsValidPressureTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validPressure = 0.5;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["pressure"] = validPressure;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsValidDurationTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validDuration = 2000;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["duration"] = validDuration;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, ClickAtTouchOptionsValidPressureTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.clickAtWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto pointX = 100;
    auto pointY = 200;
    auto validPressure = 0.5;
    auto point1 = json();
    point1["x"] = pointX;
    point1["y"] = pointY;
    auto touchOptions = json();
    touchOptions["pressure"] = validPressure;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, LongClickAtTouchOptionsValidDurationTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.longClickAtWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto pointX = 100;
    auto pointY = 200;
    auto validDuration = 2000;
    auto point1 = json();
    point1["x"] = pointX;
    point1["y"] = pointY;
    auto touchOptions = json();
    touchOptions["duration"] = validDuration;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, SwipeBetweenTouchOptionsValidSpeedTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.swipeBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validSpeed = 1000;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["speed"] = validSpeed;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsInvalidDurationTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto invalidDuration = 1000;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["duration"] = invalidDuration;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(ERR_INVALID_PARAM, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsInvalidSpeedTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto invalidSpeed = -100;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["speed"] = invalidSpeed;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(ERR_INVALID_PARAM, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsInvalidPressureTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto invalidPressure = 2.0;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["pressure"] = invalidPressure;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(ERR_INVALID_PARAM, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, ClickAtTouchOptionsInvalidPressureTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.clickAtWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto pointX = 100;
    auto pointY = 200;
    auto invalidPressure = 2.0;
    auto point1 = json();
    point1["x"] = pointX;
    point1["y"] = pointY;
    auto touchOptions = json();
    touchOptions["pressure"] = invalidPressure;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(ERR_INVALID_PARAM, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsBoundarySpeedMinTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validSpeedMin = 200;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["speed"] = validSpeedMin;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsBoundarySpeedMaxTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validSpeedMax = 40000;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["speed"] = validSpeedMax;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsBoundaryPressureMinTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validPressureMin = 0.0;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["pressure"] = validPressureMin;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, DragBetweenTouchOptionsBoundaryPressureMaxTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.dragBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validPressureMax = 1.0;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["pressure"] = validPressureMax;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, SwipeBetweenTouchOptionsBoundarySpeedMinTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.swipeBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validSpeedMin = 200;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["speed"] = validSpeedMin;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, SwipeBetweenTouchOptionsBoundarySpeedMaxTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.swipeBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validSpeedMax = 40000;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["speed"] = validSpeedMax;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, SwipeBetweenTouchOptionsBoundaryPressureMinTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.swipeBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validPressureMin = 0.0;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["pressure"] = validPressureMin;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, SwipeBetweenTouchOptionsBoundaryPressureMaxTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.swipeBetweenWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto startPointX = 100;
    auto startPointY = 200;
    auto endPointX = 200;
    auto endPointY = 200;
    auto validPressureMax = 1.0;
    auto point1 = json();
    point1["x"] = startPointX;
    point1["y"] = startPointY;
    auto point2 = json();
    point2["x"] = endPointX;
    point2["y"] = endPointY;
    auto touchOptions = json();
    touchOptions["pressure"] = validPressureMax;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, LongClickAtTouchOptionsBoundaryDurationMinTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.longClickAtWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto pointX = 100;
    auto pointY = 200;
    auto validDurationMin = 1500;
    auto point1 = json();
    point1["x"] = pointX;
    point1["y"] = pointY;
    auto touchOptions = json();
    touchOptions["duration"] = validDurationMin;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, LongClickAtTouchOptionsBoundaryDurationMaxTest)
{
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.longClickAtWithOptions",
        .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto pointX = 100;
    auto pointY = 200;
    auto validDurationMax = 10000;
    auto point1 = json();
    point1["x"] = pointX;
    point1["y"] = pointY;
    auto touchOptions = json();
    touchOptions["duration"] = validDurationMax;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, MouseDragWithKey1)
{
    static constexpr int32_t testPoint1X = 100;
    static constexpr int32_t testPoint1Y = 200;
    static constexpr int32_t testPoint2X = 300;
    static constexpr int32_t testPoint2Y = 400;
    static constexpr int32_t key1Code = 17;
    static constexpr uint32_t testSpeed = 1000;
    static constexpr uint32_t duration = 1500;
    
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "Driver.mouseDragWithOptions",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto point1 = json();
    point1["x"] = testPoint1X;
    point1["y"] = testPoint1Y;
    auto point2 = json();
    point2["x"] = testPoint2X;
    point2["y"] = testPoint2Y;
    auto touchOptions = json();
    touchOptions["speed"] = testSpeed;
    touchOptions["duration"] = duration;
    auto keyOptions = json();
    keyOptions["key1"] = key1Code;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    call2.paramList_.emplace_back(keyOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, MouseDragWithKey1AndKey2)
{
    static constexpr int32_t testPoint1X = 100;
    static constexpr int32_t testPoint1Y = 200;
    static constexpr int32_t testPoint2X = 300;
    static constexpr int32_t testPoint2Y = 400;
    static constexpr int32_t key1Code = 17;
    static constexpr int32_t key2Code = 2072;
    static constexpr uint32_t testSpeed = 1000;
    static constexpr uint32_t duration = 1500;
    
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "Driver.mouseDragWithOptions",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto point1 = json();
    point1["x"] = testPoint1X;
    point1["y"] = testPoint1Y;
    auto point2 = json();
    point2["x"] = testPoint2X;
    point2["y"] = testPoint2Y;
    auto touchOptions = json();
    touchOptions["speed"] = testSpeed;
    touchOptions["duration"] = duration;
    auto keyOptions = json();
    keyOptions["key1"] = key1Code;
    keyOptions["key2"] = key2Code;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    call2.paramList_.emplace_back(keyOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, MouseDragWithKey2OnlyShouldFail)
{
    static constexpr int32_t testPoint1X = 100;
    static constexpr int32_t testPoint1Y = 200;
    static constexpr int32_t testPoint2X = 300;
    static constexpr int32_t testPoint2Y = 400;
    static constexpr int32_t key2Code = 2072;
    
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "Driver.mouseDragWithOptions",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto point1 = json();
    point1["x"] = testPoint1X;
    point1["y"] = testPoint1Y;
    auto point2 = json();
    point2["x"] = testPoint2X;
    point2["y"] = testPoint2Y;
    auto touchOptions = json();
    auto keyOptions = json();
    keyOptions["key2"] = key2Code;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    call2.paramList_.emplace_back(keyOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(ERR_INVALID_PARAM, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, MouseDragSpeedBelowRange)
{
    static constexpr int32_t testPoint1X = 100;
    static constexpr int32_t testPoint1Y = 200;
    static constexpr int32_t testPoint2X = 300;
    static constexpr int32_t testPoint2Y = 400;
    static constexpr uint32_t testSpeedZero = 0;
    static constexpr uint32_t testSpeedAbove = 50000;
    static constexpr uint32_t duration = 1500;
    
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "Driver.mouseDragWithOptions",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto point1 = json();
    point1["x"] = testPoint1X;
    point1["y"] = testPoint1Y;
    auto point2 = json();
    point2["x"] = testPoint2X;
    point2["y"] = testPoint2Y;
    auto touchOptions = json();
    touchOptions["speed"] = testSpeedZero;
    touchOptions["duration"] = duration;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);

    auto call3 = ApiCallInfo {.apiId_ = "Driver.mouseDragWithOptions",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    touchOptions["speed"] = testSpeedAbove;
    call3.paramList_.emplace_back(point1);
    call3.paramList_.emplace_back(point2);
    call3.paramList_.emplace_back(touchOptions);
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    EXPECT_EQ(NO_ERROR, reply3.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, MouseDragNegativeSpeedShouldFail)
{
    static constexpr int32_t testPoint1X = 100;
    static constexpr int32_t testPoint1Y = 200;
    static constexpr int32_t testPoint2X = 300;
    static constexpr int32_t testPoint2Y = 400;
    static constexpr int32_t testNegativeSpeed = -500;
    static constexpr int32_t testSpeed = 600;
    static constexpr int32_t testDurationBelow = 1000;
    static constexpr uint32_t duration = 1500;
    
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "Driver.mouseDragWithOptions",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto point1 = json();
    point1["x"] = testPoint1X;
    point1["y"] = testPoint1Y;
    auto point2 = json();
    point2["x"] = testPoint2X;
    point2["y"] = testPoint2Y;
    auto touchOptions = json();
    touchOptions["speed"] = testNegativeSpeed;
    touchOptions["duration"] = duration;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(ERR_INVALID_PARAM, reply2.exception_.code_);

    auto call3 = ApiCallInfo {.apiId_ = "Driver.mouseDragWithOptions",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    touchOptions["speed"] = testSpeed;
    touchOptions["duration"] = testDurationBelow;
    call3.paramList_.emplace_back(point1);
    call3.paramList_.emplace_back(point2);
    call3.paramList_.emplace_back(touchOptions);
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    EXPECT_EQ(ERR_INVALID_PARAM, reply3.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, MouseDragNegativePointsShouldFail)
{
    static constexpr int32_t testPoint1X = -100;
    static constexpr int32_t testPoint1Y = 200;
    static constexpr int32_t testPoint2X = 300;
    static constexpr int32_t testPoint2Y = -400;
    static constexpr uint32_t testSpeed = 1000;
    static constexpr uint32_t duration = 1500;
    
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "Driver.mouseDragWithOptions",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto point1 = json();
    point1["x"] = testPoint1X;
    point1["y"] = testPoint1Y;
    auto point2 = json();
    point2["x"] = testPoint2X;
    point2["y"] = testPoint2Y;
    auto touchOptions = json();
    touchOptions["speed"] = testSpeed;
    touchOptions["duration"] = duration;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(ERR_INVALID_PARAM, reply2.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, MouseDragOldInterface)
{
    static constexpr int32_t testPoint1X = 100;
    static constexpr int32_t testPoint1Y = 200;
    static constexpr int32_t testPoint2X = 300;
    static constexpr int32_t testPoint2Y = 400;
    static constexpr uint32_t testSpeed = 1000;
    static constexpr uint32_t testDuration = 2000;
    static constexpr int32_t testInvalidDuration = 1000;
    
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "Driver.mouseDrag", .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto point1 = json();
    point1["x"] = testPoint1X;
    point1["y"] = testPoint1Y;
    auto point2 = json();
    point2["x"] = testPoint2X;
    point2["y"] = testPoint2Y;
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(testSpeed);
    call2.paramList_.emplace_back(testDuration);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);

    auto call3 = ApiCallInfo {.apiId_ = "Driver.mouseDrag", .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto speed = testSpeed;
    auto duration = testInvalidDuration;
    call3.paramList_.emplace_back(testSpeed);
    call3.paramList_.emplace_back(testInvalidDuration);
    call3.paramList_.emplace_back(speed);
    call3.paramList_.emplace_back(duration);
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    EXPECT_EQ(ERR_INVALID_INPUT, reply3.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, MouseDragNewInterfaceWithoutOptions)
{
    static constexpr int32_t testPoint1X = 100;
    static constexpr int32_t testPoint1Y = 200;
    static constexpr int32_t testPoint2X = 300;
    static constexpr int32_t testPoint2Y = 400;
    
    const auto& server = FrontendApiServer::Get();
    auto call1 = ApiCallInfo {.apiId_ = "Driver.create"};
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    auto call2 = ApiCallInfo {.apiId_ = "Driver.mouseDragWithOptions",
                              .callerObjRef_ = reply1.resultValue_.get<string>()};
    auto point1 = json();
    point1["x"] = testPoint1X;
    point1["y"] = testPoint1Y;
    auto point2 = json();
    point2["x"] = testPoint2X;
    point2["y"] = testPoint2Y;
    auto touchOptions = json();  // Empty options should use defaults
    call2.paramList_.emplace_back(point1);
    call2.paramList_.emplace_back(point2);
    call2.paramList_.emplace_back(touchOptions);
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    EXPECT_EQ(NO_ERROR, reply2.exception_.code_);  // Should use default speed and duration
}

// ==================== Component.isReachable tests ====================

/**
 * Fixture for isReachable tests: registers a MockController so the real API chain
 * (Driver.create -> On.text -> Driver.findComponent -> Component.isReachable)
 * runs against a controllable widget tree built from json descriptions.
 */
class FrontendApiHandlerIsReachableTest : public testing::Test {
public:
    ~FrontendApiHandlerIsReachableTest() override = default;
protected:
    void SetUp() override
    {
        auto mockController = make_unique<MockController>();
        controller_ = mockController.get();
        UiDriver::RegisterController(move(mockController));
    }

    void TearDown() override
    {
        controller_ = nullptr;
    }

        /** Register a window with the given widget-tree json, then run the real API chain
         *  and deliver the isReachable reply. */
        void CallIsReachable(const Window &win, const string &treeJson, const string &content, ApiReplyInfo &reply)
        {
            auto eles = MockElementNodeIterator::ConstructIteratorByJson(treeJson)->elementInfoLists_;
            controller_->AddWindowsAndNode(win, eles);
            CallIsReachableWithRegisteredTree(win, content, reply);
        }

        /** Replace the registered widget tree of the given window at runtime (keeping the same
         *  window id and hierarchies), so a found component can be re-evaluated against the
         *  changed UI. To be used together with CallIsReachable. */
        void ReplaceRegisteredTree(const Window &win, const string &treeJson)
        {
            auto eles = MockElementNodeIterator::ConstructIteratorByJson(treeJson)->elementInfoLists_;
            controller_->RemoveWindowsAndNode(win);
            controller_->AddWindowsAndNode(win, eles);
        }

        /** Register the tree on the window and run Driver.create -> On.text('target') ->
         *  Driver.findComponent against it, returning the found component ref. */
        string FindTargetComponent(const Window &win, const string &treeJson)
        {
            auto eles = MockElementNodeIterator::ConstructIteratorByJson(treeJson)->elementInfoLists_;
            controller_->AddWindowsAndNode(win, eles);
            const auto &server = FrontendApiServer::Get();
            auto call0 = ApiCallInfo {.apiId_ = "Driver.create", .callerObjRef_ = string()};
            auto reply0 = ApiReplyInfo();
            server.Call(call0, reply0);
            EXPECT_EQ(NO_ERROR, reply0.exception_.code_);
            auto call1 = ApiCallInfo {.apiId_ = "On.text", .callerObjRef_ = string(REF_SEED_ON)};
            call1.paramList_.emplace_back("target");
            auto reply1 = ApiReplyInfo();
            server.Call(call1, reply1);
            EXPECT_EQ(NO_ERROR, reply1.exception_.code_);
            auto call2 = ApiCallInfo {.apiId_ = "Driver.findComponent",
                                      .callerObjRef_ = reply0.resultValue_.get<string>()};
            call2.paramList_.emplace_back(reply1.resultValue_.get<string>());
            auto reply2 = ApiReplyInfo();
            server.Call(call2, reply2);
            EXPECT_EQ(NO_ERROR, reply2.exception_.code_);
            return reply2.resultValue_.get<string>();
        }

        /** Replace the registered tree at runtime and re-check reachability of the
         *  previously found component against the changed UI. */
        void FlipTreeAndCheckReachable(const Window &win, const string &treeAfter,
            const string &componentRef, ApiReplyInfo &reply)
        {
            ReplaceRegisteredTree(win, treeAfter);
            const auto &server = FrontendApiServer::Get();
            auto call = ApiCallInfo {.apiId_ = "Component.isReachable", .callerObjRef_ = componentRef};
            server.Call(call, reply);
        }

        /** Run the real API chain (Driver.create -> On.text -> Driver.findComponent ->
         *  Component.isReachable) against the currently registered tree. */
        void CallIsReachableWithRegisteredTree(const Window &win, const string &content, ApiReplyInfo &reply)
    {
        const auto &server = FrontendApiServer::Get();
        // create driver
        auto call0 = ApiCallInfo {.apiId_ = "Driver.create", .callerObjRef_ = string()};
        auto reply0 = ApiReplyInfo();
        server.Call(call0, reply0);
        ASSERT_EQ(NO_ERROR, reply0.exception_.code_);
        const auto driverRef = reply0.resultValue_.get<string>();
        // build selector by content
        auto call1 = ApiCallInfo {.apiId_ = "On.text", .callerObjRef_ = string(REF_SEED_ON)};
        call1.paramList_.emplace_back(content);
        auto reply1 = ApiReplyInfo();
        server.Call(call1, reply1);
        ASSERT_EQ(NO_ERROR, reply1.exception_.code_);
        const auto onRef = reply1.resultValue_.get<string>();
        // find the component
        auto call2 = ApiCallInfo {.apiId_ = "Driver.findComponent", .callerObjRef_ = driverRef};
        call2.paramList_.emplace_back(onRef);
        auto reply2 = ApiReplyInfo();
        server.Call(call2, reply2);
        ASSERT_EQ(NO_ERROR, reply2.exception_.code_);
        ASSERT_TRUE(reply2.resultValue_.is_string());
        const auto componentRef = reply2.resultValue_.get<string>();
        ASSERT_TRUE(componentRef.find("Component#") != string::npos);
        // call isReachable
        auto call3 = ApiCallInfo {.apiId_ = "Component.isReachable", .callerObjRef_ = componentRef};
        server.Call(call3, reply);
    }

        MockController *controller_ = nullptr;

        // shared window ids / display / stacking layers used by the isReachable test cases
        static constexpr int32_t testWindowId = 12;
        static constexpr int32_t testOccluderWindowId = 13;
        static constexpr int32_t testDisplayId = 0;
        static constexpr int32_t lowerWindowLayer = 1;
        static constexpr int32_t higherWindowLayer = 10;

        /** Build a square test window with the given id, display and stacking layer. */
        static Window MakeTestWindow(int32_t id, int32_t displayId, int32_t layer)
        {
            Window win(id);
            win.displayId_ = displayId;
            win.windowLayer_ = layer;
            win.bounds_ = Rect{0, 400, 0, 400};
            return win;
        }

        /** Register two windows (same display) with the given trees, then run the real API chain
         *  and deliver the isReachable reply for the target content. */
        void CallIsReachableTwoWindows(const Window &win0, const string &tree0, const Window &win1,
            const string &tree1, const string &content, ApiReplyInfo &reply)
        {
            auto eles0 = MockElementNodeIterator::ConstructIteratorByJson(tree0)->elementInfoLists_;
            auto eles1 = MockElementNodeIterator::ConstructIteratorByJson(tree1)->elementInfoLists_;
            controller_->AddWindowsAndNode(win0, eles0);
            controller_->AddWindowsAndNode(win1, eles1);
            const auto &server = FrontendApiServer::Get();
            // create driver
            auto call0 = ApiCallInfo {.apiId_ = "Driver.create", .callerObjRef_ = string()};
            auto reply0 = ApiReplyInfo();
            server.Call(call0, reply0);
            ASSERT_EQ(NO_ERROR, reply0.exception_.code_);
            const auto driverRef = reply0.resultValue_.get<string>();
            // build selector by content
            auto call1 = ApiCallInfo {.apiId_ = "On.text", .callerObjRef_ = string(REF_SEED_ON)};
            call1.paramList_.emplace_back(content);
            auto reply1 = ApiReplyInfo();
            server.Call(call1, reply1);
            ASSERT_EQ(NO_ERROR, reply1.exception_.code_);
            // find the component
            auto call2 = ApiCallInfo {.apiId_ = "Driver.findComponent", .callerObjRef_ = driverRef};
            call2.paramList_.emplace_back(reply1.resultValue_.get<string>());
            auto reply2 = ApiReplyInfo();
            server.Call(call2, reply2);
            ASSERT_EQ(NO_ERROR, reply2.exception_.code_);
            // call isReachable
            auto call3 = ApiCallInfo {.apiId_ = "Component.isReachable",
                                      .callerObjRef_ = reply2.resultValue_.get<string>()};
            server.Call(call3, reply);
        }
    };

/** center point not occluded → true */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableNotOccluded)
{
    // tree: root with two children; sibling does not cover the target center (200,200)
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"other",
                    "rectInScreen":"0, 80, 0, 80", "zIndex":"1"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // not occluded → true
}

/** center point occluded by higher z-index → false */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableOccludedByHigherZIndex)
{
    // overlay z-index 1 covers the target center (200,200)
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"overlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"1"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // occluded by higher z-index → false
}

/** partial occlusion but center visible → true */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachablePartialOcclusionCenterVisible)
{
    // overlay covers only the corner [100,180], the center (200,200) is NOT covered
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"partial",
                    "rectInScreen":"100, 180, 100, 180", "zIndex":"1"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // center visible → true
}

/** A component that flips to invisible at runtime is not reachable. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableInvisible)
{
    // step1: find the component in a normal tree
    const string treeBefore = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    // step2: flip the target to invisible at runtime (explicit visible=false, non-zero bounds)
    const string treeAfter = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0", "visible":"false"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    const auto componentRef = FindTargetComponent(win, treeBefore);
    ApiReplyInfo replyAfter;
    FlipTreeAndCheckReachable(win, treeAfter, componentRef, replyAfter);
    if (replyAfter.exception_.code_ == NO_ERROR) {
        ASSERT_TRUE(replyAfter.resultValue_.is_boolean());
        EXPECT_FALSE(replyAfter.resultValue_.get<bool>()); // invisible → false
    } else {
        // the flipped widget is no longer retrievable from the UI tree
        EXPECT_EQ(ERR_COMPONENT_LOST, replyAfter.exception_.code_);
    }
}

/** A component whose bounds flip to zero area at runtime is not reachable. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableZeroArea)
{
    // step1: find the component in a normal tree
    const string treeBefore = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    // step2: shrink the target bounds to zero area at runtime
    const string treeAfter = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 100, 100, 100", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    const auto componentRef = FindTargetComponent(win, treeBefore);
    ApiReplyInfo replyAfter;
    FlipTreeAndCheckReachable(win, treeAfter, componentRef, replyAfter);
    if (replyAfter.exception_.code_ == NO_ERROR) {
        ASSERT_TRUE(replyAfter.resultValue_.is_boolean());
        EXPECT_FALSE(replyAfter.resultValue_.get<bool>()); // zero area → false
    } else {
        // the flipped widget is no longer retrievable from the UI tree
        EXPECT_EQ(ERR_COMPONENT_LOST, replyAfter.exception_.code_);
    }
}

/** component destroyed → 17000004 */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableComponentLost)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    const auto componentRef = FindTargetComponent(win, tree);
    const auto &server = FrontendApiServer::Get();
    // the window stays but the component is gone, then isReachable should deliver 17000004
    controller_->RemoveWindowsAndNode(win);
    controller_->AddWindowsAndNode(win, {});
    auto call3 = ApiCallInfo {.apiId_ = "Component.isReachable", .callerObjRef_ = componentRef};
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    ASSERT_EQ(ERR_COMPONENT_LOST, reply3.exception_.code_);
    EXPECT_TRUE(reply3.resultValue_.is_null());
}

/** same z-index, sibling drawn later covers the center → false */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableSameZIndexLaterSiblingOccludes)
{
    // both widgets have z-index 0; the overlay appears after the target in the tree,
    // so it is drawn later and stacks on top of the target
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"overlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // same z-index, drawn later → occludes
}

/** same z-index, sibling drawn earlier does not occlude → true */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableSameZIndexEarlierSiblingNotOcclude)
{
    // both widgets have z-index 0; the sibling appears before the target in the tree,
    // so the target is drawn later and stacks on top of the sibling
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"underlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // same z-index, drawn earlier → not occluded
}

/** center point on the occluder's edge counts as occluded → false */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableCenterOnOccluderEdge)
{
    // overlay bounds [100,200,100,300]: its right edge (x=200) passes exactly through
    // the target center (200,200) → occluded (edge-inclusive)
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"overlay",
                    "rectInScreen":"100, 200, 100, 300", "zIndex":"1"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // edge point counts as occluded
}

/** only widgets on the same display are considered */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableDifferentDisplayNotOcclude)
{
    // window 12 on display 0 holds the target; window 13 on display 1 holds an occluder
    // with identical local coordinates — it must not occlude the target (local coords overlap)
    const string targetTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "windowId":"12",
                "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "windowId":"12", "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    const string otherTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"2", "windowId":"13",
                "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Text", "accessibilityId":"20", "content":"otherdisplay",
                    "windowId":"13", "rectInScreen":"150, 250, 150, 250", "zIndex":"1"}, "children":[]}
            ]
        }
    )";
    constexpr int32_t secondDisplayId = 1;
    Window win0 = MakeTestWindow(testWindowId, testDisplayId, 0);
    Window win1 = MakeTestWindow(testOccluderWindowId, secondDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachableTwoWindows(win0, targetTree, win1, otherTree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // different display → not occluded
}

/** lower z-index widget covering the center does not occlude → true */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableLowerZIndexNotOcclude)
{
    // target z-index 1; overlay z-index 0 covers the target center — lower z never occludes
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"1"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"underlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // lower z-index → not occluded
}

/** Cross-window: a widget in a higher-layer window occludes even with a lower widget z-index. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableCrossWindowHigherLayerOccludes)
{
    // window 12 (layer 1) holds the target (z-index 100); window 13 (layer 10) holds an occluder
    // with z-index 0 covering the target center. Widget z-index is not comparable across windows,
    // the window layer decides the stacking: the occluder stacks higher and occludes.
    const string targetTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "windowId":"12",
                "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "windowId":"12", "rectInScreen":"100, 300, 100, 300", "zIndex":"100"}, "children":[]}
            ]
        }
    )";
    const string occluderTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"2", "windowId":"13",
                "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Text", "accessibilityId":"20", "content":"floatlayer",
                    "windowId":"13", "rectInScreen":"150, 250, 150, 250", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    Window win0 = MakeTestWindow(testWindowId, testDisplayId, lowerWindowLayer);
    Window win1 = MakeTestWindow(testOccluderWindowId, testDisplayId, higherWindowLayer);
    ApiReplyInfo reply;
    CallIsReachableTwoWindows(win0, targetTree, win1, occluderTree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // higher window layer → occludes
}

/** Cross-window: a widget in a lower-layer window never occludes even if it stacks later
 *  in the traversal order or carries a higher widget z-index. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableCrossWindowLowerLayerNotOcclude)
{
    // window 13 (layer 1) holds an occluder (z-index 100) covering the target center; the target
    // lives in window 12 (layer 10). The window layer decides: the occluder is below the target.
    const string targetTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "windowId":"12",
                "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "windowId":"12", "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    const string occluderTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"2", "windowId":"13",
                "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Text", "accessibilityId":"20", "content":"underwindow",
                    "windowId":"13", "rectInScreen":"150, 250, 150, 250", "zIndex":"100"}, "children":[]}
            ]
        }
    )";
    Window win0 = MakeTestWindow(testWindowId, testDisplayId, higherWindowLayer);
    Window win1 = MakeTestWindow(testOccluderWindowId, testDisplayId, lowerWindowLayer);
    ApiReplyInfo reply;
    CallIsReachableTwoWindows(win0, targetTree, win1, occluderTree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // lower window layer → not occluded
}

/** Cross-window: with equal widget z-index the window layer still decides the direction. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableCrossWindowSameZIndexByWindowLayer)
{
    // both widgets have z-index 0; the occluder lives in a higher-layer window and covers the
    // target center → occluded. (Traversal-order approximation would flip this direction.)
    const string targetTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "windowId":"12",
                "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "windowId":"12", "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    const string occluderTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"2", "windowId":"13",
                "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Text", "accessibilityId":"20", "content":"dialogcover",
                    "windowId":"13", "rectInScreen":"150, 250, 150, 250", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    Window win0 = MakeTestWindow(testWindowId, testDisplayId, lowerWindowLayer);
    Window win1 = MakeTestWindow(testOccluderWindowId, testDisplayId, higherWindowLayer);
    ApiReplyInfo reply;
    CallIsReachableTwoWindows(win0, targetTree, win1, occluderTree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // equal z-index, higher window layer → occludes
}

/** Same window: a transparent overlay passes touch through and does not occlude. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableTransparentOverlayNotOcclude)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"overlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"1",
                    "hitTestBehavior":"HitTestMode.Transparent"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // transparent overlay passes touch through
}

/** Same window: a none hit-test overlay passes touch through and does not occlude. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableNoneOverlayNotOcclude)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"overlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"1",
                    "hitTestBehavior":"HitTestMode.None"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // none overlay passes touch through
}

/** Same window: a blocking overlay still occludes (hit-test exemption does not over-apply). */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableBlockOverlayStillOccludes)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"overlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"1",
                    "hitTestBehavior":"HitTestMode.Block"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // blocking overlay occludes
}

/** Self: a widget with HitTestMode.None does not respond to touch and is not reachable. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableSelfHitTestModeNone)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0",
                    "hitTestBehavior":"HitTestMode.None"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // self does not respond to touch test
}

/** Same window: a default hit-test child covering the parent center does not occlude it.
 *  This is the common Button() { Text() } custom-content shape observed on real devices:
 *  the Text child shares the Button bounds, has the same z-index and is drawn later, but a
 *  Default child never blocks the ancestor (the touch hit bubbles up), so the Button stays
 *  reachable. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableDefaultChildNotOcclude)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[
                    {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"label",
                        "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
                ]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // default child bubbles touch to the ancestor
}

/** Same window: a Block child covering the parent center occludes it (Block blocks ancestors). */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableBlockChildOccludes)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[
                    {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"label",
                        "rectInScreen":"100, 300, 100, 300", "zIndex":"0",
                        "hitTestBehavior":"HitTestMode.Block"}, "children":[]}
                ]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // block child blocks the ancestor
}

/** Same window: a BLOCK_HIERARCHY child covering the parent center occludes it
 *  (BLOCK_HIERARCHY blocks the parent node). */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableBlockHierarchyChildOccludes)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[
                    {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"label",
                        "rectInScreen":"100, 300, 100, 300", "zIndex":"0",
                        "hitTestBehavior":"HitTestMode.BLOCK_HIERARCHY"}, "children":[]}
                ]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // block-hierarchy child blocks the parent
}

/** Same window: a BLOCK_DESCENDANTS child covering the parent center does not occlude it
 *  (BLOCK_DESCENDANTS only disables itself and its own descendants, never the ancestors). */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableBlockDescendantsChildNotOcclude)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[
                    {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"label",
                        "rectInScreen":"100, 300, 100, 300", "zIndex":"0",
                        "hitTestBehavior":"HitTestMode.BLOCK_DESCENDANTS"}, "children":[]}
                ]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // block-descendants child never blocks ancestors
}

/** Same window: a default ancestor covering the target center never occludes it. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableDefaultAncestorNotOcclude)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Column", "accessibilityId":"5",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[
                    {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                        "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
                ]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // ancestors never block their own descendants
}

/** Same window: a Block ancestor covering the target center occludes it (Block blocks children). */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableBlockAncestorOccludes)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Column", "accessibilityId":"5",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0",
                    "hitTestBehavior":"HitTestMode.Block"}, "children":[
                    {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                        "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
                ]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // block ancestor blocks the descendants
}

/** Same window: a BLOCK_DESCENDANTS ancestor covering the target center occludes it
 *  (BLOCK_DESCENDANTS disables all its descendants). */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableBlockDescendantsAncestorOccludes)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Column", "accessibilityId":"5",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0",
                    "hitTestBehavior":"HitTestMode.BLOCK_DESCENDANTS"}, "children":[
                    {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                        "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
                ]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // block-descendants ancestor blocks the target
}

/** Same window: a BLOCK_HIERARCHY ancestor covering the target center does not occlude it
 *  (BLOCK_HIERARCHY blocks its own parent and lower-priority siblings, not its descendants). */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableBlockHierarchyAncestorNotOcclude)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Column", "accessibilityId":"5",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0",
                    "hitTestBehavior":"HitTestMode.BLOCK_HIERARCHY"}, "children":[
                    {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                        "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]}
                ]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // block-hierarchy ancestor keeps descendants live
}

/** Same window: a BLOCK_DESCENDANTS sibling covering the center passes touch through
 *  (it does not respond itself and only blocks its own descendants, so siblings stay reachable). */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableBlockDescendantsSiblingNotOcclude)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"overlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"1",
                    "hitTestBehavior":"HitTestMode.BLOCK_DESCENDANTS"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // block-descendants overlay passes siblings through
}

/** Self: a widget with HitTestMode.BLOCK_DESCENDANTS does not respond to touch and is not reachable. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableSelfBlockDescendants)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0",
                    "hitTestBehavior":"HitTestMode.BLOCK_DESCENDANTS"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // self does not respond to touch test
}

/** Regression for the dangling snapshot pointer: the widget pointer returned by
 *  RetrieveWidget refers to an element of UiDriver::visitWidgets_, which is cleared and
 *  refilled by FindWidgetsCoveringPoint. Here the higher-layer decoy window (visited first)
 *  realigns the refilled vector, so the stale snapshot slot holds the decoy widget with
 *  z-index 5 instead of the target (z-index 0). With the stale data the same-zindex overlay
 *  in the target window would compare against z-index 5 and wrongly pass; the correct
 *  snapshot (z-index 0, later same-zindex overlay covering the center) must be occluded. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableStaleSnapshotAcrossWindows)
{
    const string targetTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "windowId":"12",
                "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "windowId":"12", "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"overlay",
                    "windowId":"12", "rectInScreen":"150, 250, 150, 250", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"12", "content":"extra1",
                    "windowId":"12", "rectInScreen":"0, 80, 0, 80", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"13", "content":"extra2",
                    "windowId":"12", "rectInScreen":"320, 400, 320, 400", "zIndex":"0"}, "children":[]}
            ]
        }
    )";
    const string decoyTree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"2", "windowId":"13",
                "rectInScreen":"0, 100, 0, 100"},
            "children":[
                {"attributes":{"componentType":"Text", "accessibilityId":"20", "content":"decoy",
                    "windowId":"13", "rectInScreen":"0, 50, 0, 50", "zIndex":"5"}, "children":[]}
            ]
        }
    )";
    Window win0 = MakeTestWindow(testWindowId, testDisplayId, lowerWindowLayer);
    Window win1 = MakeTestWindow(testOccluderWindowId, testDisplayId, higherWindowLayer);
    ApiReplyInfo reply;
    CallIsReachableTwoWindows(win0, targetTree, win1, decoyTree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    // correct answer: same-zindex overlay drawn later covers the target center → occluded
    EXPECT_FALSE(reply.resultValue_.get<bool>());
}
