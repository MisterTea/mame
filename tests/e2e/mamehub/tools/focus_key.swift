import Cocoa
import CoreGraphics

guard CommandLine.arguments.count >= 3, let pid = pid_t(CommandLine.arguments[1]) else {
    fputs("usage: mamehub_focus_key <pid> <focus|down|enter|up|escape|type|raw_down|raw_enter> [count|text]\n", stderr)
    exit(2)
}
let action = CommandLine.arguments[2]

func windowBounds(pid: pid_t) -> CGRect? {
    guard let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] else { return nil }
    for w in windows {
        guard let owner = w[kCGWindowOwnerPID as String] as? pid_t, owner == pid else { continue }
        guard let layer = w[kCGWindowLayer as String] as? Int, layer == 0 else { continue }
        guard let b = w[kCGWindowBounds as String] as? [String: Any],
              let x = (b["X"] as? NSNumber)?.doubleValue,
              let y = (b["Y"] as? NSNumber)?.doubleValue,
              let width = (b["Width"] as? NSNumber)?.doubleValue,
              let height = (b["Height"] as? NSNumber)?.doubleValue,
              width > 100, height > 100 else { continue }
        return CGRect(x: x, y: y, width: width, height: height)
    }
    return nil
}

guard let rect = windowBounds(pid: pid) else {
    fputs("window not found\n", stderr)
    exit(3)
}

let source = CGEventSource(stateID: .hidSystemState)

func runAppleScript(_ sourceText: String) {
    if let appleScript = NSAppleScript(source: sourceText) {
        var err: NSDictionary?
        appleScript.executeAndReturnError(&err)
        if let err { fputs("osascript err: \(err)\n", stderr) }
    }
}

func activateFrontmost() {
    runAppleScript("tell application \"System Events\" to set frontmost of (first process whose unix id is \(pid)) to true")
    usleep(120_000)
}

/// System Events key codes survive tool rebuilds better than CGEvent HID (TCC).
func seKeyCode(_ code: Int, count: Int = 1, delayMs: Int = 120) {
    let delay = String(format: "%.3f", Double(delayMs) / 1000.0)
    let script = """
    tell application "System Events"
      set frontmost of (first process whose unix id is \(pid)) to true
      repeat \(count) times
        key code \(code)
        delay \(delay)
      end repeat
    end tell
    """
    runAppleScript(script)
}

func seKeystroke(_ text: String) {
    let escaped = text
        .replacingOccurrences(of: "\\", with: "\\\\")
        .replacingOccurrences(of: "\"", with: "\\\"")
    let script = """
    tell application "System Events"
      set frontmost of (first process whose unix id is \(pid)) to true
      keystroke "\(escaped)"
    end tell
    """
    runAppleScript(script)
    usleep(UInt32(max(80_000, text.count * 40_000)))
}

func clickContent() {
    activateFrontmost()
    let pt = CGPoint(x: rect.midX, y: rect.midY * 0.55)
    let md = CGEvent(mouseEventSource: source, mouseType: .leftMouseDown, mouseCursorPosition: pt, mouseButton: .left)!
    let mu = CGEvent(mouseEventSource: source, mouseType: .leftMouseUp, mouseCursorPosition: pt, mouseButton: .left)!
    md.post(tap: .cghidEventTap)
    usleep(40_000)
    mu.post(tap: .cghidEventTap)
    usleep(100_000)
}

func focusTitlebar() {
    let focusPt = CGPoint(x: rect.midX, y: rect.minY + 12)
    let md = CGEvent(mouseEventSource: source, mouseType: .leftMouseDown, mouseCursorPosition: focusPt, mouseButton: .left)!
    let mu = CGEvent(mouseEventSource: source, mouseType: .leftMouseUp, mouseCursorPosition: focusPt, mouseButton: .left)!
    md.post(tap: .cghidEventTap)
    usleep(40_000)
    mu.post(tap: .cghidEventTap)
    usleep(200_000)
}

let skipFocus = action.hasPrefix("raw_")
let baseAction = skipFocus ? String(action.dropFirst(4)) : action
let count = CommandLine.arguments.count > 3 ? (Int(CommandLine.arguments[3]) ?? 1) : 1

switch baseAction {
case "focus":
    focusTitlebar()
case "down":
    // Content click first so menu (not titlebar) receives Down.
    if !skipFocus { clickContent() } else { activateFrontmost() }
    seKeyCode(125, count: count, delayMs: 180)
case "up":
    if !skipFocus { clickContent() } else { activateFrontmost() }
    seKeyCode(126, count: count, delayMs: 180)
case "enter":
    if !skipFocus { clickContent() } else { activateFrontmost() }
    seKeyCode(36, count: count, delayMs: 200)
case "escape":
    seKeyCode(53, count: 1, delayMs: 120)
case "type":
    guard CommandLine.arguments.count >= 4 else { exit(2) }
    seKeystroke(CommandLine.arguments[3].lowercased())
default:
    fputs("bad action\n", stderr)
    exit(4)
}
fputs("ok \(action) pid \(pid)\n", stderr)
