# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
    {
      'target_name': 'auto_scan_manager',
      'dependencies': [
        'switch_access_interface',
        'prefs',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'automation_manager',
      'dependencies': [
        '<(EXTERNS_GYP):accessibility_private',
        '<(EXTERNS_GYP):automation',
        'automation_predicate',
        'tree_walker',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'automation_predicate',
      'dependencies': [
        '<(EXTERNS_GYP):automation',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'background',
      'dependencies': [
        '<(EXTERNS_GYP):chrome_extensions',
        'switch_access',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'keyboard_handler',
      'dependencies': [
        'switch_access_interface',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'options',
      'dependencies': [
        '<(EXTERNS_GYP):chrome_extensions',
        'prefs',
        'switch_access',
        'background',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'prefs',
      'dependencies': [
        '<(EXTERNS_GYP):chrome_extensions',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'switch_access',
      'dependencies': [
        '<(EXTERNS_GYP):automation',
        '<(EXTERNS_GYP):chrome_extensions',
        'prefs',
        'auto_scan_manager',
        'automation_manager',
        'keyboard_handler',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'switch_access_interface',
      'dependencies': [
        'prefs',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'tree_walker',
      'dependencies': [
        '<(EXTERNS_GYP):automation',
      ],
      'includes': ['../../../../../third_party/closure_compiler/compile_js2.gypi'],
    }
  ],
}
