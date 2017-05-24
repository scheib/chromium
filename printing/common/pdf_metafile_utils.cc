// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/common/pdf_metafile_utils.h"

#include "base/time/time.h"
#include "third_party/skia/include/core/SkTime.h"

namespace {

SkTime::DateTime TimeToSkTime(base::Time time) {
  base::Time::Exploded exploded;
  time.UTCExplode(&exploded);
  SkTime::DateTime skdate;
  skdate.fTimeZoneMinutes = 0;
  skdate.fYear = exploded.year;
  skdate.fMonth = exploded.month;
  skdate.fDayOfWeek = exploded.day_of_week;
  skdate.fDay = exploded.day_of_month;
  skdate.fHour = exploded.hour;
  skdate.fMinute = exploded.minute;
  skdate.fSecond = exploded.second;
  return skdate;
}

}  // namespace

namespace printing {

sk_sp<SkDocument> MakePdfDocument(const std::string& creator,
                                  SkWStream* stream) {
  SkDocument::PDFMetadata metadata;
  SkTime::DateTime now = TimeToSkTime(base::Time::Now());
  metadata.fCreation.fEnabled = true;
  metadata.fCreation.fDateTime = now;
  metadata.fModified.fEnabled = true;
  metadata.fModified.fDateTime = now;
  metadata.fCreator = creator.empty()
                          ? SkString("Chromium")
                          : SkString(creator.c_str(), creator.size());
  return SkDocument::MakePDF(stream, SK_ScalarDefaultRasterDPI, metadata,
                             nullptr, false);
}

}  // namespace printing
