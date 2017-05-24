/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Use this file to assert that various public API enum values continue
// matching blink defined enum values.

#include "bindings/core/v8/serialization/SerializedScriptValue.h"
#include "core/dom/AXObjectCache.h"
#include "core/dom/IconURL.h"
#include "core/editing/SelectionType.h"
#include "core/editing/TextAffinity.h"
#include "core/editing/markers/DocumentMarker.h"
#if OS(MACOSX)
#include "core/events/WheelEvent.h"
#endif
#include "core/fileapi/FileError.h"
#include "core/frame/Frame.h"
#include "core/frame/FrameTypes.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/Settings.h"
#include "core/frame/csp/CSPSource.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLMediaElement.h"
#include "core/html/forms/TextControlInnerElements.h"
#include "core/html/media/AutoplayPolicy.h"
#include "core/layout/compositing/CompositedSelectionBound.h"
#include "core/loader/FrameLoaderTypes.h"
#include "core/loader/NavigationPolicy.h"
#include "core/loader/appcache/ApplicationCacheHost.h"
#include "core/page/PageVisibilityState.h"
#include "core/style/ComputedStyleConstants.h"
#include "modules/accessibility/AXObjectImpl.h"
#include "modules/indexeddb/IDBKey.h"
#include "modules/indexeddb/IDBKeyPath.h"
#include "modules/indexeddb/IDBMetadata.h"
#include "modules/indexeddb/IndexedDB.h"
#include "modules/navigatorcontentutils/NavigatorContentUtilsClient.h"
#include "modules/quota/DeprecatedStorageQuota.h"
#include "modules/speech/SpeechRecognitionError.h"
#include "platform/Cursor.h"
#include "platform/FileMetadata.h"
#include "platform/FileSystemType.h"
#include "platform/fonts/FontDescription.h"
#include "platform/fonts/FontSmoothingMode.h"
#include "platform/loader/fetch/ResourceLoadPriority.h"
#include "platform/loader/fetch/ResourceResponse.h"
#include "platform/mediastream/MediaStreamSource.h"
#include "platform/network/ContentSecurityPolicyParsers.h"
#include "platform/scroll/ScrollTypes.h"
#include "platform/text/TextChecking.h"
#include "platform/text/TextDecoration.h"
#include "platform/weborigin/ReferrerPolicy.h"
#include "platform/weborigin/SchemeRegistry.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/text/StringImpl.h"
#include "public/platform/WebApplicationCacheHost.h"
#include "public/platform/WebClipboard.h"
#include "public/platform/WebContentSecurityPolicy.h"
#include "public/platform/WebContentSecurityPolicyStruct.h"
#include "public/platform/WebCursorInfo.h"
#include "public/platform/WebFileError.h"
#include "public/platform/WebFileInfo.h"
#include "public/platform/WebFileSystem.h"
#include "public/platform/WebFontDescription.h"
#include "public/platform/WebHistoryScrollRestorationType.h"
#include "public/platform/WebInputEvent.h"
#include "public/platform/WebMediaPlayer.h"
#include "public/platform/WebMediaPlayerClient.h"
#include "public/platform/WebMediaSource.h"
#include "public/platform/WebMediaStreamSource.h"
#include "public/platform/WebMouseWheelEvent.h"
#include "public/platform/WebPageVisibilityState.h"
#include "public/platform/WebReferrerPolicy.h"
#include "public/platform/WebScrollbar.h"
#include "public/platform/WebScrollbarBehavior.h"
#include "public/platform/WebSelectionBound.h"
#include "public/platform/WebStorageQuotaError.h"
#include "public/platform/WebStorageQuotaType.h"
#include "public/platform/WebURLRequest.h"
#include "public/platform/WebURLResponse.h"
#include "public/platform/modules/indexeddb/WebIDBCursor.h"
#include "public/platform/modules/indexeddb/WebIDBDatabase.h"
#include "public/platform/modules/indexeddb/WebIDBDatabaseException.h"
#include "public/platform/modules/indexeddb/WebIDBFactory.h"
#include "public/platform/modules/indexeddb/WebIDBKey.h"
#include "public/platform/modules/indexeddb/WebIDBKeyPath.h"
#include "public/platform/modules/indexeddb/WebIDBMetadata.h"
#include "public/platform/modules/indexeddb/WebIDBTypes.h"
#include "public/web/WebAXEnums.h"
#include "public/web/WebAXObject.h"
#include "public/web/WebClientRedirectPolicy.h"
#include "public/web/WebConsoleMessage.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebFrameLoadType.h"
#include "public/web/WebHistoryCommitType.h"
#include "public/web/WebHistoryItem.h"
#include "public/web/WebIconURL.h"
#include "public/web/WebInputElement.h"
#include "public/web/WebNavigationPolicy.h"
#include "public/web/WebNavigatorContentUtilsClient.h"
#include "public/web/WebRemoteFrameClient.h"
#include "public/web/WebSandboxFlags.h"
#include "public/web/WebSecurityPolicy.h"
#include "public/web/WebSelection.h"
#include "public/web/WebSerializedScriptValueVersion.h"
#include "public/web/WebSettings.h"
#include "public/web/WebSpeechRecognizerClient.h"
#include "public/web/WebTextCheckingResult.h"
#include "public/web/WebTextDecorationType.h"
#include "public/web/WebView.h"

namespace blink {

#define STATIC_ASSERT_ENUM(a, b)                            \
  static_assert(static_cast<int>(a) == static_cast<int>(b), \
                "mismatching enum: " #a)

STATIC_ASSERT_ENUM(kWebAXEventActiveDescendantChanged,
                   AXObjectCache::kAXActiveDescendantChanged);
STATIC_ASSERT_ENUM(kWebAXEventAlert, AXObjectCache::kAXAlert);
STATIC_ASSERT_ENUM(kWebAXEventAriaAttributeChanged,
                   AXObjectCache::kAXAriaAttributeChanged);
STATIC_ASSERT_ENUM(kWebAXEventAutocorrectionOccured,
                   AXObjectCache::kAXAutocorrectionOccured);
STATIC_ASSERT_ENUM(kWebAXEventBlur, AXObjectCache::kAXBlur);
STATIC_ASSERT_ENUM(kWebAXEventCheckedStateChanged,
                   AXObjectCache::kAXCheckedStateChanged);
STATIC_ASSERT_ENUM(kWebAXEventChildrenChanged,
                   AXObjectCache::kAXChildrenChanged);
STATIC_ASSERT_ENUM(kWebAXEventClicked, AXObjectCache::kAXClicked);
STATIC_ASSERT_ENUM(kWebAXEventDocumentSelectionChanged,
                   AXObjectCache::kAXDocumentSelectionChanged);
STATIC_ASSERT_ENUM(kWebAXEventExpandedChanged,
                   AXObjectCache::kAXExpandedChanged);
STATIC_ASSERT_ENUM(kWebAXEventFocus, AXObjectCache::kAXFocusedUIElementChanged);
STATIC_ASSERT_ENUM(kWebAXEventHide, AXObjectCache::kAXHide);
STATIC_ASSERT_ENUM(kWebAXEventHover, AXObjectCache::kAXHover);
STATIC_ASSERT_ENUM(kWebAXEventInvalidStatusChanged,
                   AXObjectCache::kAXInvalidStatusChanged);
STATIC_ASSERT_ENUM(kWebAXEventLayoutComplete, AXObjectCache::kAXLayoutComplete);
STATIC_ASSERT_ENUM(kWebAXEventLiveRegionChanged,
                   AXObjectCache::kAXLiveRegionChanged);
STATIC_ASSERT_ENUM(kWebAXEventLoadComplete, AXObjectCache::kAXLoadComplete);
STATIC_ASSERT_ENUM(kWebAXEventLocationChanged,
                   AXObjectCache::kAXLocationChanged);
STATIC_ASSERT_ENUM(kWebAXEventMenuListItemSelected,
                   AXObjectCache::kAXMenuListItemSelected);
STATIC_ASSERT_ENUM(kWebAXEventMenuListItemUnselected,
                   AXObjectCache::kAXMenuListItemUnselected);
STATIC_ASSERT_ENUM(kWebAXEventMenuListValueChanged,
                   AXObjectCache::kAXMenuListValueChanged);
STATIC_ASSERT_ENUM(kWebAXEventRowCollapsed, AXObjectCache::kAXRowCollapsed);
STATIC_ASSERT_ENUM(kWebAXEventRowCountChanged,
                   AXObjectCache::kAXRowCountChanged);
STATIC_ASSERT_ENUM(kWebAXEventRowExpanded, AXObjectCache::kAXRowExpanded);
STATIC_ASSERT_ENUM(kWebAXEventScrollPositionChanged,
                   AXObjectCache::kAXScrollPositionChanged);
STATIC_ASSERT_ENUM(kWebAXEventScrolledToAnchor,
                   AXObjectCache::kAXScrolledToAnchor);
STATIC_ASSERT_ENUM(kWebAXEventSelectedChildrenChanged,
                   AXObjectCache::kAXSelectedChildrenChanged);
STATIC_ASSERT_ENUM(kWebAXEventSelectedTextChanged,
                   AXObjectCache::kAXSelectedTextChanged);
STATIC_ASSERT_ENUM(kWebAXEventShow, AXObjectCache::kAXShow);
STATIC_ASSERT_ENUM(kWebAXEventTextChanged, AXObjectCache::kAXTextChanged);
STATIC_ASSERT_ENUM(kWebAXEventTextInserted, AXObjectCache::kAXTextInserted);
STATIC_ASSERT_ENUM(kWebAXEventTextRemoved, AXObjectCache::kAXTextRemoved);
STATIC_ASSERT_ENUM(kWebAXEventValueChanged, AXObjectCache::kAXValueChanged);

STATIC_ASSERT_ENUM(kWebAXRoleAbbr, kAbbrRole);
STATIC_ASSERT_ENUM(kWebAXRoleAlertDialog, kAlertDialogRole);
STATIC_ASSERT_ENUM(kWebAXRoleAlert, kAlertRole);
STATIC_ASSERT_ENUM(kWebAXRoleAnchor, kAnchorRole);
STATIC_ASSERT_ENUM(kWebAXRoleAnnotation, kAnnotationRole);
STATIC_ASSERT_ENUM(kWebAXRoleApplication, kApplicationRole);
STATIC_ASSERT_ENUM(kWebAXRoleArticle, kArticleRole);
STATIC_ASSERT_ENUM(kWebAXRoleAudio, kAudioRole);
STATIC_ASSERT_ENUM(kWebAXRoleBanner, kBannerRole);
STATIC_ASSERT_ENUM(kWebAXRoleBlockquote, kBlockquoteRole);
STATIC_ASSERT_ENUM(kWebAXRoleBusyIndicator, kBusyIndicatorRole);
STATIC_ASSERT_ENUM(kWebAXRoleButton, kButtonRole);
STATIC_ASSERT_ENUM(kWebAXRoleCanvas, kCanvasRole);
STATIC_ASSERT_ENUM(kWebAXRoleCaption, kCaptionRole);
STATIC_ASSERT_ENUM(kWebAXRoleCell, kCellRole);
STATIC_ASSERT_ENUM(kWebAXRoleCheckBox, kCheckBoxRole);
STATIC_ASSERT_ENUM(kWebAXRoleColorWell, kColorWellRole);
STATIC_ASSERT_ENUM(kWebAXRoleColumnHeader, kColumnHeaderRole);
STATIC_ASSERT_ENUM(kWebAXRoleColumn, kColumnRole);
STATIC_ASSERT_ENUM(kWebAXRoleComboBox, kComboBoxRole);
STATIC_ASSERT_ENUM(kWebAXRoleComplementary, kComplementaryRole);
STATIC_ASSERT_ENUM(kWebAXRoleContentInfo, kContentInfoRole);
STATIC_ASSERT_ENUM(kWebAXRoleDate, kDateRole);
STATIC_ASSERT_ENUM(kWebAXRoleDateTime, kDateTimeRole);
STATIC_ASSERT_ENUM(kWebAXRoleDefinition, kDefinitionRole);
STATIC_ASSERT_ENUM(kWebAXRoleDescriptionListDetail, kDescriptionListDetailRole);
STATIC_ASSERT_ENUM(kWebAXRoleDescriptionList, kDescriptionListRole);
STATIC_ASSERT_ENUM(kWebAXRoleDescriptionListTerm, kDescriptionListTermRole);
STATIC_ASSERT_ENUM(kWebAXRoleDetails, kDetailsRole);
STATIC_ASSERT_ENUM(kWebAXRoleDialog, kDialogRole);
STATIC_ASSERT_ENUM(kWebAXRoleDirectory, kDirectoryRole);
STATIC_ASSERT_ENUM(kWebAXRoleDisclosureTriangle, kDisclosureTriangleRole);
STATIC_ASSERT_ENUM(kWebAXRoleDocument, kDocumentRole);
STATIC_ASSERT_ENUM(kWebAXRoleEmbeddedObject, kEmbeddedObjectRole);
STATIC_ASSERT_ENUM(kWebAXRoleFeed, kFeedRole);
STATIC_ASSERT_ENUM(kWebAXRoleFigcaption, kFigcaptionRole);
STATIC_ASSERT_ENUM(kWebAXRoleFigure, kFigureRole);
STATIC_ASSERT_ENUM(kWebAXRoleFooter, kFooterRole);
STATIC_ASSERT_ENUM(kWebAXRoleForm, kFormRole);
STATIC_ASSERT_ENUM(kWebAXRoleGenericContainer, kGenericContainerRole);
STATIC_ASSERT_ENUM(kWebAXRoleGrid, kGridRole);
STATIC_ASSERT_ENUM(kWebAXRoleGroup, kGroupRole);
STATIC_ASSERT_ENUM(kWebAXRoleHeading, kHeadingRole);
STATIC_ASSERT_ENUM(kWebAXRoleIframe, kIframeRole);
STATIC_ASSERT_ENUM(kWebAXRoleIframePresentational, kIframePresentationalRole);
STATIC_ASSERT_ENUM(kWebAXRoleIgnored, kIgnoredRole);
STATIC_ASSERT_ENUM(kWebAXRoleImageMapLink, kImageMapLinkRole);
STATIC_ASSERT_ENUM(kWebAXRoleImageMap, kImageMapRole);
STATIC_ASSERT_ENUM(kWebAXRoleImage, kImageRole);
STATIC_ASSERT_ENUM(kWebAXRoleInlineTextBox, kInlineTextBoxRole);
STATIC_ASSERT_ENUM(kWebAXRoleInputTime, kInputTimeRole);
STATIC_ASSERT_ENUM(kWebAXRoleLabel, kLabelRole);
STATIC_ASSERT_ENUM(kWebAXRoleLegend, kLegendRole);
STATIC_ASSERT_ENUM(kWebAXRoleLineBreak, kLineBreakRole);
STATIC_ASSERT_ENUM(kWebAXRoleLink, kLinkRole);
STATIC_ASSERT_ENUM(kWebAXRoleListBoxOption, kListBoxOptionRole);
STATIC_ASSERT_ENUM(kWebAXRoleListBox, kListBoxRole);
STATIC_ASSERT_ENUM(kWebAXRoleListItem, kListItemRole);
STATIC_ASSERT_ENUM(kWebAXRoleListMarker, kListMarkerRole);
STATIC_ASSERT_ENUM(kWebAXRoleList, kListRole);
STATIC_ASSERT_ENUM(kWebAXRoleLog, kLogRole);
STATIC_ASSERT_ENUM(kWebAXRoleMain, kMainRole);
STATIC_ASSERT_ENUM(kWebAXRoleMark, kMarkRole);
STATIC_ASSERT_ENUM(kWebAXRoleMarquee, kMarqueeRole);
STATIC_ASSERT_ENUM(kWebAXRoleMath, kMathRole);
STATIC_ASSERT_ENUM(kWebAXRoleMenuBar, kMenuBarRole);
STATIC_ASSERT_ENUM(kWebAXRoleMenuButton, kMenuButtonRole);
STATIC_ASSERT_ENUM(kWebAXRoleMenuItem, kMenuItemRole);
STATIC_ASSERT_ENUM(kWebAXRoleMenuItemCheckBox, kMenuItemCheckBoxRole);
STATIC_ASSERT_ENUM(kWebAXRoleMenuItemRadio, kMenuItemRadioRole);
STATIC_ASSERT_ENUM(kWebAXRoleMenuListOption, kMenuListOptionRole);
STATIC_ASSERT_ENUM(kWebAXRoleMenuListPopup, kMenuListPopupRole);
STATIC_ASSERT_ENUM(kWebAXRoleMenu, kMenuRole);
STATIC_ASSERT_ENUM(kWebAXRoleMeter, kMeterRole);
STATIC_ASSERT_ENUM(kWebAXRoleNavigation, kNavigationRole);
STATIC_ASSERT_ENUM(kWebAXRoleNone, kNoneRole);
STATIC_ASSERT_ENUM(kWebAXRoleNote, kNoteRole);
STATIC_ASSERT_ENUM(kWebAXRoleOutline, kOutlineRole);
STATIC_ASSERT_ENUM(kWebAXRoleParagraph, kParagraphRole);
STATIC_ASSERT_ENUM(kWebAXRolePopUpButton, kPopUpButtonRole);
STATIC_ASSERT_ENUM(kWebAXRolePre, kPreRole);
STATIC_ASSERT_ENUM(kWebAXRolePresentational, kPresentationalRole);
STATIC_ASSERT_ENUM(kWebAXRoleProgressIndicator, kProgressIndicatorRole);
STATIC_ASSERT_ENUM(kWebAXRoleRadioButton, kRadioButtonRole);
STATIC_ASSERT_ENUM(kWebAXRoleRadioGroup, kRadioGroupRole);
STATIC_ASSERT_ENUM(kWebAXRoleRegion, kRegionRole);
STATIC_ASSERT_ENUM(kWebAXRoleRootWebArea, kRootWebAreaRole);
STATIC_ASSERT_ENUM(kWebAXRoleRowHeader, kRowHeaderRole);
STATIC_ASSERT_ENUM(kWebAXRoleRow, kRowRole);
STATIC_ASSERT_ENUM(kWebAXRoleRuby, kRubyRole);
STATIC_ASSERT_ENUM(kWebAXRoleRuler, kRulerRole);
STATIC_ASSERT_ENUM(kWebAXRoleSVGRoot, kSVGRootRole);
STATIC_ASSERT_ENUM(kWebAXRoleScrollArea, kScrollAreaRole);
STATIC_ASSERT_ENUM(kWebAXRoleScrollBar, kScrollBarRole);
STATIC_ASSERT_ENUM(kWebAXRoleSeamlessWebArea, kSeamlessWebAreaRole);
STATIC_ASSERT_ENUM(kWebAXRoleSearch, kSearchRole);
STATIC_ASSERT_ENUM(kWebAXRoleSearchBox, kSearchBoxRole);
STATIC_ASSERT_ENUM(kWebAXRoleSlider, kSliderRole);
STATIC_ASSERT_ENUM(kWebAXRoleSliderThumb, kSliderThumbRole);
STATIC_ASSERT_ENUM(kWebAXRoleSpinButtonPart, kSpinButtonPartRole);
STATIC_ASSERT_ENUM(kWebAXRoleSpinButton, kSpinButtonRole);
STATIC_ASSERT_ENUM(kWebAXRoleSplitter, kSplitterRole);
STATIC_ASSERT_ENUM(kWebAXRoleStaticText, kStaticTextRole);
STATIC_ASSERT_ENUM(kWebAXRoleStatus, kStatusRole);
STATIC_ASSERT_ENUM(kWebAXRoleSwitch, kSwitchRole);
STATIC_ASSERT_ENUM(kWebAXRoleTabGroup, kTabGroupRole);
STATIC_ASSERT_ENUM(kWebAXRoleTabList, kTabListRole);
STATIC_ASSERT_ENUM(kWebAXRoleTabPanel, kTabPanelRole);
STATIC_ASSERT_ENUM(kWebAXRoleTab, kTabRole);
STATIC_ASSERT_ENUM(kWebAXRoleTableHeaderContainer, kTableHeaderContainerRole);
STATIC_ASSERT_ENUM(kWebAXRoleTable, kTableRole);
STATIC_ASSERT_ENUM(kWebAXRoleTerm, kTermRole);
STATIC_ASSERT_ENUM(kWebAXRoleTextField, kTextFieldRole);
STATIC_ASSERT_ENUM(kWebAXRoleTime, kTimeRole);
STATIC_ASSERT_ENUM(kWebAXRoleTimer, kTimerRole);
STATIC_ASSERT_ENUM(kWebAXRoleToggleButton, kToggleButtonRole);
STATIC_ASSERT_ENUM(kWebAXRoleToolbar, kToolbarRole);
STATIC_ASSERT_ENUM(kWebAXRoleTreeGrid, kTreeGridRole);
STATIC_ASSERT_ENUM(kWebAXRoleTreeItem, kTreeItemRole);
STATIC_ASSERT_ENUM(kWebAXRoleTree, kTreeRole);
STATIC_ASSERT_ENUM(kWebAXRoleUnknown, kUnknownRole);
STATIC_ASSERT_ENUM(kWebAXRoleUserInterfaceTooltip, kUserInterfaceTooltipRole);
STATIC_ASSERT_ENUM(kWebAXRoleVideo, kVideoRole);
STATIC_ASSERT_ENUM(kWebAXRoleWebArea, kWebAreaRole);
STATIC_ASSERT_ENUM(kWebAXRoleWindow, kWindowRole);

STATIC_ASSERT_ENUM(kWebAXStateBusy, kAXBusyState);
STATIC_ASSERT_ENUM(kWebAXStateChecked, kAXCheckedState);
STATIC_ASSERT_ENUM(kWebAXStateEnabled, kAXEnabledState);
STATIC_ASSERT_ENUM(kWebAXStateExpanded, kAXExpandedState);
STATIC_ASSERT_ENUM(kWebAXStateFocusable, kAXFocusableState);
STATIC_ASSERT_ENUM(kWebAXStateFocused, kAXFocusedState);
STATIC_ASSERT_ENUM(kWebAXStateHaspopup, kAXHaspopupState);
STATIC_ASSERT_ENUM(kWebAXStateHovered, kAXHoveredState);
STATIC_ASSERT_ENUM(kWebAXStateInvisible, kAXInvisibleState);
STATIC_ASSERT_ENUM(kWebAXStateLinked, kAXLinkedState);
STATIC_ASSERT_ENUM(kWebAXStateMultiline, kAXMultilineState);
STATIC_ASSERT_ENUM(kWebAXStateMultiselectable, kAXMultiselectableState);
STATIC_ASSERT_ENUM(kWebAXStateOffscreen, kAXOffscreenState);
STATIC_ASSERT_ENUM(kWebAXStatePressed, kAXPressedState);
STATIC_ASSERT_ENUM(kWebAXStateProtected, kAXProtectedState);
STATIC_ASSERT_ENUM(kWebAXStateReadonly, kAXReadonlyState);
STATIC_ASSERT_ENUM(kWebAXStateRequired, kAXRequiredState);
STATIC_ASSERT_ENUM(kWebAXStateSelectable, kAXSelectableState);
STATIC_ASSERT_ENUM(kWebAXStateSelected, kAXSelectedState);
STATIC_ASSERT_ENUM(kWebAXStateVertical, kAXVerticalState);
STATIC_ASSERT_ENUM(kWebAXStateVisited, kAXVisitedState);

STATIC_ASSERT_ENUM(WebAXDefaultActionVerb::kNone, AXDefaultActionVerb::kNone);
STATIC_ASSERT_ENUM(WebAXDefaultActionVerb::kActivate,
                   AXDefaultActionVerb::kActivate);
STATIC_ASSERT_ENUM(WebAXDefaultActionVerb::kCheck, AXDefaultActionVerb::kCheck);
STATIC_ASSERT_ENUM(WebAXDefaultActionVerb::kClick, AXDefaultActionVerb::kClick);
STATIC_ASSERT_ENUM(WebAXDefaultActionVerb::kJump, AXDefaultActionVerb::kJump);
STATIC_ASSERT_ENUM(WebAXDefaultActionVerb::kOpen, AXDefaultActionVerb::kOpen);
STATIC_ASSERT_ENUM(WebAXDefaultActionVerb::kPress, AXDefaultActionVerb::kPress);
STATIC_ASSERT_ENUM(WebAXDefaultActionVerb::kSelect,
                   AXDefaultActionVerb::kSelect);
STATIC_ASSERT_ENUM(WebAXDefaultActionVerb::kUncheck,
                   AXDefaultActionVerb::kUncheck);

STATIC_ASSERT_ENUM(kWebAXTextDirectionLR, kAccessibilityTextDirectionLTR);
STATIC_ASSERT_ENUM(kWebAXTextDirectionRL, kAccessibilityTextDirectionRTL);
STATIC_ASSERT_ENUM(kWebAXTextDirectionTB, kAccessibilityTextDirectionTTB);
STATIC_ASSERT_ENUM(kWebAXTextDirectionBT, kAccessibilityTextDirectionBTT);

STATIC_ASSERT_ENUM(kWebAXSortDirectionUndefined, kSortDirectionUndefined);
STATIC_ASSERT_ENUM(kWebAXSortDirectionNone, kSortDirectionNone);
STATIC_ASSERT_ENUM(kWebAXSortDirectionAscending, kSortDirectionAscending);
STATIC_ASSERT_ENUM(kWebAXSortDirectionDescending, kSortDirectionDescending);
STATIC_ASSERT_ENUM(kWebAXSortDirectionOther, kSortDirectionOther);

STATIC_ASSERT_ENUM(kWebAXExpandedUndefined, kExpandedUndefined);
STATIC_ASSERT_ENUM(kWebAXExpandedCollapsed, kExpandedCollapsed);
STATIC_ASSERT_ENUM(kWebAXExpandedExpanded, kExpandedExpanded);

STATIC_ASSERT_ENUM(kWebAXOrientationUndefined,
                   kAccessibilityOrientationUndefined);
STATIC_ASSERT_ENUM(kWebAXOrientationVertical,
                   kAccessibilityOrientationVertical);
STATIC_ASSERT_ENUM(kWebAXOrientationHorizontal,
                   kAccessibilityOrientationHorizontal);

STATIC_ASSERT_ENUM(kWebAXAriaCurrentStateUndefined, kAriaCurrentStateUndefined);
STATIC_ASSERT_ENUM(kWebAXAriaCurrentStateFalse, kAriaCurrentStateFalse);
STATIC_ASSERT_ENUM(kWebAXAriaCurrentStateTrue, kAriaCurrentStateTrue);
STATIC_ASSERT_ENUM(kWebAXAriaCurrentStatePage, kAriaCurrentStatePage);
STATIC_ASSERT_ENUM(kWebAXAriaCurrentStateStep, kAriaCurrentStateStep);
STATIC_ASSERT_ENUM(kWebAXAriaCurrentStateLocation, kAriaCurrentStateLocation);
STATIC_ASSERT_ENUM(kWebAXAriaCurrentStateDate, kAriaCurrentStateDate);
STATIC_ASSERT_ENUM(kWebAXAriaCurrentStateTime, kAriaCurrentStateTime);

STATIC_ASSERT_ENUM(kWebAXInvalidStateUndefined, kInvalidStateUndefined);
STATIC_ASSERT_ENUM(kWebAXInvalidStateFalse, kInvalidStateFalse);
STATIC_ASSERT_ENUM(kWebAXInvalidStateTrue, kInvalidStateTrue);
STATIC_ASSERT_ENUM(kWebAXInvalidStateSpelling, kInvalidStateSpelling);
STATIC_ASSERT_ENUM(kWebAXInvalidStateGrammar, kInvalidStateGrammar);
STATIC_ASSERT_ENUM(kWebAXInvalidStateOther, kInvalidStateOther);

STATIC_ASSERT_ENUM(kWebAXMarkerTypeSpelling, DocumentMarker::kSpelling);
STATIC_ASSERT_ENUM(kWebAXMarkerTypeGrammar, DocumentMarker::kGrammar);
STATIC_ASSERT_ENUM(kWebAXMarkerTypeTextMatch, DocumentMarker::kTextMatch);

STATIC_ASSERT_ENUM(kWebAXTextStyleNone, kTextStyleNone);
STATIC_ASSERT_ENUM(kWebAXTextStyleBold, kTextStyleBold);
STATIC_ASSERT_ENUM(kWebAXTextStyleItalic, kTextStyleItalic);
STATIC_ASSERT_ENUM(kWebAXTextStyleUnderline, kTextStyleUnderline);
STATIC_ASSERT_ENUM(kWebAXTextStyleLineThrough, kTextStyleLineThrough);

STATIC_ASSERT_ENUM(kWebAXNameFromUninitialized, kAXNameFromUninitialized);
STATIC_ASSERT_ENUM(kWebAXNameFromAttribute, kAXNameFromAttribute);
STATIC_ASSERT_ENUM(kWebAXNameFromAttributeExplicitlyEmpty,
                   kAXNameFromAttributeExplicitlyEmpty);
STATIC_ASSERT_ENUM(kWebAXNameFromCaption, kAXNameFromCaption);
STATIC_ASSERT_ENUM(kWebAXNameFromContents, kAXNameFromContents);
STATIC_ASSERT_ENUM(kWebAXNameFromPlaceholder, kAXNameFromPlaceholder);
STATIC_ASSERT_ENUM(kWebAXNameFromRelatedElement, kAXNameFromRelatedElement);
STATIC_ASSERT_ENUM(kWebAXNameFromValue, kAXNameFromValue);
STATIC_ASSERT_ENUM(kWebAXNameFromTitle, kAXNameFromTitle);

STATIC_ASSERT_ENUM(kWebAXDescriptionFromUninitialized,
                   kAXDescriptionFromUninitialized);
STATIC_ASSERT_ENUM(kWebAXDescriptionFromAttribute, kAXDescriptionFromAttribute);
STATIC_ASSERT_ENUM(kWebAXDescriptionFromContents, kAXDescriptionFromContents);
STATIC_ASSERT_ENUM(kWebAXDescriptionFromRelatedElement,
                   kAXDescriptionFromRelatedElement);

STATIC_ASSERT_ENUM(kWebAXTextAffinityUpstream, TextAffinity::kUpstream);
STATIC_ASSERT_ENUM(kWebAXTextAffinityDownstream, TextAffinity::kDownstream);

STATIC_ASSERT_ENUM(WebAXStringAttribute::kAriaKeyShortcuts,
                   AXStringAttribute::kAriaKeyShortcuts);
STATIC_ASSERT_ENUM(WebAXStringAttribute::kAriaRoleDescription,
                   AXStringAttribute::kAriaRoleDescription);
STATIC_ASSERT_ENUM(WebAXObjectAttribute::kAriaActiveDescendant,
                   AXObjectAttribute::kAriaActiveDescendant);
STATIC_ASSERT_ENUM(WebAXObjectAttribute::kAriaErrorMessage,
                   AXObjectAttribute::kAriaErrorMessage);
STATIC_ASSERT_ENUM(WebAXObjectVectorAttribute::kAriaControls,
                   AXObjectVectorAttribute::kAriaControls);
STATIC_ASSERT_ENUM(WebAXObjectVectorAttribute::kAriaDetails,
                   AXObjectVectorAttribute::kAriaDetails);
STATIC_ASSERT_ENUM(WebAXObjectVectorAttribute::kAriaFlowTo,
                   AXObjectVectorAttribute::kAriaFlowTo);

STATIC_ASSERT_ENUM(WebApplicationCacheHost::kUncached,
                   ApplicationCacheHost::kUncached);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kIdle, ApplicationCacheHost::kIdle);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kChecking,
                   ApplicationCacheHost::kChecking);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kDownloading,
                   ApplicationCacheHost::kDownloading);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kUpdateReady,
                   ApplicationCacheHost::kUpdateready);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kObsolete,
                   ApplicationCacheHost::kObsolete);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kCheckingEvent,
                   ApplicationCacheHost::kCheckingEvent);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kErrorEvent,
                   ApplicationCacheHost::kErrorEvent);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kNoUpdateEvent,
                   ApplicationCacheHost::kNoupdateEvent);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kDownloadingEvent,
                   ApplicationCacheHost::kDownloadingEvent);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kProgressEvent,
                   ApplicationCacheHost::kProgressEvent);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kUpdateReadyEvent,
                   ApplicationCacheHost::kUpdatereadyEvent);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kCachedEvent,
                   ApplicationCacheHost::kCachedEvent);
STATIC_ASSERT_ENUM(WebApplicationCacheHost::kObsoleteEvent,
                   ApplicationCacheHost::kObsoleteEvent);

STATIC_ASSERT_ENUM(WebClientRedirectPolicy::kNotClientRedirect,
                   ClientRedirectPolicy::kNotClientRedirect);
STATIC_ASSERT_ENUM(WebClientRedirectPolicy::kClientRedirect,
                   ClientRedirectPolicy::kClientRedirect);

STATIC_ASSERT_ENUM(WebCursorInfo::kTypePointer, Cursor::kPointer);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeCross, Cursor::kCross);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeHand, Cursor::kHand);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeIBeam, Cursor::kIBeam);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeWait, Cursor::kWait);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeHelp, Cursor::kHelp);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeEastResize, Cursor::kEastResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNorthResize, Cursor::kNorthResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNorthEastResize,
                   Cursor::kNorthEastResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNorthWestResize,
                   Cursor::kNorthWestResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeSouthResize, Cursor::kSouthResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeSouthEastResize,
                   Cursor::kSouthEastResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeSouthWestResize,
                   Cursor::kSouthWestResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeWestResize, Cursor::kWestResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNorthSouthResize,
                   Cursor::kNorthSouthResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeEastWestResize, Cursor::kEastWestResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNorthEastSouthWestResize,
                   Cursor::kNorthEastSouthWestResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNorthWestSouthEastResize,
                   Cursor::kNorthWestSouthEastResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeColumnResize, Cursor::kColumnResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeRowResize, Cursor::kRowResize);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeMiddlePanning, Cursor::kMiddlePanning);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeEastPanning, Cursor::kEastPanning);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNorthPanning, Cursor::kNorthPanning);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNorthEastPanning,
                   Cursor::kNorthEastPanning);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNorthWestPanning,
                   Cursor::kNorthWestPanning);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeSouthPanning, Cursor::kSouthPanning);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeSouthEastPanning,
                   Cursor::kSouthEastPanning);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeSouthWestPanning,
                   Cursor::kSouthWestPanning);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeWestPanning, Cursor::kWestPanning);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeMove, Cursor::kMove);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeVerticalText, Cursor::kVerticalText);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeCell, Cursor::kCell);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeContextMenu, Cursor::kContextMenu);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeAlias, Cursor::kAlias);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeProgress, Cursor::kProgress);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNoDrop, Cursor::kNoDrop);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeCopy, Cursor::kCopy);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNone, Cursor::kNone);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeNotAllowed, Cursor::kNotAllowed);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeZoomIn, Cursor::kZoomIn);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeZoomOut, Cursor::kZoomOut);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeGrab, Cursor::kGrab);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeGrabbing, Cursor::kGrabbing);
STATIC_ASSERT_ENUM(WebCursorInfo::kTypeCustom, Cursor::kCustom);

STATIC_ASSERT_ENUM(WebFontDescription::kGenericFamilyNone,
                   FontDescription::kNoFamily);
STATIC_ASSERT_ENUM(WebFontDescription::kGenericFamilyStandard,
                   FontDescription::kStandardFamily);
STATIC_ASSERT_ENUM(WebFontDescription::kGenericFamilySerif,
                   FontDescription::kSerifFamily);
STATIC_ASSERT_ENUM(WebFontDescription::kGenericFamilySansSerif,
                   FontDescription::kSansSerifFamily);
STATIC_ASSERT_ENUM(WebFontDescription::kGenericFamilyMonospace,
                   FontDescription::kMonospaceFamily);
STATIC_ASSERT_ENUM(WebFontDescription::kGenericFamilyCursive,
                   FontDescription::kCursiveFamily);
STATIC_ASSERT_ENUM(WebFontDescription::kGenericFamilyFantasy,
                   FontDescription::kFantasyFamily);

STATIC_ASSERT_ENUM(WebFontDescription::kSmoothingAuto, kAutoSmoothing);
STATIC_ASSERT_ENUM(WebFontDescription::kSmoothingNone, kNoSmoothing);
STATIC_ASSERT_ENUM(WebFontDescription::kSmoothingGrayscale, kAntialiased);
STATIC_ASSERT_ENUM(WebFontDescription::kSmoothingSubpixel,
                   kSubpixelAntialiased);

STATIC_ASSERT_ENUM(WebFontDescription::kWeight100, kFontWeight100);
STATIC_ASSERT_ENUM(WebFontDescription::kWeight200, kFontWeight200);
STATIC_ASSERT_ENUM(WebFontDescription::kWeight300, kFontWeight300);
STATIC_ASSERT_ENUM(WebFontDescription::kWeight400, kFontWeight400);
STATIC_ASSERT_ENUM(WebFontDescription::kWeight500, kFontWeight500);
STATIC_ASSERT_ENUM(WebFontDescription::kWeight600, kFontWeight600);
STATIC_ASSERT_ENUM(WebFontDescription::kWeight700, kFontWeight700);
STATIC_ASSERT_ENUM(WebFontDescription::kWeight800, kFontWeight800);
STATIC_ASSERT_ENUM(WebFontDescription::kWeight900, kFontWeight900);
STATIC_ASSERT_ENUM(WebFontDescription::kWeightNormal, kFontWeightNormal);
STATIC_ASSERT_ENUM(WebFontDescription::kWeightBold, kFontWeightBold);

STATIC_ASSERT_ENUM(WebFrameOwnerProperties::ScrollingMode::kAuto,
                   kScrollbarAuto);
STATIC_ASSERT_ENUM(WebFrameOwnerProperties::ScrollingMode::kAlwaysOff,
                   kScrollbarAlwaysOff);
STATIC_ASSERT_ENUM(WebFrameOwnerProperties::ScrollingMode::kAlwaysOn,
                   kScrollbarAlwaysOn);

STATIC_ASSERT_ENUM(WebIconURL::kTypeInvalid, kInvalidIcon);
STATIC_ASSERT_ENUM(WebIconURL::kTypeFavicon, kFavicon);
STATIC_ASSERT_ENUM(WebIconURL::kTypeTouch, kTouchIcon);
STATIC_ASSERT_ENUM(WebIconURL::kTypeTouchPrecomposed, kTouchPrecomposedIcon);

STATIC_ASSERT_ENUM(WebMediaPlayer::kReadyStateHaveNothing,
                   HTMLMediaElement::kHaveNothing);
STATIC_ASSERT_ENUM(WebMediaPlayer::kReadyStateHaveMetadata,
                   HTMLMediaElement::kHaveMetadata);
STATIC_ASSERT_ENUM(WebMediaPlayer::kReadyStateHaveCurrentData,
                   HTMLMediaElement::kHaveCurrentData);
STATIC_ASSERT_ENUM(WebMediaPlayer::kReadyStateHaveFutureData,
                   HTMLMediaElement::kHaveFutureData);
STATIC_ASSERT_ENUM(WebMediaPlayer::kReadyStateHaveEnoughData,
                   HTMLMediaElement::kHaveEnoughData);

STATIC_ASSERT_ENUM(WebScrollbar::kHorizontal, kHorizontalScrollbar);
STATIC_ASSERT_ENUM(WebScrollbar::kVertical, kVerticalScrollbar);

STATIC_ASSERT_ENUM(WebScrollbar::kScrollByLine, kScrollByLine);
STATIC_ASSERT_ENUM(WebScrollbar::kScrollByPage, kScrollByPage);
STATIC_ASSERT_ENUM(WebScrollbar::kScrollByDocument, kScrollByDocument);
STATIC_ASSERT_ENUM(WebScrollbar::kScrollByPixel, kScrollByPixel);

STATIC_ASSERT_ENUM(WebScrollbar::kRegularScrollbar, kRegularScrollbar);
STATIC_ASSERT_ENUM(WebScrollbar::kSmallScrollbar, kSmallScrollbar);
STATIC_ASSERT_ENUM(WebScrollbar::kNoPart, kNoPart);
STATIC_ASSERT_ENUM(WebScrollbar::kBackButtonStartPart, kBackButtonStartPart);
STATIC_ASSERT_ENUM(WebScrollbar::kForwardButtonStartPart,
                   kForwardButtonStartPart);
STATIC_ASSERT_ENUM(WebScrollbar::kBackTrackPart, kBackTrackPart);
STATIC_ASSERT_ENUM(WebScrollbar::kThumbPart, kThumbPart);
STATIC_ASSERT_ENUM(WebScrollbar::kForwardTrackPart, kForwardTrackPart);
STATIC_ASSERT_ENUM(WebScrollbar::kBackButtonEndPart, kBackButtonEndPart);
STATIC_ASSERT_ENUM(WebScrollbar::kForwardButtonEndPart, kForwardButtonEndPart);
STATIC_ASSERT_ENUM(WebScrollbar::kScrollbarBGPart, kScrollbarBGPart);
STATIC_ASSERT_ENUM(WebScrollbar::kTrackBGPart, kTrackBGPart);
STATIC_ASSERT_ENUM(WebScrollbar::kAllParts, kAllParts);
STATIC_ASSERT_ENUM(kWebScrollbarOverlayColorThemeDark,
                   kScrollbarOverlayColorThemeDark);
STATIC_ASSERT_ENUM(kWebScrollbarOverlayColorThemeLight,
                   kScrollbarOverlayColorThemeLight);

STATIC_ASSERT_ENUM(WebSettings::kEditingBehaviorMac, kEditingMacBehavior);
STATIC_ASSERT_ENUM(WebSettings::kEditingBehaviorWin, kEditingWindowsBehavior);
STATIC_ASSERT_ENUM(WebSettings::kEditingBehaviorUnix, kEditingUnixBehavior);
STATIC_ASSERT_ENUM(WebSettings::kEditingBehaviorAndroid,
                   kEditingAndroidBehavior);

STATIC_ASSERT_ENUM(WebSettings::PassiveEventListenerDefault::kFalse,
                   PassiveListenerDefault::kFalse);
STATIC_ASSERT_ENUM(WebSettings::PassiveEventListenerDefault::kTrue,
                   PassiveListenerDefault::kTrue);
STATIC_ASSERT_ENUM(WebSettings::PassiveEventListenerDefault::kForceAllTrue,
                   PassiveListenerDefault::kForceAllTrue);

STATIC_ASSERT_ENUM(kWebIDBDatabaseExceptionUnknownError, kUnknownError);
STATIC_ASSERT_ENUM(kWebIDBDatabaseExceptionConstraintError, kConstraintError);
STATIC_ASSERT_ENUM(kWebIDBDatabaseExceptionDataError, kDataError);
STATIC_ASSERT_ENUM(kWebIDBDatabaseExceptionVersionError, kVersionError);
STATIC_ASSERT_ENUM(kWebIDBDatabaseExceptionAbortError, kAbortError);
STATIC_ASSERT_ENUM(kWebIDBDatabaseExceptionQuotaError, kQuotaExceededError);
STATIC_ASSERT_ENUM(kWebIDBDatabaseExceptionTimeoutError, kTimeoutError);

STATIC_ASSERT_ENUM(kWebIDBKeyTypeInvalid, IDBKey::kInvalidType);
STATIC_ASSERT_ENUM(kWebIDBKeyTypeArray, IDBKey::kArrayType);
STATIC_ASSERT_ENUM(kWebIDBKeyTypeBinary, IDBKey::kBinaryType);
STATIC_ASSERT_ENUM(kWebIDBKeyTypeString, IDBKey::kStringType);
STATIC_ASSERT_ENUM(kWebIDBKeyTypeDate, IDBKey::kDateType);
STATIC_ASSERT_ENUM(kWebIDBKeyTypeNumber, IDBKey::kNumberType);

STATIC_ASSERT_ENUM(kWebIDBKeyPathTypeNull, IDBKeyPath::kNullType);
STATIC_ASSERT_ENUM(kWebIDBKeyPathTypeString, IDBKeyPath::kStringType);
STATIC_ASSERT_ENUM(kWebIDBKeyPathTypeArray, IDBKeyPath::kArrayType);

STATIC_ASSERT_ENUM(WebIDBMetadata::kNoVersion, IDBDatabaseMetadata::kNoVersion);

STATIC_ASSERT_ENUM(WebFileSystem::kTypeTemporary, kFileSystemTypeTemporary);
STATIC_ASSERT_ENUM(WebFileSystem::kTypePersistent, kFileSystemTypePersistent);
STATIC_ASSERT_ENUM(WebFileSystem::kTypeExternal, kFileSystemTypeExternal);
STATIC_ASSERT_ENUM(WebFileSystem::kTypeIsolated, kFileSystemTypeIsolated);
STATIC_ASSERT_ENUM(WebFileInfo::kTypeUnknown, FileMetadata::kTypeUnknown);
STATIC_ASSERT_ENUM(WebFileInfo::kTypeFile, FileMetadata::kTypeFile);
STATIC_ASSERT_ENUM(WebFileInfo::kTypeDirectory, FileMetadata::kTypeDirectory);

STATIC_ASSERT_ENUM(kWebFileErrorNotFound, FileError::kNotFoundErr);
STATIC_ASSERT_ENUM(kWebFileErrorSecurity, FileError::kSecurityErr);
STATIC_ASSERT_ENUM(kWebFileErrorAbort, FileError::kAbortErr);
STATIC_ASSERT_ENUM(kWebFileErrorNotReadable, FileError::kNotReadableErr);
STATIC_ASSERT_ENUM(kWebFileErrorEncoding, FileError::kEncodingErr);
STATIC_ASSERT_ENUM(kWebFileErrorNoModificationAllowed,
                   FileError::kNoModificationAllowedErr);
STATIC_ASSERT_ENUM(kWebFileErrorInvalidState, FileError::kInvalidStateErr);
STATIC_ASSERT_ENUM(kWebFileErrorSyntax, FileError::kSyntaxErr);
STATIC_ASSERT_ENUM(kWebFileErrorInvalidModification,
                   FileError::kInvalidModificationErr);
STATIC_ASSERT_ENUM(kWebFileErrorQuotaExceeded, FileError::kQuotaExceededErr);
STATIC_ASSERT_ENUM(kWebFileErrorTypeMismatch, FileError::kTypeMismatchErr);
STATIC_ASSERT_ENUM(kWebFileErrorPathExists, FileError::kPathExistsErr);

STATIC_ASSERT_ENUM(kWebTextDecorationTypeSpelling, kTextDecorationTypeSpelling);
STATIC_ASSERT_ENUM(kWebTextDecorationTypeGrammar, kTextDecorationTypeGrammar);

STATIC_ASSERT_ENUM(kWebStorageQuotaErrorNotSupported, kNotSupportedError);
STATIC_ASSERT_ENUM(kWebStorageQuotaErrorInvalidModification,
                   kInvalidModificationError);
STATIC_ASSERT_ENUM(kWebStorageQuotaErrorInvalidAccess, kInvalidAccessError);
STATIC_ASSERT_ENUM(kWebStorageQuotaErrorAbort, kAbortError);

STATIC_ASSERT_ENUM(kWebStorageQuotaTypeTemporary,
                   DeprecatedStorageQuota::kTemporary);
STATIC_ASSERT_ENUM(kWebStorageQuotaTypePersistent,
                   DeprecatedStorageQuota::kPersistent);

STATIC_ASSERT_ENUM(kWebPageVisibilityStateVisible, kPageVisibilityStateVisible);
STATIC_ASSERT_ENUM(kWebPageVisibilityStateHidden, kPageVisibilityStateHidden);
STATIC_ASSERT_ENUM(kWebPageVisibilityStatePrerender,
                   kPageVisibilityStatePrerender);

STATIC_ASSERT_ENUM(WebMediaStreamSource::kTypeAudio,
                   MediaStreamSource::kTypeAudio);
STATIC_ASSERT_ENUM(WebMediaStreamSource::kTypeVideo,
                   MediaStreamSource::kTypeVideo);
STATIC_ASSERT_ENUM(WebMediaStreamSource::kReadyStateLive,
                   MediaStreamSource::kReadyStateLive);
STATIC_ASSERT_ENUM(WebMediaStreamSource::kReadyStateMuted,
                   MediaStreamSource::kReadyStateMuted);
STATIC_ASSERT_ENUM(WebMediaStreamSource::kReadyStateEnded,
                   MediaStreamSource::kReadyStateEnded);

STATIC_ASSERT_ENUM(WebSpeechRecognizerClient::kOtherError,
                   SpeechRecognitionError::kErrorCodeOther);
STATIC_ASSERT_ENUM(WebSpeechRecognizerClient::kNoSpeechError,
                   SpeechRecognitionError::kErrorCodeNoSpeech);
STATIC_ASSERT_ENUM(WebSpeechRecognizerClient::kAbortedError,
                   SpeechRecognitionError::kErrorCodeAborted);
STATIC_ASSERT_ENUM(WebSpeechRecognizerClient::kAudioCaptureError,
                   SpeechRecognitionError::kErrorCodeAudioCapture);
STATIC_ASSERT_ENUM(WebSpeechRecognizerClient::kNetworkError,
                   SpeechRecognitionError::kErrorCodeNetwork);
STATIC_ASSERT_ENUM(WebSpeechRecognizerClient::kNotAllowedError,
                   SpeechRecognitionError::kErrorCodeNotAllowed);
STATIC_ASSERT_ENUM(WebSpeechRecognizerClient::kServiceNotAllowedError,
                   SpeechRecognitionError::kErrorCodeServiceNotAllowed);
STATIC_ASSERT_ENUM(WebSpeechRecognizerClient::kBadGrammarError,
                   SpeechRecognitionError::kErrorCodeBadGrammar);
STATIC_ASSERT_ENUM(WebSpeechRecognizerClient::kLanguageNotSupportedError,
                   SpeechRecognitionError::kErrorCodeLanguageNotSupported);

STATIC_ASSERT_ENUM(kWebReferrerPolicyAlways, kReferrerPolicyAlways);
STATIC_ASSERT_ENUM(kWebReferrerPolicyDefault, kReferrerPolicyDefault);
STATIC_ASSERT_ENUM(kWebReferrerPolicyNoReferrerWhenDowngrade,
                   kReferrerPolicyNoReferrerWhenDowngrade);
STATIC_ASSERT_ENUM(kWebReferrerPolicyNever, kReferrerPolicyNever);
STATIC_ASSERT_ENUM(kWebReferrerPolicyOrigin, kReferrerPolicyOrigin);
STATIC_ASSERT_ENUM(kWebReferrerPolicyOriginWhenCrossOrigin,
                   kReferrerPolicyOriginWhenCrossOrigin);
STATIC_ASSERT_ENUM(
    kWebReferrerPolicyNoReferrerWhenDowngradeOriginWhenCrossOrigin,
    kReferrerPolicyNoReferrerWhenDowngradeOriginWhenCrossOrigin);

STATIC_ASSERT_ENUM(kWebContentSecurityPolicyTypeReport,
                   kContentSecurityPolicyHeaderTypeReport);
STATIC_ASSERT_ENUM(kWebContentSecurityPolicyTypeEnforce,
                   kContentSecurityPolicyHeaderTypeEnforce);

STATIC_ASSERT_ENUM(kWebContentSecurityPolicySourceHTTP,
                   kContentSecurityPolicyHeaderSourceHTTP);
STATIC_ASSERT_ENUM(kWebContentSecurityPolicySourceMeta,
                   kContentSecurityPolicyHeaderSourceMeta);

STATIC_ASSERT_ENUM(kWebWildcardDispositionNoWildcard, CSPSource::kNoWildcard);
STATIC_ASSERT_ENUM(kWebWildcardDispositionHasWildcard, CSPSource::kHasWildcard);

STATIC_ASSERT_ENUM(WebURLResponse::kHTTPVersionUnknown,
                   ResourceResponse::kHTTPVersionUnknown);
STATIC_ASSERT_ENUM(WebURLResponse::kHTTPVersion_0_9,
                   ResourceResponse::kHTTPVersion_0_9);
STATIC_ASSERT_ENUM(WebURLResponse::kHTTPVersion_1_0,
                   ResourceResponse::kHTTPVersion_1_0);
STATIC_ASSERT_ENUM(WebURLResponse::kHTTPVersion_1_1,
                   ResourceResponse::kHTTPVersion_1_1);
STATIC_ASSERT_ENUM(WebURLResponse::kHTTPVersion_2_0,
                   ResourceResponse::kHTTPVersion_2_0);

STATIC_ASSERT_ENUM(WebURLRequest::kPriorityUnresolved,
                   kResourceLoadPriorityUnresolved);
STATIC_ASSERT_ENUM(WebURLRequest::kPriorityVeryLow,
                   kResourceLoadPriorityVeryLow);
STATIC_ASSERT_ENUM(WebURLRequest::kPriorityLow, kResourceLoadPriorityLow);
STATIC_ASSERT_ENUM(WebURLRequest::kPriorityMedium, kResourceLoadPriorityMedium);
STATIC_ASSERT_ENUM(WebURLRequest::kPriorityHigh, kResourceLoadPriorityHigh);
STATIC_ASSERT_ENUM(WebURLRequest::kPriorityVeryHigh,
                   kResourceLoadPriorityVeryHigh);

STATIC_ASSERT_ENUM(kWebNavigationPolicyIgnore, kNavigationPolicyIgnore);
STATIC_ASSERT_ENUM(kWebNavigationPolicyDownload, kNavigationPolicyDownload);
STATIC_ASSERT_ENUM(kWebNavigationPolicyCurrentTab, kNavigationPolicyCurrentTab);
STATIC_ASSERT_ENUM(kWebNavigationPolicyNewBackgroundTab,
                   kNavigationPolicyNewBackgroundTab);
STATIC_ASSERT_ENUM(kWebNavigationPolicyNewForegroundTab,
                   kNavigationPolicyNewForegroundTab);
STATIC_ASSERT_ENUM(kWebNavigationPolicyNewWindow, kNavigationPolicyNewWindow);
STATIC_ASSERT_ENUM(kWebNavigationPolicyNewPopup, kNavigationPolicyNewPopup);

STATIC_ASSERT_ENUM(kWebStandardCommit, kStandardCommit);
STATIC_ASSERT_ENUM(kWebBackForwardCommit, kBackForwardCommit);
STATIC_ASSERT_ENUM(kWebInitialCommitInChildFrame, kInitialCommitInChildFrame);
STATIC_ASSERT_ENUM(kWebHistoryInertCommit, kHistoryInertCommit);

STATIC_ASSERT_ENUM(kWebHistorySameDocumentLoad, kHistorySameDocumentLoad);
STATIC_ASSERT_ENUM(kWebHistoryDifferentDocumentLoad,
                   kHistoryDifferentDocumentLoad);

STATIC_ASSERT_ENUM(kWebHistoryScrollRestorationManual,
                   kScrollRestorationManual);
STATIC_ASSERT_ENUM(kWebHistoryScrollRestorationAuto, kScrollRestorationAuto);

STATIC_ASSERT_ENUM(WebConsoleMessage::kLevelVerbose, kVerboseMessageLevel);
STATIC_ASSERT_ENUM(WebConsoleMessage::kLevelInfo, kInfoMessageLevel);
STATIC_ASSERT_ENUM(WebConsoleMessage::kLevelWarning, kWarningMessageLevel);
STATIC_ASSERT_ENUM(WebConsoleMessage::kLevelError, kErrorMessageLevel);

STATIC_ASSERT_ENUM(kWebCustomHandlersNew,
                   NavigatorContentUtilsClient::kCustomHandlersNew);
STATIC_ASSERT_ENUM(kWebCustomHandlersRegistered,
                   NavigatorContentUtilsClient::kCustomHandlersRegistered);
STATIC_ASSERT_ENUM(kWebCustomHandlersDeclined,
                   NavigatorContentUtilsClient::kCustomHandlersDeclined);

STATIC_ASSERT_ENUM(WebSelection::kNoSelection, kNoSelection);
STATIC_ASSERT_ENUM(WebSelection::kCaretSelection, kCaretSelection);
STATIC_ASSERT_ENUM(WebSelection::kRangeSelection, kRangeSelection);

STATIC_ASSERT_ENUM(WebSettings::kImageAnimationPolicyAllowed,
                   kImageAnimationPolicyAllowed);
STATIC_ASSERT_ENUM(WebSettings::kImageAnimationPolicyAnimateOnce,
                   kImageAnimationPolicyAnimateOnce);
STATIC_ASSERT_ENUM(WebSettings::kImageAnimationPolicyNoAnimation,
                   kImageAnimationPolicyNoAnimation);

STATIC_ASSERT_ENUM(WebSettings::kV8CacheOptionsDefault, kV8CacheOptionsDefault);
STATIC_ASSERT_ENUM(WebSettings::kV8CacheOptionsNone, kV8CacheOptionsNone);
STATIC_ASSERT_ENUM(WebSettings::kV8CacheOptionsParse, kV8CacheOptionsParse);
STATIC_ASSERT_ENUM(WebSettings::kV8CacheOptionsCode, kV8CacheOptionsCode);

STATIC_ASSERT_ENUM(WebSettings::V8CacheStrategiesForCacheStorage::kDefault,
                   V8CacheStrategiesForCacheStorage::kDefault);
STATIC_ASSERT_ENUM(WebSettings::V8CacheStrategiesForCacheStorage::kNone,
                   V8CacheStrategiesForCacheStorage::kNone);
STATIC_ASSERT_ENUM(WebSettings::V8CacheStrategiesForCacheStorage::kNormal,
                   V8CacheStrategiesForCacheStorage::kNormal);
STATIC_ASSERT_ENUM(WebSettings::V8CacheStrategiesForCacheStorage::kAggressive,
                   V8CacheStrategiesForCacheStorage::kAggressive);

STATIC_ASSERT_ENUM(WebSandboxFlags::kNone, kSandboxNone);
STATIC_ASSERT_ENUM(WebSandboxFlags::kNavigation, kSandboxNavigation);
STATIC_ASSERT_ENUM(WebSandboxFlags::kPlugins, kSandboxPlugins);
STATIC_ASSERT_ENUM(WebSandboxFlags::kOrigin, kSandboxOrigin);
STATIC_ASSERT_ENUM(WebSandboxFlags::kForms, kSandboxForms);
STATIC_ASSERT_ENUM(WebSandboxFlags::kScripts, kSandboxScripts);
STATIC_ASSERT_ENUM(WebSandboxFlags::kTopNavigation, kSandboxTopNavigation);
STATIC_ASSERT_ENUM(WebSandboxFlags::kPopups, kSandboxPopups);
STATIC_ASSERT_ENUM(WebSandboxFlags::kAutomaticFeatures,
                   kSandboxAutomaticFeatures);
STATIC_ASSERT_ENUM(WebSandboxFlags::kPointerLock, kSandboxPointerLock);
STATIC_ASSERT_ENUM(WebSandboxFlags::kDocumentDomain, kSandboxDocumentDomain);
STATIC_ASSERT_ENUM(WebSandboxFlags::kOrientationLock, kSandboxOrientationLock);
STATIC_ASSERT_ENUM(WebSandboxFlags::kPropagatesToAuxiliaryBrowsingContexts,
                   kSandboxPropagatesToAuxiliaryBrowsingContexts);
STATIC_ASSERT_ENUM(WebSandboxFlags::kModals, kSandboxModals);

STATIC_ASSERT_ENUM(LocalFrameClient::kBeforeUnloadHandler,
                   WebFrameClient::kBeforeUnloadHandler);
STATIC_ASSERT_ENUM(LocalFrameClient::kUnloadHandler,
                   WebFrameClient::kUnloadHandler);

STATIC_ASSERT_ENUM(WebFrameLoadType::kStandard, kFrameLoadTypeStandard);
STATIC_ASSERT_ENUM(WebFrameLoadType::kBackForward, kFrameLoadTypeBackForward);
STATIC_ASSERT_ENUM(WebFrameLoadType::kReload, kFrameLoadTypeReload);
STATIC_ASSERT_ENUM(WebFrameLoadType::kReplaceCurrentItem,
                   kFrameLoadTypeReplaceCurrentItem);
STATIC_ASSERT_ENUM(WebFrameLoadType::kInitialInChildFrame,
                   kFrameLoadTypeInitialInChildFrame);
STATIC_ASSERT_ENUM(WebFrameLoadType::kInitialHistoryLoad,
                   kFrameLoadTypeInitialHistoryLoad);
STATIC_ASSERT_ENUM(WebFrameLoadType::kReloadBypassingCache,
                   kFrameLoadTypeReloadBypassingCache);

STATIC_ASSERT_ENUM(FrameDetachType::kRemove,
                   WebFrameClient::DetachType::kRemove);
STATIC_ASSERT_ENUM(FrameDetachType::kSwap, WebFrameClient::DetachType::kSwap);
STATIC_ASSERT_ENUM(FrameDetachType::kRemove,
                   WebRemoteFrameClient::DetachType::kRemove);
STATIC_ASSERT_ENUM(FrameDetachType::kSwap,
                   WebRemoteFrameClient::DetachType::kSwap);

STATIC_ASSERT_ENUM(WebSettings::ProgressBarCompletion::kLoadEvent,
                   ProgressBarCompletion::kLoadEvent);
STATIC_ASSERT_ENUM(WebSettings::ProgressBarCompletion::kResourcesBeforeDCL,
                   ProgressBarCompletion::kResourcesBeforeDCL);
STATIC_ASSERT_ENUM(WebSettings::ProgressBarCompletion::kDOMContentLoaded,
                   ProgressBarCompletion::kDOMContentLoaded);
STATIC_ASSERT_ENUM(
    WebSettings::ProgressBarCompletion::kResourcesBeforeDCLAndSameOriginIFrames,
    ProgressBarCompletion::kResourcesBeforeDCLAndSameOriginIFrames);

STATIC_ASSERT_ENUM(WebSettings::AutoplayPolicy::kNoUserGestureRequired,
                   AutoplayPolicy::Type::kNoUserGestureRequired);
STATIC_ASSERT_ENUM(WebSettings::AutoplayPolicy::kUserGestureRequired,
                   AutoplayPolicy::Type::kUserGestureRequired);
STATIC_ASSERT_ENUM(
    WebSettings::AutoplayPolicy::kUserGestureRequiredForCrossOrigin,
    AutoplayPolicy::Type::kUserGestureRequiredForCrossOrigin);

// This ensures that the version number published in
// WebSerializedScriptValueVersion.h matches the serializer's understanding.
// TODO(jbroman): Fix this to also account for the V8-side version. See
// https://crbug.com/704293.
static_assert(kSerializedScriptValueVersion ==
                  SerializedScriptValue::kWireFormatVersion,
              "Update WebSerializedScriptValueVersion.h.");

}  // namespace blink
