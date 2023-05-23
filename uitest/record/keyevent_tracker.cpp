/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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
#include <chrono>
#include "keyevent_tracker.h"

namespace OHOS::uitest {
    const std::vector<int32_t> KeyeventTracker::COMBINATION_KET = {
        MMI::KeyEvent::KEYCODE_CTRL_LEFT,
        MMI::KeyEvent::KEYCODE_CTRL_RIGHT,
        MMI::KeyEvent::KEYCODE_ALT_LEFT,
        MMI::KeyEvent::KEYCODE_ALT_RIGHT,
        MMI::KeyEvent::KEYCODE_SHIFT_LEFT,
        MMI::KeyEvent::KEYCODE_SHIFT_RIGHT,
        MMI::KeyEvent::KEYCODE_POWER
    };

    const std::string KeyeventTracker::EVENT_TYPE = "key" ;

    bool KeyeventTracker::AddDownKeyEvent(KeyEventInfo &info)
    {
        // 该按键是否已down
        if (std::find(infos_.begin(), infos_.end(), info) != infos_.end()) {
            return false;
        }
        infos_.push_back(info);
        return true;
    }

    void KeyeventTracker::AddUpKeyEvent(KeyEventInfo &info)
    {
        KeyCodeDone(info.GetKeyCode());
        if (infos_.size() == 0) {
            isCombination = false;
        }
        return ;
    }

    KeyeventTracker KeyeventTracker::GetSnapshootKey(KeyEventInfo &info)
    {
        KeyeventTracker snapshootKeyTracker;
        snapshootKeyTracker.infos_ =  infos_;
        snapshootKeyTracker.actionUpTime = info.GetActionTime();
        snapshootKeyTracker.isNeedRecord = true;
        return snapshootKeyTracker;
    }

    void KeyeventTracker::KeyCodeDone(int32_t keyCode)
    {
        auto infoIt = std::find_if(infos_.begin(), infos_.end(), [keyCode](const KeyEventInfo& info) {
            return info.GetKeyCode() == keyCode;
        });
        if (infoIt != infos_.end()) {
            infos_.erase(infoIt);
            return;
        }
        LOG_E("keyCode:%{keyCode} did not received down event before the up event.", keyCode);
    }

    // cout
    bool KeyeventTracker::WriteCombinationData(shared_ptr<mutex> &cout_lock)
    {
        if (infos_.size()==0) {
            LOG_E("Failed to obtain the combination_key when cout keyEvent.");
            return false;
        }
        buildEventItems();
        std::lock_guard<mutex> guard(*cout_lock);
        for (size_t i = 0; i < INFO_SIZE-1; i++) {
            std::cout << eventItems[i] << ", ";
        }
        std::cout << eventItems[INFO_SIZE-1] << std::endl;
        return true;
    }
    bool KeyeventTracker::WriteSingleData(KeyEventInfo &info, shared_ptr<mutex> &cout_lock)
    {
        buildEventItems(info);
        std::lock_guard<mutex> guard(*cout_lock);
        for (size_t i = 0; i < INFO_SIZE-1; i++) {
            std::cout << eventItems[i] << ", ";
        }
        std::cout << eventItems[INFO_SIZE-1] << std::endl;
        return true;
    }

    // record.csv
    bool KeyeventTracker::WriteCombinationData(ofstream& outFile, shared_ptr<mutex> &csv_lock)
    {
        if (infos_.size()==0) {
            LOG_E("Failed to obtain the combination_key when save keyEvent into record.csv.");
            return false;
        }
        buildEventItems();
        std::lock_guard<mutex> guard(*csv_lock);
        if (outFile.is_open()) {
            for (size_t i = 0; i < INFO_SIZE-1; i++) {
                outFile << eventItems[i] << ", ";
            }
            outFile << eventItems[INFO_SIZE-1] << std::endl;
        }
        return true;
    }

    bool KeyeventTracker::WriteSingleData(KeyEventInfo &info, ofstream &outFile, shared_ptr<mutex> &csv_lock)
    {
        buildEventItems(info);
        std::lock_guard<mutex> guard(*csv_lock);
        if (outFile.is_open()) {
            for (size_t i = 0; i < INFO_SIZE-1; i++) {
                outFile << eventItems[i] << ", ";
            }
            outFile << eventItems[INFO_SIZE-1] << std::endl;
        }
        return true;
    }

    void KeyeventTracker::buildEventItems(KeyEventInfo &info)
    {
        if (eventItems[0] != "-1") {
            return ;
        }
        actionStartTime = infos_.size()==0?info.GetActionTime():infos_[0].GetActionTime();
        eventItems[0] = std::to_string(actionStartTime);
        eventItems[1] = std::to_string(actionUpTime - actionStartTime);
        eventItems[EVENT_TYPE_INDEX] = EVENT_TYPE;
        eventItems[KEY_COUNT_INDEX] = std::to_string(infos_.size()+1);
        for (size_t i = 0; i < infos_.size() && i < MAX_COMBINATION_SIZE; i++) {
            eventItems[KEY_COUNT_INDEX+1+i] = std::to_string(infos_[i].GetKeyCode());
        }
        eventItems[KEY_COUNT_INDEX+1+infos_.size()] = std::to_string(info.GetKeyCode());
    }

    void KeyeventTracker::buildEventItems()
    {
        if (eventItems[0] != "-1" || infos_.size() == 0) {
            return ;
        }
        actionStartTime = infos_[0].GetActionTime();
        eventItems[0] = std::to_string(actionStartTime);
        eventItems[1] = std::to_string(actionUpTime - actionStartTime);
        eventItems[EVENT_TYPE_INDEX] = EVENT_TYPE;
        eventItems[KEY_COUNT_INDEX] = std::to_string(infos_.size());
        for (size_t i = 0; i < infos_.size() && i < MAX_COMBINATION_SIZE; i++) {
            eventItems[KEY_COUNT_INDEX+1+i] = std::to_string(infos_[i].GetKeyCode());
        }
    }

    void KeyeventTracker::printEventItems()
    {
        std::cout << "infos:" ;
        for (size_t i = 0; i < infos_.size() ; i++) {
            std::cout << std::to_string(infos_[i].GetKeyCode()) << ",";
        }
        std::cout << std::endl;
    }
} // namespace OHOS::uitest