/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

/**
 * Holds data definitions for our UI Telemetry implementation.
 */
public interface TelemetryContract {

    /**
     * Holds event names. Intended for use with
     * Telemetry.sendUIEvent() as the "action" parameter.
     */
    public interface Event {
        // Outcome of data policy notification: can be true or false.
        public static final String POLICY_NOTIFICATION_SUCCESS = "policynotification.success.1:";

        // Top site pinned.
        public static final String TOP_SITES_PIN = "pin.1";

        // Top site un-pinned.
        public static final String TOP_SITES_UNPIN = "unpin.1";

        // Top site edited.
        public static final String TOP_SITES_EDIT = "edit.1";

        // Set default panel.
        public static final String PANEL_SET_DEFAULT = "setdefault.1";

        // Sanitizing private data.
        public static final String SANITIZE = "sanitize.1";
    }

    /**
     * Holds event methods. Intended for use in
     * Telemetry.sendUIEvent() as the "method" parameter.
     */
    public interface Method {
        // Action triggered from a dialog.
        public static final String DIALOG = "dialog";
    }

    /**
     * Holds session names. Intended for use with
     * Telemetry.startUISession() as the "sessionName" parameter.
     */
    public interface Session {
        // Started when a user enters about:home.
        public static final String HOME = "home.1";

        // Started when a user enters a given home panel.
        // Session name is dynamic, encoded as "homepanel.1:<panel_id>"
        public static final String HOME_PANEL = "homepanel.1:";
    }

    /**
     * Holds reasons for stopping a session. Intended for use in
     * Telemetry.stopUISession() as the "reason" parameter.
     */
    public interface Reason {}
}
