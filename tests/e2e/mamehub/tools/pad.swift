import Cocoa
import CoreGraphics
import Foundation

// Send gamepad-like keys to a MAMEHub process via CGEvent.postToPid.
// In netplay, each peer's local player is mapped onto P1 keyboard defaults
// (directions/buttons) and Coin1/Start1 keys (5 / 1).

guard CommandLine.arguments.count >= 3, let pid = pid_t(CommandLine.arguments[1]) else {
    fputs("""
    usage:
      pad <pid> tap <key> [hold_ms=80]
      pad <pid> chord <key1,key2,...> [hold_ms=80]
      pad <pid> mash <seconds> [seed]
      pad <pid> mash_arcade <seconds> [seed]
    keys: up down left right a b x y start select coin space
          btn1 btn2 btn3 1 2 3 4 5 enter esc
    """, stderr)
    exit(2)
}

let action = CommandLine.arguments[2]
let source = CGEventSource(stateID: .hidSystemState)

// macOS virtual keycodes matching MAME defaults (P1 / Coin1 / Start1)
let keyMap: [String: CGKeyCode] = [
    "a": 49,        // SPACE = BUTTON3 / SNES A
    "b": 58,        // LALT  = BUTTON2 / SNES B
    "y": 59,        // LCTRL = BUTTON1 / SNES Y
    "x": 56,        // LSHIFT= BUTTON4 / SNES X
    "btn1": 59,     // LCTRL
    "btn2": 58,     // LALT
    "btn3": 49,     // SPACE
    "up": 126,
    "down": 125,
    "left": 123,
    "right": 124,
    "start": 18,    // KEYCODE_1 (Start1; remapped per netplay player)
    "coin": 23,     // KEYCODE_5 (Coin1; remapped per netplay player)
    "select": 23,   // KEYCODE_5
    "space": 49,
    "1": 18,
    "2": 19,
    "3": 20,
    "4": 21,
    "5": 23,
    "6": 22,
    "enter": 36,
    "esc": 53,
]

func flags(for code: CGKeyCode) -> CGEventFlags {
    switch code {
    case 59: return .maskControl
    case 58: return .maskAlternate
    case 56: return .maskShift
    default: return []
    }
}

func post(_ code: CGKeyCode, down: Bool) {
    let ev = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: down)!
    let f = flags(for: code)
    if !f.isEmpty {
        ev.flags = down ? f : []
    }
    ev.postToPid(pid)
}

func hold(_ codes: [CGKeyCode], ms: Int) {
    for c in codes { post(c, down: true) }
    usleep(useconds_t(max(20, ms) * 1000))
    for c in codes.reversed() { post(c, down: false) }
}

func resolve(_ name: String) -> CGKeyCode? {
    keyMap[name.lowercased()]
}

func runMash(seconds: Double, seed: UInt64, faces: [String]) {
    var rng = SeededGenerator(seed: seed)
    let dirs = ["up", "down", "left", "right"]
    let deadline = Date().addingTimeInterval(seconds)
    var n = 0
    while Date() < deadline {
        var names: [String] = []
        if Bool.random(using: &rng) {
            names.append(dirs.randomElement(using: &rng)!)
            if Bool.random(using: &rng) {
                let d2 = dirs.randomElement(using: &rng)!
                if d2 != names[0] { names.append(d2) }
            }
        }
        if names.isEmpty || Bool.random(using: &rng) {
            names.append(faces.randomElement(using: &rng)!)
            if Bool.random(using: &rng) {
                let f2 = faces.randomElement(using: &rng)!
                if !names.contains(f2) { names.append(f2) }
            }
        }
        let codes = names.compactMap { resolve($0) }
        let holdMs = Int.random(in: 40...180, using: &rng)
        hold(codes, ms: holdMs)
        let gapMs = Int.random(in: 20...120, using: &rng)
        usleep(useconds_t(gapMs * 1000))
        n += 1
        if n % 50 == 0 {
            fputs("mash pid=\(pid) pulses=\(n) remain=\(Int(deadline.timeIntervalSinceNow))s\n", stderr)
        }
    }
    fputs("mash done pid=\(pid) pulses=\(n)\n", stderr)
}

switch action {
case "tap":
    guard CommandLine.arguments.count >= 4, let code = resolve(CommandLine.arguments[3]) else {
        fputs("bad key\n", stderr); exit(4)
    }
    let ms = CommandLine.arguments.count > 4 ? (Int(CommandLine.arguments[4]) ?? 80) : 80
    hold([code], ms: ms)
case "chord":
    guard CommandLine.arguments.count >= 4 else { fputs("need keys\n", stderr); exit(4) }
    let names = CommandLine.arguments[3].split(separator: ",").map(String.init)
    var codes: [CGKeyCode] = []
    for n in names {
        guard let c = resolve(n) else { fputs("bad key \(n)\n", stderr); exit(4) }
        codes.append(c)
    }
    let ms = CommandLine.arguments.count > 4 ? (Int(CommandLine.arguments[4]) ?? 80) : 80
    hold(codes, ms: ms)
case "mash":
    guard CommandLine.arguments.count >= 4, let seconds = Double(CommandLine.arguments[3]) else {
        fputs("need seconds\n", stderr); exit(4)
    }
    let seed = CommandLine.arguments.count > 4
        ? (UInt64(CommandLine.arguments[4]) ?? UInt64(pid))
        : UInt64(pid) &+ UInt64(Date().timeIntervalSince1970)
    runMash(seconds: seconds, seed: seed, faces: ["a", "b", "x", "y"])
case "mash_arcade":
    guard CommandLine.arguments.count >= 4, let seconds = Double(CommandLine.arguments[3]) else {
        fputs("need seconds\n", stderr); exit(4)
    }
    let seed = CommandLine.arguments.count > 4
        ? (UInt64(CommandLine.arguments[4]) ?? UInt64(pid))
        : UInt64(pid) &+ UInt64(Date().timeIntervalSince1970)
    // X-Men / typical Konami: 3 attack buttons
    runMash(seconds: seconds, seed: seed, faces: ["btn1", "btn2", "btn3"])
default:
    fputs("bad action\n", stderr)
    exit(4)
}

struct SeededGenerator: RandomNumberGenerator {
    private var state: UInt64
    init(seed: UInt64) { state = seed == 0 ? 0x9e3779b97f4a7c15 : seed }
    mutating func next() -> UInt64 {
        state &+= 0x9e3779b97f4a7c15
        var z = state
        z = (z ^ (z >> 30)) &* 0xbf58476d1ce4e5b9
        z = (z ^ (z >> 27)) &* 0x94d049bb133111eb
        return z ^ (z >> 31)
    }
}
