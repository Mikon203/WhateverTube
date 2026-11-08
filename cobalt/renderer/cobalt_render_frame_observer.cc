// Copyright 2025 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cobalt/renderer/cobalt_render_frame_observer.h"

#include "base/command_line.h"
#include "cobalt/browser/switches.h"
#include "content/public/renderer/render_frame.h"
#include "net/base/ip_address.h"
#include "net/base/url_util.h"
#include "starboard/extension/graphics.h"
#include "starboard/system.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace cobalt {

namespace {

// Applied only to YouTube pages. The icon elements and SVG artwork are left
// alone so the site's existing iconography remains intact.
constexpr char kWhateverTubeAppearanceScript[] = R"JS(
(() => {
  if (!/(^|\\.)youtube(?:-nocookie)?\\.com$/i.test(location.hostname)) {
    return;
  }
  if (document.getElementById('whatevertube-appearance')) {
    return;
  }

  const font = document.createElement('link');
  font.id = 'whatevertube-iosevka';
  font.rel = 'stylesheet';
  font.href =
      'https://cdn.jsdelivr.net/fontsource/css/iosevka@5.3.0/latin.min.css';
  (document.head || document.documentElement).appendChild(font);

  const style = document.createElement('style');
  style.id = 'whatevertube-appearance';
  style.textContent = `
    /* Square off cards, thumbnails, controls, menus and dialogs. */
    *, *::before, *::after {
      border-radius: 0 !important;
    }

    /* Use Iosevka for page text without replacing icon fonts or SVG icons. */
    html, body,
    body *:not(svg):not(yt-icon):not(iron-icon):not([class*="icon"]) {
      font-family: 'Iosevka', monospace !important;
    }

    input, textarea, select, button {
      font-family: 'Iosevka', monospace !important;
      border-radius: 0 !important;
    }

    img, video, iframe {
      border-radius: 0 !important;
    }
  `;
  (document.head || document.documentElement).appendChild(style);
})();
)JS";

}  // namespace

CobaltRenderFrameObserver::CobaltRenderFrameObserver(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {
  if (render_frame && render_frame->GetWebFrame()) {
    render_frame->GetWebFrame()->SetContentSettingsClient(this);
  }
}

CobaltRenderFrameObserver::~CobaltRenderFrameObserver() = default;

void CobaltRenderFrameObserver::OnDestruct() {
  delete this;
}

void CobaltRenderFrameObserver::DidFinishLoad() {
  if (!render_frame() || !render_frame()->IsMainFrame() ||
      !render_frame()->GetWebFrame()) {
    return;
  }

  render_frame()->GetWebFrame()->ExecuteScript(blink::WebScriptSource(
      blink::WebString::FromUTF8(kWhateverTubeAppearanceScript)));
}

void CobaltRenderFrameObserver::DidMeaningfulLayout(
    blink::WebMeaningfulLayout meaningful_layout) {
  if (meaningful_layout == blink::WebMeaningfulLayout::kVisuallyNonEmpty) {
    const CobaltExtensionGraphicsApi* graphics_extension =
        static_cast<const CobaltExtensionGraphicsApi*>(
            SbSystemGetExtension(kCobaltExtensionGraphicsName));
    if (graphics_extension &&
        strcmp(graphics_extension->name, kCobaltExtensionGraphicsName) == 0 &&
        graphics_extension->version >= 6) {
      graphics_extension->ReportFullyDrawn();
    }
  }
}

bool CobaltRenderFrameObserver::AllowRunningInsecureContent(
    bool enabled_per_settings,
    const blink::WebURL& url) {
  if (enabled_per_settings) {
    return true;
  }
  const GURL gurl(url);
  // Allow insecure WebSocket connections (ws:) to local/private network
  // endpoints (RFC 1918 / loopback) for testing frameworks such as YTS.
  // See b/377410179, b/545796867.
  if (gurl.SchemeIs(url::kWsScheme)) {
    if (net::IsLocalhost(gurl)) {
      return true;
    }
    net::IPAddress ip_address;
    if (ip_address.AssignFromIPLiteral(gurl.host_piece()) &&
        !ip_address.IsPubliclyRoutable()) {
      return true;
    }
  }
  return false;
}

}  // namespace cobalt
