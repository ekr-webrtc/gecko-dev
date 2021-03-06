/// Copyright (c) 2012 Ecma International.  All rights reserved. 
/// Ecma International makes this code available under the terms and conditions set
/// forth on http://hg.ecmascript.org/tests/test262/raw-file/tip/LICENSE (the 
/// "Use Terms").   Any redistribution of this code must retain the above 
/// copyright and this notice and otherwise comply with the Use Terms.
/**
 * @path ch07/7.6/7.6.1/7.6.1.2/7.6.1.2-13-s.js
 * @description Strict Mode - SyntaxError isn't thrown when 'implementss' occurs in strict mode code
 * @onlyStrict
 */


function testcase() {
        "use strict";
        var implementss = 1;
        return implementss === 1;
    }
runTestCase(testcase);
