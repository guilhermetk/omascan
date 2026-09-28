pragma Singleton

import QtQuick
import Omascan 1.0

// Every colour, size, radius and duration in OmaScan lives here; nothing else
// holds a colour literal or a magic number. It is the Oma family's palette —
// the same one OmaShow uses — and it is fixed: a scan is judged by eye, and a
// surround that changes hue changes how the paper reads.
//
// The one exception is the accent, which follows the Omarchy theme. Buttons,
// selection and focus are chrome and belong to the desktop.
QtObject {
    readonly property string appName: "OmaScan"

    // ── Colour ──────────────────────────────────────────────────────────────
    readonly property color windowBg: "#0B0E13"
    readonly property color panelBg: "#111722"
    readonly property color panelRaised: "#171E2A"
    readonly property color controlBg: "#1B2431"
    readonly property color controlHover: "#222D3D"
    readonly property color border: "#293241"
    readonly property color borderStrong: "#3A4658"

    readonly property color textPrimary: "#E7EDF7"
    readonly property color textSecondary: "#A7B1C1"
    readonly property color textMuted: "#727E90"

    readonly property color omaAccent: "#27C2FF"
    readonly property color accent: OmarchyTheme.accentFollowed ? OmarchyTheme.accent : omaAccent
    readonly property color accentPressed: Qt.darker(accent, 1.35)
    // Near-black or near-white, whichever reads on the accent.
    readonly property color accentText:
        (0.2126 * accent.r + 0.7152 * accent.g + 0.0722 * accent.b) > 0.55 ? "#04121A" : "#F2F7FF"
    readonly property color warning: "#FBBF24"
    readonly property color danger: "#EF4444"
    readonly property color success: "#34D399"

    function pressedOn(base) { return Qt.darker(base, 1.08) }
    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    readonly property real disabledOpacity: 0.38

    // ── Surfaces ────────────────────────────────────────────────────────────
    // The pasteboard the page lies on: darker than the panels so the paper
    // reads as the object and the surround as nothing.
    readonly property color pasteboard: "#1A1F26"
    readonly property color paperShadow: "#000000"
    readonly property color scrim: "#000000"
    readonly property color cropShade: withAlpha(scrim, 0.55)

    // ── Spacing: 4, 8, 12, 16, 24 ───────────────────────────────────────────
    readonly property int s1: 4
    readonly property int s2: 8
    readonly property int s3: 12
    readonly property int s4: 16
    readonly property int s5: 24
    readonly property int s6: 40

    // ── Radius ──────────────────────────────────────────────────────────────
    readonly property int rControl: 4
    readonly property int rMenu: 6
    readonly property int rCard: 6
    readonly property int rHandle: 2

    // ── Lines ───────────────────────────────────────────────────────────────
    readonly property int hairline: 1
    readonly property int focusRing: 1
    readonly property int selectionRing: 2

    // ── Type ────────────────────────────────────────────────────────────────
    readonly property string fontFamily: {
        const families = Qt.fontFamilies()
        if (families.indexOf("Inter") >= 0) return "Inter"
        if (families.indexOf("Noto Sans") >= 0) return "Noto Sans"
        return "sans-serif"
    }
    readonly property string monoFamily: {
        const families = Qt.fontFamilies()
        if (families.indexOf("JetBrainsMono Nerd Font") >= 0) return "JetBrainsMono Nerd Font"
        if (families.indexOf("Noto Sans Mono") >= 0) return "Noto Sans Mono"
        return "monospace"
    }
    readonly property int fsCaption: 10
    readonly property int fsLabel: 12
    readonly property int fsControl: 13
    readonly property int fsHeading: 13
    readonly property int fsSection: 14
    readonly property int fsTitle: 15
    readonly property int fsHero: 22
    readonly property int wHeading: Font.DemiBold
    readonly property int wNormal: Font.Normal
    readonly property real capsTracking: 0.8

    // ── Metrics ─────────────────────────────────────────────────────────────
    readonly property int hTitleBar: 34
    readonly property int hToolbar: 48
    readonly property int hStatusBar: 28
    readonly property int hControl: 28
    readonly property int hRow: 30
    readonly property int hNavHeader: 36
    readonly property int hCardHeader: 36
    readonly property int hHeroButton: 40
    readonly property int wFieldLabel: 84
    readonly property int wNavigator: 200
    readonly property int wInspector: 300
    readonly property int wThumb: 132
    readonly property int wDialog: 620
    readonly property int szIcon: 16
    readonly property int szIconLarge: 20
    readonly property int szIconHero: 40
    readonly property int szIconHit: 28
    readonly property int szHandle: 12
    readonly property int szStatusDot: 8
    readonly property int szKey: 22
    readonly property int pagePadding: 40

    // ── Motion ──────────────────────────────────────────────────────────────
    readonly property int dFast: 100
    readonly property int dNormal: 130
    readonly property int dSlow: 220
    readonly property int easing: Easing.OutCubic
    readonly property int tooltipDelay: 450
    readonly property int toastDuration: 4000
}
