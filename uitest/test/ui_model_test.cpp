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
#include "gtest/gtest.h"
#include "ui_model.h"
#include <regex.h>

using namespace OHOS::uitest;
using namespace std;

static constexpr auto ATTR_TEXT = "text";
static constexpr auto ATTR_ID = "id";

TEST(REGEXPTest, testRegex)
{
    // make a widget object
    Widget widget("hierarchy");
    widget.SetAttr(UiAttr::TEXT, "testButton123");
    widget.SetAttr(UiAttr::ID, "btnTest");
    widget.SetBounds(Rect(1, 2, 3, 4));

    // use regex to match widget
    string pattern = "REG_EXP";
    auto matcher_text = WidgetMatchModel(static_cast<UiAttr>('text'), "^testButton\\d{2,4}$",
                                         static_cast<ValueMatchPattern>(matchPattern));
    auto matcher_id = WidgetMatchModel(static_cast<UiAttr>('id'), "btn\\w{2,4}",
                                       static_cast<ValueMatchPattern>(matchPattern));
    bool match_result_text = widget.MatchAttr(matcher_text);
    bool match_result_id = widget.MatchAttr(matcher_id);
    ASSERT_EQ(true, match_result_text);
    ASSERT_EQ(true, match_result_id);
    
    // use regex to match widget, use REG_ICASE
    string pattern = "REG_EXP_ICASE";
    auto matcher_text_i = WidgetMatchModel(static_cast<UiAttr>('text'), "^testbutton\\d{2,4}$",
                                           static_cast<ValueMatchPattern>(matchPattern));
    bool match_result_text_i = widget.MatchAttr(matcher_text_i);
    ASSERT_EQ(true, match_result_text_i);
}

TEST(RectTest, testRectBase)
{
    Rect rect(100, 200, 300, 400);
    ASSERT_EQ(100, rect.left_);
    ASSERT_EQ(200, rect.right_);
    ASSERT_EQ(300, rect.top_);
    ASSERT_EQ(400, rect.bottom_);

    ASSERT_EQ(rect.GetCenterX(), (100 + 200) / 2);
    ASSERT_EQ(rect.GetCenterY(), (300 + 400) / 2);
    ASSERT_EQ(rect.GetHeight(), 400 - 300);
    ASSERT_EQ(rect.GetWidth(), 200 - 100);
}

TEST(WidgetTest, testAttributes)
{
    Widget widget("hierarchy");
    // get not-exist attribute, should return default value
    ASSERT_EQ("none", widget.GetAttr(UiAttr::MAX));
    // get exist attribute, should return actual value
    widget.SetAttr(UiAttr::MAX, "wyz");
    ASSERT_EQ("", widget.GetAttr(UiAttr::TEXT));
}

/** NOTE:: Widget need to be movable since we need to move a constructed widget to
 * its hosting tree. We must ensure that the move-created widget be same as the
 * moved one (No any attribute/filed should be lost during moving).
 * */
TEST(WidgetTest, testSafeMovable)
{
    Widget widget("hierarchy");
    widget.SetAttr(UiAttr::TEXT, "wyz");
    widget.SetAttr(UiAttr::ID, "100");
    widget.SetBounds(Rect(1, 2, 3, 4));

    auto newWidget = move(widget);
    ASSERT_EQ("hierarchy", newWidget.GetHierarchy());
    ASSERT_EQ("wyz", newWidget.GetAttr(UiAttr::TEXT));
    ASSERT_EQ("100", newWidget.GetAttr(UiAttr::ID));
    auto bounds = newWidget.GetBounds();
    ASSERT_EQ(1, bounds.left_);
    ASSERT_EQ(2, bounds.right_);
    ASSERT_EQ(3, bounds.top_);
    ASSERT_EQ(4, bounds.bottom_);
}

TEST(WidgetTest, testToStr)
{
    Widget widget("hierarchy");

    // get exist attribute, should return default value
    widget.SetAttr(UiAttr::TEXT, "wyz");
    widget.SetAttr(UiAttr::ID, "100");

    auto str0 = widget.ToStr();
    ASSERT_TRUE(str0.find(ATTR_TEXT) != string::npos);
    ASSERT_TRUE(str0.find("wyz") != string::npos);
    ASSERT_TRUE(str0.find(ATTR_ID) != string::npos);
    ASSERT_TRUE(str0.find("100") != string::npos);

    // change attribute
    widget.SetAttr(UiAttr::ID, "211");
    str0 = widget.ToStr();
    ASSERT_TRUE(str0.find(ATTR_TEXT) != string::npos);
    ASSERT_TRUE(str0.find("wyz") != string::npos);
    ASSERT_TRUE(str0.find(ATTR_ID) != string::npos);
    ASSERT_TRUE(str0.find("211") != string::npos);
    ASSERT_TRUE(str0.find("100") == string::npos);
}

TEST(WidgetTest, testClone)
{
    Widget widget("hierarchy");
    // get exist attribute, should return default value
    widget.SetAttr(UiAttr::TEXT, "Camera");
    widget.SetAttr(UiAttr::ACCESSIBILITY_ID, "12");
    auto ret = widget.Clone("TEST");
    ASSERT_TRUE(ret->GetAttr(UiAttr::TEXT) == "Camera");
}