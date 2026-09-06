import Cocoa
import CoreGraphics
import Foundation

let args = CommandLine.arguments
guard args.count >= 3, let pid = pid_t(args[1]) else {
    fputs("usage: mamehub_key <pid> <cmd> [args]\n", stderr)
    exit(2)
}

func activate() {
    // postToPid does not require frontmost; avoid System Events here (can stall).
    usleep(30_000)
}

func tap(_ code: CGKeyCode, count: Int = 1, gapUs: useconds_t = 160_000) {
    let source = CGEventSource(stateID: .hidSystemState)
    for _ in 0..<count {
        let down = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: true)!
        let up = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: false)!
        down.postToPid(pid)
        usleep(25_000)
        up.postToPid(pid)
        usleep(gapUs)
    }
}

let letterMap: [Character: CGKeyCode] = [
    "a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8, "v": 9,
    "b": 11, "q": 12, "w": 13, "e": 14, "r": 15, "y": 16, "t": 17,
    "1": 18, "2": 19, "3": 20, "4": 21, "5": 23, "6": 22, "7": 26, "8": 28, "9": 25, "0": 29,
    "o": 31, "u": 32, "i": 34, "p": 35, "l": 37, "j": 38, "k": 40, "n": 45, "m": 46
]

activate()
switch args[2] {
case "type":
    guard args.count >= 4 else { exit(2) }
    for ch in args[3].lowercased() {
        guard let code = letterMap[ch] else { continue }
        tap(code, gapUs: 160_000)
    }
case "enter":
    tap(36, gapUs: 200_000)
case "escape":
    tap(53)
case "down":
    tap(125, count: args.count > 3 ? (Int(args[3]) ?? 1) : 1, gapUs: 200_000)
case "up":
    tap(126, count: args.count > 3 ? (Int(args[3]) ?? 1) : 1, gapUs: 200_000)
case "wait":
    usleep(UInt32((Int(args[3]) ?? 100) * 1000))
default:
    exit(2)
}
