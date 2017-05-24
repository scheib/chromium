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

#ifndef WebPrintParams_h
#define WebPrintParams_h

#include "public/platform/WebRect.h"
#include "public/platform/WebSize.h"
#include "WebPrintScalingOption.h"

namespace blink {

struct WebPrintParams {
  // Specifies printable content rect in points (a point is 1/72 of an inch).
  WebRect print_content_area;

  // Specifies the selected printer default printable area details in
  // points.
  WebRect printable_area;

  // Specifies the selected printer default paper size in points.
  WebSize paper_size;

  // Specifies user selected DPI for printing.
  int printer_dpi;

  // Specifies whether to print PDFs as image.
  bool rasterize_pdf = false;

  // Specifies whether to reduce/enlarge/retain the print contents to fit the
  // printable area. (This is used only by plugin printing).
  WebPrintScalingOption print_scaling_option;

  WebPrintParams()
      : printer_dpi(72),
        print_scaling_option(kWebPrintScalingOptionFitToPrintableArea) {}

  WebPrintParams(const WebSize& paper_size)
      : print_content_area(WebRect(0, 0, paper_size.width, paper_size.height)),
        printable_area(WebRect(0, 0, paper_size.width, paper_size.height)),
        paper_size(paper_size),
        printer_dpi(72),
        print_scaling_option(kWebPrintScalingOptionSourceSize) {}

  WebPrintParams(const WebRect& print_content_area,
                 const WebRect& printable_area,
                 const WebSize& paper_size,
                 int printer_dpi,
                 WebPrintScalingOption print_scaling_option)
      : print_content_area(print_content_area),
        printable_area(printable_area),
        paper_size(paper_size),
        printer_dpi(printer_dpi),
        print_scaling_option(print_scaling_option) {}
};

}  // namespace blink

#endif
