// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_TOOLBAR_TOOLBAR_CONTROLLER_H_
#define IOS_CHROME_BROWSER_UI_TOOLBAR_TOOLBAR_CONTROLLER_H_

#import <UIKit/UIKit.h>

#include <stdint.h>

#include <map>

#include "base/ios/weak_nsobject.h"
#import "base/mac/scoped_nsobject.h"
#include "ios/chrome/browser/ui/rtl_geometry.h"
#import "ios/chrome/browser/ui/tools_menu/tools_popup_controller.h"
#include "ios/chrome/browser/ui/ui_util.h"
#import "ios/chrome/browser/ui/util/relaxed_bounds_constraints_hittest.h"

class ReadingListModel;
@class ToolsMenuConfiguration;

// The time delay before non-initial button images are loaded.
extern const int64_t kNonInitialImageAdditionDelayNanosec;
// Notification that the tools menu has been requested to be shown.
extern NSString* const kMenuWillShowNotification;
// Notification that the tools menu is closed.
extern NSString* const kMenuWillHideNotification;
// Accessibility identifier of the toolbar view (for use by integration tests).
extern NSString* const kToolbarIdentifier;
// Accessibility identifier of the incognito toolbar view (for use by
// integration tests).
extern NSString* const kIncognitoToolbarIdentifier;
// Accessibility identifier of the tools menu button (for use by integration
// tests).
extern NSString* const kToolbarToolsMenuButtonIdentifier;
// Accessibility identifier of the stack button (for use by integration
// tests).
extern NSString* const kToolbarStackButtonIdentifier;
// The maximum number to display in the tab switcher button.
extern NSInteger const kStackButtonMaxTabCount;

// Toolbar style.  Determines which button images are used.
enum ToolbarControllerStyle {
  ToolbarControllerStyleLightMode = 0,
  ToolbarControllerStyleDarkMode,
  ToolbarControllerStyleIncognitoMode,
  ToolbarControllerStyleMaxStyles
};

enum ToolbarButtonMode {
  ToolbarButtonModeNormal,
  ToolbarButtonModeReversed,
};

enum ToolbarButtonUIState {
  ToolbarButtonUIStateNormal = 0,
  ToolbarButtonUIStatePressed,
  ToolbarButtonUIStateDisabled,
  NumberOfToolbarButtonUIStates,
};

// This enumerates the different buttons used by the toolbar and is used to map
// the resource IDs for the button's icons.  Subclasses with additional buttons
// should extend these values.  The first new enum should be set to
// |NumberOfToolbarButtonNames|.  Note that functions that use these values use
// an int rather than the |ToolbarButtonName| to accommodate additional values.
// Also, if this enum is extended by a subclass, the subclass must also override
// -imageIdForImageEnum:style:forState: to provide mapping from enum to resource
// ID for the various states.
enum ToolbarButtonName {
  ToolbarButtonNameStack = 0,
  ToolbarButtonNameShare,
  NumberOfToolbarButtonNames,
};

// Style used to specify the direction of the toolbar transition animations.
typedef enum {
  TOOLBAR_TRANSITION_STYLE_TO_STACK_VIEW,
  TOOLBAR_TRANSITION_STYLE_TO_BVC
} ToolbarTransitionStyle;

// Toolbar frames shared with subclasses.
extern const CGRect kToolbarFrame[INTERFACE_IDIOM_COUNT];

// Create a thin wrapper around UIImageView to catch frame change and window
// events.
@protocol ToolbarFrameDelegate
- (void)frameDidChangeFrame:(CGRect)newFrame fromFrame:(CGRect)oldFrame;
- (void)windowDidChange;
@end

@interface ToolbarView : UIImageView<RelaxedBoundsConstraintsHitTestSupport> {
  base::WeakNSProtocol<id<ToolbarFrameDelegate>> delegate_;
}
- (void)setDelegate:(id<ToolbarFrameDelegate>)delegate;
// Records whether or not the toolbar is currently involved in a transition
// animation.
@property(nonatomic, assign, getter=isAnimatingTransition)
    BOOL animatingTransition;
@end

// Base class for a toolbar, containing the standard button set that is common
// across different types of toolbars and action handlers for those buttons
// (forwarding to the delegate).
// This is not intended to be used on its own, but to be subclassed by more
// specific toolbars that provide more buttons in the empty space.
@interface ToolbarController : NSObject<PopupMenuDelegate>

// The top-level toolbar view. It is a |UIImageView| even though it does not
// hold any image for testability: unlike |UIView|, a |UIImageView| that is
// visible in the UI automation view hierarchy does not prevent its subviews
// from also being visible.
// TODO(crbug.com/228469): Figure out how to fix this and have the top-level
// view be a UIView.
@property(nonatomic, readonly, strong) ToolbarView* view;
// The view for the toolbar background image. This is a subview of |view| to
// allow clients to alter the transparency of the background image without
// affecting the other components of the toolbar.
@property(nonatomic, readonly, strong) UIImageView* backgroundView;
// The view for the toolbar shadow image.  This is a subview of |view| to allow
// clients to alter the visibility of the shadow without affecting other
// components of the toolbar.
@property(nonatomic, readonly, strong) UIImageView* shadowView;

// The tools popup controller. Nil if the tools popup menu is not visible.
@property(nonatomic, readonly, strong)
    ToolsPopupController* toolsPopupController;

// Style of this toolbar.
@property(nonatomic, readonly, assign) ToolbarControllerStyle style;

// The reading list model reflected by the toolbar.
@property(nonatomic, readwrite, assign) ReadingListModel* readingListModel;

// Designated initializer.  |style| determines how the toolbar draws itself.
- (instancetype)initWithStyle:(ToolbarControllerStyle)style
    NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

// Called when the application has entered the background.
- (void)applicationDidEnterBackground:(NSNotification*)notify;

// Sets whether the share button is enabled or not.
- (void)setShareButtonEnabled:(BOOL)enabled;

// Sets up |button| with images named by the given |imageEnum| and the current
// toolbar style.  Sets images synchronously for |initialState|, and
// asynchronously for the other states. Optionally sets the image for the
// disabled state as well.  Meant to be called during initialization.
// Note:  |withImageEnum| should be one of the ToolbarButtonName values, or an
// extended value provided by a subclass.  It is an int to support "subclassing"
// of the enum and overriding helper functions.
- (void)setUpButton:(UIButton*)button
       withImageEnum:(int)imageEnum
     forInitialState:(UIControlState)initialState
    hasDisabledImage:(BOOL)hasDisabledImage
       synchronously:(BOOL)synchronously;

// TRUE if |imageEnum| should be flipped when in RTL layout.
// Currently none of this class' images have this property, but subclasses
// can override this method if they need to flip some of their images.
- (BOOL)imageShouldFlipForRightToLeftLayoutDirection:(int)imageEnum;

// Shows the tools popup menu.
- (void)showToolsMenuPopupWithConfiguration:
    (ToolsMenuConfiguration*)configuration;

// If |toolsPopupController_| is non-nil, dismisses the tools popup menu with
// animation.
- (void)dismissToolsMenuPopup;

// Returns the bound of the share button. Used to position the share menu.
- (CGRect)shareButtonAnchorRect;

// Returns the share button's view. Used to position the share menu.
- (UIView*)shareButtonView;

// Sets the background to a particular alpha value. Intended for use by
// subcleasses that need to set the opacity of the entire toolbar.
- (void)setBackgroundAlpha:(CGFloat)alpha;

// Called whenever one of the standard controls is triggered. Does nothing, but
// can be overridden by subclasses to clear any state (e.g., close menus).
- (void)standardButtonPressed:(UIButton*)sender;

// Updates the tab stack button (if there is one) based on the given tab count.
// If |tabCount| > |kStackButtonMaxTabCount|, an easter egg is shown instead of
// the actual number of tabs.
- (void)setTabCount:(NSInteger)tabCount;

// Called when buttons are pressed. Records action metrics.
// Subclasses must call |super| if they override this method.
- (IBAction)recordUserMetrics:(id)sender;

// Called when a touch down is registered on the stack view button.
- (IBAction)stackButtonTouchDown:(id)sender;

// Height of the toolbar's drop shadow.  This drop shadow is drawn by the
// toolbar and included in the toolbar's height, so it must be subtracted away
// when positioning the web content area.
+ (CGFloat)toolbarDropShadowHeight;

// Height and Y offset to account for the status bar. Overridden by subclasses
// if the toolbar shouldn't extend through the status bar.
- (CGFloat)statusBarOffset;

- (void)traitCollectionDidChange:(UITraitCollection*)previousTraitCollection;

@end

@interface ToolbarController (ProtectedMethods)

// Returns the area that subclasses should use for adding their own controls.
// Nothing should be added outside this frame without first calling
// |setStandardControlsVisible:NO|.
- (CGRect)specificControlsArea;

// Sets the standard control set to be visible or invisible. Uses alpha, so it
// can be animated if called from an animation context. Intended for use by
// subclasses that need to temporarily take over the entire toolbar.
- (void)setStandardControlsVisible:(BOOL)visible;

// Sets the standard control to a particular alpha value. Intended for use by
// subclasses that need to temporarily take over the entire toolbar.
- (void)setStandardControlsAlpha:(CGFloat)alpha;

// Sets the transform that is applied to the standard controls. Can be
// animated if called from an animation context. Intended for use by subclasses
// that need to apply a transform to all toolbar buttons.
- (void)setStandardControlsTransform:(CGAffineTransform)transform;

// Returns the UIImage from the resources bundle for the |imageEnum| and
// |state|.  Uses the toolbar's current style.
- (UIImage*)imageForImageEnum:(int)imageEnum
                     forState:(ToolbarButtonUIState)state;

// Returns the image enum for a given button object.  If the user taps a button
// before its secondary images have been loaded, the image(s) will be loaded
// then, synchronously.  Called by -standardButtonPressed.
- (int)imageEnumForButton:(UIButton*)button;

// Returns the resource ID for the image with enum |index|, corresponding to
// |style| and |state|.  Returns 0 if there is not a corresponding ID.
// If a subclass extends the enum ToolbarButtonName, it must also override this
// method to provide the correct mapping from enum to resource ID.
- (int)imageIdForImageEnum:(int)index
                     style:(ToolbarControllerStyle)style
                  forState:(ToolbarButtonUIState)state;

// Creates a hash of the UI state of the toolbar.
- (uint32_t)snapshotHash;

// Animates the tools menu button and stack button for full bleed omnibox
// animation used for Material.
- (void)animateStandardControlsForOmniboxExpansion:(BOOL)growOmnibox;

// Add position and opacity animations to |view|'s layer. The opacity animation
// goes from 0 to 1. The position animation goes from [view.layer.position
// offset in the leading direction by |leadingOffset|)  to view.layer.position.
// Both animations occur after |delay| seconds.
- (void)fadeInView:(UIView*)view
    fromLeadingOffset:(LayoutOffset)leadingOffset
         withDuration:(NSTimeInterval)duration
           afterDelay:(NSTimeInterval)delay;

// Performs the transition animation specified by |style|, animating the toolbar
// view from |beginFrame| to |endFrame|.
// Animations are added to subview depending on |style|:
//   - ToolbarTransitionStyleToStackView: faded out immediately
//   - ToolbarTransitionStyleToBVC: fade in from a vertical offset after a delay
- (void)animateTransitionWithBeginFrame:(CGRect)beginFrame
                               endFrame:(CGRect)endFrame
                        transitionStyle:(ToolbarTransitionStyle)style;

// Reverses transition animations that are cancelled before they can finish.
- (void)reverseTransitionAnimations;

// Called when transition animations can be removed.
- (void)cleanUpTransitionAnimations;

// Shows/hides iPhone toolbar views for when the new tab page is displayed.
- (void)hideViewsForNewTabPage:(BOOL)hide;

@end

#endif  // IOS_CHROME_BROWSER_UI_TOOLBAR_TOOLBAR_CONTROLLER_H_
