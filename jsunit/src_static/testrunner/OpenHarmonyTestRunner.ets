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
import abilityDelegatorRegistry from '@ohos.app.ability.abilityDelegatorRegistry';
import TestRunner from '@ohos.application.testRunner';
import hilog from '@ohos.hilog';
import util from '@ohos.util';
import { Hypium } from '../index';
import testsuite from '../test/List.test';

let abilityDelegator: abilityDelegatorRegistry.AbilityDelegator;
let abilityDelegatorArguments: abilityDelegatorRegistry.AbilityDelegatorArgs;
const jsonPath: string = 'mock/mock-config.json';
const domain: number = 0x0000; //日志标识,0x0000作为测试框架的业务标识
const tag: string = 'testTag'; //日志标识字符串,作为tag标识当前runner类下的测试行为

export class OpenHarmonyTestRunner implements TestRunner {
  constructor() {}

  onPrepare() {
    hilog.info(domain, tag, '%{public}s', 'OpenHarmonyTestRunner OnPrepare');
  }

  onRun() {
    hilog.info(domain, tag, '%{public}s', 'OpenHarmonyTestRunner onRun run');
    abilityDelegatorArguments = abilityDelegatorRegistry.getArguments();
    abilityDelegator = abilityDelegatorRegistry.getAbilityDelegator();
    try {
      checkMock(abilityDelegator, abilityDelegatorArguments)
    } catch(error: Error) {
      let code = error.code;
      let message = error.message;
      hilog.error(domain, tag, `checkMock failed, error code: ${code}, message: ${message}.`);
    }
    hilog.info(domain, tag, '%{public}s', 'start run testcase!!!');
    Hypium.hypiumTest(abilityDelegator, abilityDelegatorArguments, testsuite);
    hilog.info(domain, tag, '%{public}s', 'OpenHarmonyTestRunner onRun end');
  }
}

function checkMock(
  abilityDelegator: abilityDelegatorRegistry.AbilityDelegator,
  abilityDelegatorArguments: abilityDelegatorRegistry.AbilityDelegatorArgs
) {
  const parameters = abilityDelegatorArguments.parameters;
  let moduleName = '';
  for (const key of parameters.keys()) {
    if (key === '-m') {
      moduleName = parameters[key];
    }
  }
  const context = abilityDelegator.getAppContext().getApplicationContext().createModuleContext(moduleName);
  const resourceManager = context.resourceManager;
  let rawFile: Uint8Array;
  try {
    rawFile = resourceManager.getRawFileContentSync(jsonPath);
    hilog.info(domain, tag, 'MockList file exists');
    const mockStr: string = util.TextDecoder.create('utf-8', { ignoreBOM: true }).decodeWithStream(rawFile);
    const mockMap: Record<string, string> = getMockList(mockStr);
    try {
      abilityDelegator.setMockList(mockMap);
    } catch (error: Error) {
      let code = error.code;
      let message = error.message;
      hilog.error(domain, tag, `abilityDelegator.setMockList failed, error code: ${code}, message: ${message}.`);
    }
  } catch (error: Error) {
    let code = error.code;
    let message = error.message;
    hilog.error(
      domain,
      tag,
      `ResourceManager:callback getRawFileContent failed, error code: ${code}, message: ${message}.`
    );
  }
}

interface SourceObj {
  source?: string;
}

function getMockList(jsonStr: string) {
  let jsonObj: Record<string, Object> = JSON.parse(jsonStr);
  let map: Map<string, object> = new Map<string, object>(Object.entries(jsonObj));
  let mockList: Record<string, string> = {};
  map.forEach((value: object, key: string) => {
    if (value) {
      const sourceValue = value as SourceObj;
      const source = sourceValue.source;
      if (source) {
        mockList[key] = source;
      }
    }
  });
  hilog.info(domain, tag, '%{public}s', 'mock-json value:' + JSON.stringify(mockList));
  return mockList;
}
