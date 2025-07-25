/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

import { AssertResult } from '../modal/assertModel';

export function assertClose(actualValue: number, expected: number[]): AssertResult {
  if (actualValue === null && expected[0] === null) {
    throw new Error('actualValue and expected can not be both null!!!');
  }
  let result = false;
  let diff = Math.abs(expected[0] - actualValue);
  let actualAbs = Math.abs(actualValue);
  if (actualAbs - 0 === 0) {
    if (diff - 0 === 0) {
      result = true;
    } else {
      result = false;
    }
  } else if (diff / actualAbs < expected[1]) {
    result = true;
  } else {
    result = false;
  }
  return {
    pass: result,
    message: '|' + actualValue + ' - ' + expected[0] + '|/' + actualValue + ' is not less than ' + expected[1],
  };
}
