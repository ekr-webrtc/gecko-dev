/*
 * Copyright 2012, Mozilla Foundation and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

'use strict';
// <INJECTED SOURCE:START>

// THIS FILE IS GENERATED FROM SOURCE IN THE GCLI PROJECT
// DO NOT EDIT IT DIRECTLY

var exports = {};

var TEST_URI = "data:text/html;charset=utf-8,<p id='gcli-input'>gcli-testContext.js</p>";

function test() {
  return Task.spawn(function() {
    let options = yield helpers.openTab(TEST_URI);
    yield helpers.openToolbar(options);
    gcli.addItems(mockCommands.items);

    yield helpers.runTests(options, exports);

    gcli.removeItems(mockCommands.items);
    yield helpers.closeToolbar(options);
    yield helpers.closeTab(options);
  }).then(finish, helpers.handleError);
}

// <INJECTED SOURCE:END>

// var helpers = require('./helpers');

exports.testBaseline = function(options) {
  return helpers.audit(options, [
    // These 3 establish a baseline for comparison when we have used the
    // context command
    {
      setup:    'ext',
      check: {
        input:  'ext',
        hints:     ' -> context',
        markup: 'III',
        message: '',
        predictions: [ 'context', 'tsn ext', 'tsn exte', 'tsn exten', 'tsn extend' ],
        unassigned: [ ],
      }
    },
    {
      setup:    'ext test',
      check: {
        input:  'ext test',
        hints:          '',
        markup: 'IIIVEEEE',
        status: 'ERROR',
        message: 'Too many arguments',
        unassigned: [ ' test' ],
      }
    },
    {
      setup:    'tsn',
      check: {
        input:  'tsn',
        hints:     ' deep down nested cmd',
        markup: 'III',
        cursor: 3,
        current: '__command',
        status: 'ERROR',
        predictionsContains: [ 'tsn deep down nested cmd', 'tsn ext', 'tsn exte' ],
        args: {
          command: { name: 'tsn' },
        }
      }
    }
  ]);
};

exports.testContext = function(options) {
  return helpers.audit(options, [
    // Use the 'tsn' context
    {
      setup:    'context tsn',
      check: {
        input:  'context tsn',
        hints:             ' deep down nested cmd',
        markup: 'VVVVVVVVVVV',
        message: '',
        predictionsContains: [ 'tsn deep down nested cmd', 'tsn ext', 'tsn exte' ],
        args: {
          command: { name: 'context' },
          prefix: {
            value: options.requisition.canon.getCommand('tsn'),
            status: 'VALID',
            message: ''
          }
        }
      },
      exec: {
        output: 'Using tsn as a command prefix'
      }
    },
    // For comparison with earlier
    {
      setup:    'ext',
      check: {
        input:  'ext',
        hints:     ' <text>',
        markup: 'VVV',
        predictions: [ 'tsn ext', 'tsn exte', 'tsn exten', 'tsn extend' ],
        args: {
          command: { name: 'tsn ext' },
          text: {
            value: undefined,
            arg: '',
            status: 'INCOMPLETE'
          }
        }
      }
    },
    {
      setup:    'ext test',
      check: {
        input:  'ext test',
        hints:          '',
        markup: 'VVVVVVVV',
        args: {
          command: { name: 'tsn ext' },
          text: {
            value: 'test',
            arg: ' test',
            status: 'VALID',
            message: ''
          }
        }
      },
      exec: {
        output: 'Exec: tsnExt text=test'
      }
    },
    {
      setup:    'tsn',
      check: {
        input:  'tsn',
        hints:     ' deep down nested cmd',
        markup: 'III',
        message: '',
        predictionsContains: [ 'tsn deep down nested cmd', 'tsn ext', 'tsn exte' ],
        args: {
          command: { name: 'tsn' },
        }
      }
    },
    // Does it actually work?
    {
      setup:    'tsb true',
      check: {
        input:  'tsb true',
        hints:          '',
        markup: 'VVVVVVVV',
        options: [ 'true' ],
        message: '',
        predictions: [ 'true' ],
        unassigned: [ ],
        args: {
          command: { name: 'tsb' },
          toggle: { value: true, arg: ' true', status: 'VALID', message: '' }
        }
      }
    },
    {
      // Bug 866710 - GCLI should allow argument merging for non-string parameters
      setup: 'context tsn ext',
      skip: true
    },
    {
      setup:    'context "tsn ext"',
      check: {
        input:  'context "tsn ext"',
        hints:                   '',
        markup: 'VVVVVVVVVVVVVVVVV',
        message: '',
        predictions: [ 'tsn ext', 'tsn exte', 'tsn exten', 'tsn extend' ],
        unassigned: [ ],
        args: {
          command: { name: 'context' },
          prefix: {
            value: options.requisition.canon.getCommand('tsn ext'),
            status: 'VALID',
            message: ''
          }
        }
      },
      exec: {
        output: 'Can\'t use \'tsn ext\' as a prefix because it is not a parent command.',
        error: true
      }
    },
    /*
    {
      setup:    'context "tsn deep"',
      check: {
        input:  'context "tsn deep"',
        hints:                    '',
        markup: 'VVVVVVVVVVVVVVVVVV',
        status: 'ERROR',
        message: '',
        predictions: [ 'tsn deep' ],
        unassigned: [ ],
        args: {
          command: { name: 'context' },
          prefix: {
            value: options.requisition.canon.getCommand('tsn deep'),
            status: 'VALID',
            message: ''
          }
        }
      },
      exec: {
        output: ''
      }
    },
    */
    {
      setup:    'context',
      check: {
        input:  'context',
        hints:         ' [prefix]',
        markup: 'VVVVVVV',
        status: 'VALID',
        unassigned: [ ],
        args: {
          command: { name: 'context' },
          prefix: { value: undefined, arg: '', status: 'VALID', message: '' },
        }
      },
      exec: {
        output: 'Command prefix is unset',
        type: 'string',
        error: false
      }
    }
  ]);
};
