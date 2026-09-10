import Cocoa
import CoreGraphics
import Foundation

// SDL3 on macOS often ignores keyboard until the content view is clicked.
// Mirror focus_key: activate, click content, then deliver keys via System Events.
let args = CommandLine.arguments
guard args.count >= 3, let pid = pid_t(args[1]) else {
    fputs("usage: mamehub_key <pid> <cmd> [args]\n", stderr)
    exit(2)
}

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

func runAppleScript(_ sourceText: String) {
    if let appleScript = NSAppleScript(source: sourceText) {
        var err: NSDictionary?
        appleScript.executeAndReturnError(&err)
        if let err { fputs("osascript err: \(err)\n", stderr) }
    }
}

func activateAndClickContent() {
    runAppleScript("tell application \"System Events\" to set frontmost of (first process whose unix id is \(pid)) to true")
    usleep(80_000)
    guard let rect = windowBounds(pid: pid) else { return }
    let source = CGEventSource(stateID: .hidSystemState)
    // Click in upper-middle content so UI menus (not titlebar) get focus.
    let pt = CGPoint(x: rect.midX, y: rect.minY + rect.height * 0.45)
    let md = CGEvent(mouseEventSource: source, mouseType: .leftMouseDown, mouseCursorPosition: pt, mouseButton: .left)!
    let mu = CGEvent(mouseEventSource: source, mouseType: .leftMouseUp, mouseCursorPosition: pt, mouseButton: .left)!
    md.post(tap: .cghidEventTap)
    usleep(40_000)
    mu.post(tap: .cghidEventTap)
    usleep(120_000)
}

func seKeyCode(_ code: Int, count: Int = 1, delayMs: Int = 160) {
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

activateAndClickContent()
switch args[2] {
case "type":
    guard args.count >= 4 else { exit(2) }
    seKeystroke(args[3].lowercased())
case "enter":
    seKeyCode(36, count: 1, delayMs: 200)
case "escape":
    seKeyCode(53)
case "down":
    seKeyCode(125, count: args.count > 3 ? (Int(args[3]) ?? 1) : 1, delayMs: 200)
case "up":
    seKeyCode(126, count: args.count > 3 ? (Int(args[3]) ?? 1) : 1, delayMs: 200)
case "wait":
    usleep(UInt32((Int(args[3]) ?? 100) * 1000))
default:
    exit(2)
}
