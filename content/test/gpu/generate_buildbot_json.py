#!/usr/bin/env python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Script to generate chromium.gpu.json and chromium.gpu.fyi.json in
the src/testing/buildbot directory. Maintaining these files by hand is
too unwieldy.
"""

import copy
import json
import os
import string
import sys

THIS_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.dirname(os.path.dirname(os.path.dirname(THIS_DIR)))

# "Types" of waterfalls and bots. A bot's type is the union of its own
# type and the type of its waterfall. Predicates can apply to these
# sets in order to run tests only on a certain subset of the bots.
class Types(object):
  GPU = 'gpu'
  GPU_FYI = 'gpu_fyi'
  OPTIONAL = 'optional'
  V8_FYI = 'v8_fyi'

# The predicate functions receive a list of types as input and
# determine whether the test should run on the given bot.
class Predicates(object):
  @staticmethod
  def DEFAULT(x):
    # By default, tests run on the chromium.gpu and chromium.gpu.fyi
    # waterfalls, but not on the optional tryservers or on the
    # client.v8.fyi waterfall.
    return Types.OPTIONAL not in x and Types.V8_FYI not in x

  @staticmethod
  def FYI_ONLY(x):
    # This predicate is more complex than desired because the optional
    # tryservers are considered to be on the chromium.gpu.fyi
    # waterfall.
    return Types.GPU_FYI in x and Types.OPTIONAL not in x

  @staticmethod
  def FYI_AND_OPTIONAL(x):
    return Predicates.FYI_ONLY(x) or Types.OPTIONAL in x

  @staticmethod
  def FYI_OPTIONAL_AND_V8(x):
    return Predicates.FYI_AND_OPTIONAL(x) or Types.V8_FYI in x

  @staticmethod
  def DEFAULT_PLUS_V8(x):
    return Predicates.DEFAULT(x) or Types.V8_FYI in x

  @staticmethod
  def DEFAULT_AND_OPTIONAL(x):
    return Predicates.DEFAULT(x) or Types.OPTIONAL in x

WATERFALL = {
  'name': 'chromium.gpu',
  'type': Types.GPU,

  'builders': {
    'GPU Win Builder' : {},
    'GPU Win Builder (dbg)' : {},
    'GPU Mac Builder' : {},
    'GPU Mac Builder (dbg)' : {},
    'GPU Linux Builder' : {},
    'GPU Linux Builder (dbg)' : {},
   },

  'testers': {
    'Win7 Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'win',
    },
    'Win7 Debug (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'win',
    },
    'Mac Release (Intel)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:0a2e',
          'os': 'Mac-10.12'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
    },
    'Mac Debug (Intel)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:0a2e',
          'os': 'Mac-10.12'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'mac',
    },
    'Mac Retina Release (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6821',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
    },
    'Mac Retina Debug (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6821',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'mac',
    },
    'Linux Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'linux',
    },
    'Linux Debug (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'linux',
    },
  }
}

FYI_WATERFALL = {
  'name': 'chromium.gpu.fyi',
  'type': Types.GPU_FYI,

  'builders': {
    'GPU Win Builder' : {},
    'GPU Win Builder (dbg)' : {},
    'GPU Win x64 Builder' : {},
    'GPU Win x64 Builder (dbg)' : {},
    'GPU Mac Builder' : {},
    'GPU Mac Builder (dbg)' : {},
    'GPU Linux Builder' : {},
    'GPU Linux Builder (dbg)' : {},
    'Linux ChromiumOS Builder' : {
      'additional_compile_targets' : [ "All" ]
    },
    'Linux ChromiumOS Ozone Builder' : {},
  },

  'testers': {
    'Win7 Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'win',
    },
    'Win7 Debug (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'win',
    },
    'Win10 Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-10'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'win',
    },
    'Win10 Debug (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-10'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'win',
    },
    'Win7 Release (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6613',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'win',
    },
    'Win7 Debug (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6613',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'win',
    },
    'Win10 Release (Intel HD 530)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:1912',
          'os': 'Windows-10',
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'win',
    },
    'Win10 Debug (Intel HD 530)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:1912',
          'os': 'Windows-10',
        },
      ],
      'build_config': 'Debug',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'win',
    },
    'Win10 Release (NVIDIA Quadro P400)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:1cb3',
          'os': 'Windows-10'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'win',
    },
    'Win7 Release (AMD R7 240)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6613',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'win',
    },
    'Win7 x64 Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Release_x64',
      'swarming': True,
      'os_type': 'win',
    },
    'Win7 x64 Debug (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Debug_x64',
      'swarming': True,
      'os_type': 'win',
    },
    'Mac Release (Intel)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:0a2e',
          'os': 'Mac-10.12'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
    },
    'Mac Debug (Intel)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:0a2e',
          'os': 'Mac-10.12'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'mac',
    },
    'Mac Pro Release (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:679e',
          'os': 'Mac-10.10'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'mac',
    },
    'Mac Pro Debug (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:679e',
          'os': 'Mac-10.10'
        },
      ],
      'build_config': 'Debug',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'mac',
    },
    'Mac Retina Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:0fe9',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
    },
    'Mac Retina Debug (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:0fe9',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'mac',
    },
    'Mac Retina Release (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6821',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
    },
    'Mac Retina Debug (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6821',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'mac',
    },
    'Mac Experimental Release (Intel)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:0a2e',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off for testing purposes.
      'swarming': False,
      'os_type': 'mac',
    },
    'Mac Experimental Retina Release (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6821',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off for testing purposes.
      'swarming': False,
      'os_type': 'mac',
    },
    'Mac Experimental Retina Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:0fe9',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off for testing purposes.
      'swarming': False,
      'os_type': 'mac',
    },
    'Mac GPU ASAN Release': {
      # This bot spawns jobs on multiple GPU types.
      'swarming_dimensions': [
        {
          'gpu': '8086:0a2e',
          'os': 'Mac-10.12'
        },
        {
          'gpu': '1002:6821',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
      'is_asan': True,
    },
    'Linux Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'linux',
    },
    'Linux Release (NVIDIA Quadro P400)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:1cb3',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'linux',
    },
    'Linux Debug (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Debug',
      'swarming': True,
      'os_type': 'linux',
    },
    'Linux Release (Intel HD 530)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:1912',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'linux',
    },
    'Linux Debug (Intel HD 530)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:1912',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Debug',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'linux',
    },
    'Linux Release (AMD R7 240)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6613',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'linux',
    },
    'Linux GPU TSAN Release': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'linux',
      'instrumentation_type': 'tsan',
    },
    'Linux ChromiumOS Ozone (Intel)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:1912',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Release',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'linux',
    },
    'Android Release (Nexus 5)': {
      'swarming_dimensions': [
        {
          # There are no PCI IDs on Android.
          # This is a hack to get the script working.
          'gpu': '0000:0000',
          'os': 'Android'
        },
      ],
      'build_config': 'android-chromium',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'android',
    },
    'Android Release (Nexus 5X)': {
      'swarming_dimensions': [
        {
          'device_type': 'bullhead',
          'device_os': 'M',
          'os': 'Android'
        },
      ],
      'build_config': 'android-chromium',
      'swarming': True,
      'os_type': 'android',
    },
    'Android Release (Nexus 6)': {
      'swarming_dimensions': [
        {
          # There are no PCI IDs on Android.
          # This is a hack to get the script working.
          'gpu': '0000:0000',
          'os': 'Android'
        },
      ],
      'build_config': 'android-chromium',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'android',
    },
    'Android Release (Nexus 6P)': {
      'swarming_dimensions': [
        {
          # There are no PCI IDs on Android.
          # This is a hack to get the script working.
          'gpu': '0000:0000',
          'os': 'Android'
        },
      ],
      'build_config': 'android-chromium',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'android',
    },
    'Android Release (Nexus 9)': {
      'swarming_dimensions': [
        {
          # There are no PCI IDs on Android.
          # This is a hack to get the script working.
          'gpu': '0000:0000',
          'os': 'Android'
        },
      ],
      'build_config': 'android-chromium',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'android',
    },
    'Android Release (NVIDIA Shield TV)': {
      'swarming_dimensions': [
        {
          # There are no PCI IDs on Android.
          # This is a hack to get the script working.
          'gpu': '0000:0000',
          'os': 'Android'
        },
      ],
      'build_config': 'android-chromium',
      # This bot is a one-off and doesn't have similar slaves in the
      # swarming pool.
      'swarming': False,
      'os_type': 'android',
    },

    # The following "optional" testers don't actually exist on the
    # waterfall. They are present here merely to specify additional
    # tests which aren't on the main tryservers. Unfortunately we need
    # a completely different (redundant) bot specification to handle
    # this.
    'Optional Win7 Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'win',
      'type': Types.OPTIONAL,
    },
    'Optional Win7 Release (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6613',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'win',
      'type': Types.OPTIONAL,
    },
    'Optional Mac Release (Intel)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:0a2e',
          'os': 'Mac-10.12'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
      'type': Types.OPTIONAL,
    },
    'Optional Mac Retina Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:0fe9',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
      'type': Types.OPTIONAL,
    },
    'Optional Mac Retina Release (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6821',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
      'type': Types.OPTIONAL,
    },
    # BEGIN obsolete Mac bots -- remove after waterfall restarted successfully.
    'Optional Mac 10.10 Release (Intel)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:0a2e',
          'os': 'Mac-10.12'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
      'type': Types.OPTIONAL,
    },
    'Optional Mac Retina Release': {
      'swarming_dimensions': [
        {
          'gpu': '10de:0fe9',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
      'type': Types.OPTIONAL,
    },
    'Optional Mac 10.10 Retina Release (AMD)': {
      'swarming_dimensions': [
        {
          'gpu': '1002:6821',
          'hidpi': '1',
          'os': 'Mac'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
      'type': Types.OPTIONAL,
    },
    # END obsolete Mac bots -- remove after waterfall restarted successfully.
    'Optional Linux Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'linux',
      'type': Types.OPTIONAL,
    },
  }
}

V8_FYI_WATERFALL = {
  'name': 'client.v8.fyi',
  'type': Types.V8_FYI,

  'prologue': {
    "V8 Android GN (dbg)": {
      "additional_compile_targets": [
        "chrome_public_apk"
      ],
      "gtest_tests": []
    },
    "V8 Linux GN": {
      "additional_compile_targets": [
        "accessibility_unittests",
        "aura_unittests",
        "browser_tests",
        "cacheinvalidation_unittests",
        "capture_unittests",
        "cast_unittests",
        "cc_unittests",
        "chromedriver_unittests",
        "components_browsertests",
        "components_unittests",
        "content_browsertests",
        "content_unittests",
        "crypto_unittests",
        "dbus_unittests",
        "device_unittests",
        "display_unittests",
        "events_unittests",
        "extensions_browsertests",
        "extensions_unittests",
        "gcm_unit_tests",
        "gfx_unittests",
        "gn_unittests",
        "google_apis_unittests",
        "gpu_ipc_service_unittests",
        "gpu_unittests",
        "interactive_ui_tests",
        "ipc_tests",
        "jingle_unittests",
        "media_unittests",
        "media_blink_unittests",
        "mojo_common_unittests",
        "mojo_public_bindings_unittests",
        "mojo_public_system_unittests",
        "mojo_system_unittests",
        "nacl_loader_unittests",
        "net_unittests",
        "pdf_unittests",
        "ppapi_unittests",
        "printing_unittests",
        "remoting_unittests",
        "sandbox_linux_unittests",
        "skia_unittests",
        "sql_unittests",
        "storage_unittests",
        "sync_integration_tests",
        "ui_base_unittests",
        "ui_touch_selection_unittests",
        "unit_tests",
        "url_unittests",
        "views_unittests",
        "wm_unittests"
      ]
    }
  },
  'testers': {
    'Win Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Windows-2008ServerR2-SP1'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'win',
    },
    'Mac Release (Intel)': {
      'swarming_dimensions': [
        {
          'gpu': '8086:0a2e',
          'os': 'Mac-10.12'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'mac',
    },
    'Linux Release (NVIDIA)': {
      'swarming_dimensions': [
        {
          'gpu': '10de:104a',
          'os': 'Ubuntu'
        },
      ],
      'build_config': 'Release',
      'swarming': True,
      'os_type': 'linux',
    },
    'Android Release (Nexus 5X)': {
      'swarming_dimensions': [
        {
          'device_type': 'bullhead',
          'device_os': 'M',
          'os': 'Android'
        },
      ],
      'build_config': 'android-chromium',
      'swarming': True,
      'os_type': 'android',
    },
  }
}

COMMON_GTESTS = {
  'angle_deqp_egl_tests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        # Run only on the Win7 Release NVIDIA 32- and 64-bit bots
        # (and trybots) for the time being, at least until more capacity is
        # added.
        # TODO(jmadill): Run on the Linux Release NVIDIA bots.
        'build_configs': ['Release', 'Release_x64'],
        'swarming_dimension_sets': [
          {
            'gpu': '10de:104a',
            'os': 'Windows-2008ServerR2-SP1'
          }
        ],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'args': [
      '--test-launcher-batch-limit=400'
    ]
  },

  'angle_deqp_gles2_d3d11_tests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        # Run only on the Win7 NVIDIA/AMD R7 240 32- and 64-bit bots (and
        # trybots) for the time being, at least until more capacity is
        # added.
        'build_configs': ['Release', 'Release_x64'],
        'swarming_dimension_sets': [
          # NVIDIA Win 7
          {
            'gpu': '10de:104a',
            'os': 'Windows-2008ServerR2-SP1'
          },
          # AMD Win 7
          {
            'gpu': '1002:6613',
            'os': 'Windows-2008ServerR2-SP1'
          },
        ],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'desktop_swarming': {
      'shards': 4,
    },
    'test': 'angle_deqp_gles2_tests',
    'args': [
      '--test-launcher-batch-limit=400',
      '--deqp-egl-display-type=angle-d3d11'
    ]
  },

  'angle_deqp_gles2_gl_tests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        # Run only on the Linux Release NVIDIA 32- and 64-bit bots (and
        # trybots) for the time being, at least until more capacity is added.
        'build_configs': ['Release', 'Release_x64'],
        'swarming_dimension_sets': [
          # NVIDIA Linux
          {
            'gpu': '10de:104a',
            'os': 'Ubuntu'
          },
        ],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'desktop_swarming': {
      'shards': 4,
    },
    'test': 'angle_deqp_gles2_tests',
    'args': [
      '--test-launcher-batch-limit=400',
      '--deqp-egl-display-type=angle-gl'
    ]
  },

  'angle_deqp_gles2_gles_tests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        # Run on Nexus 5X swarmed bots.
        'build_configs': ['android-chromium'],
        'swarming_dimension_sets': [
          # Nexus 5X
          {
            'device_type': 'bullhead',
            'device_os': 'M',
            'os': 'Android'
          }
        ],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'test': 'angle_deqp_gles2_tests',
    # Only pass the display type to desktop. The Android runner doesn't support
    # passing args to the executable but only one display type is supported on
    # Android anyways.
    'desktop_args': [
      '--test-launcher-batch-limit=400',
      '--deqp-egl-display-type=angle-gles'
    ],
    'android_args': ['--enable-xml-result-parsing']
  },

  'angle_deqp_gles3_gles_tests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        # Run on Nexus 5X swarmed bots.
        'build_configs': ['android-chromium'],
        'swarming_dimension_sets': [
          # Nexus 5X
          {
            'device_type': 'bullhead',
            'device_os': 'M',
            'os': 'Android'
          }
        ],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'test': 'angle_deqp_gles3_tests',
    # Only pass the display type to desktop. The Android runner doesn't support
    # passing args to the executable but only one display type is supported on
    # Android anyways.
    'desktop_args': [
      '--test-launcher-batch-limit=400',
      '--deqp-egl-display-type=angle-gles'
    ],
    'android_args': [
      '--enable-xml-result-parsing',
      '--shard-timeout=300'
    ],
  },

  'angle_deqp_gles3_d3d11_tests': {
    'tester_configs': [
      {
        # TODO(jmadill): Run this on ANGLE roll tryservers.
        'predicate': Predicates.FYI_ONLY,
        # Run only on the NVIDIA and AMD Win7 bots (and trybots) for the time
        # being, at least until more capacity is added.
        'build_configs': ['Release'],
        'swarming_dimension_sets': [
          # NVIDIA Win 7
          {
            'gpu': '10de:104a',
            'os': 'Windows-2008ServerR2-SP1'
          },
          # AMD Win 7
          {
            'gpu': '1002:6613',
            'os': 'Windows-2008ServerR2-SP1'
          }
        ],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'swarming': {
      'shards': 12,
    },
    'test': 'angle_deqp_gles3_tests',
    'args': [
      '--test-launcher-batch-limit=400',
      '--deqp-egl-display-type=angle-d3d11'
    ]
  },

  'angle_deqp_gles3_gl_tests': {
    'tester_configs': [
      {
        # TODO(jmadill): Run this on ANGLE roll tryservers.
        'predicate': Predicates.FYI_ONLY,
        # Run only on the Linux Release NVIDIA 32-bit bots (and trybots) for
        # the time being, at least until more capacity is added.
        'build_configs': ['Release'],
        'swarming_dimension_sets': [
          # NVIDIA Linux
          {
            'gpu': '10de:104a',
            'os': 'Ubuntu'
          }
        ],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'swarming': {
      'shards': 12,
    },
    'test': 'angle_deqp_gles3_tests',
    'args': [
      '--test-launcher-batch-limit=400',
      '--deqp-egl-display-type=angle-gl'
    ]
  },

  'angle_deqp_gles31_d3d11_tests': {
    'tester_configs': [
      {
        'predicate': Predicates.FYI_ONLY,
        # Run on the Win Release NVIDIA bots.
        'build_configs': ['Release'],
        'swarming_dimension_sets': [
          {
            'gpu': '10de:104a',
            'os': 'Windows-2008ServerR2-SP1'
          }
        ],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'swarming': {
      # TODO(geofflang): Increase the number of shards as more tests start to
      # pass and runtime increases.
      'shards': 4,
    },
    'test': 'angle_deqp_gles31_tests',
    'args': [
      '--test-launcher-batch-limit=400',
      '--deqp-egl-display-type=angle-d3d11'
    ]
  },

  'angle_deqp_gles31_gl_tests': {
    'tester_configs': [
      {
        'predicate': Predicates.FYI_ONLY,
        # Run on the Win Release NVIDIA bots.
        'build_configs': ['Release'],
        'swarming_dimension_sets': [
          {
            'gpu': '10de:104a',
            'os': 'Windows-2008ServerR2-SP1'
          },
          {
            'gpu': '10de:104a',
            'os': 'Ubuntu'
          }
        ],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'swarming': {
      # TODO(geofflang): Increase the number of shards as more tests start to
      # pass and runtime increases.
      'shards': 4,
    },
    'test': 'angle_deqp_gles31_tests',
    'args': [
      '--test-launcher-batch-limit=400',
      '--deqp-egl-display-type=angle-gl'
    ]
  },

  # Until we have more capacity, run angle_end2end_tests only on the
  # FYI waterfall, the ANGLE trybots (which mirror the FYI waterfall),
  # and the optional trybots (mainly used during ANGLE rolls).
  'angle_end2end_tests': {
    'tester_configs': [
      {
        'predicate': Predicates.FYI_AND_OPTIONAL,
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          # TODO(ynovikov) Investigate why the test breaks on older devices.
          'Android Release (Nexus 5)',
          'Android Release (Nexus 6)',
          'Android Release (Nexus 9)',
        ],
      },
    ],
    'desktop_args': [
      '--use-gpu-in-tests',
      # ANGLE test retries deliberately disabled to prevent flakiness.
      # http://crbug.com/669196
      '--test-launcher-retry-limit=0'
    ]
  },
  # white_box tests should run where end2end tests run
  'angle_white_box_tests': {
    'tester_configs': [
      {
        'predicate': Predicates.FYI_AND_OPTIONAL,
        # There are only Windows white box tests for now.
        # Enable on more configs when there will be relevant tests.
        'os_types': ['win'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'desktop_args': [
      # ANGLE test retries deliberately disabled to prevent flakiness.
      # http://crbug.com/669196
      '--test-launcher-retry-limit=0'
    ]
  },
  'angle_unittests': {
    'desktop_args': [
      '--use-gpu-in-tests',
      # ANGLE test retries deliberately disabled to prevent flakiness.
      # http://crbug.com/669196
      '--test-launcher-retry-limit=0'
    ]
  },
  # Until the media-only tests are extracted from content_unittests,
  # and audio_unittests and content_unittests can be run on the commit
  # queue with --require-audio-hardware-for-testing, run them only on
  # the FYI waterfall.
  #
  # Note that the transition to the Chromium recipe has forced the
  # removal of the --require-audio-hardware-for-testing flag for the
  # time being. See crbug.com/574942.
  'audio_unittests': {
    'tester_configs': [
      {
        'predicate': Predicates.FYI_ONLY,
      }
    ],
    # Don't run these tests on Android.
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
      {
        'os_types': ['android'],
      },
    ],
    'args': ['--use-gpu-in-tests']
  },
  # TODO(kbr): content_unittests is killing the Linux GPU swarming
  # bots. crbug.com/582094 . It's not useful now anyway until audio
  # hardware is deployed on the swarming bots, so stop running it
  # everywhere.
  # 'content_unittests': {},
  'gl_tests': {
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'desktop_args': ['--use-gpu-in-tests']
  },
  'gl_unittests': {
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'desktop_args': ['--use-gpu-in-tests']
  },
  # The gles2_conform_tests are closed-source and deliberately only run
  # on the FYI waterfall and the optional tryservers.
  'gles2_conform_test': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
      }
    ],
    # Don't run these tests on Android.
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
      {
        'os_types': ['android'],
      },
    ],
    'args': ['--use-gpu-in-tests']
  },
  'gles2_conform_d3d9_test': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        'os_types': ['win'],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'args': [
      '--use-gpu-in-tests',
      '--use-angle=d3d9',
    ],
    'test': 'gles2_conform_test',
  },
  'gles2_conform_gl_test': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        'os_types': ['win'],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'args': [
      '--use-gpu-in-tests',
      '--use-angle=gl',
      '--disable-gpu-sandbox',
    ],
    'test': 'gles2_conform_test',
  },
  # Face and barcode detection unit tests, which currently only run on
  # Mac OS, and require physical hardware.
  'service_unittests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        'os_types': ['mac'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
      {
        'swarming_dimension_sets': [
          # These tests fail on the Mac Pros.
          {
            'gpu': '1002:679e',
          },
        ],
      },
    ],
    'args': [
      '--gtest_filter=*Detection*',
      '--use-gpu-in-tests'
    ]
  },
  'swiftshader_unittests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        'os_types': ['win', 'linux', 'mac'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
          'Mac Experimental Release (Intel)',
          'Mac Experimental Retina Release (AMD)',
          'Mac Experimental Retina Release (NVIDIA)',
          'Mac Pro Release (AMD)',
        ],
      },
    ],
  },
  'tab_capture_end2end_tests': {
    'tester_configs': [
      {
        'build_configs': ['Release', 'Release_x64'],
        'disabled_instrumentation_types': ['tsan'],
      }
    ],
    # Don't run these tests on Android.
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
        'os_types': ['android'],
      },
    ],
    'args': [
      '--enable-gpu',
      '--test-launcher-bot-mode',
      '--test-launcher-jobs=1',
      '--gtest_filter=CastStreamingApiTestWithPixelOutput.EndToEnd*:' + \
          'TabCaptureApiPixelTest.EndToEnd*'
    ],
    'linux_args': [ '--no-xvfb' ],
    'test': 'browser_tests',
  },
  'video_decode_accelerator_d3d11_unittest': {
    'tester_configs': [
      {
        'os_types': ['win']
      },
    ],
    'args': [
      '--use-angle=d3d11',
      '--use-test-data-path',
      '--test_video_data=test-25fps.h264:320:240:250:258:::1',
    ],
    'test': 'video_decode_accelerator_unittest',
  },
  'video_decode_accelerator_d3d9_unittest': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_ONLY,
        'os_types': ['win']
      },
    ],
    'args': [
      '--use-angle=d3d9',
      '--use-test-data-path',
      '--test_video_data=test-25fps.h264:320:240:250:258:::1',
    ],
    'test': 'video_decode_accelerator_unittest',
  },
  'video_decode_accelerator_gl_unittest': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_ONLY,
        'os_types': ['win']
      },
    ],
    # Windows Intel doesn't have the GL extensions to support this test
    'disabled_tester_configs': [
      {
        'names': [
          'Win10 Debug (Intel HD 530)',
          'Win10 Release (Intel HD 530)',
        ],
      },
    ],
    'args': [
      '--use-angle=gl',
      '--use-test-data-path',
      '--test_video_data=test-25fps.h264:320:240:250:258:::1',
    ],
    'test': 'video_decode_accelerator_unittest',
  },
}

# This requires a hack because the isolate's name is different than
# the executable's name. On the few non-swarmed testers, this causes
# the executable to not be found. It would be better if the Chromium
# recipe supported running isolates locally. crbug.com/581953

NON_SWARMED_GTESTS = {
  'tab_capture_end2end_tests': {
    'swarming': {
      'can_use_on_swarming_builders': False
    },
    # Don't run these tests on Android.
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
        'os_types': ['android'],
      },
    ],
    'test': 'browser_tests',
    'args': [
      '--enable-gpu',
      '--no-xvfb',
      '--test-launcher-jobs=1',
      '--gtest_filter=CastStreamingApiTestWithPixelOutput.EndToEnd*:' + \
          'TabCaptureApiPixelTest.EndToEnd*'
    ],
    'swarming': {
      'can_use_on_swarming_builders': False,
    },
  }
}

# These tests use Telemetry's new browser_test_runner, which is a much
# simpler harness for correctness testing.
TELEMETRY_GPU_INTEGRATION_TESTS = {
  'context_lost': {
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_PLUS_V8,
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
  },
  'depth_capture': {
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_PLUS_V8,
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
  },
  'gpu_process_launch_tests': {
    'target_name': 'gpu_process',
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_PLUS_V8,
        'disabled_instrumentation_types': ['tsan'],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
  },
  'hardware_accelerated_feature': {
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_PLUS_V8,
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
  },
  'info_collection_tests': {
    'target_name': 'info_collection',
    'args': [
      '--expected-vendor-id',
      '${gpu_vendor_id}',
      '--expected-device-id',
      '${gpu_device_id}',
    ],
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_AND_OPTIONAL,
        'disabled_instrumentation_types': ['tsan'],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',

          # The Mac ASAN swarming runs on two different GPU types so we can't
          # have one expected vendor ID / device ID
          'Mac GPU ASAN Release',
        ],
      },
    ],
  },
  'maps_pixel_test': {
    'target_name': 'maps',
    'args': [
      '--os-type',
      '${os_type}',
      '--build-revision',
      '${got_revision}',
      '--test-machine-name',
      '${buildername}',
    ],
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_PLUS_V8,
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
  },
  'pixel_test': {
    'target_name': 'pixel',
    'args': [
      '--refimg-cloud-storage-bucket',
      'chromium-gpu-archive/reference-images',
      '--os-type',
      '${os_type}',
      '--build-revision',
      '${got_revision}',
      '--test-machine-name',
      '${buildername}',
    ],
    'non_precommit_args': [
      '--upload-refimg-to-cloud-storage',
    ],
    'precommit_args': [
      '--download-refimg-from-cloud-storage',
    ],
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_PLUS_V8,
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
  },
  'screenshot_sync': {
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_PLUS_V8,
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
  },
  'trace_test': {
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_PLUS_V8,
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
  },
  'webgl_conformance': {
    'tester_configs': [
      {
        'predicate': Predicates.DEFAULT_PLUS_V8,
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'asan_args': ['--is-asan'],
  },
  'webgl_conformance_d3d9_tests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        'os_types': ['win'],
        'disabled_instrumentation_types': ['tsan'],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'target_name': 'webgl_conformance',
    'extra_browser_args': [
      '--use-angle=d3d9',
    ],
    'asan_args': ['--is-asan'],
  },
  'webgl_conformance_gl_tests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        'os_types': ['win'],
        'disabled_instrumentation_types': ['tsan'],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
      {
        'swarming_dimension_sets': [
          # crbug.com/555545 and crbug.com/649824:
          # Disable webgl_conformance_gl_tests on some Win/AMD cards.
          # Always fails on older cards, flaky on newer cards.
          # Note that these must match the GPUs exactly; wildcard
          # matches (i.e., only device ID) aren't supported!
          {
            'gpu': '1002:6779',
            'os': 'Windows-2008ServerR2-SP1'
          },
          {
            'gpu': '1002:6613',
            'os': 'Windows-2008ServerR2-SP1'
          },
          # BUG 590951: Disable webgl_conformance_gl_tests on Win/Intel
          {
            'gpu': '8086:041a',
            'os': 'Windows-2008ServerR2-SP1'
          },
          {
            'gpu': '8086:0412',
            'os': 'Windows-2008ServerR2-SP1'
          },
        ],
      },
    ],
    'target_name': 'webgl_conformance',
    'extra_browser_args': [
      '--use-angle=gl',
    ],
    'asan_args': ['--is-asan'],
  },
  'webgl_conformance_angle_tests': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        'os_types': ['linux'],
        'disabled_instrumentation_types': ['tsan'],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'target_name': 'webgl_conformance',
    'extra_browser_args': [
      '--use-gl=angle',
    ],
    'asan_args': ['--is-asan'],
  },
  'webgl_conformance_d3d11_passthrough': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        'os_types': ['win'],
        'disabled_instrumentation_types': ['tsan'],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'target_name': 'webgl_conformance',
    'extra_browser_args': [
      '--use-angle=d3d11',
      '--use-passthrough-cmd-decoder',
      # TODO(geofflang): Remove --disable-es3-apis once crbug.com/671217 is
      # complete.
      '--disable-es3-apis',
      # TODO(geofflang): --disable-es3-gl-context is required because of
      # crbug.com/680522
      '--disable-es3-gl-context',
    ],
    'asan_args': ['--is-asan'],
  },
  'webgl_conformance_gl_passthrough': {
    'tester_configs': [
      {
        # Run this on the FYI waterfall and optional tryservers.
        'predicate': Predicates.FYI_AND_OPTIONAL,
        'os_types': ['linux'],
        'disabled_instrumentation_types': ['tsan'],
      }
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'target_name': 'webgl_conformance',
    'extra_browser_args': [
      '--use-gl=angle',
      '--use-angle=gl',
      '--use-passthrough-cmd-decoder',
    ],
    'asan_args': ['--is-asan'],
  },
  'webgl2_conformance_tests': {
    'tester_configs': [
      {
         # The WebGL 2.0 conformance tests take over an hour to run on
         # the Debug bots, which is too long.
        'build_configs': ['Release', 'Release_x64'],
        'predicate': Predicates.FYI_OPTIONAL_AND_V8,
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',

          # http://crbug.com/599451: this test is currently too slow
          # to run on x64 in Debug mode. Need to shard the tests.
          'Win7 x64 Debug (NVIDIA)',
        ],
        # Don't run these tests on Android yet.
        'os_types': ['android'],
      },
    ],
    'target_name': 'webgl_conformance',
    'args': [
      '--webgl-conformance-version=2.0.1',
      # The current working directory when run via isolate is
      # out/Debug or out/Release. Reference this file relatively to
      # it.
      '--read-abbreviated-json-results-from=' + \
      '../../content/test/data/gpu/webgl2_conformance_tests_output.json',
    ],
    'asan_args': ['--is-asan'],
    'swarming': {
      # These tests currently take about an hour and fifteen minutes
      # to run. Split them into roughly 5-minute shards.
      'shards': 15,
    },
  },
  'webgl2_conformance_angle_tests': {
    'tester_configs': [
      {
         # The WebGL 2.0 conformance tests take over an hour to run on
         # the Debug bots, which is too long.
        'build_configs': ['Release'],
        'predicate': Predicates.FYI_ONLY,
        # Only run on the NVIDIA Release and Intel Release Linux bots.
        'swarming_dimension_sets': [
          {
            'gpu': '10de:104a',
            'os': 'Ubuntu'
          },
          {
            'gpu': '8086:0412',
            'os': 'Ubuntu'
          },
          {
            'gpu': '8086:1912',
            'os': 'Ubuntu'
          },
        ],
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'target_name': 'webgl_conformance',
    'extra_browser_args': [
      '--use-gl=angle',
    ],
    'args': [
      '--webgl-conformance-version=2.0.1',
      # The current working directory when run via isolate is
      # out/Debug or out/Release. Reference this file relatively to
      # it.
      '--read-abbreviated-json-results-from=' + \
      '../../content/test/data/gpu/webgl2_conformance_tests_output.json',
    ],
    'asan_args': ['--is-asan'],
    'swarming': {
      # These tests currently take about an hour and fifteen minutes
      # to run. Split them into roughly 5-minute shards.
      'shards': 15,
    },
  },
  'webgl2_conformance_gl_tests': {
    'tester_configs': [
      {
         # The WebGL 2.0 conformance tests take over an hour to run on
         # the Debug bots, which is too long.
        'build_configs': ['Release'],
        'predicate': Predicates.FYI_ONLY,
        # Only run on the NVIDIA Release Windows bots.
        'swarming_dimension_sets': [
          {
            'gpu': '10de:104a',
            'os': 'Windows-2008ServerR2-SP1'
          },
        ],
        'disabled_instrumentation_types': ['tsan'],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
    'target_name': 'webgl_conformance',
    'extra_browser_args': [
      '--use-angle=gl',
    ],
    'args': [
      '--webgl-conformance-version=2.0.1',
      # The current working directory when run via isolate is
      # out/Debug or out/Release. Reference this file relatively to
      # it.
      '--read-abbreviated-json-results-from=' + \
      '../../content/test/data/gpu/webgl2_conformance_tests_output.json',
    ],
    'asan_args': ['--is-asan'],
    'swarming': {
      # These tests currently take about an hour and fifteen minutes
      # to run. Split them into roughly 5-minute shards.
      'shards': 15,
    },
  },
}

# These isolated tests don't use telemetry. They need to be placed in the
# isolated_scripts section of the generated json.
NON_TELEMETRY_ISOLATED_SCRIPT_TESTS = {
  # We run angle_perftests on the ANGLE CQ to ensure the tests don't crash.
  'angle_perftests': {
    'tester_configs': [
      {
        'predicate': Predicates.FYI_AND_OPTIONAL,
        # Run on the Win/Linux Release NVIDIA bots.
        'build_configs': ['Release'],
        'swarming_dimension_sets': [
          {
            'gpu': '10de:104a',
            'os': 'Windows-2008ServerR2-SP1'
          },
          {
            'gpu': '10de:104a',
            'os': 'Ubuntu'
          }
        ],
      },
    ],
    'disabled_tester_configs': [
      {
        'names': [
          'Linux ChromiumOS Ozone (Intel)',
        ],
      },
    ],
  },
}

def substitute_args(tester_config, args):
  """Substitutes the variables in |args| from tester_config properties."""
  substitutions = {
    'os_type': tester_config['os_type'],
    'gpu_vendor_id': '0',
    'gpu_device_id': '0',
  }

  if 'gpu' in tester_config['swarming_dimensions'][0]:
    gpu = tester_config['swarming_dimensions'][0]['gpu'].split(':')
    substitutions['gpu_vendor_id'] = gpu[0]
    substitutions['gpu_device_id'] = gpu[1]

  return [string.Template(arg).safe_substitute(substitutions) for arg in args]

def matches_swarming_dimensions(tester_config, dimension_sets):
  for dimensions in dimension_sets:
    for cur_dims in tester_config['swarming_dimensions']:
      if set(dimensions.items()).issubset(cur_dims.items()):
        return True
  return False

def is_android(tester_config):
  return tester_config['os_type'] == 'android'

def is_linux(tester_config):
  return tester_config['os_type'] == 'linux'

def is_asan(tester_config):
  return tester_config.get('is_asan', False)


# Returns a list describing the type of this tester. It may include
# both the type of the bot as well as the waterfall.
def get_tester_type(tester_config):
  result = []
  if 'type' in tester_config:
    result.append(tester_config['type'])
  result.append(tester_config['parent']['type'])
  return result

def tester_config_matches_tester(tester_name, tester_config, tc,
                                 check_waterfall):
  if check_waterfall:
    if not tc.get('predicate', Predicates.DEFAULT)(
        get_tester_type(tester_config)):
      return False

  if 'names' in tc:
    # Give priority to matching the tester_name.
    if tester_name in tc['names']:
      return True
    if not tester_name in tc['names']:
      return False
  if 'os_types' in tc:
    if not tester_config['os_type'] in tc['os_types']:
      return False
  if 'instrumentation_type' in tester_config:
    if 'disabled_instrumentation_types' in tc:
      if tester_config['instrumentation_type'] in \
            tc['disabled_instrumentation_types']:
        return False
  if 'build_configs' in tc:
    if not tester_config['build_config'] in tc['build_configs']:
      return False
  if 'swarming_dimension_sets' in tc:
    if not matches_swarming_dimensions(tester_config,
                                       tc['swarming_dimension_sets']):
      return False
  return True

def should_run_on_tester(tester_name, tester_config, test_config):
  # Check if this config is disabled on this tester
  if 'disabled_tester_configs' in test_config:
    for dtc in test_config['disabled_tester_configs']:
      if tester_config_matches_tester(tester_name, tester_config, dtc, False):
        return False
  if 'tester_configs' in test_config:
    for tc in test_config['tester_configs']:
      if tester_config_matches_tester(tester_name, tester_config, tc, True):
        return True
    return False
  else:
    # If tester_configs is unspecified, run nearly all tests by default,
    # but let tester_config_matches_tester filter out any undesired
    # tests, such as ones that should only run on the Optional bots.
    return tester_config_matches_tester(tester_name, tester_config, {}, True)

def remove_tester_configs_from_result(result):
  if 'tester_configs' in result:
    # Don't print the tester_configs in the JSON.
    result.pop('tester_configs')
  if 'disabled_tester_configs' in result:
    # Don't print the disabled_tester_configs in the JSON.
    result.pop('disabled_tester_configs')

def generate_gtest(tester_name, tester_config, test, test_config):
  if not should_run_on_tester(tester_name, tester_config, test_config):
    return None
  result = copy.deepcopy(test_config)
  if 'test' in result:
    result['name'] = test
  else:
    result['test'] = test
  if (not tester_config['swarming']) and test in NON_SWARMED_GTESTS:
    # Need to override this result.
    result = copy.deepcopy(NON_SWARMED_GTESTS[test])
    result['name'] = test
  else:
    if not 'swarming' in result:
      result['swarming'] = {}
    result['swarming'].update({
      'can_use_on_swarming_builders': tester_config['swarming'],
      'dimension_sets': tester_config['swarming_dimensions']
    })
    if is_android(tester_config):
      # Integrate with the unified logcat system.
      result['swarming'].update({
        'cipd_packages': [
          {
            'cipd_package': 'infra/tools/luci/logdog/butler/${platform}',
            'location': 'bin',
            'revision': 'git_revision:ff387eadf445b24c935f1cf7d6ddd279f8a6b04c'
          }
        ],
        'output_links': [
          {
            'link': [
              'https://luci-logdog.appspot.com/v/?s',
              '=android%2Fswarming%2Flogcats%2F',
              '${TASK_ID}%2F%2B%2Funified_logcats'
            ],
            'name': 'shard #${SHARD_INDEX} logcats'
          }
        ]
      })

  def add_conditional_args(key, fn):
    if key in result:
      if fn(tester_config):
        if not 'args' in result:
          result['args'] = []
        result['args'] += result[key]
      # Don't put the conditional args in the JSON.
      result.pop(key)

  add_conditional_args('desktop_args', lambda cfg: not is_android(cfg))
  add_conditional_args('linux_args', is_linux)
  add_conditional_args('android_args', is_android)

  if 'desktop_swarming' in result:
    if not is_android(tester_config):
      result['swarming'].update(result['desktop_swarming'])
    # Don't put the desktop_swarming in the JSON.
    result.pop('desktop_swarming')
  # Remove the tester_configs and disabled_tester_configs, if present,
  # from the result.
  remove_tester_configs_from_result(result)

  # This flag only has an effect on the Linux bots that run tests
  # locally (as opposed to via Swarming), which are only those couple
  # on the chromium.gpu.fyi waterfall. Still, there is no harm in
  # specifying it everywhere.
  result['use_xvfb'] = False
  return result

def generate_gtests(tester_name, tester_config, test_dictionary):
  # The relative ordering of some of the tests is important to
  # minimize differences compared to the handwritten JSON files, since
  # Python's sorts are stable and there are some tests with the same
  # key (see gles2_conform_d3d9_test and similar variants). Avoid
  # losing the order by avoiding coalescing the dictionaries into one.
  gtests = []
  for test_name, test_config in sorted(test_dictionary.iteritems()):
    test = generate_gtest(tester_name, tester_config,
                          test_name, test_config)
    if test:
      # generate_gtest may veto the test generation on this platform.
      gtests.append(test)
  return gtests

def generate_isolated_test(tester_name, tester_config, test, test_config,
                           extra_browser_args, isolate_name,
                           override_compile_targets, prefix_args):
  if not should_run_on_tester(tester_name, tester_config, test_config):
    return None
  test_args = ['-v']
  extra_browser_args_string = ""
  if extra_browser_args != None:
    extra_browser_args_string += ' '.join(extra_browser_args)
  if 'extra_browser_args' in test_config:
    extra_browser_args_string += ' ' + ' '.join(
        test_config['extra_browser_args'])
  if extra_browser_args_string != "":
    test_args.append('--extra-browser-args=' + extra_browser_args_string)
  if 'args' in test_config:
    test_args.extend(substitute_args(tester_config, test_config['args']))
  if 'desktop_args' in test_config and not is_android(tester_config):
    test_args.extend(substitute_args(tester_config,
                                     test_config['desktop_args']))
  if 'android_args' in test_config and is_android(tester_config):
    test_args.extend(substitute_args(tester_config,
                                     test_config['android_args']))
  if 'asan_args' in test_config and is_asan(tester_config):
    test_args.extend(substitute_args(tester_config,
                                     test_config['asan_args']))
  # The step name must end in 'test' or 'tests' in order for the
  # results to automatically show up on the flakiness dashboard.
  # (At least, this was true some time ago.) Continue to use this
  # naming convention for the time being to minimize changes.
  step_name = test
  if not (step_name.endswith('test') or step_name.endswith('tests')):
    step_name = '%s_tests' % step_name
  # Prepend GPU-specific flags.
  swarming = {
    'can_use_on_swarming_builders': tester_config['swarming'],
    'dimension_sets': tester_config['swarming_dimensions']
  }
  if 'swarming' in test_config:
    swarming.update(test_config['swarming'])
  result = {
    'args': prefix_args + test_args,
    'isolate_name': isolate_name,
    'name': step_name,
    'swarming': swarming,
  }
  if override_compile_targets != None:
    result['override_compile_targets'] = override_compile_targets
  if 'non_precommit_args' in test_config:
    result['non_precommit_args'] = test_config['non_precommit_args']
  if 'precommit_args' in test_config:
    result['precommit_args'] = test_config['precommit_args']
  return result

def generate_telemetry_test(tester_name, tester_config,
                            test, test_config):
  extra_browser_args = ['--enable-logging=stderr', '--js-flags=--expose-gc']
  benchmark_name = test_config.get('target_name') or test
  prefix_args = [
    benchmark_name,
    '--show-stdout',
    '--browser=%s' % tester_config['build_config'].lower(),
    # --passthrough displays more of the logging in Telemetry when run
    # --via typ, in particular some of the warnings about tests being
    # --expected to fail, but passing.
    '--passthrough',
  ]
  return generate_isolated_test(tester_name, tester_config, test,
                                test_config, extra_browser_args,
                                'telemetry_gpu_integration_test',
                                ['telemetry_gpu_integration_test'],
                                prefix_args)

def generate_telemetry_tests(tester_name, tester_config,
                             test_dictionary):
  isolated_scripts = []
  for test_name, test_config in sorted(test_dictionary.iteritems()):
    test = generate_telemetry_test(
      tester_name, tester_config, test_name, test_config)
    if test:
      isolated_scripts.append(test)
  return isolated_scripts

def generate_non_telemetry_isolated_test(tester_name, tester_config,
                                         test, test_config):
  return generate_isolated_test(tester_name, tester_config, test,
                                test_config,
                                None, test, None, [])

def generate_non_telemetry_isolated_tests(tester_name, tester_config,
                                          test_dictionary):
  isolated_scripts = []
  for test_name, test_config in sorted(test_dictionary.iteritems()):
    test = generate_non_telemetry_isolated_test(
      tester_name, tester_config, test_name, test_config)
    if test:
      isolated_scripts.append(test)
  return isolated_scripts

def install_parent_links(waterfall):
  # Make the testers point back to the top-level waterfall so that we
  # can ask about its properties when determining whether a given test
  # should run on a given waterfall.
  for name, config in waterfall.get('testers', {}).iteritems():
    config['parent'] = waterfall

def generate_all_tests(waterfall, filename):
  tests = {}
  for builder, config in waterfall.get('prologue', {}).iteritems():
    tests[builder] = config
  for builder, config in waterfall.get('builders', {}).iteritems():
    tests[builder] = config
  for name, config in waterfall['testers'].iteritems():
    gtests = generate_gtests(name, config, COMMON_GTESTS)
    isolated_scripts = \
      generate_telemetry_tests(
        name, config, TELEMETRY_GPU_INTEGRATION_TESTS) + \
      generate_non_telemetry_isolated_tests(name, config,
        NON_TELEMETRY_ISOLATED_SCRIPT_TESTS)
    tests[name] = {
      'gtest_tests': sorted(gtests, key=lambda x: x['test']),
      'isolated_scripts': sorted(isolated_scripts, key=lambda x: x['name'])
    }
  tests['AAAAA1 AUTOGENERATED FILE DO NOT EDIT'] = {}
  tests['AAAAA2 See generate_buildbot_json.py to make changes'] = {}
  with open(os.path.join(SRC_DIR, 'testing', 'buildbot', filename), 'wb') as fp:
    json.dump(tests, fp, indent=2, separators=(',', ': '), sort_keys=True)
    fp.write('\n')

def main():
  install_parent_links(FYI_WATERFALL)
  install_parent_links(WATERFALL)
  install_parent_links(V8_FYI_WATERFALL)

  generate_all_tests(FYI_WATERFALL, 'chromium.gpu.fyi.json')
  generate_all_tests(WATERFALL, 'chromium.gpu.json')
  generate_all_tests(V8_FYI_WATERFALL, 'client.v8.fyi.json')
  return 0

if __name__ == "__main__":
  sys.exit(main())
