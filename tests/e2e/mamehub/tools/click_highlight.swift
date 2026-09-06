import Cocoa
import CoreGraphics
import AppKit

guard CommandLine.arguments.count >= 2, let pid = pid_t(CommandLine.arguments[1]) else {
    fputs("usage: mamehub_click_highlight <pid> [rowOffset=0|join] [enter=1] [rowPts=30]\n", stderr)
    exit(2)
}
let rowArg = CommandLine.arguments.count > 2 ? CommandLine.arguments[2] : "0"
let probeOnly = (rowArg == "probe")
let findJoinRow = (rowArg == "join")
let rowOffset = (probeOnly || findJoinRow) ? 0 : (Int(rowArg) ?? 0)
let doEnter = !probeOnly && (CommandLine.arguments.count > 3 ? CommandLine.arguments[3] : "1") != "0"
let rowPts = CGFloat(CommandLine.arguments.count > 4 ? (Double(CommandLine.arguments[4]) ?? 30.0) : 30.0)

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

func capture(_ rect: CGRect) -> NSBitmapImageRep? {
    let outDir = ProcessInfo.processInfo.environment["MAMEHUB_E2E_OUT"]
        ?? (ProcessInfo.processInfo.environment["TMPDIR"] ?? "/tmp")
    let dirURL = URL(fileURLWithPath: outDir, isDirectory: true)
    try? FileManager.default.createDirectory(at: dirURL, withIntermediateDirectories: true)
    let tmp = dirURL.appendingPathComponent("_win.png")
    let task = Process()
    task.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
    task.arguments = ["-x", "-R", "\(Int(rect.origin.x)),\(Int(rect.origin.y)),\(Int(rect.width)),\(Int(rect.height))", tmp.path]
    try? task.run(); task.waitUntilExit()
    guard let img = NSImage(contentsOf: tmp), let tiff = img.tiffRepresentation else { return nil }
    return NSBitmapImageRep(data: tiff)
}

func isHighlight(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat) -> Bool {
    if r > 0.70 && g > 0.50 && b < 0.40 && r > b + 0.25 { return true }
    if b > 0.55 && b > r + 0.15 && b > g * 0.9 && r < 0.55 { return true }
    return false
}

func isWhiteText(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat) -> Bool {
    return r > 0.78 && g > 0.78 && b > 0.78
}

func findHighlight(in bmp: NSBitmapImageRep) -> (Int, Int)? {
    let w = bmp.pixelsWide, h = bmp.pixelsHigh
    let y0 = h / 6
    let y1 = (h * 5) / 6
    let x0 = w / 8
    let x1 = (w * 7) / 8
    var bestY = -1, bestCount = 0, bestX = w/2
    for y in y0..<y1 {
        var count = 0, sumX = 0
        for x in stride(from: x0, to: x1, by: 2) {
            guard let c = bmp.colorAt(x: x, y: y) else { continue }
            var r: CGFloat=0,g: CGFloat=0,b: CGFloat=0,a: CGFloat=0
            (c.usingColorSpace(.sRGB) ?? c).getRed(&r, green: &g, blue: &b, alpha: &a)
            if isHighlight(r,g,b) {
                count += 1; sumX += x
            }
        }
        if count > bestCount { bestCount = count; bestY = y; bestX = count>0 ? sumX/count : bestX }
    }
    fputs("highlight bestCount=\(bestCount) at (\(bestX),\(bestY)) bounds=\(w)x\(h)\n", stderr)
    return bestCount >= 8 ? (bestX, bestY) : nil
}

/// Find the next white menu-text row below the yellow Host Game highlight (= Join Game).
func findJoinTextRow(in bmp: NSBitmapImageRep, below highlightY: Int, atX highlightX: Int) -> (Int, Int)? {
    let w = bmp.pixelsWide, h = bmp.pixelsHigh
    let x0 = max(0, highlightX - w/5)
    let x1 = min(w, highlightX + w/5)
    var bestY = -1, bestCount = 0, bestX = highlightX
    let startY = min(h - 1, highlightY + 8)
    let endY = min(h - 1, highlightY + max(80, h/10))
    for y in startY...endY {
        var count = 0, sumX = 0
        for x in stride(from: x0, to: x1, by: 2) {
            guard let c = bmp.colorAt(x: x, y: y) else { continue }
            var r: CGFloat=0,g: CGFloat=0,b: CGFloat=0,a: CGFloat=0
            (c.usingColorSpace(.sRGB) ?? c).getRed(&r, green: &g, blue: &b, alpha: &a)
            if isHighlight(r,g,b) { continue }
            if isWhiteText(r,g,b) {
                count += 1; sumX += x
            }
        }
        if count > bestCount {
            bestCount = count
            bestY = y
            bestX = count > 0 ? sumX / count : highlightX
        }
    }
    fputs("joinText bestCount=\(bestCount) at (\(bestX),\(bestY)) below=\(highlightY)\n", stderr)
    // Require a real text band a bit below the highlight.
    guard bestCount >= 10, bestY > highlightY + 12 else { return nil }
    return (bestX, bestY)
}

func activateFrontmost() {
    let script = "tell application \"System Events\" to set frontmost of (first process whose unix id is \(pid)) to true"
    if let appleScript = NSAppleScript(source: script) {
        var err: NSDictionary?
        appleScript.executeAndReturnError(&err)
    }
    usleep(120_000)
}

func click(_ p: CGPoint) {
    let source = CGEventSource(stateID: .hidSystemState)
    let mv = CGEvent(mouseEventSource: source, mouseType: .mouseMoved, mouseCursorPosition: p, mouseButton: .left)!
    mv.post(tap: .cghidEventTap); usleep(20_000)
    let d = CGEvent(mouseEventSource: source, mouseType: .leftMouseDown, mouseCursorPosition: p, mouseButton: .left)!
    let u = CGEvent(mouseEventSource: source, mouseType: .leftMouseUp, mouseCursorPosition: p, mouseButton: .left)!
    d.post(tap: .cghidEventTap); usleep(60_000); u.post(tap: .cghidEventTap)
}

activateFrontmost()
guard let rect = windowBounds(pid: pid) else { fputs("window not found\n", stderr); exit(3) }
guard let bmp = capture(rect), let hi = findHighlight(in: bmp) else { fputs("highlight not found\n", stderr); exit(4) }
if probeOnly {
    fputs("probe highlight=\(hi.0),\(hi.1)\n", stderr)
    print("\(hi.1)")
    exit(0)
}
let sx = CGFloat(bmp.pixelsWide)/rect.width
let sy = CGFloat(bmp.pixelsHigh)/rect.height

let target: (Int, Int)
if findJoinRow {
    guard let join = findJoinTextRow(in: bmp, below: hi.1, atX: hi.0) else {
        fputs("join text row not found\n", stderr)
        exit(5)
    }
    target = join
} else {
    target = (hi.0, hi.1 + Int(CGFloat(rowOffset) * rowPts * sy))
}

let pt = CGPoint(x: rect.origin.x + CGFloat(target.0)/sx,
                 y: rect.origin.y + CGFloat(target.1)/sy)
fputs("click at \(pt) mode=\(findJoinRow ? "join" : "offset") rowOffset=\(rowOffset)\n", stderr)
activateFrontmost()
click(pt)
usleep(120_000)
if findJoinRow {
    // Double-click Join Game so UI_SELECT fires on the row even if
    // keyboard focus still thinks Host Game is selected.
    click(pt)
    usleep(200_000)
} else if doEnter {
    let script = """
    tell application "System Events"
      set frontmost of (first process whose unix id is \(pid)) to true
      key code 36
    end tell
    """
    if let appleScript = NSAppleScript(source: script) {
        var err: NSDictionary?
        appleScript.executeAndReturnError(&err)
    }
}
