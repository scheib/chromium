// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_TRAY_THROBBER_VIEW_H_
#define ASH_SYSTEM_TRAY_THROBBER_VIEW_H_

#include "base/macros.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/view.h"

namespace ash {

// A SmoothedThrobber with tooltip.
class SystemTrayThrobber : public views::SmoothedThrobber {
 public:
  SystemTrayThrobber();
  ~SystemTrayThrobber() override;

  void SetTooltipText(const base::string16& tooltip_text);

  // Overriden from views::View.
  bool GetTooltipText(const gfx::Point& p,
                      base::string16* tooltip) const override;

 private:
  // The current tooltip text.
  base::string16 tooltip_text_;

  DISALLOW_COPY_AND_ASSIGN(SystemTrayThrobber);
};

// A View containing a SystemTrayThrobber with animation for starting/stopping.
class ThrobberView : public views::View {
 public:
  ThrobberView();
  ~ThrobberView() override;

  void Start();
  void Stop();
  void SetTooltipText(const base::string16& tooltip_text);

  // Overriden from views::View.
  gfx::Size CalculatePreferredSize() const override;
  void Layout() override;
  bool GetTooltipText(const gfx::Point& p,
                      base::string16* tooltip) const override;

 private:
  // Schedules animation for starting/stopping throbber.
  void ScheduleAnimation(bool start_throbber);

  SystemTrayThrobber* throbber_;

  // The current tooltip text.
  base::string16 tooltip_text_;

  DISALLOW_COPY_AND_ASSIGN(ThrobberView);
};

}  // namespace ash

#endif  // ASH_SYSTEM_TRAY_THROBBER_VIEW_H_
