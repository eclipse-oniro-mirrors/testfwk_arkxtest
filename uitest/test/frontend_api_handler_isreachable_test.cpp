/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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
#include "nlohmann/json.hpp"
// For testing private method
#define private public
#include "frontend_api_handler.h"
#undef private
#include "mock_element_node_iterator.h"
#include "mock_controller.h"
#include "widget_selector.h"
#include "ui_driver.h"

using namespace OHOS::uitest;
using namespace std;
using namespace nlohmann;



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

/** Same window: a negative z-index overlay covering the center never occludes a z=0 target
 *  (a lower stacking level stays below even when negative). */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableNegativeZIndexOverlayNotOcclude)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"0"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"underlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"-1"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_TRUE(reply.resultValue_.get<bool>()); // negative z-index is still the lower level
}

/** Same window: a z=0 sibling covering the center occludes a negative z-index target. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableNegativeZIndexTargetOccluded)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"-1"}, "children":[]},
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
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // the negative-z target is stacked below the sibling
}

/** Same window: with both widgets at the same negative z-index, the one drawn later occludes. */
TEST_F(FrontendApiHandlerIsReachableTest, componentIsReachableBothNegativeSameZIndex)
{
    const string tree = R"(
        {
            "attributes":{"componentType":"Root", "accessibilityId":"1", "rectInScreen":"0, 400, 0, 400"},
            "children":[
                {"attributes":{"componentType":"Button", "accessibilityId":"10", "content":"target",
                    "rectInScreen":"100, 300, 100, 300", "zIndex":"-3"}, "children":[]},
                {"attributes":{"componentType":"Text", "accessibilityId":"11", "content":"overlay",
                    "rectInScreen":"150, 250, 150, 250", "zIndex":"-3"}, "children":[]}
            ]
        }
    )";
    Window win = MakeTestWindow(testWindowId, testDisplayId, 0);
    ApiReplyInfo reply;
    CallIsReachable(win, tree, "target", reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.resultValue_.is_boolean());
    EXPECT_FALSE(reply.resultValue_.get<bool>()); // equal negative z-index, later drawn wins
}
