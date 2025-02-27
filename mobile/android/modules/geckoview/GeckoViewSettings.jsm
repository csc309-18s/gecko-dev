/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = ["GeckoViewSettings"];

ChromeUtils.import("resource://gre/modules/GeckoViewModule.jsm");
ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");

ChromeUtils.defineModuleGetter(this, "SafeBrowsing",
  "resource://gre/modules/SafeBrowsing.jsm");

XPCOMUtils.defineLazyGetter(this, "dump", () =>
    ChromeUtils.import("resource://gre/modules/AndroidLog.jsm",
                       {}).AndroidLog.d.bind(null, "ViewSettings"));

function debug(aMsg) {
  // dump(aMsg);
}

// Handles GeckoView settings including:
// * tracking protection
// * multiprocess
class GeckoViewSettings extends GeckoViewModule {
  init() {
    this._isSafeBrowsingInit = false;
    this._useTrackingProtection = false;

    // We only allow to set this setting during initialization, further updates
    // will be ignored.
    this.useMultiprocess = !!this.settings.useMultiprocess;
    this._displayMode = Ci.nsIDocShell.DISPLAY_MODE_BROWSER;
  }

  onSettingsUpdate() {
    debug("onSettingsUpdate: " + JSON.stringify(this.settings));

    this.useTrackingProtection = !!this.settings.useTrackingProtection;
    this.displayMode = this.settings.displayMode;
  }

  get useTrackingProtection() {
    return this._useTrackingProtection;
  }

  set useTrackingProtection(aUse) {
    if (aUse && !this._isSafeBrowsingInit) {
      SafeBrowsing.init();
      this._isSafeBrowsingInit = true;
    }
    if (aUse != this._useTrackingProtection) {
      this.messageManager.loadFrameScript("data:," +
        `docShell.useTrackingProtection = ${aUse}`,
        true
      );
      this._useTrackingProtection = aUse;
    }
  }

  get useMultiprocess() {
    return this.browser.getAttribute("remote") == "true";
  }

  set useMultiprocess(aUse) {
    if (aUse == this.useMultiprocess) {
      return;
    }
    let parentNode = this.browser.parentNode;
    parentNode.removeChild(this.browser);

    if (aUse) {
      this.browser.setAttribute("remote", "true");
    } else {
      this.browser.removeAttribute("remote");
    }
    parentNode.appendChild(this.browser);
  }

  get displayMode() {
    return this._displayMode;
  }

  set displayMode(aMode) {
    if (!this.useMultiprocess) {
      this.window.QueryInterface(Ci.nsIInterfaceRequestor)
                   .getInterface(Ci.nsIDocShell)
                   .displayMode = aMode;
    } else {
      this.messageManager.loadFrameScript("data:," +
        `docShell.displayMode = ${aMode}`,
        true
      );
    }
    this._displayMode = aMode;
  }
}
