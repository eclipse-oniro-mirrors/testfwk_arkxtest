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

#ifndef UI_DRIVER_H
#define UI_DRIVER_H

#include "ui_controller.h"
#include "ui_action.h"
#include "widget_selector.h"

namespace OHOS::uitest {
    struct WindowCacheModel {
        explicit WindowCacheModel(const Window &win) : window_(win), widgetIterator_(nullptr) {}
        Window window_;
        std::unique_ptr<ElementNodeIterator> widgetIterator_;
    };
    class UiDriver : public BackendClass {
    public:
        UiDriver() {}

        ~UiDriver() override {}

        /**Find widgets with the given selector. Results are arranged in the receiver in <b>DFS</b> order.
         * @returns the widget object.
         **/
        void FindWidgets(WidgetSelector &select,
                                   vector<unique_ptr<Widget>> &rev,
                                   ApiCallErr &err,
                                   bool updateUi = true);

        /**Wait for the matching widget appear in the given timeout.*/
        std::unique_ptr<Widget> WaitForWidget(WidgetSelector &select, const UiOpArgs &opt, ApiCallErr &err);
        /**Find window matching the given matcher.*/
        std::unique_ptr<Window> FindWindow(std::function<bool(const Window &)> matcher,
                                           bool isMatchBundleName,
                                           ApiCallErr &err);

        /**Retrieve widget from updated UI.*/
        const Widget *RetrieveWidget(const Widget &widget, ApiCallErr &err, bool updateUi = true);

        /**Retrieve window from updated UI.*/
        const Window *RetrieveWindow(const Window &window, ApiCallErr &err);

        string GetHostApp(const Widget &widget);

        /**Trigger the given key action. */
        void TriggerKey(const KeyAction &key, const UiOpArgs &opt, ApiCallErr &error);

        /**Perform the given touch action.*/
        void PerformTouch(const TouchAction &touch, const UiOpArgs &opt, ApiCallErr &err);

        void PerformMouseAction(const MouseAction &touch, const UiOpArgs &opt, ApiCallErr &err);

        /**Delay current thread for given duration.*/
        static void DelayMs(uint32_t ms);

        /**Take screen capture, save to given file path as PNG.*/
        void TakeScreenCap(int32_t fd, ApiCallErr &err, Rect rect);

        void DumpUiHierarchy(nlohmann::json &out, bool listWindows, bool addExternAttr, ApiCallErr &error);

        const FrontEndClassDef &GetFrontendClassDef() const override
        {
            return DRIVER_DEF;
        }

        void SetDisplayRotation(DisplayRotation rotation, ApiCallErr &error);

        DisplayRotation GetDisplayRotation(ApiCallErr &error);

        void SetDisplayRotationEnabled(bool enabled, ApiCallErr &error);

        bool WaitForUiSteady(uint32_t idleThresholdMs, uint32_t timeoutSec, ApiCallErr &error);

        void WakeUpDisplay(ApiCallErr &error);

        Point GetDisplaySize(ApiCallErr &error);

        Point GetDisplayDensity(ApiCallErr &error);

        static void RegisterController(std::unique_ptr<UiController> controller);

        bool CheckStatus(bool isConnected, ApiCallErr &error);

        static void RegisterUiEventListener(std::shared_ptr<UiEventListener> listener);

        void InputText(string_view text, ApiCallErr &error);

        void GetMergeWindowBounds(Rect& mergeRect);

    private:
        bool TextToKeyEvents(string_view text, std::vector<KeyEvent> &events, ApiCallErr &error);
        // UI objects that are needed to be updated before each interaction and used in the interaction
        void UpdateUIWindows(ApiCallErr &error);
        void DFSMarshalWidget(int index,
                              nlohmann::json &dom,
                              const std::map<std::string, int> &widgetChildCountMap,
                              std::map<std::string, int> &visitWidgetMap);
        void DumpWindowsInfoToJson(bool listWindows, Rect& mergeBounds, nlohmann::json& childDom);
        static std::unique_ptr<UiController> uiController_;
        // CacheModel:
        // 保留有查找中的窗口信息和查找的中间数据，用于对该窗口进行继续查找
        // win对象，win对应的节点访问迭代器
        std::vector<WindowCacheModel> windowCacheVec_;
        // 已访问过的节点信息，作为唯一数据保存
        std::vector<Widget> visitWidgets_;
        // 存放下标
        std::vector<int> targetWidgetsIndex_;
    };
} // namespace OHOS::uitest

#endif