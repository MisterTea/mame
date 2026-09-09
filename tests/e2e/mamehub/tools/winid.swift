import Cocoa

// Print CGWindow id (and optional bounds) for a process's main on-screen window.
// usage: winid <pid>           -> window id
//        winid <pid> bounds    -> id x y w h  (global display coords)

guard CommandLine.arguments.count >= 2, let pid = Int32(CommandLine.arguments[1]) else {
    fputs("usage: winid <pid> [bounds]\n", stderr)
    exit(2)
}
let wantBounds = CommandLine.arguments.count >= 3 && CommandLine.arguments[2] == "bounds"

let opts = CGWindowListOption(arrayLiteral: .optionOnScreenOnly, .excludeDesktopElements)
guard let info = CGWindowListCopyWindowInfo(opts, kCGNullWindowID) as? [[String: Any]] else {
    exit(1)
}
for w in info {
    guard let owner = w[kCGWindowOwnerPID as String] as? Int32, owner == pid else { continue }
    guard let layer = w[kCGWindowLayer as String] as? Int, layer == 0 else { continue }
    guard let wid = w[kCGWindowNumber as String] as? Int else { continue }
    let bounds = w[kCGWindowBounds as String] as? [String: Any]
    let x = (bounds?["X"] as? NSNumber)?.intValue ?? 0
    let y = (bounds?["Y"] as? NSNumber)?.intValue ?? 0
    let wdt = (bounds?["Width"] as? NSNumber)?.intValue ?? 0
    let hgt = (bounds?["Height"] as? NSNumber)?.intValue ?? 0
    if wdt < 50 || hgt < 50 { continue }
    if wantBounds {
        print("\(wid) \(x) \(y) \(wdt) \(hgt)")
    } else {
        print(wid)
    }
    exit(0)
}
exit(1)
