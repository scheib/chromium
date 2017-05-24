// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/accessibility/browser_accessibility_com_win.h"

#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/enum_variant.h"
#include "base/win/scoped_comptr.h"
#include "base/win/windows_version.h"
#include "content/browser/accessibility/browser_accessibility_event_win.h"
#include "content/browser/accessibility/browser_accessibility_manager_win.h"
#include "content/browser/accessibility/browser_accessibility_state_impl.h"
#include "content/browser/accessibility/browser_accessibility_win.h"
#include "content/common/accessibility_messages.h"
#include "content/common/accessibility_mode.h"
#include "content/public/common/content_client.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_text_utils.h"
#include "ui/base/win/accessibility_ids_win.h"
#include "ui/base/win/accessibility_misc_utils.h"
#include "ui/base/win/atl_module.h"

namespace {

// IMPORTANT!
// These values are written to logs.  Do not renumber or delete
// existing items; add new entries to the end of the list.
enum {
  UMA_API_ACC_DO_DEFAULT_ACTION = 0,
  UMA_API_ACC_HIT_TEST = 1,
  UMA_API_ACC_LOCATION = 2,
  UMA_API_ACC_NAVIGATE = 3,
  UMA_API_ACC_SELECT = 4,
  UMA_API_ADD_SELECTION = 5,
  UMA_API_CONVERT_RETURNED_ELEMENT = 6,
  UMA_API_DO_ACTION = 7,
  UMA_API_GET_ACCESSIBLE_AT = 8,
  UMA_API_GET_ACC_CHILD = 9,
  UMA_API_GET_ACC_CHILD_COUNT = 10,
  UMA_API_GET_ACC_DEFAULT_ACTION = 11,
  UMA_API_GET_ACC_DESCRIPTION = 12,
  UMA_API_GET_ACC_FOCUS = 13,
  UMA_API_GET_ACC_HELP = 14,
  UMA_API_GET_ACC_HELP_TOPIC = 15,
  UMA_API_GET_ACC_KEYBOARD_SHORTCUT = 16,
  UMA_API_GET_ACC_NAME = 17,
  UMA_API_GET_ACC_PARENT = 18,
  UMA_API_GET_ACC_ROLE = 19,
  UMA_API_GET_ACC_SELECTION = 20,
  UMA_API_GET_ACC_STATE = 21,
  UMA_API_GET_ACC_VALUE = 22,
  UMA_API_GET_ANCHOR = 23,
  UMA_API_GET_ANCHOR_TARGET = 24,
  UMA_API_GET_APP_NAME = 25,
  UMA_API_GET_APP_VERSION = 26,
  UMA_API_GET_ATTRIBUTES_FOR_NAMES = 27,
  UMA_API_GET_CAPTION = 28,
  UMA_API_GET_CARET_OFFSET = 29,
  UMA_API_GET_CELL_AT = 30,
  UMA_API_GET_CHARACTER_EXTENTS = 31,
  UMA_API_GET_CHILD_AT = 32,
  UMA_API_GET_CHILD_INDEX = 33,
  UMA_API_GET_CLIPPED_SUBSTRING_BOUNDS = 34,
  UMA_API_GET_COLUMN_DESCRIPTION = 35,
  UMA_API_GET_COLUMN_EXTENT = 36,
  UMA_API_GET_COLUMN_EXTENT_AT = 37,
  UMA_API_GET_COLUMN_HEADER = 38,
  UMA_API_GET_COLUMN_HEADER_CELLS = 39,
  UMA_API_GET_COLUMN_INDEX = 40,
  UMA_API_GET_COMPUTED_STYLE = 41,
  UMA_API_GET_COMPUTED_STYLE_FOR_PROPERTIES = 42,
  UMA_API_GET_CURRENT_VALUE = 43,
  UMA_API_GET_DESCRIPTION = 44,
  UMA_API_GET_DOC_TYPE = 45,
  UMA_API_GET_DOM_TEXT = 46,
  UMA_API_GET_END_INDEX = 47,
  UMA_API_GET_EXTENDED_ROLE = 48,
  UMA_API_GET_EXTENDED_STATES = 49,
  UMA_API_GET_FIRST_CHILD = 50,
  UMA_API_GET_FONT_FAMILY = 51,
  UMA_API_GET_GROUP_POSITION = 52,
  UMA_API_GET_HOST_RAW_ELEMENT_PROVIDER = 53,
  UMA_API_GET_HYPERLINK = 54,
  UMA_API_GET_HYPERLINK_INDEX = 55,
  UMA_API_GET_IACCESSIBLE_PAIR = 56,
  UMA_API_GET_IMAGE_POSITION = 57,
  UMA_API_GET_IMAGE_SIZE = 58,
  UMA_API_GET_INDEX_IN_PARENT = 59,
  UMA_API_GET_INNER_HTML = 60,
  UMA_API_GET_IS_COLUMN_SELECTED = 61,
  UMA_API_GET_IS_ROW_SELECTED = 62,
  UMA_API_GET_IS_SELECTED = 63,
  UMA_API_GET_KEY_BINDING = 64,
  UMA_API_GET_LANGUAGE = 65,
  UMA_API_GET_LAST_CHILD = 66,
  UMA_API_GET_LOCALE = 67,
  UMA_API_GET_LOCALIZED_EXTENDED_ROLE = 68,
  UMA_API_GET_LOCALIZED_EXTENDED_STATES = 69,
  UMA_API_GET_LOCALIZED_NAME = 70,
  UMA_API_GET_LOCAL_INTERFACE = 71,
  UMA_API_GET_MAXIMUM_VALUE = 72,
  UMA_API_GET_MIME_TYPE = 73,
  UMA_API_GET_MINIMUM_VALUE = 74,
  UMA_API_GET_NAME = 75,
  UMA_API_GET_NAMESPACE_URI_FOR_ID = 76,
  UMA_API_GET_NEW_TEXT = 77,
  UMA_API_GET_NEXT_SIBLING = 78,
  UMA_API_GET_NODE_INFO = 79,
  UMA_API_GET_N_CHARACTERS = 80,
  UMA_API_GET_N_COLUMNS = 81,
  UMA_API_GET_N_EXTENDED_STATES = 82,
  UMA_API_GET_N_HYPERLINKS = 83,
  UMA_API_GET_N_RELATIONS = 84,
  UMA_API_GET_N_ROWS = 85,
  UMA_API_GET_N_SELECTED_CELLS = 86,
  UMA_API_GET_N_SELECTED_CHILDREN = 87,
  UMA_API_GET_N_SELECTED_COLUMNS = 88,
  UMA_API_GET_N_SELECTED_ROWS = 89,
  UMA_API_GET_N_SELECTIONS = 90,
  UMA_API_GET_OBJECT_FOR_CHILD = 91,
  UMA_API_GET_OFFSET_AT_POINT = 92,
  UMA_API_GET_OLD_TEXT = 93,
  UMA_API_GET_PARENT_NODE = 94,
  UMA_API_GET_PATTERN_PROVIDER = 95,
  UMA_API_GET_PREVIOUS_SIBLING = 96,
  UMA_API_GET_PROPERTY_VALUE = 97,
  UMA_API_GET_PROVIDER_OPTIONS = 98,
  UMA_API_GET_RELATION = 99,
  UMA_API_GET_RELATIONS = 100,
  UMA_API_GET_ROW_COLUMN_EXTENTS = 101,
  UMA_API_GET_ROW_COLUMN_EXTENTS_AT_INDEX = 102,
  UMA_API_GET_ROW_DESCRIPTION = 103,
  UMA_API_GET_ROW_EXTENT = 104,
  UMA_API_GET_ROW_EXTENT_AT = 105,
  UMA_API_GET_ROW_HEADER = 106,
  UMA_API_GET_ROW_HEADER_CELLS = 107,
  UMA_API_GET_ROW_INDEX = 108,
  UMA_API_GET_RUNTIME_ID = 109,
  UMA_API_GET_SELECTED_CELLS = 110,
  UMA_API_GET_SELECTED_CHILDREN = 111,
  UMA_API_GET_SELECTED_COLUMNS = 112,
  UMA_API_GET_SELECTED_ROWS = 113,
  UMA_API_GET_SELECTION = 114,
  UMA_API_GET_START_INDEX = 115,
  UMA_API_GET_STATES = 116,
  UMA_API_GET_SUMMARY = 117,
  UMA_API_GET_TABLE = 118,
  UMA_API_GET_TEXT = 119,
  UMA_API_GET_TEXT_AFTER_OFFSET = 120,
  UMA_API_GET_TEXT_AT_OFFSET = 121,
  UMA_API_GET_TEXT_BEFORE_OFFSET = 122,
  UMA_API_GET_TITLE = 123,
  UMA_API_GET_TOOLKIT_NAME = 124,
  UMA_API_GET_TOOLKIT_VERSION = 125,
  UMA_API_GET_UNCLIPPED_SUBSTRING_BOUNDS = 126,
  UMA_API_GET_UNIQUE_ID = 127,
  UMA_API_GET_URL = 128,
  UMA_API_GET_VALID = 129,
  UMA_API_GET_WINDOW_HANDLE = 130,
  UMA_API_IA2_GET_ATTRIBUTES = 131,
  UMA_API_IA2_SCROLL_TO = 132,
  UMA_API_IAACTION_GET_DESCRIPTION = 133,
  UMA_API_IATEXT_GET_ATTRIBUTES = 134,
  UMA_API_ISIMPLEDOMNODE_GET_ATTRIBUTES = 135,
  UMA_API_ISIMPLEDOMNODE_SCROLL_TO = 136,
  UMA_API_N_ACTIONS = 137,
  UMA_API_PUT_ALTERNATE_VIEW_MEDIA_TYPES = 138,
  UMA_API_QUERY_SERVICE = 139,
  UMA_API_REMOVE_SELECTION = 140,
  UMA_API_ROLE = 141,
  UMA_API_SCROLL_SUBSTRING_TO = 142,
  UMA_API_SCROLL_SUBSTRING_TO_POINT = 143,
  UMA_API_SCROLL_TO_POINT = 144,
  UMA_API_SCROLL_TO_SUBSTRING = 145,
  UMA_API_SELECT_COLUMN = 146,
  UMA_API_SELECT_ROW = 147,
  UMA_API_SET_CARET_OFFSET = 148,
  UMA_API_SET_CURRENT_VALUE = 149,
  UMA_API_SET_SELECTION = 150,
  UMA_API_TABLE2_GET_SELECTED_COLUMNS = 151,
  UMA_API_TABLE2_GET_SELECTED_ROWS = 152,
  UMA_API_TABLECELL_GET_COLUMN_INDEX = 153,
  UMA_API_TABLECELL_GET_IS_SELECTED = 154,
  UMA_API_TABLECELL_GET_ROW_INDEX = 155,
  UMA_API_UNSELECT_COLUMN = 156,
  UMA_API_UNSELECT_ROW = 157,

  // This must always be the last enum. It's okay for its value to
  // increase, but none of the other enum values may change.
  UMA_API_MAX
};

#define WIN_ACCESSIBILITY_API_HISTOGRAM(enum_value) \
  UMA_HISTOGRAM_ENUMERATION("Accessibility.WinAPIs", enum_value, UMA_API_MAX)

// There is no easy way to decouple |kScreenReader| and |kHTML| accessibility
// modes when Windows screen readers are used. For example, certain roles use
// the HTML tag name. Input fields require their type attribute to be exposed.
const uint32_t kScreenReaderAndHTMLAccessibilityModes =
    content::AccessibilityMode::kScreenReader |
    content::AccessibilityMode::kHTML;

const WCHAR* const IA2_RELATION_DETAILS = L"details";
const WCHAR* const IA2_RELATION_DETAILS_FOR = L"detailsFor";
const WCHAR* const IA2_RELATION_ERROR_MESSAGE = L"errorMessage";

}  // namespace

namespace content {

using AXPlatformPositionInstance = AXPlatformPosition::AXPositionInstance;
using AXPlatformRange = ui::AXRange<AXPlatformPositionInstance::element_type>;

// These nonstandard GUIDs are taken directly from the Mozilla sources
// (accessible/src/msaa/nsAccessNodeWrap.cpp); some documentation is here:
// http://developer.mozilla.org/en/Accessibility/AT-APIs/ImplementationFeatures/MSAA
const GUID GUID_ISimpleDOM = {0x0c539790,
                              0x12e4,
                              0x11cf,
                              {0xb6, 0x61, 0x00, 0xaa, 0x00, 0x4c, 0xd6, 0xd8}};
const GUID GUID_IAccessibleContentDocument = {
    0xa5d8e1f3,
    0x3571,
    0x4d8f,
    {0x95, 0x21, 0x07, 0xed, 0x28, 0xfb, 0x07, 0x2e}};

const base::char16 BrowserAccessibilityComWin::kEmbeddedCharacter = L'\xfffc';

void AddAccessibilityModeFlags(AccessibilityMode mode_flags) {
  BrowserAccessibilityStateImpl::GetInstance()->AddAccessibilityModeFlags(
      mode_flags);
}

//
// BrowserAccessibilityRelation
//
// A simple implementation of IAccessibleRelation, used to represent
// a relationship between two accessible nodes in the tree.
//

class BrowserAccessibilityRelation
    : public CComObjectRootEx<CComMultiThreadModel>,
      public IAccessibleRelation {
  BEGIN_COM_MAP(BrowserAccessibilityRelation)
  COM_INTERFACE_ENTRY(IAccessibleRelation)
  END_COM_MAP()

  CONTENT_EXPORT BrowserAccessibilityRelation() {}
  CONTENT_EXPORT virtual ~BrowserAccessibilityRelation() {}

  CONTENT_EXPORT void Initialize(BrowserAccessibilityComWin* owner,
                                 const base::string16& type);
  CONTENT_EXPORT void AddTarget(int target_id);
  CONTENT_EXPORT void RemoveTarget(int target_id);

  // Accessors.
  const base::string16& get_type() const { return type_; }
  const std::vector<int>& get_target_ids() const { return target_ids_; }

  // IAccessibleRelation methods.
  CONTENT_EXPORT STDMETHODIMP get_relationType(BSTR* relation_type) override;
  CONTENT_EXPORT STDMETHODIMP get_nTargets(long* n_targets) override;
  CONTENT_EXPORT STDMETHODIMP get_target(long target_index,
                                         IUnknown** target) override;
  CONTENT_EXPORT STDMETHODIMP get_targets(long max_targets,
                                          IUnknown** targets,
                                          long* n_targets) override;

  // IAccessibleRelation methods not implemented.
  CONTENT_EXPORT STDMETHODIMP
  get_localizedRelationType(BSTR* relation_type) override {
    return E_NOTIMPL;
  }

 private:
  base::string16 type_;
  base::win::ScopedComPtr<BrowserAccessibilityComWin> owner_;
  std::vector<int> target_ids_;
};

void BrowserAccessibilityRelation::Initialize(BrowserAccessibilityComWin* owner,
                                              const base::string16& type) {
  owner_ = owner;
  type_ = type;
}

void BrowserAccessibilityRelation::AddTarget(int target_id) {
  target_ids_.push_back(target_id);
}

void BrowserAccessibilityRelation::RemoveTarget(int target_id) {
  target_ids_.erase(
      std::remove(target_ids_.begin(), target_ids_.end(), target_id),
      target_ids_.end());
}

STDMETHODIMP BrowserAccessibilityRelation::get_relationType(
    BSTR* relation_type) {
  if (!relation_type)
    return E_INVALIDARG;

  if (!owner_->owner())
    return E_FAIL;

  *relation_type = SysAllocString(type_.c_str());
  DCHECK(*relation_type);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityRelation::get_nTargets(long* n_targets) {
  if (!n_targets)
    return E_INVALIDARG;

  if (!owner_->owner())
    return E_FAIL;

  *n_targets = static_cast<long>(target_ids_.size());

  for (long i = *n_targets - 1; i >= 0; --i) {
    BrowserAccessibilityComWin* result = owner_->GetFromID(target_ids_[i]);
    if (!result || !result->owner()) {
      *n_targets = 0;
      break;
    }
  }
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityRelation::get_target(long target_index,
                                                      IUnknown** target) {
  if (!target)
    return E_INVALIDARG;

  if (!owner_->owner())
    return E_FAIL;

  auto* manager = owner_->Manager();
  if (!manager)
    return E_FAIL;

  if (target_index < 0 ||
      target_index >= static_cast<long>(target_ids_.size())) {
    return E_INVALIDARG;
  }

  BrowserAccessibility* result = manager->GetFromID(target_ids_[target_index]);
  if (!result || !result->instance_active())
    return E_FAIL;

  *target = static_cast<IAccessible*>(
      ToBrowserAccessibilityComWin(result)->NewReference());
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityRelation::get_targets(long max_targets,
                                                       IUnknown** targets,
                                                       long* n_targets) {
  if (!targets || !n_targets)
    return E_INVALIDARG;

  if (!owner_->owner())
    return E_FAIL;

  long count = static_cast<long>(target_ids_.size());
  if (count > max_targets)
    count = max_targets;

  *n_targets = count;
  if (count == 0)
    return S_FALSE;

  for (long i = 0; i < count; ++i) {
    HRESULT result = get_target(i, &targets[i]);
    if (result != S_OK)
      return result;
  }

  return S_OK;
}

//
// BrowserAccessibilityComWin::WinAttributes
//

BrowserAccessibilityComWin::WinAttributes::WinAttributes()
    : ia_role(0), ia_state(0), ia2_role(0), ia2_state(0) {}

BrowserAccessibilityComWin::WinAttributes::~WinAttributes() {}

//
// BrowserAccessibilityComWin
//
BrowserAccessibilityComWin::BrowserAccessibilityComWin()
    : owner_(nullptr),
      win_attributes_(new WinAttributes()),
      previous_scroll_x_(0),
      previous_scroll_y_(0) {}

BrowserAccessibilityComWin::~BrowserAccessibilityComWin() {
  for (BrowserAccessibilityRelation* relation : relations_)
    relation->Release();
}

//
// IAccessible methods.
//
// Conventions:
// * Always test for owner() first and return E_FAIL if it's false.
// * Always check for invalid arguments first, even if they're unused.
// * Return S_FALSE if the only output is a string argument and it's empty.
// * There are some methods that don't touch any state such as get_toolkitName.
//   For these rare cases, you may not need to call owner().
//

HRESULT BrowserAccessibilityComWin::accDoDefaultAction(VARIANT var_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ACC_DO_DEFAULT_ACTION);

  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  BrowserAccessibilityComWin* target = GetTargetFromChildID(var_id);
  if (!target)
    return E_INVALIDARG;

  // Return an error if it hasn't got a custom action verb to describe what
  // happens on click.
  if (!target->owner()->HasIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB))
    return DISP_E_MEMBERNOTFOUND;

  manager->DoDefaultAction(*target->owner());
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::accHitTest(LONG x_left,
                                                    LONG y_top,
                                                    VARIANT* child) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ACC_HIT_TEST);

  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!child)
    return E_INVALIDARG;

  gfx::Point point(x_left, y_top);
  if (!owner()->GetScreenBoundsRect().Contains(point)) {
    // Return S_FALSE and VT_EMPTY when outside the object's boundaries.
    child->vt = VT_EMPTY;
    return S_FALSE;
  }

  BrowserAccessibility* result = manager->CachingAsyncHitTest(point);
  if (result == owner()) {
    // Point is within this object.
    child->vt = VT_I4;
    child->lVal = CHILDID_SELF;
  } else {
    child->vt = VT_DISPATCH;
    child->pdispVal = ToBrowserAccessibilityComWin(result)->NewReference();
  }
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::accLocation(LONG* x_left,
                                                     LONG* y_top,
                                                     LONG* width,
                                                     LONG* height,
                                                     VARIANT var_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ACC_LOCATION);
  if (!owner())
    return E_FAIL;

  return AXPlatformNodeWin::accLocation(x_left, y_top, width, height, var_id);
}

STDMETHODIMP BrowserAccessibilityComWin::accNavigate(LONG nav_dir,
                                                     VARIANT start,
                                                     VARIANT* end) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ACC_NAVIGATE);
  if (!owner())
    return E_FAIL;

  // Forward all non-spatial directions (e.g. NAVDIR_NEXT) to the platform node
  // implementation.
  if (nav_dir != NAVDIR_DOWN && nav_dir != NAVDIR_UP &&
      nav_dir != NAVDIR_LEFT && nav_dir != NAVDIR_RIGHT) {
    return AXPlatformNodeWin::accNavigate(nav_dir, start, end);
  }

  if (end) {
    end->vt = VT_EMPTY;
  } else {
    return E_INVALIDARG;
  }

  BrowserAccessibilityComWin* target = GetTargetFromChildID(start);
  if (!target)
    return E_INVALIDARG;

  BrowserAccessibility* result = nullptr;
  // Only handle spatial directions for tables here.
  switch (nav_dir) {
    case NAVDIR_DOWN:
      result = target->owner()->GetTableCell(
          owner()->GetTableRow() + owner()->GetTableRowSpan(),
          owner()->GetTableColumn());
      break;
    case NAVDIR_UP:
      result = target->owner()->GetTableCell(owner()->GetTableRow() - 1,
                                             owner()->GetTableColumn());
      break;
    case NAVDIR_LEFT:
      result = target->owner()->GetTableCell(owner()->GetTableRow(),
                                             owner()->GetTableColumn() - 1);
      break;
    case NAVDIR_RIGHT:
      result = target->owner()->GetTableCell(
          owner()->GetTableRow(),
          owner()->GetTableColumn() + owner()->GetTableColumnSpan());
      break;
  }

  if (!result)
    return S_FALSE;

  end->vt = VT_DISPATCH;
  end->pdispVal = ToBrowserAccessibilityComWin(result)->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accChild(VARIANT var_child,
                                                      IDispatch** disp_child) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_CHILD);
  if (!owner())
    return E_FAIL;

  if (!disp_child)
    return E_INVALIDARG;

  *disp_child = NULL;

  BrowserAccessibilityComWin* target = GetTargetFromChildID(var_child);
  if (!target)
    return E_INVALIDARG;

  (*disp_child) = target->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accChildCount(LONG* child_count) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_CHILD_COUNT);
  if (!owner())
    return E_FAIL;

  if (!child_count)
    return E_INVALIDARG;

  *child_count = owner()->PlatformChildCount();

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accDefaultAction(
    VARIANT var_id,
    BSTR* def_action) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_DEFAULT_ACTION);
  if (!owner())
    return E_FAIL;

  if (!def_action)
    return E_INVALIDARG;

  BrowserAccessibilityComWin* target = GetTargetFromChildID(var_id);
  if (!target)
    return E_INVALIDARG;

  return target->get_localizedName(0, def_action);
}

STDMETHODIMP BrowserAccessibilityComWin::get_accDescription(VARIANT var_id,
                                                            BSTR* desc) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_DESCRIPTION);
  if (!owner())
    return E_FAIL;

  if (!desc)
    return E_INVALIDARG;

  BrowserAccessibilityComWin* target = GetTargetFromChildID(var_id);
  if (!target)
    return E_INVALIDARG;

  base::string16 description_str = target->description();
  if (description_str.empty())
    return S_FALSE;

  *desc = SysAllocString(description_str.c_str());

  DCHECK(*desc);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accFocus(VARIANT* focus_child) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_FOCUS);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!focus_child)
    return E_INVALIDARG;

  BrowserAccessibilityWin* focus =
      static_cast<BrowserAccessibilityWin*>(manager->GetFocus());
  if (focus == owner()) {
    focus_child->vt = VT_I4;
    focus_child->lVal = CHILDID_SELF;
  } else if (focus == NULL) {
    focus_child->vt = VT_EMPTY;
  } else {
    focus_child->vt = VT_DISPATCH;
    focus_child->pdispVal = focus->GetCOM()->NewReference();
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accHelp(VARIANT var_id,
                                                     BSTR* help) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_HELP);
  if (!owner())
    return E_FAIL;

  return AXPlatformNodeWin::get_accHelp(var_id, help);
}

STDMETHODIMP BrowserAccessibilityComWin::get_accKeyboardShortcut(
    VARIANT var_id,
    BSTR* acc_key) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_KEYBOARD_SHORTCUT);
  if (!owner())
    return E_FAIL;

  if (!acc_key)
    return E_INVALIDARG;

  BrowserAccessibilityComWin* target = GetTargetFromChildID(var_id);
  if (!target)
    return E_INVALIDARG;

  if (target->HasStringAttribute(ui::AX_ATTR_KEY_SHORTCUTS)) {
    return target->GetStringAttributeAsBstr(ui::AX_ATTR_KEY_SHORTCUTS, acc_key);
  }

  return target->GetStringAttributeAsBstr(ui::AX_ATTR_SHORTCUT, acc_key);
}

STDMETHODIMP BrowserAccessibilityComWin::get_accName(VARIANT var_id,
                                                     BSTR* name) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_NAME);
  if (!owner())
    return E_FAIL;

  return AXPlatformNodeWin::get_accName(var_id, name);
}

STDMETHODIMP BrowserAccessibilityComWin::get_accParent(
    IDispatch** disp_parent) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_PARENT);
  if (!owner())
    return E_FAIL;

  if (!disp_parent)
    return E_INVALIDARG;

  IAccessible* parent_obj =
      ToBrowserAccessibilityComWin(owner()->PlatformGetParent());
  if (parent_obj == NULL) {
    // This happens if we're the root of the tree;
    // return the IAccessible for the window.
    parent_obj =
        Manager()->ToBrowserAccessibilityManagerWin()->GetParentIAccessible();
    // |parent| can only be NULL if the manager was created before the parent
    // IAccessible was known and it wasn't subsequently set before a client
    // requested it. This has been fixed. |parent| may also be NULL during
    // destruction. Possible cases where this could occur include tabs being
    // dragged to a new window, etc.
    if (!parent_obj) {
      DVLOG(1) << "In Function: " << __func__
               << ". Parent IAccessible interface is NULL. Returning failure";
      return E_FAIL;
    }
  }
  parent_obj->AddRef();
  *disp_parent = parent_obj;
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accRole(VARIANT var_id,
                                                     VARIANT* role) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_ROLE);
  if (!owner())
    return E_FAIL;

  if (!role)
    return E_INVALIDARG;

  BrowserAccessibilityComWin* target = GetTargetFromChildID(var_id);
  if (!target)
    return E_INVALIDARG;

  if (!target->role_name().empty()) {
    role->vt = VT_BSTR;
    role->bstrVal = SysAllocString(target->role_name().c_str());
  } else {
    role->vt = VT_I4;
    role->lVal = target->ia_role();
  }
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accState(VARIANT var_id,
                                                      VARIANT* state) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_STATE);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!state)
    return E_INVALIDARG;

  BrowserAccessibilityComWin* target = GetTargetFromChildID(var_id);
  if (!target)
    return E_INVALIDARG;

  state->vt = VT_I4;
  state->lVal = target->ia_state();
  if (manager->GetFocus() == owner())
    state->lVal |= STATE_SYSTEM_FOCUSED;

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accValue(VARIANT var_id,
                                                      BSTR* value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_VALUE);
  if (!owner())
    return E_FAIL;

  if (!value)
    return E_INVALIDARG;

  BrowserAccessibilityComWin* target = GetTargetFromChildID(var_id);
  if (!target)
    return E_INVALIDARG;

  if (target->ia_role() == ROLE_SYSTEM_PROGRESSBAR ||
      target->ia_role() == ROLE_SYSTEM_SCROLLBAR ||
      target->ia_role() == ROLE_SYSTEM_SLIDER) {
    base::string16 value_text = target->GetValueText();
    *value = SysAllocString(value_text.c_str());
    DCHECK(*value);
    return S_OK;
  }

  // Expose color well value.
  if (target->ia2_role() == IA2_ROLE_COLOR_CHOOSER) {
    unsigned int color = static_cast<unsigned int>(
        target->owner()->GetIntAttribute(ui::AX_ATTR_COLOR_VALUE));
    unsigned int red = SkColorGetR(color);
    unsigned int green = SkColorGetG(color);
    unsigned int blue = SkColorGetB(color);
    base::string16 value_text;
    value_text = base::UintToString16(red * 100 / 255) + L"% red " +
                 base::UintToString16(green * 100 / 255) + L"% green " +
                 base::UintToString16(blue * 100 / 255) + L"% blue";
    *value = SysAllocString(value_text.c_str());
    DCHECK(*value);
    return S_OK;
  }

  *value = SysAllocString(target->value().c_str());
  DCHECK(*value);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accHelpTopic(BSTR* help_file,
                                                          VARIANT var_id,
                                                          LONG* topic_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_HELP_TOPIC);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_accSelection(VARIANT* selected) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACC_SELECTION);
  if (!owner())
    return E_FAIL;

  if (owner()->GetRole() != ui::AX_ROLE_LIST_BOX)
    return E_NOTIMPL;

  unsigned long selected_count = 0;
  for (size_t i = 0; i < owner()->InternalChildCount(); ++i) {
    if (owner()->InternalGetChild(i)->HasState(ui::AX_STATE_SELECTED))
      ++selected_count;
  }

  if (selected_count == 0) {
    selected->vt = VT_EMPTY;
    return S_OK;
  }

  if (selected_count == 1) {
    for (size_t i = 0; i < owner()->InternalChildCount(); ++i) {
      if (owner()->InternalGetChild(i)->HasState(ui::AX_STATE_SELECTED)) {
        selected->vt = VT_DISPATCH;
        selected->pdispVal =
            ToBrowserAccessibilityComWin(owner()->InternalGetChild(i))
                ->NewReference();
        return S_OK;
      }
    }
  }

  // Multiple items are selected.
  base::win::EnumVariant* enum_variant =
      new base::win::EnumVariant(selected_count);
  enum_variant->AddRef();
  unsigned long index = 0;
  for (size_t i = 0; i < owner()->InternalChildCount(); ++i) {
    if (owner()->InternalGetChild(i)->HasState(ui::AX_STATE_SELECTED)) {
      enum_variant->ItemAt(index)->vt = VT_DISPATCH;
      enum_variant->ItemAt(index)->pdispVal =
          ToBrowserAccessibilityComWin(owner()->InternalGetChild(i))
              ->NewReference();
      ++index;
    }
  }
  selected->vt = VT_UNKNOWN;
  selected->punkVal = static_cast<IUnknown*>(
      static_cast<base::win::IUnknownImpl*>(enum_variant));
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::accSelect(LONG flags_sel,
                                                   VARIANT var_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ACC_SELECT);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (flags_sel & SELFLAG_TAKEFOCUS) {
    manager->SetFocus(*owner());
    return S_OK;
  }

  return S_FALSE;
}

STDMETHODIMP
BrowserAccessibilityComWin::put_accName(VARIANT var_id, BSTR put_name) {
  return E_NOTIMPL;
}
STDMETHODIMP
BrowserAccessibilityComWin::put_accValue(VARIANT var_id, BSTR put_val) {
  return E_NOTIMPL;
}

//
// IAccessible2 methods.
//

STDMETHODIMP BrowserAccessibilityComWin::role(LONG* role) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ROLE);
  if (!owner())
    return E_FAIL;

  if (!role)
    return E_INVALIDARG;

  *role = ia2_role();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_attributes(BSTR* attributes) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_IA2_GET_ATTRIBUTES);
  if (!owner())
    return E_FAIL;
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!attributes)
    return E_INVALIDARG;
  *attributes = nullptr;

  if (!owner())
    return E_FAIL;

  base::string16 str;
  for (const base::string16& attribute : ia2_attributes())
    str += attribute + L';';

  if (str.empty())
    return S_FALSE;

  *attributes = SysAllocString(str.c_str());
  DCHECK(*attributes);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_states(AccessibleStates* states) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_STATES);
  if (!owner())
    return E_FAIL;
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);

  if (!states)
    return E_INVALIDARG;

  *states = ia2_state();

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_uniqueID(LONG* unique_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_UNIQUE_ID);
  if (!owner())
    return E_FAIL;

  if (!unique_id)
    return E_INVALIDARG;

  *unique_id = -owner()->unique_id();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_windowHandle(HWND* window_handle) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_WINDOW_HANDLE);
  if (!owner())
    return E_FAIL;

  if (!window_handle)
    return E_INVALIDARG;

  *window_handle =
      Manager()->ToBrowserAccessibilityManagerWin()->GetParentHWND();
  if (!*window_handle)
    return E_FAIL;

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_indexInParent(
    LONG* index_in_parent) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_INDEX_IN_PARENT);
  if (!owner())
    return E_FAIL;

  if (!index_in_parent)
    return E_INVALIDARG;

  *index_in_parent = GetIndexInParent();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nRelations(LONG* n_relations) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_RELATIONS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!n_relations)
    return E_INVALIDARG;

  *n_relations = relations_.size();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_relation(
    LONG relation_index,
    IAccessibleRelation** relation) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_RELATION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (relation_index < 0 ||
      relation_index >= static_cast<long>(relations_.size())) {
    return E_INVALIDARG;
  }

  if (!relation)
    return E_INVALIDARG;

  relations_[relation_index]->AddRef();
  *relation = relations_[relation_index];
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_relations(
    LONG max_relations,
    IAccessibleRelation** relations,
    LONG* n_relations) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_RELATIONS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!relations || !n_relations)
    return E_INVALIDARG;

  long count = static_cast<long>(relations_.size());
  *n_relations = count;
  if (count == 0)
    return S_FALSE;

  for (long i = 0; i < count; ++i) {
    relations_[i]->AddRef();
    relations[i] = relations_[i];
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::scrollTo(IA2ScrollType scroll_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_IA2_SCROLL_TO);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();

  if (!manager)
    return E_FAIL;

  gfx::Rect r = owner()->GetFrameBoundsRect();
  switch (scroll_type) {
    case IA2_SCROLL_TYPE_TOP_LEFT:
      manager->ScrollToMakeVisible(*owner(), gfx::Rect(r.x(), r.y(), 0, 0));
      break;
    case IA2_SCROLL_TYPE_BOTTOM_RIGHT:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.right(), r.bottom(), 0, 0));
      break;
    case IA2_SCROLL_TYPE_TOP_EDGE:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.x(), r.y(), r.width(), 0));
      break;
    case IA2_SCROLL_TYPE_BOTTOM_EDGE:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.x(), r.bottom(), r.width(), 0));
      break;
    case IA2_SCROLL_TYPE_LEFT_EDGE:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.x(), r.y(), 0, r.height()));
      break;
    case IA2_SCROLL_TYPE_RIGHT_EDGE:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.right(), r.y(), 0, r.height()));
      break;
    case IA2_SCROLL_TYPE_ANYWHERE:
    default:
      manager->ScrollToMakeVisible(*owner(), r);
      break;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::scrollToPoint(
    IA2CoordinateType coordinate_type,
    LONG x,
    LONG y) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SCROLL_TO_POINT);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  gfx::Point scroll_to(x, y);

  if (coordinate_type == IA2_COORDTYPE_SCREEN_RELATIVE) {
    scroll_to -= manager->GetViewBounds().OffsetFromOrigin();
  } else if (coordinate_type == IA2_COORDTYPE_PARENT_RELATIVE) {
    if (owner()->PlatformGetParent()) {
      scroll_to +=
          owner()->PlatformGetParent()->GetFrameBoundsRect().OffsetFromOrigin();
    }
  } else {
    return E_INVALIDARG;
  }

  manager->ScrollToPoint(*owner(), scroll_to);

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_groupPosition(
    LONG* group_level,
    LONG* similar_items_in_group,
    LONG* position_in_group) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_GROUP_POSITION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!group_level || !similar_items_in_group || !position_in_group)
    return E_INVALIDARG;

  *group_level = owner()->GetIntAttribute(ui::AX_ATTR_HIERARCHICAL_LEVEL);
  *similar_items_in_group = owner()->GetIntAttribute(ui::AX_ATTR_SET_SIZE);
  *position_in_group = owner()->GetIntAttribute(ui::AX_ATTR_POS_IN_SET);

  if (*group_level == *similar_items_in_group == *position_in_group == 0)
    return S_FALSE;
  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_localizedExtendedRole(
    BSTR* localized_extended_role) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LOCALIZED_EXTENDED_ROLE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!localized_extended_role)
    return E_INVALIDARG;

  return GetStringAttributeAsBstr(ui::AX_ATTR_ROLE_DESCRIPTION,
                                  localized_extended_role);
}

//
// IAccessible2 methods not implemented.
//

STDMETHODIMP BrowserAccessibilityComWin::get_extendedRole(BSTR* extended_role) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_EXTENDED_ROLE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}
STDMETHODIMP
BrowserAccessibilityComWin::get_nExtendedStates(LONG* n_extended_states) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_EXTENDED_STATES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}
STDMETHODIMP
BrowserAccessibilityComWin::get_extendedStates(LONG max_extended_states,
                                               BSTR** extended_states,
                                               LONG* n_extended_states) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_EXTENDED_STATES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}
STDMETHODIMP
BrowserAccessibilityComWin::get_localizedExtendedStates(
    LONG max_localized_extended_states,
    BSTR** localized_extended_states,
    LONG* n_localized_extended_states) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LOCALIZED_EXTENDED_STATES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}
STDMETHODIMP BrowserAccessibilityComWin::get_locale(IA2Locale* locale) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LOCALE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

//
// IAccessibleApplication methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_appName(BSTR* app_name) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_APP_NAME);

  if (!app_name)
    return E_INVALIDARG;

  // GetProduct() returns a string like "Chrome/aa.bb.cc.dd", split out
  // the part before the "/".
  std::vector<std::string> product_components =
      base::SplitString(GetContentClient()->GetProduct(), "/",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  DCHECK_EQ(2U, product_components.size());
  if (product_components.size() != 2)
    return E_FAIL;
  *app_name = SysAllocString(base::UTF8ToUTF16(product_components[0]).c_str());
  DCHECK(*app_name);
  return *app_name ? S_OK : E_FAIL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_appVersion(BSTR* app_version) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_APP_VERSION);

  if (!app_version)
    return E_INVALIDARG;

  // GetProduct() returns a string like "Chrome/aa.bb.cc.dd", split out
  // the part after the "/".
  std::vector<std::string> product_components =
      base::SplitString(GetContentClient()->GetProduct(), "/",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  DCHECK_EQ(2U, product_components.size());
  if (product_components.size() != 2)
    return E_FAIL;
  *app_version =
      SysAllocString(base::UTF8ToUTF16(product_components[1]).c_str());
  DCHECK(*app_version);
  return *app_version ? S_OK : E_FAIL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_toolkitName(BSTR* toolkit_name) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TOOLKIT_NAME);
  if (!toolkit_name)
    return E_INVALIDARG;

  // This is hard-coded; all products based on the Chromium engine
  // will have the same toolkit name, so that assistive technology can
  // detect any Chrome-based product.
  *toolkit_name = SysAllocString(L"Chrome");
  DCHECK(*toolkit_name);
  return *toolkit_name ? S_OK : E_FAIL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_toolkitVersion(
    BSTR* toolkit_version) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TOOLKIT_VERSION);
  if (!toolkit_version)
    return E_INVALIDARG;

  std::string user_agent = GetContentClient()->GetUserAgent();
  *toolkit_version = SysAllocString(base::UTF8ToUTF16(user_agent).c_str());
  DCHECK(*toolkit_version);
  return *toolkit_version ? S_OK : E_FAIL;
}

//
// IAccessibleImage methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_description(BSTR* desc) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_DESCRIPTION);
  if (!owner())
    return E_FAIL;

  if (!desc)
    return E_INVALIDARG;

  if (description().empty())
    return S_FALSE;

  *desc = SysAllocString(description().c_str());

  DCHECK(*desc);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_imagePosition(
    IA2CoordinateType coordinate_type,
    LONG* x,
    LONG* y) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_IMAGE_POSITION);
  if (!owner())
    return E_FAIL;

  if (!x || !y)
    return E_INVALIDARG;

  if (coordinate_type == IA2_COORDTYPE_SCREEN_RELATIVE) {
    gfx::Rect bounds = owner()->GetScreenBoundsRect();
    *x = bounds.x();
    *y = bounds.y();
  } else if (coordinate_type == IA2_COORDTYPE_PARENT_RELATIVE) {
    gfx::Rect bounds = owner()->GetPageBoundsRect();
    gfx::Rect parent_bounds =
        owner()->PlatformGetParent()
            ? owner()->PlatformGetParent()->GetPageBoundsRect()
            : gfx::Rect();
    *x = bounds.x() - parent_bounds.x();
    *y = bounds.y() - parent_bounds.y();
  } else {
    return E_INVALIDARG;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_imageSize(LONG* height,
                                                       LONG* width) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_IMAGE_SIZE);
  if (!owner())
    return E_FAIL;

  if (!height || !width)
    return E_INVALIDARG;

  *height = owner()->GetPageBoundsRect().height();
  *width = owner()->GetPageBoundsRect().width();
  return S_OK;
}

//
// IAccessibleTable methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_accessibleAt(
    long row,
    long column,
    IUnknown** accessible) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ACCESSIBLE_AT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!accessible)
    return E_INVALIDARG;

  BrowserAccessibility* cell =
      owner()->GetTableCell(static_cast<int>(row), static_cast<int>(column));
  if (cell && ToBrowserAccessibilityComWin(cell)) {
    *accessible = static_cast<IAccessible*>(
        ToBrowserAccessibilityComWin(cell)->NewReference());
    return S_OK;
  }

  *accessible = nullptr;
  return E_INVALIDARG;
}

STDMETHODIMP BrowserAccessibilityComWin::get_caption(IUnknown** accessible) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CAPTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!accessible)
    return E_INVALIDARG;

  // TODO(dmazzoni): implement
  *accessible = nullptr;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_childIndex(long row,
                                                        long column,
                                                        long* cell_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CHILD_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!cell_index)
    return E_INVALIDARG;

  BrowserAccessibility* cell =
      owner()->GetTableCell(static_cast<int>(row), static_cast<int>(column));
  if (cell) {
    *cell_index = static_cast<LONG>(cell->GetTableCellIndex());
    return S_OK;
  }

  *cell_index = 0;
  return E_INVALIDARG;
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnDescription(
    long column,
    BSTR* description) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COLUMN_DESCRIPTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!description)
    return E_INVALIDARG;

  int columns = owner()->GetTableColumnCount();
  if (column < 0 || column >= columns)
    return E_INVALIDARG;

  int rows = owner()->GetTableRowCount();
  if (rows <= 0) {
    *description = nullptr;
    return S_FALSE;
  }

  for (int i = 0; i < rows; ++i) {
    BrowserAccessibility* cell = owner()->GetTableCell(i, column);
    if (ToBrowserAccessibilityComWin(cell) &&
        cell->GetRole() == ui::AX_ROLE_COLUMN_HEADER) {
      base::string16 cell_name = cell->GetString16Attribute(ui::AX_ATTR_NAME);
      if (cell_name.size() > 0) {
        *description = SysAllocString(cell_name.c_str());
        return S_OK;
      }

      if (ToBrowserAccessibilityComWin(cell)->description().size() > 0) {
        *description = SysAllocString(
            ToBrowserAccessibilityComWin(cell)->description().c_str());
        return S_OK;
      }
    }
  }

  *description = nullptr;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnExtentAt(
    long row,
    long column,
    long* n_columns_spanned) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COLUMN_EXTENT_AT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!n_columns_spanned)
    return E_INVALIDARG;

  BrowserAccessibility* cell =
      owner()->GetTableCell(static_cast<int>(row), static_cast<int>(column));
  if (!cell)
    return E_INVALIDARG;

  *n_columns_spanned = cell->GetTableColumnSpan();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnHeader(
    IAccessibleTable** accessible_table,
    long* starting_row_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COLUMN_HEADER);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  // TODO(dmazzoni): implement
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnIndex(long cell_index,
                                                         long* column_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COLUMN_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!column_index)
    return E_INVALIDARG;

  BrowserAccessibility* cell = owner()->GetTableCell(cell_index);
  if (!cell)
    return E_INVALIDARG;
  *column_index = cell->GetTableColumn();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nColumns(long* column_count) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_COLUMNS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!column_count)
    return E_INVALIDARG;

  *column_count = owner()->GetTableColumnCount();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nRows(long* row_count) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_ROWS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!row_count)
    return E_INVALIDARG;

  *row_count = owner()->GetTableRowCount();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelectedChildren(
    long* cell_count) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_SELECTED_CHILDREN);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!cell_count)
    return E_INVALIDARG;

  // TODO(dmazzoni): add support for selected cells/rows/columns in tables.
  *cell_count = 0;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelectedColumns(
    long* column_count) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_SELECTED_COLUMNS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!column_count)
    return E_INVALIDARG;

  *column_count = 0;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelectedRows(long* row_count) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_SELECTED_ROWS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!row_count)
    return E_INVALIDARG;

  *row_count = 0;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowDescription(long row,
                                                            BSTR* description) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ROW_DESCRIPTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!description)
    return E_INVALIDARG;

  if (row < 0 || row >= owner()->GetTableRowCount())
    return E_INVALIDARG;

  int columns = owner()->GetTableColumnCount();
  if (columns <= 0) {
    *description = nullptr;
    return S_FALSE;
  }

  for (int i = 0; i < columns; ++i) {
    BrowserAccessibility* cell = owner()->GetTableCell(row, i);
    if (ToBrowserAccessibilityComWin(cell) &&
        cell->GetRole() == ui::AX_ROLE_ROW_HEADER) {
      base::string16 cell_name = cell->GetString16Attribute(ui::AX_ATTR_NAME);
      if (cell_name.size() > 0) {
        *description = SysAllocString(cell_name.c_str());
        return S_OK;
      }

      if (ToBrowserAccessibilityComWin(cell)->description().size() > 0) {
        *description = SysAllocString(
            ToBrowserAccessibilityComWin(cell)->description().c_str());
        return S_OK;
      }
    }
  }

  *description = nullptr;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowExtentAt(long row,
                                                         long column,
                                                         long* n_rows_spanned) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ROW_EXTENT_AT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!n_rows_spanned)
    return E_INVALIDARG;

  BrowserAccessibility* cell = owner()->GetTableCell(row, column);
  if (!cell)
    return E_INVALIDARG;

  *n_rows_spanned = owner()->GetTableRowSpan();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowHeader(
    IAccessibleTable** accessible_table,
    long* starting_column_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ROW_HEADER);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  // TODO(dmazzoni): implement
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowIndex(long cell_index,
                                                      long* row_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ROW_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!row_index)
    return E_INVALIDARG;

  BrowserAccessibility* cell = owner()->GetTableCell(cell_index);
  if (!cell)
    return E_INVALIDARG;

  *row_index = cell->GetTableRow();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedChildren(
    long max_children,
    long** children,
    long* n_children) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_SELECTED_CHILDREN);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!children || !n_children)
    return E_INVALIDARG;

  // TODO(dmazzoni): Implement this.
  *n_children = 0;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedColumns(long max_columns,
                                                             long** columns,
                                                             long* n_columns) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_SELECTED_COLUMNS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!columns || !n_columns)
    return E_INVALIDARG;

  // TODO(dmazzoni): Implement this.
  *n_columns = 0;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedRows(long max_rows,
                                                          long** rows,
                                                          long* n_rows) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_SELECTED_ROWS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!rows || !n_rows)
    return E_INVALIDARG;

  // TODO(dmazzoni): Implement this.
  *n_rows = 0;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_summary(IUnknown** accessible) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_SUMMARY);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!accessible)
    return E_INVALIDARG;

  // TODO(dmazzoni): implement
  *accessible = nullptr;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_isColumnSelected(
    long column,
    boolean* is_selected) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_IS_COLUMN_SELECTED);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!is_selected)
    return E_INVALIDARG;

  // TODO(dmazzoni): Implement this.
  *is_selected = false;
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_isRowSelected(
    long row,
    boolean* is_selected) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_IS_ROW_SELECTED);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!is_selected)
    return E_INVALIDARG;

  // TODO(dmazzoni): Implement this.
  *is_selected = false;
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_isSelected(long row,
                                                        long column,
                                                        boolean* is_selected) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_IS_SELECTED);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!is_selected)
    return E_INVALIDARG;

  // TODO(dmazzoni): Implement this.
  *is_selected = false;
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowColumnExtentsAtIndex(
    long index,
    long* row,
    long* column,
    long* row_extents,
    long* column_extents,
    boolean* is_selected) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ROW_COLUMN_EXTENTS_AT_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!row || !column || !row_extents || !column_extents || !is_selected)
    return E_INVALIDARG;

  BrowserAccessibility* cell = owner()->GetTableCell(index);
  if (!cell)
    return E_INVALIDARG;

  *row = cell->GetTableRow();
  *column = cell->GetTableColumn();
  *row_extents = owner()->GetTableRowSpan();
  *column_extents = owner()->GetTableColumnSpan();
  *is_selected = false;  // Not supported.

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::selectRow(long row) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SELECT_ROW);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::selectColumn(long column) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SELECT_COLUMN);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::unselectRow(long row) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_UNSELECT_ROW);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::unselectColumn(long column) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_UNSELECT_COLUMN);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_modelChange(IA2TableModelChange* model_change) {
  return E_NOTIMPL;
}

//
// IAccessibleTable2 methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_cellAt(long row,
                                                    long column,
                                                    IUnknown** cell) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CELL_AT);
  AddAccessibilityModeFlags(AccessibilityMode::kScreenReader);
  if (!owner())
    return E_FAIL;

  if (!cell)
    return E_INVALIDARG;

  BrowserAccessibility* table_cell =
      owner()->GetTableCell(static_cast<int>(row), static_cast<int>(column));
  if (ToBrowserAccessibilityComWin(table_cell)) {
    return ToBrowserAccessibilityComWin(table_cell)
        ->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(cell));
  }

  *cell = nullptr;
  return E_INVALIDARG;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelectedCells(long* cell_count) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_SELECTED_CELLS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return get_nSelectedChildren(cell_count);
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedCells(
    IUnknown*** cells,
    long* n_selected_cells) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_SELECTED_CELLS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!cells || !n_selected_cells)
    return E_INVALIDARG;

  // TODO(dmazzoni): Implement this.
  *n_selected_cells = 0;
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedColumns(long** columns,
                                                             long* n_columns) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TABLE2_GET_SELECTED_COLUMNS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!columns || !n_columns)
    return E_INVALIDARG;

  // TODO(dmazzoni): Implement this.
  *n_columns = 0;
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedRows(long** rows,
                                                          long* n_rows) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TABLE2_GET_SELECTED_ROWS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!rows || !n_rows)
    return E_INVALIDARG;

  // TODO(dmazzoni): Implement this.
  *n_rows = 0;
  return S_OK;
}

//
// IAccessibleTableCell methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_columnExtent(
    long* n_columns_spanned) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COLUMN_EXTENT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!n_columns_spanned)
    return E_INVALIDARG;

  *n_columns_spanned = owner()->GetTableColumnSpan();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnHeaderCells(
    IUnknown*** cell_accessibles,
    long* n_column_header_cells) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COLUMN_HEADER_CELLS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!cell_accessibles || !n_column_header_cells)
    return E_INVALIDARG;

  *n_column_header_cells = 0;
  BrowserAccessibility* table = owner()->GetTable();
  if (!table) {
    NOTREACHED();
    return S_FALSE;
  }

  int column = owner()->GetTableColumn();
  int columns = owner()->GetTableColumnCount();
  int rows = owner()->GetTableRowCount();
  if (columns <= 0 || rows <= 0 || column < 0 || column >= columns)
    return S_FALSE;

  for (int i = 0; i < rows; ++i) {
    BrowserAccessibility* cell = owner()->GetTableCell(i, column);
    if (cell && cell->GetRole() == ui::AX_ROLE_COLUMN_HEADER)
      (*n_column_header_cells)++;
  }

  *cell_accessibles = static_cast<IUnknown**>(
      CoTaskMemAlloc((*n_column_header_cells) * sizeof(cell_accessibles[0])));
  int index = 0;
  for (int i = 0; i < rows; ++i) {
    BrowserAccessibility* cell = owner()->GetTableCell(i, column);
    if (cell && cell->GetRole() == ui::AX_ROLE_COLUMN_HEADER) {
      (*cell_accessibles)[index] = static_cast<IAccessible*>(
          ToBrowserAccessibilityComWin(cell)->NewReference());
      ++index;
    }
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnIndex(long* column_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TABLECELL_GET_COLUMN_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!column_index)
    return E_INVALIDARG;

  *column_index = owner()->GetTableColumn();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowExtent(long* n_rows_spanned) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ROW_EXTENT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!n_rows_spanned)
    return E_INVALIDARG;

  *n_rows_spanned = owner()->GetTableRowSpan();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowHeaderCells(
    IUnknown*** cell_accessibles,
    long* n_row_header_cells) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ROW_HEADER_CELLS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!cell_accessibles || !n_row_header_cells)
    return E_INVALIDARG;

  *n_row_header_cells = 0;
  BrowserAccessibility* table = owner()->GetTable();
  if (!table) {
    NOTREACHED();
    return S_FALSE;
  }

  int row = owner()->GetTableRow();
  int columns = owner()->GetTableColumnCount();
  int rows = owner()->GetTableRowCount();
  if (columns <= 0 || rows <= 0 || row < 0 || row >= rows)
    return S_FALSE;

  for (int i = 0; i < columns; ++i) {
    BrowserAccessibility* cell = owner()->GetTableCell(row, i);
    if (cell && cell->GetRole() == ui::AX_ROLE_ROW_HEADER)
      (*n_row_header_cells)++;
  }

  *cell_accessibles = static_cast<IUnknown**>(
      CoTaskMemAlloc((*n_row_header_cells) * sizeof(cell_accessibles[0])));
  int index = 0;
  for (int i = 0; i < columns; ++i) {
    BrowserAccessibility* cell = owner()->GetTableCell(row, i);
    if (cell && cell->GetRole() == ui::AX_ROLE_ROW_HEADER) {
      (*cell_accessibles)[index] = static_cast<IAccessible*>(
          ToBrowserAccessibilityComWin(cell)->NewReference());
      ++index;
    }
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowIndex(long* row_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TABLECELL_GET_ROW_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!row_index)
    return E_INVALIDARG;

  *row_index = owner()->GetTableRow();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_isSelected(boolean* is_selected) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_TABLECELL_GET_IS_SELECTED);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!is_selected)
    return E_INVALIDARG;

  *is_selected = false;
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowColumnExtents(
    long* row_index,
    long* column_index,
    long* row_extents,
    long* column_extents,
    boolean* is_selected) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ROW_COLUMN_EXTENTS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!row_index || !column_index || !row_extents || !column_extents ||
      !is_selected) {
    return E_INVALIDARG;
  }

  *row_index = owner()->GetTableRow();
  *column_index = owner()->GetTableColumn();
  *row_extents = owner()->GetTableRowSpan();
  *column_extents = owner()->GetTableColumnSpan();
  *is_selected = false;  // Not supported.

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_table(IUnknown** table) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TABLE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!table)
    return E_INVALIDARG;

  BrowserAccessibility* find_table = owner()->GetTable();
  if (!find_table || !ToBrowserAccessibilityComWin(find_table)) {
    *table = nullptr;
    return S_FALSE;
  }

  *table = static_cast<IAccessibleTable*>(
      ToBrowserAccessibilityComWin(find_table)->NewReference());
  return S_OK;
}

//
// IAccessibleText methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_nCharacters(LONG* n_characters) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_CHARACTERS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!n_characters)
    return E_INVALIDARG;

  *n_characters = static_cast<LONG>(owner()->GetText().size());
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_caretOffset(LONG* offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CARET_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!offset)
    return E_INVALIDARG;

  if (!owner()->HasCaret())
    return S_FALSE;

  int selection_start, selection_end;
  GetSelectionOffsets(&selection_start, &selection_end);
  // The caret is always at the end of the selection.
  *offset = selection_end;
  if (*offset < 0)
    return S_FALSE;

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_characterExtents(
    LONG offset,
    IA2CoordinateType coordinate_type,
    LONG* out_x,
    LONG* out_y,
    LONG* out_width,
    LONG* out_height) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CHARACTER_EXTENTS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!out_x || !out_y || !out_width || !out_height)
    return E_INVALIDARG;

  const base::string16& text_str = owner()->GetText();
  HandleSpecialTextOffset(&offset);
  if (offset < 0 || offset > static_cast<LONG>(text_str.size()))
    return E_INVALIDARG;

  gfx::Rect character_bounds;
  if (coordinate_type == IA2_COORDTYPE_SCREEN_RELATIVE) {
    character_bounds = owner()->GetScreenBoundsForRange(offset, 1);
  } else if (coordinate_type == IA2_COORDTYPE_PARENT_RELATIVE) {
    character_bounds = owner()->GetPageBoundsForRange(offset, 1);
    if (owner()->PlatformGetParent()) {
      character_bounds -=
          owner()->PlatformGetParent()->GetPageBoundsRect().OffsetFromOrigin();
    }
  } else {
    return E_INVALIDARG;
  }

  *out_x = character_bounds.x();
  *out_y = character_bounds.y();
  *out_width = character_bounds.width();
  *out_height = character_bounds.height();

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelections(LONG* n_selections) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_SELECTIONS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!n_selections)
    return E_INVALIDARG;

  *n_selections = 0;
  int selection_start, selection_end;
  GetSelectionOffsets(&selection_start, &selection_end);
  if (selection_start >= 0 && selection_end >= 0 &&
      selection_start != selection_end) {
    *n_selections = 1;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_selection(LONG selection_index,
                                                       LONG* start_offset,
                                                       LONG* end_offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_SELECTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!start_offset || !end_offset || selection_index != 0)
    return E_INVALIDARG;

  *start_offset = 0;
  *end_offset = 0;
  int selection_start, selection_end;
  GetSelectionOffsets(&selection_start, &selection_end);
  if (selection_start >= 0 && selection_end >= 0 &&
      selection_start != selection_end) {
    // We should ignore the direction of the selection when exposing start and
    // end offsets. According to the IA2 Spec the end offset is always increased
    // by one past the end of the selection. This wouldn't make sense if
    // end < start.
    if (selection_end < selection_start)
      std::swap(selection_start, selection_end);

    *start_offset = selection_start;
    *end_offset = selection_end;
    return S_OK;
  }

  return E_INVALIDARG;
}

STDMETHODIMP BrowserAccessibilityComWin::get_text(LONG start_offset,
                                                  LONG end_offset,
                                                  BSTR* text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TEXT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!text)
    return E_INVALIDARG;

  const base::string16& text_str = owner()->GetText();
  HandleSpecialTextOffset(&start_offset);
  HandleSpecialTextOffset(&end_offset);

  // The spec allows the arguments to be reversed.
  if (start_offset > end_offset) {
    LONG tmp = start_offset;
    start_offset = end_offset;
    end_offset = tmp;
  }

  // The spec does not allow the start or end offsets to be out or range;
  // we must return an error if so.
  LONG len = text_str.length();
  if (start_offset < 0)
    return E_INVALIDARG;
  if (end_offset > len)
    return E_INVALIDARG;

  base::string16 substr =
      text_str.substr(start_offset, end_offset - start_offset);

  if (substr.empty())
    return S_FALSE;

  *text = SysAllocString(substr.c_str());
  DCHECK(*text);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_textAtOffset(
    LONG offset,
    IA2TextBoundaryType boundary_type,
    LONG* start_offset,
    LONG* end_offset,
    BSTR* text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TEXT_AT_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!start_offset || !end_offset || !text)
    return E_INVALIDARG;

  HandleSpecialTextOffset(&offset);
  if (offset < 0)
    return E_INVALIDARG;

  const base::string16& text_str = owner()->GetText();
  LONG text_len = text_str.length();
  if (offset > text_len)
    return E_INVALIDARG;

  // The IAccessible2 spec says we don't have to implement the "sentence"
  // boundary type, we can just let the screenreader handle it.
  if (boundary_type == IA2_TEXT_BOUNDARY_SENTENCE) {
    *start_offset = 0;
    *end_offset = 0;
    *text = NULL;
    return S_FALSE;
  }

  // According to the IA2 Spec, only line boundaries should succeed when
  // the offset is one past the end of the text.
  if (offset == text_len) {
    if (boundary_type == IA2_TEXT_BOUNDARY_LINE) {
      --offset;
    } else {
      *start_offset = 0;
      *end_offset = 0;
      *text = nullptr;
      return S_FALSE;
    }
  }

  *start_offset =
      FindBoundary(text_str, boundary_type, offset, ui::BACKWARDS_DIRECTION);
  *end_offset =
      FindBoundary(text_str, boundary_type, offset, ui::FORWARDS_DIRECTION);
  return get_text(*start_offset, *end_offset, text);
}

STDMETHODIMP BrowserAccessibilityComWin::get_textBeforeOffset(
    LONG offset,
    IA2TextBoundaryType boundary_type,
    LONG* start_offset,
    LONG* end_offset,
    BSTR* text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TEXT_BEFORE_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!start_offset || !end_offset || !text)
    return E_INVALIDARG;

  // The IAccessible2 spec says we don't have to implement the "sentence"
  // boundary type, we can just let the screenreader handle it.
  if (boundary_type == IA2_TEXT_BOUNDARY_SENTENCE) {
    *start_offset = 0;
    *end_offset = 0;
    *text = NULL;
    return S_FALSE;
  }

  const base::string16& text_str = owner()->GetText();

  *start_offset =
      FindBoundary(text_str, boundary_type, offset, ui::BACKWARDS_DIRECTION);
  *end_offset = offset;
  return get_text(*start_offset, *end_offset, text);
}

STDMETHODIMP BrowserAccessibilityComWin::get_textAfterOffset(
    LONG offset,
    IA2TextBoundaryType boundary_type,
    LONG* start_offset,
    LONG* end_offset,
    BSTR* text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TEXT_AFTER_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!start_offset || !end_offset || !text)
    return E_INVALIDARG;

  // The IAccessible2 spec says we don't have to implement the "sentence"
  // boundary type, we can just let the screenreader handle it.
  if (boundary_type == IA2_TEXT_BOUNDARY_SENTENCE) {
    *start_offset = 0;
    *end_offset = 0;
    *text = NULL;
    return S_FALSE;
  }

  const base::string16& text_str = owner()->GetText();

  *start_offset = offset;
  *end_offset =
      FindBoundary(text_str, boundary_type, offset, ui::FORWARDS_DIRECTION);
  return get_text(*start_offset, *end_offset, text);
}

STDMETHODIMP BrowserAccessibilityComWin::get_newText(IA2TextSegment* new_text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NEW_TEXT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!new_text)
    return E_INVALIDARG;

  if (!old_win_attributes_)
    return E_FAIL;

  int start, old_len, new_len;
  ComputeHypertextRemovedAndInserted(&start, &old_len, &new_len);
  if (new_len == 0)
    return E_FAIL;

  base::string16 substr = owner()->GetText().substr(start, new_len);
  new_text->text = SysAllocString(substr.c_str());
  new_text->start = static_cast<long>(start);
  new_text->end = static_cast<long>(start + new_len);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_oldText(IA2TextSegment* old_text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_OLD_TEXT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!old_text)
    return E_INVALIDARG;

  if (!old_win_attributes_)
    return E_FAIL;

  int start, old_len, new_len;
  ComputeHypertextRemovedAndInserted(&start, &old_len, &new_len);
  if (old_len == 0)
    return E_FAIL;

  base::string16 old_hypertext = old_win_attributes_->hypertext;
  base::string16 substr = old_hypertext.substr(start, old_len);
  old_text->text = SysAllocString(substr.c_str());
  old_text->start = static_cast<long>(start);
  old_text->end = static_cast<long>(start + old_len);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_offsetAtPoint(
    LONG x,
    LONG y,
    IA2CoordinateType coord_type,
    LONG* offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_OFFSET_AT_POINT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!offset)
    return E_INVALIDARG;

  // TODO(dmazzoni): implement this. We're returning S_OK for now so that
  // screen readers still return partially accurate results rather than
  // completely failing.
  *offset = 0;
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::scrollSubstringTo(
    LONG start_index,
    LONG end_index,
    IA2ScrollType scroll_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SCROLL_SUBSTRING_TO);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  // TODO(dmazzoni): adjust this for the start and end index, too.
  return scrollTo(scroll_type);
}

STDMETHODIMP BrowserAccessibilityComWin::scrollSubstringToPoint(
    LONG start_index,
    LONG end_index,
    IA2CoordinateType coordinate_type,
    LONG x,
    LONG y) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SCROLL_SUBSTRING_TO_POINT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (start_index > end_index)
    std::swap(start_index, end_index);
  LONG length = end_index - start_index + 1;
  DCHECK_GE(length, 0);

  gfx::Rect string_bounds = owner()->GetPageBoundsForRange(start_index, length);
  string_bounds -= owner()->GetPageBoundsRect().OffsetFromOrigin();
  x -= string_bounds.x();
  y -= string_bounds.y();

  return scrollToPoint(coordinate_type, x, y);
}

STDMETHODIMP BrowserAccessibilityComWin::addSelection(LONG start_offset,
                                                      LONG end_offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ADD_SELECTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  // We only support one selection.
  SetIA2HypertextSelection(start_offset, end_offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::removeSelection(LONG selection_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_REMOVE_SELECTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (selection_index != 0)
    return E_INVALIDARG;

  // Simply collapse the selection to the position of the caret if a caret is
  // visible, otherwise set the selection to 0.
  LONG caret_offset = 0;
  int selection_start, selection_end;
  GetSelectionOffsets(&selection_start, &selection_end);
  if (owner()->HasCaret() && selection_end >= 0)
    caret_offset = selection_end;
  SetIA2HypertextSelection(caret_offset, caret_offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::setCaretOffset(LONG offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SET_CARET_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;
  SetIA2HypertextSelection(offset, offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::setSelection(LONG selection_index,
                                                      LONG start_offset,
                                                      LONG end_offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SET_SELECTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;
  if (selection_index != 0)
    return E_INVALIDARG;
  SetIA2HypertextSelection(start_offset, end_offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_attributes(LONG offset,
                                                        LONG* start_offset,
                                                        LONG* end_offset,
                                                        BSTR* text_attributes) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_IATEXT_GET_ATTRIBUTES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!start_offset || !end_offset || !text_attributes)
    return E_INVALIDARG;

  *start_offset = *end_offset = 0;
  *text_attributes = nullptr;
  if (!owner())
    return E_FAIL;

  const base::string16 text = owner()->GetText();
  HandleSpecialTextOffset(&offset);
  if (offset < 0 || offset > static_cast<LONG>(text.size()))
    return E_INVALIDARG;

  ComputeStylesIfNeeded();
  *start_offset = FindStartOfStyle(offset, ui::BACKWARDS_DIRECTION);
  *end_offset = FindStartOfStyle(offset, ui::FORWARDS_DIRECTION);

  base::string16 attributes_str;
  const std::vector<base::string16>& attributes =
      offset_to_text_attributes().find(*start_offset)->second;
  for (const base::string16& attribute : attributes) {
    attributes_str += attribute + L';';
  }

  if (attributes.empty())
    return S_FALSE;

  *text_attributes = SysAllocString(attributes_str.c_str());
  DCHECK(*text_attributes);
  return S_OK;
}

//
// IAccessibleHypertext methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_nHyperlinks(
    long* hyperlink_count) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_HYPERLINKS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!hyperlink_count)
    return E_INVALIDARG;

  *hyperlink_count = hyperlink_offset_to_index().size();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_hyperlink(
    long index,
    IAccessibleHyperlink** hyperlink) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_HYPERLINK);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!hyperlink || index < 0 ||
      index >= static_cast<long>(hyperlinks().size())) {
    return E_INVALIDARG;
  }

  int32_t id = hyperlinks()[index];
  BrowserAccessibilityComWin* link =
      ToBrowserAccessibilityComWin(owner()->GetFromUniqueID(id));
  if (!link)
    return E_FAIL;

  *hyperlink = static_cast<IAccessibleHyperlink*>(link->NewReference());
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_hyperlinkIndex(
    long char_index,
    long* hyperlink_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_HYPERLINK_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!hyperlink_index)
    return E_INVALIDARG;

  if (char_index < 0 ||
      char_index >= static_cast<long>(owner()->GetText().size())) {
    return E_INVALIDARG;
  }

  std::map<int32_t, int32_t>::iterator it =
      hyperlink_offset_to_index().find(char_index);
  if (it == hyperlink_offset_to_index().end()) {
    *hyperlink_index = -1;
    return S_FALSE;
  }

  *hyperlink_index = it->second;
  return S_OK;
}

//
// IAccessibleHyperlink methods.
//

// Currently, only text links are supported.
STDMETHODIMP BrowserAccessibilityComWin::get_anchor(long index,
                                                    VARIANT* anchor) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ANCHOR);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner() || !IsHyperlink())
    return E_FAIL;

  // IA2 text links can have only one anchor, that is the text inside them.
  if (index != 0 || !anchor)
    return E_INVALIDARG;

  BSTR ia2_hypertext = SysAllocString(owner()->GetText().c_str());
  DCHECK(ia2_hypertext);
  anchor->vt = VT_BSTR;
  anchor->bstrVal = ia2_hypertext;

  // Returning S_FALSE is not mentioned in the IA2 Spec, but it might have been
  // an oversight.
  if (!SysStringLen(ia2_hypertext))
    return S_FALSE;

  return S_OK;
}

// Currently, only text links are supported.
STDMETHODIMP BrowserAccessibilityComWin::get_anchorTarget(
    long index,
    VARIANT* anchor_target) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ANCHOR_TARGET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner() || !IsHyperlink())
    return E_FAIL;

  // IA2 text links can have at most one target, that is when they represent an
  // HTML hyperlink, i.e. an <a> element with a "href" attribute.
  if (index != 0 || !anchor_target)
    return E_INVALIDARG;

  BSTR target;
  if (!(ia_state() & STATE_SYSTEM_LINKED) ||
      FAILED(GetStringAttributeAsBstr(ui::AX_ATTR_URL, &target))) {
    target = SysAllocString(L"");
  }
  DCHECK(target);
  anchor_target->vt = VT_BSTR;
  anchor_target->bstrVal = target;

  // Returning S_FALSE is not mentioned in the IA2 Spec, but it might have been
  // an oversight.
  if (!SysStringLen(target))
    return S_FALSE;

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_startIndex(long* index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_START_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner() || !IsHyperlink())
    return E_FAIL;

  if (!index)
    return E_INVALIDARG;

  int32_t hypertext_offset = 0;
  auto* parent = owner()->PlatformGetParent();
  if (parent) {
    hypertext_offset =
        ToBrowserAccessibilityComWin(parent)->GetHypertextOffsetFromChild(
            *this);
  }
  *index = static_cast<LONG>(hypertext_offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_endIndex(long* index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_END_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  LONG start_index;
  HRESULT hr = get_startIndex(&start_index);
  if (hr == S_OK)
    *index = start_index + 1;
  return hr;
}

// This method is deprecated in the IA2 Spec.
STDMETHODIMP BrowserAccessibilityComWin::get_valid(boolean* valid) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_VALID);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

//
// IAccessibleAction partly implemented.
//

STDMETHODIMP BrowserAccessibilityComWin::nActions(long* n_actions) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_N_ACTIONS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!n_actions)
    return E_INVALIDARG;

  // |IsHyperlink| is required for |IAccessibleHyperlink::anchor/anchorTarget|
  // to work properly because the |IAccessibleHyperlink| interface inherits from
  // |IAccessibleAction|.
  if (IsHyperlink() ||
      owner()->HasIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB)) {
    *n_actions = 1;
  } else {
    *n_actions = 0;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::doAction(long action_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_DO_ACTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!owner()->HasIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB) ||
      action_index != 0)
    return E_INVALIDARG;

  Manager()->DoDefaultAction(*owner());
  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_description(long action_index,
                                            BSTR* description) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_IAACTION_GET_DESCRIPTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_keyBinding(long action_index,
                                                        long n_max_bindings,
                                                        BSTR** key_bindings,
                                                        long* n_bindings) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_KEY_BINDING);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_name(long action_index,
                                                  BSTR* name) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NAME);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!name)
    return E_INVALIDARG;

  int action;
  if (!owner()->GetIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB, &action) ||
      action_index != 0) {
    *name = nullptr;
    return E_INVALIDARG;
  }

  base::string16 action_verb = ui::ActionVerbToUnlocalizedString(
      static_cast<ui::AXDefaultActionVerb>(action));
  if (action_verb.empty() || action_verb == L"none") {
    *name = nullptr;
    return S_FALSE;
  }

  *name = SysAllocString(action_verb.c_str());
  DCHECK(name);
  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_localizedName(long action_index,
                                              BSTR* localized_name) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LOCALIZED_NAME);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!localized_name)
    return E_INVALIDARG;

  int action;
  if (!owner()->GetIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB, &action) ||
      action_index != 0) {
    *localized_name = nullptr;
    return E_INVALIDARG;
  }

  base::string16 action_verb = ui::ActionVerbToLocalizedString(
      static_cast<ui::AXDefaultActionVerb>(action));
  if (action_verb.empty()) {
    *localized_name = nullptr;
    return S_FALSE;
  }

  *localized_name = SysAllocString(action_verb.c_str());
  DCHECK(localized_name);
  return S_OK;
}

//
// IAccessibleValue methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_currentValue(VARIANT* value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CURRENT_VALUE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!value)
    return E_INVALIDARG;

  float float_val;
  if (GetFloatAttribute(ui::AX_ATTR_VALUE_FOR_RANGE, &float_val)) {
    value->vt = VT_R8;
    value->dblVal = float_val;
    return S_OK;
  }

  value->vt = VT_EMPTY;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_minimumValue(VARIANT* value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_MINIMUM_VALUE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!value)
    return E_INVALIDARG;

  float float_val;
  if (GetFloatAttribute(ui::AX_ATTR_MIN_VALUE_FOR_RANGE, &float_val)) {
    value->vt = VT_R8;
    value->dblVal = float_val;
    return S_OK;
  }

  value->vt = VT_EMPTY;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_maximumValue(VARIANT* value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_MAXIMUM_VALUE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!value)
    return E_INVALIDARG;

  float float_val;
  if (GetFloatAttribute(ui::AX_ATTR_MAX_VALUE_FOR_RANGE, &float_val)) {
    value->vt = VT_R8;
    value->dblVal = float_val;
    return S_OK;
  }

  value->vt = VT_EMPTY;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::setCurrentValue(VARIANT new_value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SET_CURRENT_VALUE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  // TODO(dmazzoni): Implement this.
  return E_NOTIMPL;
}

//
// ISimpleDOMDocument methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_URL(BSTR* url) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_URL);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!url)
    return E_INVALIDARG;

  if (owner() != manager->GetRoot())
    return E_FAIL;

  std::string str = manager->GetTreeData().url;
  if (str.empty())
    return S_FALSE;

  *url = SysAllocString(base::UTF8ToUTF16(str).c_str());
  DCHECK(*url);

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_title(BSTR* title) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TITLE);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!title)
    return E_INVALIDARG;

  std::string str = manager->GetTreeData().title;
  if (str.empty())
    return S_FALSE;

  *title = SysAllocString(base::UTF8ToUTF16(str).c_str());
  DCHECK(*title);

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_mimeType(BSTR* mime_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_MIME_TYPE);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!mime_type)
    return E_INVALIDARG;

  std::string str = manager->GetTreeData().mimetype;
  if (str.empty())
    return S_FALSE;

  *mime_type = SysAllocString(base::UTF8ToUTF16(str).c_str());
  DCHECK(*mime_type);

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_docType(BSTR* doc_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_DOC_TYPE);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!doc_type)
    return E_INVALIDARG;

  std::string str = manager->GetTreeData().doctype;
  if (str.empty())
    return S_FALSE;

  *doc_type = SysAllocString(base::UTF8ToUTF16(str).c_str());
  DCHECK(*doc_type);

  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_nameSpaceURIForID(short name_space_id,
                                                  BSTR* name_space_uri) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NAMESPACE_URI_FOR_ID);
  return E_NOTIMPL;
}

STDMETHODIMP
BrowserAccessibilityComWin::put_alternateViewMediaTypes(
    BSTR* comma_separated_media_types) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_PUT_ALTERNATE_VIEW_MEDIA_TYPES);
  return E_NOTIMPL;
}

//
// ISimpleDOMNode methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_nodeInfo(
    BSTR* node_name,
    short* name_space_id,
    BSTR* node_value,
    unsigned int* num_children,
    unsigned int* unique_id,
    unsigned short* node_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NODE_INFO);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!node_name || !name_space_id || !node_value || !num_children ||
      !unique_id || !node_type) {
    return E_INVALIDARG;
  }

  base::string16 tag;
  if (owner()->GetString16Attribute(ui::AX_ATTR_HTML_TAG, &tag))
    *node_name = SysAllocString(tag.c_str());
  else
    *node_name = nullptr;

  *name_space_id = 0;
  *node_value = SysAllocString(value().c_str());
  *num_children = owner()->PlatformChildCount();
  *unique_id = -owner()->unique_id();

  if (owner()->GetRole() == ui::AX_ROLE_ROOT_WEB_AREA ||
      owner()->GetRole() == ui::AX_ROLE_WEB_AREA) {
    *node_type = NODETYPE_DOCUMENT;
  } else if (owner()->IsTextOnlyObject()) {
    *node_type = NODETYPE_TEXT;
  } else {
    *node_type = NODETYPE_ELEMENT;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_attributes(
    unsigned short max_attribs,
    BSTR* attrib_names,
    short* name_space_id,
    BSTR* attrib_values,
    unsigned short* num_attribs) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ISIMPLEDOMNODE_GET_ATTRIBUTES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!attrib_names || !name_space_id || !attrib_values || !num_attribs)
    return E_INVALIDARG;

  *num_attribs = max_attribs;
  if (*num_attribs > owner()->GetHtmlAttributes().size())
    *num_attribs = owner()->GetHtmlAttributes().size();

  for (unsigned short i = 0; i < *num_attribs; ++i) {
    attrib_names[i] = SysAllocString(
        base::UTF8ToUTF16(owner()->GetHtmlAttributes()[i].first).c_str());
    name_space_id[i] = 0;
    attrib_values[i] = SysAllocString(
        base::UTF8ToUTF16(owner()->GetHtmlAttributes()[i].second).c_str());
  }
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_attributesForNames(
    unsigned short num_attribs,
    BSTR* attrib_names,
    short* name_space_id,
    BSTR* attrib_values) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ATTRIBUTES_FOR_NAMES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!attrib_names || !name_space_id || !attrib_values)
    return E_INVALIDARG;

  for (unsigned short i = 0; i < num_attribs; ++i) {
    name_space_id[i] = 0;
    bool found = false;
    std::string name = base::UTF16ToUTF8((LPCWSTR)attrib_names[i]);
    for (unsigned int j = 0; j < owner()->GetHtmlAttributes().size(); ++j) {
      if (owner()->GetHtmlAttributes()[j].first == name) {
        attrib_values[i] = SysAllocString(
            base::UTF8ToUTF16(owner()->GetHtmlAttributes()[j].second).c_str());
        found = true;
        break;
      }
    }
    if (!found) {
      attrib_values[i] = NULL;
    }
  }
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_computedStyle(
    unsigned short max_style_properties,
    boolean use_alternate_view,
    BSTR* style_properties,
    BSTR* style_values,
    unsigned short* num_style_properties) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COMPUTED_STYLE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!style_properties || !style_values)
    return E_INVALIDARG;

  // We only cache a single style property for now: DISPLAY

  base::string16 display;
  if (max_style_properties == 0 ||
      !owner()->GetString16Attribute(ui::AX_ATTR_DISPLAY, &display)) {
    *num_style_properties = 0;
    return S_OK;
  }

  *num_style_properties = 1;
  style_properties[0] = SysAllocString(L"display");
  style_values[0] = SysAllocString(display.c_str());

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_computedStyleForProperties(
    unsigned short num_style_properties,
    boolean use_alternate_view,
    BSTR* style_properties,
    BSTR* style_values) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COMPUTED_STYLE_FOR_PROPERTIES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!style_properties || !style_values)
    return E_INVALIDARG;

  // We only cache a single style property for now: DISPLAY

  for (unsigned short i = 0; i < num_style_properties; ++i) {
    base::string16 name = base::ToLowerASCII(
        reinterpret_cast<const base::char16*>(style_properties[i]));
    if (name == L"display") {
      base::string16 display =
          owner()->GetString16Attribute(ui::AX_ATTR_DISPLAY);
      style_values[i] = SysAllocString(display.c_str());
    } else {
      style_values[i] = NULL;
    }
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::scrollTo(boolean placeTopLeft) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ISIMPLEDOMNODE_SCROLL_TO);
  return scrollTo(placeTopLeft ? IA2_SCROLL_TYPE_TOP_LEFT
                               : IA2_SCROLL_TYPE_ANYWHERE);
}

STDMETHODIMP BrowserAccessibilityComWin::get_parentNode(ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PARENT_NODE);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  *node = ToBrowserAccessibilityComWin(owner()->PlatformGetParent())
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_firstChild(ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_FIRST_CHILD);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (owner()->PlatformChildCount() == 0) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(owner()->PlatformGetChild(0))
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_lastChild(ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LAST_CHILD);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (owner()->PlatformChildCount() == 0) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(
              owner()->PlatformGetChild(owner()->PlatformChildCount() - 1))
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_previousSibling(
    ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PREVIOUS_SIBLING);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (!owner()->PlatformGetParent() || GetIndexInParent() <= 0) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(
              owner()->PlatformGetParent()->InternalGetChild(
                  GetIndexInParent() - 1))
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nextSibling(
    ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NEXT_SIBLING);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (!owner()->PlatformGetParent() || GetIndexInParent() < 0 ||
      GetIndexInParent() >=
          static_cast<int>(owner()->PlatformGetParent()->InternalChildCount()) -
              1) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(
              owner()->PlatformGetParent()->InternalGetChild(
                  GetIndexInParent() + 1))
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_childAt(unsigned int child_index,
                                                     ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CHILD_AT);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (child_index >= owner()->PlatformChildCount())
    return E_INVALIDARG;

  BrowserAccessibility* child = owner()->PlatformGetChild(child_index);
  if (!child) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(child)->NewReference();
  return S_OK;
}

// We only support this method for retrieving MathML content.
STDMETHODIMP BrowserAccessibilityComWin::get_innerHTML(BSTR* innerHTML) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_INNER_HTML);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;
  if (owner()->GetRole() != ui::AX_ROLE_MATH)
    return E_NOTIMPL;

  base::string16 inner_html =
      owner()->GetString16Attribute(ui::AX_ATTR_INNER_HTML);
  *innerHTML = SysAllocString(inner_html.c_str());
  DCHECK(*innerHTML);
  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_localInterface(void** local_interface) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LOCAL_INTERFACE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_language(BSTR* language) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LANGUAGE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!language)
    return E_INVALIDARG;
  *language = nullptr;

  if (!owner())
    return E_FAIL;

  base::string16 lang =
      owner()->GetInheritedString16Attribute(ui::AX_ATTR_LANGUAGE);
  if (lang.empty())
    lang = L"en-US";

  *language = SysAllocString(lang.c_str());
  DCHECK(*language);
  return S_OK;
}

//
// ISimpleDOMText methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_domText(BSTR* dom_text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_DOM_TEXT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!dom_text)
    return E_INVALIDARG;

  return GetStringAttributeAsBstr(ui::AX_ATTR_NAME, dom_text);
}

STDMETHODIMP BrowserAccessibilityComWin::get_clippedSubstringBounds(
    unsigned int start_index,
    unsigned int end_index,
    int* out_x,
    int* out_y,
    int* out_width,
    int* out_height) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CLIPPED_SUBSTRING_BOUNDS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  // TODO(dmazzoni): fully support this API by intersecting the
  // rect with the container's rect.
  return get_unclippedSubstringBounds(start_index, end_index, out_x, out_y,
                                      out_width, out_height);
}

STDMETHODIMP BrowserAccessibilityComWin::get_unclippedSubstringBounds(
    unsigned int start_index,
    unsigned int end_index,
    int* out_x,
    int* out_y,
    int* out_width,
    int* out_height) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_UNCLIPPED_SUBSTRING_BOUNDS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!out_x || !out_y || !out_width || !out_height)
    return E_INVALIDARG;

  unsigned int text_length =
      static_cast<unsigned int>(owner()->GetText().size());
  if (start_index > text_length || end_index > text_length ||
      start_index > end_index) {
    return E_INVALIDARG;
  }

  gfx::Rect bounds =
      owner()->GetScreenBoundsForRange(start_index, end_index - start_index);
  *out_x = bounds.x();
  *out_y = bounds.y();
  *out_width = bounds.width();
  *out_height = bounds.height();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::scrollToSubstring(
    unsigned int start_index,
    unsigned int end_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SCROLL_TO_SUBSTRING);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            AccessibilityMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  unsigned int text_length =
      static_cast<unsigned int>(owner()->GetText().size());
  if (start_index > text_length || end_index > text_length ||
      start_index > end_index) {
    return E_INVALIDARG;
  }

  manager->ScrollToMakeVisible(
      *owner(),
      owner()->GetPageBoundsForRange(start_index, end_index - start_index));

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_fontFamily(BSTR* font_family) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_FONT_FAMILY);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!font_family)
    return E_INVALIDARG;
  *font_family = nullptr;

  if (!owner())
    return E_FAIL;

  base::string16 family =
      owner()->GetInheritedString16Attribute(ui::AX_ATTR_FONT_FAMILY);
  if (family.empty())
    return S_FALSE;

  *font_family = SysAllocString(family.c_str());
  DCHECK(*font_family);
  return S_OK;
}

//
// IServiceProvider methods.
//

STDMETHODIMP BrowserAccessibilityComWin::QueryService(REFGUID guid_service,
                                                      REFIID riid,
                                                      void** object) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_QUERY_SERVICE);
  if (!owner())
    return E_FAIL;

  if (guid_service == GUID_IAccessibleContentDocument) {
    // Special Mozilla extension: return the accessible for the root document.
    // Screen readers use this to distinguish between a document loaded event
    // on the root document vs on an iframe.
    BrowserAccessibility* node = owner();
    while (node->PlatformGetParent())
      node = node->PlatformGetParent()->manager()->GetRoot();
    return ToBrowserAccessibilityComWin(node)->QueryInterface(IID_IAccessible2,
                                                              object);
  }

  if (guid_service == IID_IAccessible || guid_service == IID_IAccessible2 ||
      guid_service == IID_IAccessibleAction ||
      guid_service == IID_IAccessibleApplication ||
      guid_service == IID_IAccessibleHyperlink ||
      guid_service == IID_IAccessibleHypertext ||
      guid_service == IID_IAccessibleImage ||
      guid_service == IID_IAccessibleTable ||
      guid_service == IID_IAccessibleTable2 ||
      guid_service == IID_IAccessibleTableCell ||
      guid_service == IID_IAccessibleText ||
      guid_service == IID_IAccessibleValue ||
      guid_service == IID_ISimpleDOMDocument ||
      guid_service == IID_ISimpleDOMNode ||
      guid_service == IID_ISimpleDOMText || guid_service == GUID_ISimpleDOM) {
    return QueryInterface(riid, object);
  }

  // We only support the IAccessibleEx interface on Windows 8 and above. This
  // is needed for the on-screen Keyboard to show up in metro mode, when the
  // user taps an editable portion on the page.
  // All methods in the IAccessibleEx interface are unimplemented.
  if (riid == IID_IAccessibleEx &&
      base::win::GetVersion() >= base::win::VERSION_WIN8) {
    return QueryInterface(riid, object);
  }

  *object = NULL;
  return E_FAIL;
}

STDMETHODIMP
BrowserAccessibilityComWin::GetObjectForChild(long child_id,
                                              IAccessibleEx** ret) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_OBJECT_FOR_CHILD);
  return E_NOTIMPL;
}

STDMETHODIMP
BrowserAccessibilityComWin::GetIAccessiblePair(IAccessible** acc,
                                               long* child_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_IACCESSIBLE_PAIR);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::GetRuntimeId(SAFEARRAY** runtime_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_RUNTIME_ID);
  return E_NOTIMPL;
}

STDMETHODIMP
BrowserAccessibilityComWin::ConvertReturnedElement(
    IRawElementProviderSimple* element,
    IAccessibleEx** acc) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_CONVERT_RETURNED_ELEMENT);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::GetPatternProvider(
    PATTERNID id,
    IUnknown** provider) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PATTERN_PROVIDER);
  DVLOG(1) << "In Function: " << __func__ << " for pattern id: " << id;
  if (!owner())
    return E_FAIL;

  if (id == UIA_ValuePatternId || id == UIA_TextPatternId) {
    if (owner()->HasState(ui::AX_STATE_EDITABLE)) {
      DVLOG(1) << "Returning UIA text provider";
      base::win::UIATextProvider::CreateTextProvider(GetValueText(), true,
                                                     provider);
      return S_OK;
    }
  }
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::GetPropertyValue(PROPERTYID id,
                                                          VARIANT* ret) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PROPERTY_VALUE);
  DVLOG(1) << "In Function: " << __func__ << " for property id: " << id;
  if (!owner())
    return E_FAIL;

  V_VT(ret) = VT_EMPTY;
  if (id == UIA_ControlTypePropertyId) {
    if (owner()->HasState(ui::AX_STATE_EDITABLE)) {
      V_VT(ret) = VT_I4;
      ret->lVal = UIA_EditControlTypeId;
      DVLOG(1) << "Returning Edit control type";
    } else {
      DVLOG(1) << "Returning empty control type";
    }
  }
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_ProviderOptions(
    ProviderOptions* ret) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PROVIDER_OPTIONS);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_HostRawElementProvider(
    IRawElementProviderSimple** provider) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_HOST_RAW_ELEMENT_PROVIDER);
  return E_NOTIMPL;
}

//
// CComObjectRootEx methods.
//

// static
HRESULT WINAPI BrowserAccessibilityComWin::InternalQueryInterface(
    void* this_ptr,
    const _ATL_INTMAP_ENTRY* entries,
    REFIID iid,
    void** object) {
  BrowserAccessibilityComWin* accessibility =
      reinterpret_cast<BrowserAccessibilityComWin*>(this_ptr);
  int32_t ia_role = accessibility->ia_role();
  if (iid == IID_IAccessibleImage) {
    if (ia_role != ROLE_SYSTEM_GRAPHIC) {
      *object = NULL;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_IAccessibleTable || iid == IID_IAccessibleTable2) {
    if (ia_role != ROLE_SYSTEM_TABLE) {
      *object = NULL;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_IAccessibleTableCell) {
    if (!accessibility->owner()->IsCellOrTableHeaderRole()) {
      *object = NULL;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_IAccessibleValue) {
    if (ia_role != ROLE_SYSTEM_PROGRESSBAR &&
        ia_role != ROLE_SYSTEM_SCROLLBAR && ia_role != ROLE_SYSTEM_SLIDER) {
      *object = NULL;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_ISimpleDOMDocument) {
    if (ia_role != ROLE_SYSTEM_DOCUMENT) {
      *object = NULL;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_IAccessibleHyperlink) {
    auto* ax_object =
        reinterpret_cast<const BrowserAccessibilityComWin*>(this_ptr);
    if (!ax_object || !ax_object->IsHyperlink()) {
      *object = nullptr;
      return E_NOINTERFACE;
    }
  }

  return CComObjectRootBase::InternalQueryInterface(this_ptr, entries, iid,
                                                    object);
}

void BrowserAccessibilityComWin::ComputeStylesIfNeeded() {
  if (!offset_to_text_attributes().empty())
    return;

  std::map<int, std::vector<base::string16>> attributes_map;
  if (owner()->PlatformIsLeaf() || owner()->IsSimpleTextControl()) {
    attributes_map[0] = ComputeTextAttributes();
    std::map<int, std::vector<base::string16>> spelling_attributes =
        GetSpellingAttributes();
    for (auto& spelling_attribute : spelling_attributes) {
      auto attributes_iterator = attributes_map.find(spelling_attribute.first);
      if (attributes_iterator == attributes_map.end()) {
        attributes_map[spelling_attribute.first] =
            std::move(spelling_attribute.second);
      } else {
        std::vector<base::string16>& existing_attributes =
            attributes_iterator->second;

        // There might be a spelling attribute already in the list of text
        // attributes, originating from "aria-invalid".
        auto existing_spelling_attribute =
            std::find(existing_attributes.begin(), existing_attributes.end(),
                      L"invalid:false");
        if (existing_spelling_attribute != existing_attributes.end())
          existing_attributes.erase(existing_spelling_attribute);

        existing_attributes.insert(existing_attributes.end(),
                                   spelling_attribute.second.begin(),
                                   spelling_attribute.second.end());
      }
    }
    win_attributes_->offset_to_text_attributes.swap(attributes_map);
    return;
  }

  int start_offset = 0;
  for (size_t i = 0; i < owner()->PlatformChildCount(); ++i) {
    auto* child = ToBrowserAccessibilityComWin(owner()->PlatformGetChild(i));
    DCHECK(child);
    std::vector<base::string16> attributes(child->ComputeTextAttributes());

    if (attributes_map.empty()) {
      attributes_map[start_offset] = attributes;
    } else {
      // Only add the attributes for this child if we are at the start of a new
      // style span.
      std::vector<base::string16> previous_attributes =
          attributes_map.rbegin()->second;
      if (!std::equal(attributes.begin(), attributes.end(),
                      previous_attributes.begin())) {
        attributes_map[start_offset] = attributes;
      }
    }

    if (child->owner()->IsTextOnlyObject())
      start_offset += child->owner()->GetText().length();
    else
      start_offset += 1;
  }

  win_attributes_->offset_to_text_attributes.swap(attributes_map);
}

// |offset| could either be a text character or a child index in case of
// non-text objects.
// TODO(nektar): Remove this function once selection bugs are fixed in Blink.
AXPlatformPosition::AXPositionInstance
BrowserAccessibilityComWin::CreatePositionForSelectionAt(int offset) const {
  if (!owner()->IsNativeTextControl() && !owner()->IsTextOnlyObject()) {
    auto* manager = Manager();
    DCHECK(manager);
    const BrowserAccessibilityComWin* child = this;
    // TODO(nektar): Make parents of text-only objects not include the text of
    // children in their hypertext.
    for (size_t i = 0; i < owner()->InternalChildCount(); ++i) {
      int new_offset = offset;
      child = ToBrowserAccessibilityComWin(owner()->InternalGetChild(i));
      DCHECK(child);
      if (child->owner()->IsTextOnlyObject()) {
        new_offset -= child->owner()->GetText().length();
      } else {
        new_offset -= 1;
      }
      if (new_offset <= 0)
        break;
      offset = new_offset;
    }
    AXPlatformPositionInstance position =
        AXPlatformPosition::CreateTextPosition(manager->ax_tree_id(),
                                               child->owner()->GetId(), offset,
                                               ui::AX_TEXT_AFFINITY_DOWNSTREAM)
            ->AsLeafTextPosition();
    if (position->GetAnchor() &&
        position->GetAnchor()->GetRole() == ui::AX_ROLE_INLINE_TEXT_BOX) {
      return position->CreateParentPosition();
    }
    return position;
  }
  return owner()->CreatePositionAt(offset);
}

//
// Private methods.
//

void BrowserAccessibilityComWin::UpdateStep1ComputeWinAttributes() {
  // Swap win_attributes_ to old_win_attributes_, allowing us to see
  // exactly what changed and fire appropriate events. Note that
  // old_win_attributes_ is cleared at the end of UpdateStep3FireEvents.
  old_win_attributes_.swap(win_attributes_);
  win_attributes_.reset(new WinAttributes());

  InitRoleAndState();

  win_attributes_->ia2_attributes.clear();

  // Expose some HTLM and ARIA attributes in the IAccessible2 attributes string.
  // "display", "tag", and "xml-roles" have somewhat unusual names for
  // historical reasons. Aside from that virtually every ARIA attribute
  // is exposed in a really straightforward way, i.e. "aria-foo" is exposed
  // as "foo".
  StringAttributeToIA2(ui::AX_ATTR_DISPLAY, "display");
  StringAttributeToIA2(ui::AX_ATTR_HTML_TAG, "tag");
  StringAttributeToIA2(ui::AX_ATTR_ROLE, "xml-roles");

  StringAttributeToIA2(ui::AX_ATTR_AUTO_COMPLETE, "autocomplete");
  StringAttributeToIA2(ui::AX_ATTR_ROLE_DESCRIPTION, "roledescription");
  StringAttributeToIA2(ui::AX_ATTR_KEY_SHORTCUTS, "keyshortcuts");

  IntAttributeToIA2(ui::AX_ATTR_HIERARCHICAL_LEVEL, "level");
  IntAttributeToIA2(ui::AX_ATTR_SET_SIZE, "setsize");
  IntAttributeToIA2(ui::AX_ATTR_POS_IN_SET, "posinset");

  if (ia_role() == ROLE_SYSTEM_CHECKBUTTON ||
      ia_role() == ROLE_SYSTEM_RADIOBUTTON ||
      ia2_role() == IA2_ROLE_CHECK_MENU_ITEM ||
      ia2_role() == IA2_ROLE_RADIO_MENU_ITEM ||
      ia2_role() == IA2_ROLE_TOGGLE_BUTTON) {
    win_attributes_->ia2_attributes.push_back(L"checkable:true");
  }

  // Expose live region attributes.
  StringAttributeToIA2(ui::AX_ATTR_LIVE_STATUS, "live");
  StringAttributeToIA2(ui::AX_ATTR_LIVE_RELEVANT, "relevant");
  BoolAttributeToIA2(ui::AX_ATTR_LIVE_ATOMIC, "atomic");
  BoolAttributeToIA2(ui::AX_ATTR_LIVE_BUSY, "busy");

  // Expose container live region attributes.
  StringAttributeToIA2(ui::AX_ATTR_CONTAINER_LIVE_STATUS, "container-live");
  StringAttributeToIA2(ui::AX_ATTR_CONTAINER_LIVE_RELEVANT,
                       "container-relevant");
  BoolAttributeToIA2(ui::AX_ATTR_CONTAINER_LIVE_ATOMIC, "container-atomic");
  BoolAttributeToIA2(ui::AX_ATTR_CONTAINER_LIVE_BUSY, "container-busy");

  // Expose the non-standard explicit-name IA2 attribute.
  int name_from;
  if (owner()->GetIntAttribute(ui::AX_ATTR_NAME_FROM, &name_from) &&
      name_from != ui::AX_NAME_FROM_CONTENTS) {
    win_attributes_->ia2_attributes.push_back(L"explicit-name:true");
  }

  // Expose the aria-current attribute.
  int32_t aria_current_state;
  if (owner()->GetIntAttribute(ui::AX_ATTR_ARIA_CURRENT_STATE,
                               &aria_current_state)) {
    switch (static_cast<ui::AXAriaCurrentState>(aria_current_state)) {
      case ui::AX_ARIA_CURRENT_STATE_NONE:
        break;
      case ui::AX_ARIA_CURRENT_STATE_FALSE:
        win_attributes_->ia2_attributes.push_back(L"current:false");
        break;
      case ui::AX_ARIA_CURRENT_STATE_TRUE:
        win_attributes_->ia2_attributes.push_back(L"current:true");
        break;
      case ui::AX_ARIA_CURRENT_STATE_PAGE:
        win_attributes_->ia2_attributes.push_back(L"current:page");
        break;
      case ui::AX_ARIA_CURRENT_STATE_STEP:
        win_attributes_->ia2_attributes.push_back(L"current:step");
        break;
      case ui::AX_ARIA_CURRENT_STATE_LOCATION:
        win_attributes_->ia2_attributes.push_back(L"current:location");
        break;
      case ui::AX_ARIA_CURRENT_STATE_DATE:
        win_attributes_->ia2_attributes.push_back(L"current:date");
        break;
      case ui::AX_ARIA_CURRENT_STATE_TIME:
        win_attributes_->ia2_attributes.push_back(L"current:time");
        break;
    }
  }

  // Expose table cell index.
  if (owner()->IsCellOrTableHeaderRole()) {
    BrowserAccessibility* table = owner()->PlatformGetParent();
    while (table && !table->IsTableLikeRole())
      table = table->PlatformGetParent();
    if (table) {
      const std::vector<int32_t>& unique_cell_ids =
          table->GetIntListAttribute(ui::AX_ATTR_UNIQUE_CELL_IDS);
      for (size_t i = 0; i < unique_cell_ids.size(); ++i) {
        if (unique_cell_ids[i] == owner()->GetId()) {
          win_attributes_->ia2_attributes.push_back(
              base::string16(L"table-cell-index:") + base::IntToString16(i));
        }
      }
    }
  }

  // Expose aria-colcount and aria-rowcount in a table, grid or treegrid.
  if (owner()->IsTableLikeRole()) {
    IntAttributeToIA2(ui::AX_ATTR_ARIA_COLUMN_COUNT, "colcount");
    IntAttributeToIA2(ui::AX_ATTR_ARIA_ROW_COUNT, "rowcount");
  }

  // Expose aria-colindex and aria-rowindex in a cell or row.
  if (owner()->IsCellOrTableHeaderRole() ||
      owner()->GetRole() == ui::AX_ROLE_ROW) {
    if (owner()->GetRole() != ui::AX_ROLE_ROW)
      IntAttributeToIA2(ui::AX_ATTR_ARIA_CELL_COLUMN_INDEX, "colindex");
    IntAttributeToIA2(ui::AX_ATTR_ARIA_CELL_ROW_INDEX, "rowindex");
  }

  // Expose row or column header sort direction.
  int32_t sort_direction;
  if ((ia_role() == ROLE_SYSTEM_COLUMNHEADER ||
       ia_role() == ROLE_SYSTEM_ROWHEADER) &&
      owner()->GetIntAttribute(ui::AX_ATTR_SORT_DIRECTION, &sort_direction)) {
    switch (static_cast<ui::AXSortDirection>(sort_direction)) {
      case ui::AX_SORT_DIRECTION_NONE:
        break;
      case ui::AX_SORT_DIRECTION_UNSORTED:
        win_attributes_->ia2_attributes.push_back(L"sort:none");
        break;
      case ui::AX_SORT_DIRECTION_ASCENDING:
        win_attributes_->ia2_attributes.push_back(L"sort:ascending");
        break;
      case ui::AX_SORT_DIRECTION_DESCENDING:
        win_attributes_->ia2_attributes.push_back(L"sort:descending");
        break;
      case ui::AX_SORT_DIRECTION_OTHER:
        win_attributes_->ia2_attributes.push_back(L"sort:other");
        break;
    }
  }

  win_attributes_->name = owner()->GetString16Attribute(ui::AX_ATTR_NAME);
  win_attributes_->description =
      owner()->GetString16Attribute(ui::AX_ATTR_DESCRIPTION);
  StringAttributeToIA2(ui::AX_ATTR_PLACEHOLDER, "placeholder");

  base::string16 value = owner()->GetValue();
  // On Windows, the value of a document should be its url.
  if (owner()->GetRole() == ui::AX_ROLE_ROOT_WEB_AREA ||
      owner()->GetRole() == ui::AX_ROLE_WEB_AREA) {
    value = base::UTF8ToUTF16(Manager()->GetTreeData().url);
  }
  // If this doesn't have a value and is linked then set its value to the url
  // attribute. This allows screen readers to read an empty link's destination.
  if (value.empty() && (ia_state() & STATE_SYSTEM_LINKED))
    value = owner()->GetString16Attribute(ui::AX_ATTR_URL);
  win_attributes_->value = value;

  ClearOwnRelations();
  AddBidirectionalRelations(IA2_RELATION_CONTROLLER_FOR,
                            IA2_RELATION_CONTROLLED_BY,
                            ui::AX_ATTR_CONTROLS_IDS);
  AddBidirectionalRelations(IA2_RELATION_DESCRIBED_BY,
                            IA2_RELATION_DESCRIPTION_FOR,
                            ui::AX_ATTR_DESCRIBEDBY_IDS);
  AddBidirectionalRelations(IA2_RELATION_FLOWS_TO, IA2_RELATION_FLOWS_FROM,
                            ui::AX_ATTR_FLOWTO_IDS);
  AddBidirectionalRelations(IA2_RELATION_LABELLED_BY, IA2_RELATION_LABEL_FOR,
                            ui::AX_ATTR_LABELLEDBY_IDS);
  AddBidirectionalRelations(IA2_RELATION_DETAILS, IA2_RELATION_DETAILS_FOR,
                            ui::AX_ATTR_DETAILS_IDS);

  int member_of_id;
  if (owner()->GetIntAttribute(ui::AX_ATTR_MEMBER_OF_ID, &member_of_id))
    AddRelation(IA2_RELATION_MEMBER_OF, member_of_id);

  int error_message_id;
  if (owner()->GetIntAttribute(ui::AX_ATTR_ERRORMESSAGE_ID, &error_message_id))
    AddRelation(IA2_RELATION_ERROR_MESSAGE, error_message_id);

  // Expose slider value.
  if (ia_role() == ROLE_SYSTEM_PROGRESSBAR ||
      ia_role() == ROLE_SYSTEM_SCROLLBAR || ia_role() == ROLE_SYSTEM_SLIDER) {
    base::string16 value_text = GetValueText();
    SanitizeStringAttributeForIA2(value_text, &value_text);
    win_attributes_->ia2_attributes.push_back(L"valuetext:" + value_text);
  }

  UpdateRequiredAttributes();
  // If this is a web area for a presentational iframe, give it a role of
  // something other than DOCUMENT so that the fact that it's a separate doc
  // is not exposed to AT.
  if (owner()->IsWebAreaForPresentationalIframe()) {
    win_attributes_->ia_role = ROLE_SYSTEM_GROUPING;
    win_attributes_->ia2_role = ROLE_SYSTEM_GROUPING;
  }
}

void BrowserAccessibilityComWin::UpdateStep2ComputeHypertext() {
  if (owner()->IsSimpleTextControl()) {
    win_attributes_->hypertext = value();
    return;
  }

  if (!owner()->PlatformChildCount()) {
    if (owner()->IsRichTextControl()) {
      // We don't want to expose any associated label in IA2 Hypertext.
      return;
    }
    win_attributes_->hypertext = name();
    return;
  }

  // Construct the hypertext for this node, which contains the concatenation
  // of all of the static text and widespace of this node's children and an
  // embedded object character for all the other children. Build up a map from
  // the character index of each embedded object character to the id of the
  // child object it points to.
  for (unsigned int i = 0; i < owner()->PlatformChildCount(); ++i) {
    auto* child = ToBrowserAccessibilityComWin(owner()->PlatformGetChild(i));
    DCHECK(child);
    // Similar to Firefox, we don't expose text-only objects in IA2 hypertext.
    if (child->owner()->IsTextOnlyObject()) {
      win_attributes_->hypertext += child->name();
    } else {
      int32_t char_offset = static_cast<int32_t>(owner()->GetText().size());
      int32_t child_unique_id = child->owner()->unique_id();
      int32_t index = hyperlinks().size();
      win_attributes_->hyperlink_offset_to_index[char_offset] = index;
      win_attributes_->hyperlinks.push_back(child_unique_id);
      win_attributes_->hypertext += kEmbeddedCharacter;
    }
  }
}

void BrowserAccessibilityComWin::UpdateStep3FireEvents(
    bool is_subtree_creation) {
  // Fire an event when a new subtree is created.
  if (is_subtree_creation)
    FireNativeEvent(EVENT_OBJECT_SHOW);

  // The rest of the events only fire on changes, not on new objects.
  if (old_win_attributes_->ia_role != 0 ||
      !old_win_attributes_->role_name.empty()) {
    // Fire an event if the name, description, help, or value changes.
    if (name() != old_win_attributes_->name)
      FireNativeEvent(EVENT_OBJECT_NAMECHANGE);
    if (description() != old_win_attributes_->description)
      FireNativeEvent(EVENT_OBJECT_DESCRIPTIONCHANGE);
    if (value() != old_win_attributes_->value)
      FireNativeEvent(EVENT_OBJECT_VALUECHANGE);
    if (ia_state() != old_win_attributes_->ia_state)
      FireNativeEvent(EVENT_OBJECT_STATECHANGE);

    // Handle selection being added or removed.
    bool is_selected_now = (ia_state() & STATE_SYSTEM_SELECTED) != 0;
    bool was_selected_before =
        (old_win_attributes_->ia_state & STATE_SYSTEM_SELECTED) != 0;
    if (is_selected_now || was_selected_before) {
      bool multiselect = false;
      if (owner()->PlatformGetParent() &&
          owner()->PlatformGetParent()->HasState(ui::AX_STATE_MULTISELECTABLE))
        multiselect = true;

      if (multiselect) {
        // In a multi-select box, fire SELECTIONADD and SELECTIONREMOVE events.
        if (is_selected_now && !was_selected_before) {
          FireNativeEvent(EVENT_OBJECT_SELECTIONADD);
        } else if (!is_selected_now && was_selected_before) {
          FireNativeEvent(EVENT_OBJECT_SELECTIONREMOVE);
        }
      } else if (is_selected_now && !was_selected_before) {
        // In a single-select box, only fire SELECTION events.
        FireNativeEvent(EVENT_OBJECT_SELECTION);
      }
    }

    // Fire an event if this container object has scrolled.
    int sx = 0;
    int sy = 0;
    if (owner()->GetIntAttribute(ui::AX_ATTR_SCROLL_X, &sx) &&
        owner()->GetIntAttribute(ui::AX_ATTR_SCROLL_Y, &sy)) {
      if (sx != previous_scroll_x_ || sy != previous_scroll_y_)
        FireNativeEvent(EVENT_SYSTEM_SCROLLINGEND);
      previous_scroll_x_ = sx;
      previous_scroll_y_ = sy;
    }

    // Fire hypertext-related events.
    int start, old_len, new_len;
    ComputeHypertextRemovedAndInserted(&start, &old_len, &new_len);
    if (old_len > 0) {
      // In-process screen readers may call IAccessibleText::get_oldText
      // in reaction to this event to retrieve the text that was removed.
      FireNativeEvent(IA2_EVENT_TEXT_REMOVED);
    }
    if (new_len > 0) {
      // In-process screen readers may call IAccessibleText::get_newText
      // in reaction to this event to retrieve the text that was inserted.
      FireNativeEvent(IA2_EVENT_TEXT_INSERTED);
    }

    // Changing a static text node can affect the IAccessibleText hypertext
    // of the parent node, so force an update on the parent.
    BrowserAccessibilityComWin* parent =
        ToBrowserAccessibilityComWin(owner()->PlatformGetParent());
    if (parent && owner()->IsTextOnlyObject() &&
        name() != old_win_attributes_->name) {
      parent->owner()->UpdatePlatformAttributes();
    }
  }

  old_win_attributes_.reset(nullptr);
}

BrowserAccessibilityManager* BrowserAccessibilityComWin::Manager() const {
  DCHECK(owner());

  auto* manager = owner()->manager();
  DCHECK(manager);
  return manager;
}

//
// AXPlatformNode overrides
//
void BrowserAccessibilityComWin::Destroy() {
  // Detach BrowserAccessibilityWin from us.
  owner_ = nullptr;
  AXPlatformNodeWin::Destroy();
}

void BrowserAccessibilityComWin::Init(ui::AXPlatformNodeDelegate* delegate) {
  owner_ = static_cast<BrowserAccessibilityWin*>(delegate);
  AXPlatformNodeBase::Init(delegate);
}

std::vector<base::string16> BrowserAccessibilityComWin::ComputeTextAttributes()
    const {
  std::vector<base::string16> attributes;

  // We include list markers for now, but there might be other objects that are
  // auto generated.
  // TODO(nektar): Compute what objects are auto-generated in Blink.
  if (owner()->GetRole() == ui::AX_ROLE_LIST_MARKER)
    attributes.push_back(L"auto-generated:true");
  else
    attributes.push_back(L"auto-generated:false");

  int color;
  base::string16 color_value(L"transparent");
  if (owner()->GetIntAttribute(ui::AX_ATTR_BACKGROUND_COLOR, &color)) {
    unsigned int alpha = SkColorGetA(color);
    unsigned int red = SkColorGetR(color);
    unsigned int green = SkColorGetG(color);
    unsigned int blue = SkColorGetB(color);
    if (alpha) {
      color_value = L"rgb(" + base::UintToString16(red) + L',' +
                    base::UintToString16(green) + L',' +
                    base::UintToString16(blue) + L')';
    }
  }
  SanitizeStringAttributeForIA2(color_value, &color_value);
  attributes.push_back(L"background-color:" + color_value);

  if (owner()->GetIntAttribute(ui::AX_ATTR_COLOR, &color)) {
    unsigned int red = SkColorGetR(color);
    unsigned int green = SkColorGetG(color);
    unsigned int blue = SkColorGetB(color);
    color_value = L"rgb(" + base::UintToString16(red) + L',' +
                  base::UintToString16(green) + L',' +
                  base::UintToString16(blue) + L')';
  } else {
    color_value = L"rgb(0,0,0)";
  }
  SanitizeStringAttributeForIA2(color_value, &color_value);
  attributes.push_back(L"color:" + color_value);

  base::string16 font_family(
      owner()->GetInheritedString16Attribute(ui::AX_ATTR_FONT_FAMILY));
  // Attribute has no default value.
  if (!font_family.empty()) {
    SanitizeStringAttributeForIA2(font_family, &font_family);
    attributes.push_back(L"font-family:" + font_family);
  }

  float font_size;
  // Attribute has no default value.
  if (GetFloatAttribute(ui::AX_ATTR_FONT_SIZE, &font_size)) {
    // The IA2 Spec requires the value to be in pt, not in pixels.
    // There are 72 points per inch.
    // We assume that there are 96 pixels per inch on a standard display.
    // TODO(nektar): Figure out the current value of pixels per inch.
    float points = font_size * 72.0 / 96.0;
    attributes.push_back(L"font-size:" +
                         base::UTF8ToUTF16(base::DoubleToString(points)) +
                         L"pt");
  }

  auto text_style = static_cast<ui::AXTextStyle>(
      owner()->GetIntAttribute(ui::AX_ATTR_TEXT_STYLE));
  if (text_style == ui::AX_TEXT_STYLE_NONE) {
    attributes.push_back(L"font-style:normal");
    attributes.push_back(L"font-weight:normal");
  } else {
    if (text_style & ui::AX_TEXT_STYLE_ITALIC) {
      attributes.push_back(L"font-style:italic");
    } else {
      attributes.push_back(L"font-style:normal");
    }

    if (text_style & ui::AX_TEXT_STYLE_BOLD) {
      attributes.push_back(L"font-weight:bold");
    } else {
      attributes.push_back(L"font-weight:normal");
    }
  }

  auto invalid_state = static_cast<ui::AXInvalidState>(
      owner()->GetIntAttribute(ui::AX_ATTR_INVALID_STATE));
  switch (invalid_state) {
    case ui::AX_INVALID_STATE_NONE:
    case ui::AX_INVALID_STATE_FALSE:
      attributes.push_back(L"invalid:false");
      break;
    case ui::AX_INVALID_STATE_TRUE:
      attributes.push_back(L"invalid:true");
      break;
    case ui::AX_INVALID_STATE_SPELLING:
    case ui::AX_INVALID_STATE_GRAMMAR: {
      base::string16 spelling_grammar_value;
      if (invalid_state & ui::AX_INVALID_STATE_SPELLING)
        spelling_grammar_value = L"spelling";
      else if (invalid_state & ui::AX_INVALID_STATE_GRAMMAR)
        spelling_grammar_value = L"grammar";
      else
        spelling_grammar_value = L"spelling,grammar";
      attributes.push_back(L"invalid:" + spelling_grammar_value);
      break;
    }
    case ui::AX_INVALID_STATE_OTHER: {
      base::string16 aria_invalid_value;
      if (owner()->GetString16Attribute(ui::AX_ATTR_ARIA_INVALID_VALUE,
                                        &aria_invalid_value)) {
        SanitizeStringAttributeForIA2(aria_invalid_value, &aria_invalid_value);
        attributes.push_back(L"invalid:" + aria_invalid_value);
      } else {
        // Set the attribute to L"true", since we cannot be more specific.
        attributes.push_back(L"invalid:true");
      }
      break;
    }
  }

  base::string16 language(
      owner()->GetInheritedString16Attribute(ui::AX_ATTR_LANGUAGE));
  // Default value should be L"en-US".
  if (language.empty()) {
    attributes.push_back(L"language:en-US");
  } else {
    SanitizeStringAttributeForIA2(language, &language);
    attributes.push_back(L"language:" + language);
  }

  // TODO(nektar): Add Blink support for the following attributes.
  // Currently set to their default values as dictated by the IA2 Spec.
  attributes.push_back(L"text-line-through-mode:continuous");
  if (text_style & ui::AX_TEXT_STYLE_LINE_THROUGH) {
    // TODO(nektar): Figure out a more specific value.
    attributes.push_back(L"text-line-through-style:solid");
  } else {
    attributes.push_back(L"text-line-through-style:none");
  }
  // Default value must be the empty string.
  attributes.push_back(L"text-line-through-text:");
  if (text_style & ui::AX_TEXT_STYLE_LINE_THROUGH) {
    // TODO(nektar): Figure out a more specific value.
    attributes.push_back(L"text-line-through-type:single");
  } else {
    attributes.push_back(L"text-line-through-type:none");
  }
  attributes.push_back(L"text-line-through-width:auto");
  attributes.push_back(L"text-outline:false");
  attributes.push_back(L"text-position:baseline");
  attributes.push_back(L"text-shadow:none");
  attributes.push_back(L"text-underline-mode:continuous");
  if (text_style & ui::AX_TEXT_STYLE_UNDERLINE) {
    // TODO(nektar): Figure out a more specific value.
    attributes.push_back(L"text-underline-style:solid");
    attributes.push_back(L"text-underline-type:single");
  } else {
    attributes.push_back(L"text-underline-style:none");
    attributes.push_back(L"text-underline-type:none");
  }
  attributes.push_back(L"text-underline-width:auto");

  auto text_direction = static_cast<ui::AXTextDirection>(
      owner()->GetIntAttribute(ui::AX_ATTR_TEXT_DIRECTION));
  switch (text_direction) {
    case ui::AX_TEXT_DIRECTION_NONE:
    case ui::AX_TEXT_DIRECTION_LTR:
      attributes.push_back(L"writing-mode:lr");
      break;
    case ui::AX_TEXT_DIRECTION_RTL:
      attributes.push_back(L"writing-mode:rl");
      break;
    case ui::AX_TEXT_DIRECTION_TTB:
      attributes.push_back(L"writing-mode:tb");
      break;
    case ui::AX_TEXT_DIRECTION_BTT:
      // Not listed in the IA2 Spec.
      attributes.push_back(L"writing-mode:bt");
      break;
  }

  return attributes;
}

BrowserAccessibilityComWin* BrowserAccessibilityComWin::NewReference() {
  AddRef();
  return this;
}

std::map<int, std::vector<base::string16>>
BrowserAccessibilityComWin::GetSpellingAttributes() {
  std::map<int, std::vector<base::string16>> spelling_attributes;
  if (owner()->IsTextOnlyObject()) {
    const std::vector<int32_t>& marker_types =
        owner()->GetIntListAttribute(ui::AX_ATTR_MARKER_TYPES);
    const std::vector<int>& marker_starts =
        owner()->GetIntListAttribute(ui::AX_ATTR_MARKER_STARTS);
    const std::vector<int>& marker_ends =
        owner()->GetIntListAttribute(ui::AX_ATTR_MARKER_ENDS);
    for (size_t i = 0; i < marker_types.size(); ++i) {
      if (!(static_cast<ui::AXMarkerType>(marker_types[i]) &
            ui::AX_MARKER_TYPE_SPELLING))
        continue;
      int start_offset = marker_starts[i];
      int end_offset = marker_ends[i];
      std::vector<base::string16> start_attributes;
      start_attributes.push_back(L"invalid:spelling");
      std::vector<base::string16> end_attributes;
      end_attributes.push_back(L"invalid:false");
      spelling_attributes[start_offset] = start_attributes;
      spelling_attributes[end_offset] = end_attributes;
    }
  }
  if (owner()->IsSimpleTextControl()) {
    int start_offset = 0;
    for (BrowserAccessibility* static_text =
             BrowserAccessibilityManager::NextTextOnlyObject(
                 owner()->InternalGetChild(0));
         static_text; static_text = static_text->GetNextSibling()) {
      auto* text_win = ToBrowserAccessibilityComWin(static_text);
      if (text_win) {
        std::map<int, std::vector<base::string16>> text_spelling_attributes =
            text_win->GetSpellingAttributes();
        for (auto& attribute : text_spelling_attributes) {
          spelling_attributes[start_offset + attribute.first] =
              std::move(attribute.second);
        }
        start_offset += static_cast<int>(text_win->owner()->GetText().length());
      }
    }
  }
  return spelling_attributes;
}

BrowserAccessibilityComWin* BrowserAccessibilityComWin::GetTargetFromChildID(
    const VARIANT& var_id) {
  if (!owner())
    return nullptr;

  if (var_id.vt != VT_I4)
    return nullptr;

  LONG child_id = var_id.lVal;
  if (child_id == CHILDID_SELF)
    return this;

  if (child_id >= 1 &&
      child_id <= static_cast<LONG>(owner()->PlatformChildCount()))
    return ToBrowserAccessibilityComWin(
        owner()->PlatformGetChild(child_id - 1));

  BrowserAccessibilityComWin* child = ToBrowserAccessibilityComWin(
      BrowserAccessibility::GetFromUniqueID(-child_id));
  if (child && child->owner()->IsDescendantOf(owner()))
    return child;

  return nullptr;
}

HRESULT BrowserAccessibilityComWin::GetStringAttributeAsBstr(
    ui::AXStringAttribute attribute,
    BSTR* value_bstr) {
  base::string16 str;
  if (!owner())
    return E_FAIL;

  if (!owner()->GetString16Attribute(attribute, &str))
    return S_FALSE;

  *value_bstr = SysAllocString(str.c_str());
  DCHECK(*value_bstr);

  return S_OK;
}

// Static
void BrowserAccessibilityComWin::SanitizeStringAttributeForIA2(
    const base::string16& input,
    base::string16* output) {
  DCHECK(output);
  // According to the IA2 Spec, these characters need to be escaped with a
  // backslash: backslash, colon, comma, equals and semicolon.
  // Note that backslash must be replaced first.
  base::ReplaceChars(input, L"\\", L"\\\\", output);
  base::ReplaceChars(*output, L":", L"\\:", output);
  base::ReplaceChars(*output, L",", L"\\,", output);
  base::ReplaceChars(*output, L"=", L"\\=", output);
  base::ReplaceChars(*output, L";", L"\\;", output);
}

void BrowserAccessibilityComWin::SetIA2HypertextSelection(LONG start_offset,
                                                          LONG end_offset) {
  HandleSpecialTextOffset(&start_offset);
  HandleSpecialTextOffset(&end_offset);
  AXPlatformPositionInstance start_position =
      CreatePositionForSelectionAt(static_cast<int>(start_offset));
  AXPlatformPositionInstance end_position =
      CreatePositionForSelectionAt(static_cast<int>(end_offset));
  Manager()->SetSelection(AXPlatformRange(start_position->AsTextPosition(),
                                          end_position->AsTextPosition()));
}

void BrowserAccessibilityComWin::StringAttributeToIA2(
    ui::AXStringAttribute attribute,
    const char* ia2_attr) {
  base::string16 value;
  if (owner()->GetString16Attribute(attribute, &value)) {
    SanitizeStringAttributeForIA2(value, &value);
    win_attributes_->ia2_attributes.push_back(base::ASCIIToUTF16(ia2_attr) +
                                              L":" + value);
  }
}

void BrowserAccessibilityComWin::BoolAttributeToIA2(
    ui::AXBoolAttribute attribute,
    const char* ia2_attr) {
  bool value;
  if (owner()->GetBoolAttribute(attribute, &value)) {
    win_attributes_->ia2_attributes.push_back(
        (base::ASCIIToUTF16(ia2_attr) + L":") + (value ? L"true" : L"false"));
  }
}

void BrowserAccessibilityComWin::IntAttributeToIA2(ui::AXIntAttribute attribute,
                                                   const char* ia2_attr) {
  int value;
  if (owner()->GetIntAttribute(attribute, &value)) {
    win_attributes_->ia2_attributes.push_back(
        base::ASCIIToUTF16(ia2_attr) + L":" + base::IntToString16(value));
  }
}

bool BrowserAccessibilityComWin::IsHyperlink() const {
  int32_t hyperlink_index = -1;
  auto* parent = owner()->PlatformGetParent();
  if (parent) {
    hyperlink_index =
        ToBrowserAccessibilityComWin(parent)->GetHyperlinkIndexFromChild(*this);
  }

  if (hyperlink_index >= 0)
    return true;
  return false;
}

BrowserAccessibilityComWin*
BrowserAccessibilityComWin::GetHyperlinkFromHypertextOffset(int offset) const {
  std::map<int32_t, int32_t>::iterator iterator =
      hyperlink_offset_to_index().find(offset);
  if (iterator == hyperlink_offset_to_index().end())
    return nullptr;

  int32_t index = iterator->second;
  DCHECK_GE(index, 0);
  DCHECK_LT(index, static_cast<int32_t>(hyperlinks().size()));
  int32_t id = hyperlinks()[index];
  BrowserAccessibilityComWin* hyperlink =
      ToBrowserAccessibilityComWin(owner()->GetFromUniqueID(id));
  if (!hyperlink)
    return nullptr;
  return hyperlink;
}

int32_t BrowserAccessibilityComWin::GetHyperlinkIndexFromChild(
    const BrowserAccessibilityComWin& child) const {
  if (hyperlinks().empty())
    return -1;

  auto iterator = std::find(hyperlinks().begin(), hyperlinks().end(),
                            child.owner()->unique_id());
  if (iterator == hyperlinks().end())
    return -1;

  return static_cast<int32_t>(iterator - hyperlinks().begin());
}

int32_t BrowserAccessibilityComWin::GetHypertextOffsetFromHyperlinkIndex(
    int32_t hyperlink_index) const {
  for (auto& offset_index : hyperlink_offset_to_index()) {
    if (offset_index.second == hyperlink_index)
      return offset_index.first;
  }

  return -1;
}

int32_t BrowserAccessibilityComWin::GetHypertextOffsetFromChild(
    BrowserAccessibilityComWin& child) {
  DCHECK(child.owner()->PlatformGetParent() == owner());

  // Handle the case when we are dealing with a direct text-only child.
  // (Note that this object might be a platform leaf, e.g. an ARIA searchbox,
  // and so |owner()->InternalChild...| functions need to be used. Also,
  // direct text-only children should not be present at tree roots and so no
  // cross-tree traversal is necessary.)
  if (child.owner()->IsTextOnlyObject()) {
    int32_t hypertextOffset = 0;
    int32_t index_in_parent = child.GetIndexInParent();
    DCHECK_GE(index_in_parent, 0);
    DCHECK_LT(index_in_parent,
              static_cast<int32_t>(owner()->InternalChildCount()));
    for (uint32_t i = 0; i < static_cast<uint32_t>(index_in_parent); ++i) {
      const BrowserAccessibilityComWin* sibling =
          ToBrowserAccessibilityComWin(owner()->InternalGetChild(i));
      DCHECK(sibling);
      if (sibling->owner()->IsTextOnlyObject())
        hypertextOffset += sibling->owner()->GetText().size();
      else
        ++hypertextOffset;
    }
    return hypertextOffset;
  }

  int32_t hyperlink_index = GetHyperlinkIndexFromChild(child);
  if (hyperlink_index < 0)
    return -1;

  return GetHypertextOffsetFromHyperlinkIndex(hyperlink_index);
}

int32_t BrowserAccessibilityComWin::GetHypertextOffsetFromDescendant(
    const BrowserAccessibilityComWin& descendant) const {
  auto* parent_object =
      ToBrowserAccessibilityComWin(descendant.owner()->PlatformGetParent());
  auto* current_object = const_cast<BrowserAccessibilityComWin*>(&descendant);
  while (parent_object && parent_object != this) {
    current_object = parent_object;
    parent_object = ToBrowserAccessibilityComWin(
        current_object->owner()->PlatformGetParent());
  }
  if (!parent_object)
    return -1;

  return parent_object->GetHypertextOffsetFromChild(*current_object);
}

int BrowserAccessibilityComWin::GetHypertextOffsetFromEndpoint(
    const BrowserAccessibilityComWin& endpoint_object,
    int endpoint_offset) const {
  // There are three cases:
  // 1. Either the selection endpoint is inside this object or is an ancestor of
  // of this object. endpoint_offset should be returned.
  // 2. The selection endpoint is a pure descendant of this object. The offset
  // of the character corresponding to the subtree in which the endpoint is
  // located should be returned.
  // 3. The selection endpoint is in a completely different part of the tree.
  // Either 0 or text_length should be returned depending on the direction that
  // one needs to travel to find the endpoint.

  // Case 1.
  //
  // IsDescendantOf includes the case when endpoint_object == this.
  if (owner()->IsDescendantOf(endpoint_object.owner()))
    return endpoint_offset;

  const BrowserAccessibility* common_parent = owner();
  int32_t index_in_common_parent = owner()->GetIndexInParent();
  while (common_parent &&
         !endpoint_object.owner()->IsDescendantOf(common_parent)) {
    index_in_common_parent = common_parent->GetIndexInParent();
    common_parent = common_parent->PlatformGetParent();
  }
  if (!common_parent)
    return -1;

  DCHECK_GE(index_in_common_parent, 0);
  DCHECK(!(common_parent->IsTextOnlyObject()));

  // Case 2.
  //
  // We already checked in case 1 if our endpoint is inside this object.
  // We can safely assume that it is a descendant or in a completely different
  // part of the tree.
  if (common_parent == owner()) {
    int32_t hypertext_offset =
        GetHypertextOffsetFromDescendant(endpoint_object);
    if (endpoint_object.owner()->PlatformGetParent() == owner() &&
        endpoint_object.owner()->IsTextOnlyObject()) {
      hypertext_offset += endpoint_offset;
    }

    return hypertext_offset;
  }

  // Case 3.
  //
  // We can safely assume that the endpoint is in another part of the tree or
  // at common parent, and that this object is a descendant of common parent.
  int32_t endpoint_index_in_common_parent = -1;
  for (uint32_t i = 0; i < common_parent->InternalChildCount(); ++i) {
    const BrowserAccessibility* child = common_parent->InternalGetChild(i);
    DCHECK(child);
    if (endpoint_object.owner()->IsDescendantOf(child)) {
      endpoint_index_in_common_parent = child->GetIndexInParent();
      break;
    }
  }
  DCHECK_GE(endpoint_index_in_common_parent, 0);

  if (endpoint_index_in_common_parent < index_in_common_parent)
    return 0;
  if (endpoint_index_in_common_parent > index_in_common_parent)
    return owner()->GetText().size();

  NOTREACHED();
  return -1;
}

int BrowserAccessibilityComWin::GetSelectionAnchor() const {
  int32_t anchor_id = Manager()->GetTreeData().sel_anchor_object_id;
  const BrowserAccessibilityComWin* anchor_object = GetFromID(anchor_id);
  if (!anchor_object)
    return -1;

  int anchor_offset = Manager()->GetTreeData().sel_anchor_offset;
  return GetHypertextOffsetFromEndpoint(*anchor_object, anchor_offset);
}

int BrowserAccessibilityComWin::GetSelectionFocus() const {
  int32_t focus_id = Manager()->GetTreeData().sel_focus_object_id;
  const BrowserAccessibilityComWin* focus_object = GetFromID(focus_id);
  if (!focus_object)
    return -1;

  int focus_offset = Manager()->GetTreeData().sel_focus_offset;
  return GetHypertextOffsetFromEndpoint(*focus_object, focus_offset);
}

void BrowserAccessibilityComWin::GetSelectionOffsets(int* selection_start,
                                                     int* selection_end) const {
  DCHECK(selection_start && selection_end);

  if (owner()->IsSimpleTextControl() &&
      owner()->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_START, selection_start) &&
      owner()->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_END, selection_end)) {
    return;
  }

  *selection_start = GetSelectionAnchor();
  *selection_end = GetSelectionFocus();
  if (*selection_start < 0 || *selection_end < 0)
    return;

  // There are three cases when a selection would start and end on the same
  // character:
  // 1. Anchor and focus are both in a subtree that is to the right of this
  // object.
  // 2. Anchor and focus are both in a subtree that is to the left of this
  // object.
  // 3. Anchor and focus are in a subtree represented by a single embedded
  // object character.
  // Only case 3 refers to a valid selection because cases 1 and 2 fall
  // outside this object in their entirety.
  // Selections that span more than one character are by definition inside this
  // object, so checking them is not necessary.
  if (*selection_start == *selection_end && !owner()->HasCaret()) {
    *selection_start = -1;
    *selection_end = -1;
    return;
  }

  // The IA2 Spec says that if the largest of the two offsets falls on an
  // embedded object character and if there is a selection in that embedded
  // object, it should be incremented by one so that it points after the
  // embedded object character.
  // This is a signal to AT software that the embedded object is also part of
  // the selection.
  int* largest_offset =
      (*selection_start <= *selection_end) ? selection_end : selection_start;
  BrowserAccessibilityComWin* hyperlink =
      GetHyperlinkFromHypertextOffset(*largest_offset);
  if (!hyperlink)
    return;

  LONG n_selections = 0;
  HRESULT hr = hyperlink->get_nSelections(&n_selections);
  DCHECK(SUCCEEDED(hr));
  if (n_selections > 0)
    ++(*largest_offset);
}

base::string16 BrowserAccessibilityComWin::GetValueText() {
  float fval;
  base::string16 result = value();

  if (result.empty() && GetFloatAttribute(ui::AX_ATTR_VALUE_FOR_RANGE, &fval)) {
    result = base::UTF8ToUTF16(base::DoubleToString(fval));
  }
  return result;
}

bool BrowserAccessibilityComWin::IsSameHypertextCharacter(
    size_t old_char_index,
    size_t new_char_index) {
  CHECK(old_win_attributes_);

  // For anything other than the "embedded character", we just compare the
  // characters directly.
  base::char16 old_ch = old_win_attributes_->hypertext[old_char_index];
  base::char16 new_ch = win_attributes_->hypertext[new_char_index];
  if (old_ch != new_ch)
    return false;
  if (old_ch == new_ch && new_ch != kEmbeddedCharacter)
    return true;

  // If it's an embedded character, they're only identical if the child id
  // the hyperlink points to is the same.
  std::map<int32_t, int32_t>& old_offset_to_index =
      old_win_attributes_->hyperlink_offset_to_index;
  std::vector<int32_t>& old_hyperlinks = old_win_attributes_->hyperlinks;
  int32_t old_hyperlinks_count = static_cast<int32_t>(old_hyperlinks.size());
  std::map<int32_t, int32_t>::iterator iter;
  iter = old_offset_to_index.find(old_char_index);
  int old_index = (iter != old_offset_to_index.end()) ? iter->second : -1;
  int old_child_id = (old_index >= 0 && old_index < old_hyperlinks_count)
                         ? old_hyperlinks[old_index]
                         : -1;

  std::map<int32_t, int32_t>& new_offset_to_index =
      win_attributes_->hyperlink_offset_to_index;
  std::vector<int32_t>& new_hyperlinks = win_attributes_->hyperlinks;
  int32_t new_hyperlinks_count = static_cast<int32_t>(new_hyperlinks.size());
  iter = new_offset_to_index.find(new_char_index);
  int new_index = (iter != new_offset_to_index.end()) ? iter->second : -1;
  int new_child_id = (new_index >= 0 && new_index < new_hyperlinks_count)
                         ? new_hyperlinks[new_index]
                         : -1;

  return old_child_id == new_child_id;
}

void BrowserAccessibilityComWin::ComputeHypertextRemovedAndInserted(
    int* start,
    int* old_len,
    int* new_len) {
  CHECK(old_win_attributes_);

  *start = 0;
  *old_len = 0;
  *new_len = 0;

  const base::string16& old_text = old_win_attributes_->hypertext;
  const base::string16& new_text = owner()->GetText();

  size_t common_prefix = 0;
  while (common_prefix < old_text.size() && common_prefix < new_text.size() &&
         IsSameHypertextCharacter(common_prefix, common_prefix)) {
    ++common_prefix;
  }

  size_t common_suffix = 0;
  while (common_prefix + common_suffix < old_text.size() &&
         common_prefix + common_suffix < new_text.size() &&
         IsSameHypertextCharacter(old_text.size() - common_suffix - 1,
                                  new_text.size() - common_suffix - 1)) {
    ++common_suffix;
  }

  *start = common_prefix;
  *old_len = old_text.size() - common_prefix - common_suffix;
  *new_len = new_text.size() - common_prefix - common_suffix;
}

void BrowserAccessibilityComWin::HandleSpecialTextOffset(LONG* offset) {
  if (*offset == IA2_TEXT_OFFSET_LENGTH) {
    *offset = static_cast<LONG>(owner()->GetText().length());
  } else if (*offset == IA2_TEXT_OFFSET_CARET) {
    // We shouldn't call |get_caretOffset| here as it affects UMA counts.
    int selection_start, selection_end;
    GetSelectionOffsets(&selection_start, &selection_end);
    *offset = selection_end;
  }
}

ui::TextBoundaryType BrowserAccessibilityComWin::IA2TextBoundaryToTextBoundary(
    IA2TextBoundaryType ia2_boundary) {
  switch (ia2_boundary) {
    case IA2_TEXT_BOUNDARY_CHAR:
      return ui::CHAR_BOUNDARY;
    case IA2_TEXT_BOUNDARY_WORD:
      return ui::WORD_BOUNDARY;
    case IA2_TEXT_BOUNDARY_LINE:
      return ui::LINE_BOUNDARY;
    case IA2_TEXT_BOUNDARY_SENTENCE:
      return ui::SENTENCE_BOUNDARY;
    case IA2_TEXT_BOUNDARY_PARAGRAPH:
      return ui::PARAGRAPH_BOUNDARY;
    case IA2_TEXT_BOUNDARY_ALL:
      return ui::ALL_BOUNDARY;
  }
  NOTREACHED();
  return ui::CHAR_BOUNDARY;
}

LONG BrowserAccessibilityComWin::FindBoundary(
    const base::string16& text,
    IA2TextBoundaryType ia2_boundary,
    LONG start_offset,
    ui::TextBoundaryDirection direction) {
  // If the boundary is relative to the caret, use the selection
  // affinity, otherwise default to downstream affinity.
  ui::AXTextAffinity affinity =
      start_offset == IA2_TEXT_OFFSET_CARET
          ? Manager()->GetTreeData().sel_focus_affinity
          : ui::AX_TEXT_AFFINITY_DOWNSTREAM;

  HandleSpecialTextOffset(&start_offset);
  if (ia2_boundary == IA2_TEXT_BOUNDARY_WORD) {
    switch (direction) {
      case ui::FORWARDS_DIRECTION: {
        AXPlatformPositionInstance position =
            owner()->CreatePositionAt(static_cast<int>(start_offset), affinity);
        AXPlatformPositionInstance next_word =
            position->CreateNextWordStartPosition();
        if (next_word->anchor_id() != owner()->GetId())
          next_word = position->CreatePositionAtEndOfAnchor();
        return next_word->text_offset();
      }
      case ui::BACKWARDS_DIRECTION: {
        AXPlatformPositionInstance position =
            owner()->CreatePositionAt(static_cast<int>(start_offset), affinity);
        AXPlatformPositionInstance previous_word;
        if (!position->AtStartOfWord()) {
          previous_word = position->CreatePreviousWordStartPosition();
          if (previous_word->anchor_id() != owner()->GetId())
            previous_word = position->CreatePositionAtStartOfAnchor();
        } else {
          previous_word = std::move(position);
        }
        return previous_word->text_offset();
      }
    }
  }

  if (ia2_boundary == IA2_TEXT_BOUNDARY_LINE) {
    switch (direction) {
      case ui::FORWARDS_DIRECTION: {
        AXPlatformPositionInstance position =
            owner()->CreatePositionAt(static_cast<int>(start_offset), affinity);
        AXPlatformPositionInstance next_line =
            position->CreateNextLineStartPosition();
        if (next_line->anchor_id() != owner()->GetId())
          next_line = position->CreatePositionAtEndOfAnchor();
        return next_line->text_offset();
      }
      case ui::BACKWARDS_DIRECTION: {
        AXPlatformPositionInstance position =
            owner()->CreatePositionAt(static_cast<int>(start_offset), affinity);
        AXPlatformPositionInstance previous_line;
        if (!position->AtStartOfLine()) {
          previous_line = position->CreatePreviousLineStartPosition();
          if (previous_line->anchor_id() != owner()->GetId())
            previous_line = position->CreatePositionAtStartOfAnchor();
        } else {
          previous_line = std::move(position);
        }
        return previous_line->text_offset();
      }
    }
  }

  // TODO(nektar): |AXPosition| can handle other types of boundaries as well.
  ui::TextBoundaryType boundary = IA2TextBoundaryToTextBoundary(ia2_boundary);
  return ui::FindAccessibleTextBoundary(text, owner()->GetLineStartOffsets(),
                                        boundary, start_offset, direction,
                                        affinity);
}

LONG BrowserAccessibilityComWin::FindStartOfStyle(
    LONG start_offset,
    ui::TextBoundaryDirection direction) const {
  LONG text_length = static_cast<LONG>(owner()->GetText().length());
  DCHECK_GE(start_offset, 0);
  DCHECK_LE(start_offset, text_length);

  switch (direction) {
    case ui::BACKWARDS_DIRECTION: {
      if (offset_to_text_attributes().empty())
        return 0;

      auto iterator = offset_to_text_attributes().upper_bound(start_offset);
      --iterator;
      return static_cast<LONG>(iterator->first);
    }
    case ui::FORWARDS_DIRECTION: {
      const auto iterator =
          offset_to_text_attributes().upper_bound(start_offset);
      if (iterator == offset_to_text_attributes().end())
        return text_length;
      return static_cast<LONG>(iterator->first);
    }
  }

  NOTREACHED();
  return start_offset;
}

BrowserAccessibilityComWin* BrowserAccessibilityComWin::GetFromID(
    int32_t id) const {
  if (!owner())
    return nullptr;
  return ToBrowserAccessibilityComWin(Manager()->GetFromID(id));
}

bool BrowserAccessibilityComWin::IsListBoxOptionOrMenuListOption() {
  if (!owner()->PlatformGetParent())
    return false;

  int32_t role = owner()->GetRole();
  int32_t parent_role = owner()->PlatformGetParent()->GetRole();

  if (role == ui::AX_ROLE_LIST_BOX_OPTION &&
      parent_role == ui::AX_ROLE_LIST_BOX) {
    return true;
  }

  if (role == ui::AX_ROLE_MENU_LIST_OPTION &&
      parent_role == ui::AX_ROLE_MENU_LIST_POPUP) {
    return true;
  }

  return false;
}

void BrowserAccessibilityComWin::AddRelation(
    const base::string16& relation_type,
    int target_id) {
  // Reflexive relations don't need to be exposed through IA2.
  if (target_id == owner()->GetId())
    return;

  CComObject<BrowserAccessibilityRelation>* relation;
  HRESULT hr =
      CComObject<BrowserAccessibilityRelation>::CreateInstance(&relation);
  DCHECK(SUCCEEDED(hr));
  relation->AddRef();
  relation->Initialize(this, relation_type);
  relation->AddTarget(target_id);
  relations_.push_back(relation);
}

void BrowserAccessibilityComWin::AddBidirectionalRelations(
    const base::string16& relation_type,
    const base::string16& reverse_relation_type,
    ui::AXIntListAttribute attribute) {
  if (!owner()->HasIntListAttribute(attribute))
    return;

  const std::vector<int32_t>& target_ids =
      owner()->GetIntListAttribute(attribute);
  // Reflexive relations don't need to be exposed through IA2.
  std::vector<int32_t> filtered_target_ids;
  int32_t current_id = owner()->GetId();
  std::copy_if(target_ids.begin(), target_ids.end(),
               std::back_inserter(filtered_target_ids),
               [current_id](int32_t id) { return id != current_id; });
  if (filtered_target_ids.empty())
    return;

  CComObject<BrowserAccessibilityRelation>* relation;
  HRESULT hr =
      CComObject<BrowserAccessibilityRelation>::CreateInstance(&relation);
  DCHECK(SUCCEEDED(hr));
  relation->AddRef();
  relation->Initialize(this, relation_type);

  for (int target_id : filtered_target_ids) {
    BrowserAccessibilityComWin* target =
        GetFromID(static_cast<int32_t>(target_id));
    if (!target || !target->owner())
      continue;
    relation->AddTarget(target_id);
    target->AddRelation(reverse_relation_type, owner()->GetId());
  }

  relations_.push_back(relation);
}

// Clears all the forward relations from this object to any other object and the
// associated  reverse relations on the other objects, but leaves any reverse
// relations on this object alone.
void BrowserAccessibilityComWin::ClearOwnRelations() {
  RemoveBidirectionalRelationsOfType(IA2_RELATION_CONTROLLER_FOR,
                                     IA2_RELATION_CONTROLLED_BY);
  RemoveBidirectionalRelationsOfType(IA2_RELATION_DESCRIBED_BY,
                                     IA2_RELATION_DESCRIPTION_FOR);
  RemoveBidirectionalRelationsOfType(IA2_RELATION_FLOWS_TO,
                                     IA2_RELATION_FLOWS_FROM);
  RemoveBidirectionalRelationsOfType(IA2_RELATION_LABELLED_BY,
                                     IA2_RELATION_LABEL_FOR);

  relations_.erase(
      std::remove_if(relations_.begin(), relations_.end(),
                     [](BrowserAccessibilityRelation* relation) {
                       if (relation->get_type() == IA2_RELATION_MEMBER_OF) {
                         relation->Release();
                         return true;
                       }
                       return false;
                     }),
      relations_.end());
}

void BrowserAccessibilityComWin::RemoveBidirectionalRelationsOfType(
    const base::string16& relation_type,
    const base::string16& reverse_relation_type) {
  for (auto iter = relations_.begin(); iter != relations_.end();) {
    BrowserAccessibilityRelation* relation = *iter;
    DCHECK(relation);
    if (relation->get_type() == relation_type) {
      for (int target_id : relation->get_target_ids()) {
        BrowserAccessibilityComWin* target =
            GetFromID(static_cast<int32_t>(target_id));
        if (!target || !target->owner())
          continue;
        DCHECK_NE(target, this);
        target->RemoveTargetFromRelation(reverse_relation_type,
                                         owner()->GetId());
      }
      iter = relations_.erase(iter);
      relation->Release();
    } else {
      ++iter;
    }
  }
}

void BrowserAccessibilityComWin::RemoveTargetFromRelation(
    const base::string16& relation_type,
    int target_id) {
  for (auto iter = relations_.begin(); iter != relations_.end();) {
    BrowserAccessibilityRelation* relation = *iter;
    DCHECK(relation);
    if (relation->get_type() == relation_type) {
      // If |target_id| is not present, |RemoveTarget| will do nothing.
      relation->RemoveTarget(target_id);
    }
    if (relation->get_target_ids().empty()) {
      iter = relations_.erase(iter);
      relation->Release();
    } else {
      ++iter;
    }
  }
}

void BrowserAccessibilityComWin::UpdateRequiredAttributes() {
  if (owner()->IsCellOrTableHeaderRole()) {
    // Expose colspan attribute.
    base::string16 colspan;
    if (owner()->GetHtmlAttribute("aria-colspan", &colspan)) {
      SanitizeStringAttributeForIA2(colspan, &colspan);
      win_attributes_->ia2_attributes.push_back(L"colspan:" + colspan);
    }
    // Expose rowspan attribute.
    base::string16 rowspan;
    if (owner()->GetHtmlAttribute("aria-rowspan", &rowspan)) {
      SanitizeStringAttributeForIA2(rowspan, &rowspan);
      win_attributes_->ia2_attributes.push_back(L"rowspan:" + rowspan);
    }
  }

  // Expose dropeffect attribute.
  base::string16 drop_effect;
  if (owner()->GetHtmlAttribute("aria-dropeffect", &drop_effect)) {
    SanitizeStringAttributeForIA2(drop_effect, &drop_effect);
    win_attributes_->ia2_attributes.push_back(L"dropeffect:" + drop_effect);
  }

  // Expose grabbed attribute.
  base::string16 grabbed;
  if (owner()->GetHtmlAttribute("aria-grabbed", &grabbed)) {
    SanitizeStringAttributeForIA2(grabbed, &grabbed);
    win_attributes_->ia2_attributes.push_back(L"grabbed:" + grabbed);
  }

  // Expose class attribute.
  base::string16 class_attr;
  if (owner()->GetHtmlAttribute("class", &class_attr)) {
    SanitizeStringAttributeForIA2(class_attr, &class_attr);
    win_attributes_->ia2_attributes.push_back(L"class:" + class_attr);
  }

  // Expose datetime attribute.
  base::string16 datetime;
  if (owner()->GetRole() == ui::AX_ROLE_TIME &&
      owner()->GetHtmlAttribute("datetime", &datetime)) {
    SanitizeStringAttributeForIA2(datetime, &datetime);
    win_attributes_->ia2_attributes.push_back(L"datetime:" + datetime);
  }

  // Expose id attribute.
  base::string16 id;
  if (owner()->GetHtmlAttribute("id", &id)) {
    SanitizeStringAttributeForIA2(id, &id);
    win_attributes_->ia2_attributes.push_back(L"id:" + id);
  }

  // Expose src attribute.
  base::string16 src;
  if (owner()->GetRole() == ui::AX_ROLE_IMAGE &&
      owner()->GetHtmlAttribute("src", &src)) {
    SanitizeStringAttributeForIA2(src, &src);
    win_attributes_->ia2_attributes.push_back(L"src:" + src);
  }

  // Expose input-text type attribute.
  base::string16 type;
  base::string16 html_tag = owner()->GetString16Attribute(ui::AX_ATTR_HTML_TAG);
  if (owner()->IsSimpleTextControl() && html_tag == L"input" &&
      owner()->GetHtmlAttribute("type", &type)) {
    SanitizeStringAttributeForIA2(type, &type);
    win_attributes_->ia2_attributes.push_back(L"text-input-type:" + type);
  }
}

void BrowserAccessibilityComWin::FireNativeEvent(LONG win_event_type) const {
  (new BrowserAccessibilityEventWin(BrowserAccessibilityEvent::FromTreeChange,
                                    ui::AX_EVENT_NONE, win_event_type, owner()))
      ->Fire();
}

void BrowserAccessibilityComWin::InitRoleAndState() {
  int32_t ia_role = 0;
  int32_t ia_state = 0;
  base::string16 role_name;
  int32_t ia2_role = 0;
  int32_t ia2_state = IA2_STATE_OPAQUE;

  if (owner()->HasState(ui::AX_STATE_BUSY))
    ia_state |= STATE_SYSTEM_BUSY;

  const auto checked_state = static_cast<ui::AXCheckedState>(
      owner()->GetIntAttribute(ui::AX_ATTR_CHECKED_STATE));
  switch (checked_state) {
    case ui::AX_CHECKED_STATE_TRUE:
      ia_state |= STATE_SYSTEM_CHECKED;
      break;
    case ui::AX_CHECKED_STATE_MIXED:
      ia_state |= STATE_SYSTEM_MIXED;
      break;
    default:
      break;
  }

  if (owner()->HasState(ui::AX_STATE_COLLAPSED))
    ia_state |= STATE_SYSTEM_COLLAPSED;
  if (owner()->HasState(ui::AX_STATE_EXPANDED))
    ia_state |= STATE_SYSTEM_EXPANDED;
  if (owner()->HasState(ui::AX_STATE_FOCUSABLE))
    ia_state |= STATE_SYSTEM_FOCUSABLE;
  if (owner()->HasState(ui::AX_STATE_HASPOPUP))
    ia_state |= STATE_SYSTEM_HASPOPUP;
  if (owner()->HasIntAttribute(ui::AX_ATTR_INVALID_STATE) &&
      owner()->GetIntAttribute(ui::AX_ATTR_INVALID_STATE) !=
          ui::AX_INVALID_STATE_FALSE)
    ia2_state |= IA2_STATE_INVALID_ENTRY;
  if (owner()->HasState(ui::AX_STATE_INVISIBLE))
    ia_state |= STATE_SYSTEM_INVISIBLE;
  if (owner()->HasState(ui::AX_STATE_LINKED))
    ia_state |= STATE_SYSTEM_LINKED;
  if (owner()->HasState(ui::AX_STATE_MULTISELECTABLE)) {
    ia_state |= STATE_SYSTEM_EXTSELECTABLE;
    ia_state |= STATE_SYSTEM_MULTISELECTABLE;
  }
  // TODO(ctguil): Support STATE_SYSTEM_EXTSELECTABLE/accSelect.
  if (owner()->HasState(ui::AX_STATE_OFFSCREEN))
    ia_state |= STATE_SYSTEM_OFFSCREEN;
  if (owner()->HasState(ui::AX_STATE_PRESSED))
    ia_state |= STATE_SYSTEM_PRESSED;
  if (owner()->HasState(ui::AX_STATE_PROTECTED))
    ia_state |= STATE_SYSTEM_PROTECTED;
  if (owner()->HasState(ui::AX_STATE_REQUIRED))
    ia2_state |= IA2_STATE_REQUIRED;
  if (owner()->HasState(ui::AX_STATE_SELECTABLE))
    ia_state |= STATE_SYSTEM_SELECTABLE;
  if (owner()->HasState(ui::AX_STATE_SELECTED))
    ia_state |= STATE_SYSTEM_SELECTED;
  if (owner()->HasState(ui::AX_STATE_VISITED))
    ia_state |= STATE_SYSTEM_TRAVERSED;
  if (owner()->HasState(ui::AX_STATE_DISABLED))
    ia_state |= STATE_SYSTEM_UNAVAILABLE;
  if (owner()->HasState(ui::AX_STATE_VERTICAL))
    ia2_state |= IA2_STATE_VERTICAL;
  if (owner()->HasState(ui::AX_STATE_HORIZONTAL))
    ia2_state |= IA2_STATE_HORIZONTAL;
  if (owner()->HasState(ui::AX_STATE_VISITED))
    ia_state |= STATE_SYSTEM_TRAVERSED;

  // Expose whether or not the mouse is over an element, but suppress
  // this for tests because it can make the test results flaky depending
  // on the position of the mouse.
  BrowserAccessibilityStateImpl* accessibility_state =
      BrowserAccessibilityStateImpl::GetInstance();
  if (!accessibility_state->disable_hot_tracking_for_testing()) {
    if (owner()->HasState(ui::AX_STATE_HOVERED))
      ia_state |= STATE_SYSTEM_HOTTRACKED;
  }

  if (owner()->HasState(ui::AX_STATE_EDITABLE))
    ia2_state |= IA2_STATE_EDITABLE;

  if (owner()->HasAction(ui::AX_ACTION_SET_VALUE))
    ia2_state |= IA2_STATE_EDITABLE;

  if (!owner()->GetStringAttribute(ui::AX_ATTR_AUTO_COMPLETE).empty())
    ia2_state |= IA2_STATE_SUPPORTS_AUTOCOMPLETION;

  if (owner()->GetBoolAttribute(ui::AX_ATTR_MODAL))
    ia2_state |= IA2_STATE_MODAL;

  base::string16 html_tag = owner()->GetString16Attribute(ui::AX_ATTR_HTML_TAG);
  switch (owner()->GetRole()) {
    case ui::AX_ROLE_ALERT:
      ia_role = ROLE_SYSTEM_ALERT;
      break;
    case ui::AX_ROLE_ALERT_DIALOG:
      ia_role = ROLE_SYSTEM_DIALOG;
      break;
    case ui::AX_ROLE_ANCHOR:
      ia_role = ROLE_SYSTEM_LINK;
      break;
    case ui::AX_ROLE_APPLICATION:
      ia_role = ROLE_SYSTEM_APPLICATION;
      break;
    case ui::AX_ROLE_ARTICLE:
      ia_role = ROLE_SYSTEM_DOCUMENT;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_AUDIO:
      ia_role = ROLE_SYSTEM_GROUPING;
      break;
    case ui::AX_ROLE_BANNER:
      ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_HEADER;
      break;
    case ui::AX_ROLE_BLOCKQUOTE:
      role_name = html_tag;
      ia2_role = IA2_ROLE_SECTION;
      break;
    case ui::AX_ROLE_BUSY_INDICATOR:
      ia_role = ROLE_SYSTEM_ANIMATION;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_BUTTON:
      ia_role = ROLE_SYSTEM_PUSHBUTTON;
      break;
    case ui::AX_ROLE_CANVAS:
      if (owner()->GetBoolAttribute(ui::AX_ATTR_CANVAS_HAS_FALLBACK)) {
        role_name = L"canvas";
        ia2_role = IA2_ROLE_CANVAS;
      } else {
        ia_role = ROLE_SYSTEM_GRAPHIC;
      }
      break;
    case ui::AX_ROLE_CAPTION:
      ia_role = ROLE_SYSTEM_TEXT;
      ia2_role = IA2_ROLE_CAPTION;
      break;
    case ui::AX_ROLE_CELL:
      ia_role = ROLE_SYSTEM_CELL;
      break;
    case ui::AX_ROLE_CHECK_BOX:
      ia_role = ROLE_SYSTEM_CHECKBUTTON;
      ia2_state |= IA2_STATE_CHECKABLE;
      break;
    case ui::AX_ROLE_COLOR_WELL:
      ia_role = ROLE_SYSTEM_TEXT;
      ia2_role = IA2_ROLE_COLOR_CHOOSER;
      break;
    case ui::AX_ROLE_COLUMN:
      ia_role = ROLE_SYSTEM_COLUMN;
      break;
    case ui::AX_ROLE_COLUMN_HEADER:
      ia_role = ROLE_SYSTEM_COLUMNHEADER;
      break;
    case ui::AX_ROLE_COMBO_BOX:
      ia_role = ROLE_SYSTEM_COMBOBOX;
      break;
    case ui::AX_ROLE_COMPLEMENTARY:
      ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_NOTE;
      break;
    case ui::AX_ROLE_CONTENT_INFO:
      ia_role = ROLE_SYSTEM_TEXT;
      ia2_role = IA2_ROLE_PARAGRAPH;
      break;
    case ui::AX_ROLE_DATE:
    case ui::AX_ROLE_DATE_TIME:
      ia_role = ROLE_SYSTEM_DROPLIST;
      ia2_role = IA2_ROLE_DATE_EDITOR;
      break;
    case ui::AX_ROLE_DEFINITION:
      role_name = html_tag;
      ia2_role = IA2_ROLE_PARAGRAPH;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_DESCRIPTION_LIST_DETAIL:
      role_name = html_tag;
      ia_role = ROLE_SYSTEM_TEXT;
      ia2_role = IA2_ROLE_PARAGRAPH;
      break;
    case ui::AX_ROLE_DESCRIPTION_LIST:
      role_name = html_tag;
      ia_role = ROLE_SYSTEM_LIST;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_DESCRIPTION_LIST_TERM:
      ia_role = ROLE_SYSTEM_LISTITEM;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_DETAILS:
      role_name = html_tag;
      ia_role = ROLE_SYSTEM_GROUPING;
      break;
    case ui::AX_ROLE_DIALOG:
      ia_role = ROLE_SYSTEM_DIALOG;
      break;
    case ui::AX_ROLE_DISCLOSURE_TRIANGLE:
      ia_role = ROLE_SYSTEM_PUSHBUTTON;
      break;
    case ui::AX_ROLE_DOCUMENT:
    case ui::AX_ROLE_ROOT_WEB_AREA:
    case ui::AX_ROLE_WEB_AREA:
      ia_role = ROLE_SYSTEM_DOCUMENT;
      ia_state |= STATE_SYSTEM_READONLY;
      ia_state |= STATE_SYSTEM_FOCUSABLE;
      break;
    case ui::AX_ROLE_EMBEDDED_OBJECT:
      if (owner()->PlatformChildCount()) {
        // Windows screen readers assume that IA2_ROLE_EMBEDDED_OBJECT
        // doesn't have any children, but it may be something like a
        // browser plugin that has a document inside.
        ia_role = ROLE_SYSTEM_GROUPING;
      } else {
        ia_role = ROLE_SYSTEM_CLIENT;
        ia2_role = IA2_ROLE_EMBEDDED_OBJECT;
      }
      break;
    case ui::AX_ROLE_FIGCAPTION:
      role_name = html_tag;
      ia2_role = IA2_ROLE_CAPTION;
      break;
    case ui::AX_ROLE_FIGURE:
      ia_role = ROLE_SYSTEM_GROUPING;
      break;
    case ui::AX_ROLE_FEED:
      ia_role = ROLE_SYSTEM_GROUPING;
      break;
    case ui::AX_ROLE_FORM:
      role_name = L"form";
      ia2_role = IA2_ROLE_FORM;
      break;
    case ui::AX_ROLE_FOOTER:
      ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_FOOTER;
      break;
    case ui::AX_ROLE_GENERIC_CONTAINER:
      ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_SECTION;
      role_name = html_tag.empty() ? L"div" : html_tag;
      break;
    case ui::AX_ROLE_GRID:
      ia_role = ROLE_SYSTEM_TABLE;
      // TODO(aleventhal) this changed between ARIA 1.0 and 1.1,
      // need to determine whether grids/treegrids should really be readonly
      // or editable by default
      // ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_GROUP:
      ia_role = ROLE_SYSTEM_GROUPING;
      break;
    case ui::AX_ROLE_HEADING:
      role_name = html_tag;
      if (html_tag.empty())
        ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_HEADING;
      break;
    case ui::AX_ROLE_IFRAME:
      ia_role = ROLE_SYSTEM_DOCUMENT;
      ia2_role = IA2_ROLE_INTERNAL_FRAME;
      ia_state = STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_IFRAME_PRESENTATIONAL:
      ia_role = ROLE_SYSTEM_GROUPING;
      break;
    case ui::AX_ROLE_IMAGE:
      ia_role = ROLE_SYSTEM_GRAPHIC;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_IMAGE_MAP:
      role_name = html_tag;
      ia2_role = IA2_ROLE_IMAGE_MAP;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_IMAGE_MAP_LINK:
      ia_role = ROLE_SYSTEM_LINK;
      ia_state |= STATE_SYSTEM_LINKED;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_INPUT_TIME:
      ia_role = ROLE_SYSTEM_GROUPING;
      break;
    case ui::AX_ROLE_LABEL_TEXT:
    case ui::AX_ROLE_LEGEND:
      ia_role = ROLE_SYSTEM_TEXT;
      ia2_role = IA2_ROLE_LABEL;
      break;
    case ui::AX_ROLE_LINK:
      ia_role = ROLE_SYSTEM_LINK;
      ia_state |= STATE_SYSTEM_LINKED;
      break;
    case ui::AX_ROLE_LIST:
      ia_role = ROLE_SYSTEM_LIST;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_LIST_BOX:
      ia_role = ROLE_SYSTEM_LIST;
      break;
    case ui::AX_ROLE_LIST_BOX_OPTION:
      ia_role = ROLE_SYSTEM_LISTITEM;
      if (ia_state & STATE_SYSTEM_SELECTABLE) {
        ia_state |= STATE_SYSTEM_FOCUSABLE;
      }
      break;
    case ui::AX_ROLE_LIST_ITEM:
      ia_role = ROLE_SYSTEM_LISTITEM;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_MAIN:
      ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_PARAGRAPH;
      break;
    case ui::AX_ROLE_MARK:
      ia_role = ROLE_SYSTEM_TEXT;
      ia2_role = IA2_ROLE_TEXT_FRAME;
      break;
    case ui::AX_ROLE_MARQUEE:
      ia_role = ROLE_SYSTEM_ANIMATION;
      break;
    case ui::AX_ROLE_MATH:
      ia_role = ROLE_SYSTEM_EQUATION;
      break;
    case ui::AX_ROLE_MENU:
    case ui::AX_ROLE_MENU_BUTTON:
      ia_role = ROLE_SYSTEM_MENUPOPUP;
      break;
    case ui::AX_ROLE_MENU_BAR:
      ia_role = ROLE_SYSTEM_MENUBAR;
      break;
    case ui::AX_ROLE_MENU_ITEM:
      ia_role = ROLE_SYSTEM_MENUITEM;
      break;
    case ui::AX_ROLE_MENU_ITEM_CHECK_BOX:
      ia_role = ROLE_SYSTEM_MENUITEM;
      ia2_role = IA2_ROLE_CHECK_MENU_ITEM;
      ia2_state |= IA2_STATE_CHECKABLE;
      break;
    case ui::AX_ROLE_MENU_ITEM_RADIO:
      ia_role = ROLE_SYSTEM_MENUITEM;
      ia2_role = IA2_ROLE_RADIO_MENU_ITEM;
      break;
    case ui::AX_ROLE_MENU_LIST_POPUP:
      ia_role = ROLE_SYSTEM_LIST;
      ia2_state &= ~(IA2_STATE_EDITABLE);
      break;
    case ui::AX_ROLE_MENU_LIST_OPTION:
      ia_role = ROLE_SYSTEM_LISTITEM;
      ia2_state &= ~(IA2_STATE_EDITABLE);
      if (ia_state & STATE_SYSTEM_SELECTABLE) {
        ia_state |= STATE_SYSTEM_FOCUSABLE;
      }
      break;
    case ui::AX_ROLE_METER:
      role_name = html_tag;
      ia_role = ROLE_SYSTEM_PROGRESSBAR;
      break;
    case ui::AX_ROLE_NAVIGATION:
      ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_SECTION;
      break;
    case ui::AX_ROLE_NOTE:
      ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_NOTE;
      break;
    case ui::AX_ROLE_OUTLINE:
      ia_role = ROLE_SYSTEM_OUTLINE;
      break;
    case ui::AX_ROLE_PARAGRAPH:
      role_name = L"P";
      ia2_role = IA2_ROLE_PARAGRAPH;
      break;
    case ui::AX_ROLE_POP_UP_BUTTON:
      if (html_tag == L"select") {
        ia_role = ROLE_SYSTEM_COMBOBOX;
      } else {
        ia_role = ROLE_SYSTEM_BUTTONMENU;
      }
      break;
    case ui::AX_ROLE_PRE:
      role_name = html_tag;
      ia_role = ROLE_SYSTEM_TEXT;
      ia2_role = IA2_ROLE_PARAGRAPH;
      break;
    case ui::AX_ROLE_PROGRESS_INDICATOR:
      ia_role = ROLE_SYSTEM_PROGRESSBAR;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_RADIO_BUTTON:
      ia_role = ROLE_SYSTEM_RADIOBUTTON;
      ia2_state = IA2_STATE_CHECKABLE;
      break;
    case ui::AX_ROLE_RADIO_GROUP:
      ia_role = ROLE_SYSTEM_GROUPING;
      break;
    case ui::AX_ROLE_REGION:
      if (html_tag == L"section") {
        ia_role = ROLE_SYSTEM_GROUPING;
        ia2_role = IA2_ROLE_SECTION;
      } else {
        ia_role = ROLE_SYSTEM_PANE;
      }
      break;
    case ui::AX_ROLE_ROW: {
      // Role changes depending on whether row is inside a treegrid
      // https://www.w3.org/TR/core-aam-1.1/#role-map-row
      ia_role =
          IsInTreeGrid(owner()) ? ROLE_SYSTEM_OUTLINEITEM : ROLE_SYSTEM_ROW;
      break;
    }
    case ui::AX_ROLE_ROW_HEADER:
      ia_role = ROLE_SYSTEM_ROWHEADER;
      break;
    case ui::AX_ROLE_RUBY:
      ia_role = ROLE_SYSTEM_TEXT;
      ia2_role = IA2_ROLE_TEXT_FRAME;
      break;
    case ui::AX_ROLE_RULER:
      ia_role = ROLE_SYSTEM_CLIENT;
      ia2_role = IA2_ROLE_RULER;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_SCROLL_AREA:
      ia_role = ROLE_SYSTEM_CLIENT;
      ia2_role = IA2_ROLE_SCROLL_PANE;
      ia_state |= STATE_SYSTEM_READONLY;
      ia2_state &= ~(IA2_STATE_EDITABLE);
      break;
    case ui::AX_ROLE_SCROLL_BAR:
      ia_role = ROLE_SYSTEM_SCROLLBAR;
      break;
    case ui::AX_ROLE_SEARCH:
      ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_SECTION;
      break;
    case ui::AX_ROLE_SLIDER:
      ia_role = ROLE_SYSTEM_SLIDER;
      break;
    case ui::AX_ROLE_SPIN_BUTTON:
      ia_role = ROLE_SYSTEM_SPINBUTTON;
      break;
    case ui::AX_ROLE_SPIN_BUTTON_PART:
      ia_role = ROLE_SYSTEM_PUSHBUTTON;
      break;
    case ui::AX_ROLE_ANNOTATION:
    case ui::AX_ROLE_LIST_MARKER:
    case ui::AX_ROLE_STATIC_TEXT:
      ia_role = ROLE_SYSTEM_STATICTEXT;
      break;
    case ui::AX_ROLE_STATUS:
      ia_role = ROLE_SYSTEM_STATUSBAR;
      break;
    case ui::AX_ROLE_SPLITTER:
      ia_role = ROLE_SYSTEM_SEPARATOR;
      break;
    case ui::AX_ROLE_SVG_ROOT:
      ia_role = ROLE_SYSTEM_GRAPHIC;
      break;
    case ui::AX_ROLE_SWITCH:
      role_name = L"switch";
      ia2_role = IA2_ROLE_TOGGLE_BUTTON;
      break;
    case ui::AX_ROLE_TAB:
      ia_role = ROLE_SYSTEM_PAGETAB;
      break;
    case ui::AX_ROLE_TABLE:
      ia_role = ROLE_SYSTEM_TABLE;
      break;
    case ui::AX_ROLE_TABLE_HEADER_CONTAINER:
      ia_role = ROLE_SYSTEM_GROUPING;
      ia2_role = IA2_ROLE_SECTION;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_TAB_LIST:
      ia_role = ROLE_SYSTEM_PAGETABLIST;
      break;
    case ui::AX_ROLE_TAB_PANEL:
      ia_role = ROLE_SYSTEM_PROPERTYPAGE;
      break;
    case ui::AX_ROLE_TERM:
      ia_role = ROLE_SYSTEM_LISTITEM;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_TOGGLE_BUTTON:
      ia_role = ROLE_SYSTEM_PUSHBUTTON;
      ia2_role = IA2_ROLE_TOGGLE_BUTTON;
      break;
    case ui::AX_ROLE_TEXT_FIELD:
    case ui::AX_ROLE_SEARCH_BOX:
      ia_role = ROLE_SYSTEM_TEXT;
      if (owner()->HasState(ui::AX_STATE_MULTILINE)) {
        ia2_state |= IA2_STATE_MULTI_LINE;
      } else {
        ia2_state |= IA2_STATE_SINGLE_LINE;
      }
      if (owner()->HasState(ui::AX_STATE_READ_ONLY))
        ia_state |= STATE_SYSTEM_READONLY;
      ia2_state |= IA2_STATE_SELECTABLE_TEXT;
      break;
    case ui::AX_ROLE_ABBR:
    case ui::AX_ROLE_TIME:
      role_name = html_tag;
      ia_role = ROLE_SYSTEM_TEXT;
      ia2_role = IA2_ROLE_TEXT_FRAME;
      break;
    case ui::AX_ROLE_TIMER:
      ia_role = ROLE_SYSTEM_CLOCK;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_TOOLBAR:
      ia_role = ROLE_SYSTEM_TOOLBAR;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_TOOLTIP:
      ia_role = ROLE_SYSTEM_TOOLTIP;
      ia_state |= STATE_SYSTEM_READONLY;
      break;
    case ui::AX_ROLE_TREE:
      ia_role = ROLE_SYSTEM_OUTLINE;
      break;
    case ui::AX_ROLE_TREE_GRID:
      ia_role = ROLE_SYSTEM_OUTLINE;
      break;
    case ui::AX_ROLE_TREE_ITEM:
      ia_role = ROLE_SYSTEM_OUTLINEITEM;
      break;
    case ui::AX_ROLE_LINE_BREAK:
      ia_role = ROLE_SYSTEM_WHITESPACE;
      break;
    case ui::AX_ROLE_VIDEO:
      ia_role = ROLE_SYSTEM_GROUPING;
      break;
    case ui::AX_ROLE_WINDOW:
      ia_role = ROLE_SYSTEM_WINDOW;
      break;

    // TODO(dmazzoni): figure out the proper MSAA role for all of these.
    case ui::AX_ROLE_DIRECTORY:
    case ui::AX_ROLE_IGNORED:
    case ui::AX_ROLE_LOG:
    case ui::AX_ROLE_NONE:
    case ui::AX_ROLE_PRESENTATIONAL:
    case ui::AX_ROLE_SLIDER_THUMB:
    default:
      ia_role = ROLE_SYSTEM_CLIENT;
      break;
  }

  // Compute the final value of READONLY for MSAA.
  //
  // We always set the READONLY state for elements that have the
  // aria-readonly attribute and for a few roles (in the switch above),
  // including read-only text fields.
  // The majority of focusable controls should not have the read-only state set.
  if (owner()->HasState(ui::AX_STATE_FOCUSABLE) &&
      ia_role != ROLE_SYSTEM_DOCUMENT && ia_role != ROLE_SYSTEM_TEXT) {
    ia_state &= ~(STATE_SYSTEM_READONLY);
  }
  if (!owner()->HasState(ui::AX_STATE_READ_ONLY))
    ia_state &= ~(STATE_SYSTEM_READONLY);
  if (owner()->GetBoolAttribute(ui::AX_ATTR_ARIA_READONLY))
    ia_state |= STATE_SYSTEM_READONLY;

  // The role should always be set.
  DCHECK(!role_name.empty() || ia_role);

  // If we didn't explicitly set the IAccessible2 role, make it the same
  // as the MSAA role.
  if (!ia2_role)
    ia2_role = ia_role;

  win_attributes_->ia_role = ia_role;
  win_attributes_->ia_state = ia_state;
  win_attributes_->role_name = role_name;
  win_attributes_->ia2_role = ia2_role;
  win_attributes_->ia2_state = ia2_state;
}

bool BrowserAccessibilityComWin::IsInTreeGrid(
    const BrowserAccessibility* item) {
  BrowserAccessibility* container = item->PlatformGetParent();
  if (container && container->GetRole() == ui::AX_ROLE_GROUP) {
    // If parent was a rowgroup, we need to look at the grandparent
    container = container->PlatformGetParent();
  }

  if (!container) {
    return false;
  }

  return container->GetRole() == ui::AX_ROLE_TREE_GRID;
}

BrowserAccessibilityComWin* ToBrowserAccessibilityComWin(
    BrowserAccessibility* obj) {
  if (!obj || !obj->IsNative())
    return nullptr;
  auto* result = static_cast<BrowserAccessibilityWin*>(obj)->GetCOM();
  return result;
}

}  // namespace content
