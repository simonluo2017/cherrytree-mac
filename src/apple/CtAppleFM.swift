/*
 * CtAppleFM.swift
 *
 * Apple Foundation Models bridge: exposes the on device language model of
 * Apple Intelligence (macOS 26+) through a tiny C ABI (see ct_applefm.h).
 * Built as libct_applefm.dylib and loaded at runtime by CtAiProviderApple.
 *
 * Everything runs on this Mac: the framework never sends the text anywhere.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

import Foundation
#if canImport(FoundationModels)
import FoundationModels
#endif

public typealias CtPieceCallback = @convention(c) (UnsafePointer<CChar>?, Int, UnsafeMutableRawPointer?) -> Void
public typealias CtCancelCallback = @convention(c) (UnsafeMutableRawPointer?) -> Int32

private let kAvailable: Int32 = 0
private let kUnsupported: Int32 = 1
private let kNotEnabled: Int32 = 2
private let kModelNotReady: Int32 = 3
private let kUnknown: Int32 = -1

private let kOk: Int32 = 0
private let kError: Int32 = 1
private let kCancelled: Int32 = 2

/// shared between the C caller and the task that talks to the framework
private final class GenerationBox: @unchecked Sendable {
    var result: Int32 = 0
    var errorText = ""
    let onPiece: CtPieceCallback?
    let shouldCancel: CtCancelCallback?
    let userData: UnsafeMutableRawPointer?
    init(onPiece: CtPieceCallback?, shouldCancel: CtCancelCallback?, userData: UnsafeMutableRawPointer?) {
        self.onPiece = onPiece
        self.shouldCancel = shouldCancel
        self.userData = userData
    }
    func send(_ piece: String) {
        guard !piece.isEmpty, let onPiece = onPiece else { return }
        piece.withCString { cstr in onPiece(cstr, piece.utf8.count, userData) }
    }
    func cancelled() -> Bool {
        guard let shouldCancel = shouldCancel else { return false }
        return shouldCancel(userData) != 0
    }
}

private func writeString(_ text: String, to buffer: UnsafeMutablePointer<CChar>?, length: Int) {
    guard let buffer = buffer, length > 0 else { return }
    let bytes = Array(text.utf8.prefix(length - 1))
    for (i, b) in bytes.enumerated() { buffer[i] = CChar(bitPattern: b) }
    buffer[bytes.count] = 0
}

@_cdecl("ct_applefm_version")
public func ct_applefm_version() -> Int32 {
    return 1
}

@_cdecl("ct_applefm_context_size")
public func ct_applefm_context_size() -> Int32 {
    // the on device model has a 4096 token context window (prompt + answer)
    return 4096
}

@_cdecl("ct_applefm_availability")
public func ct_applefm_availability(_ reason: UnsafeMutablePointer<CChar>?, _ reasonLen: Int) -> Int32 {
    #if canImport(FoundationModels)
    if #available(macOS 26.0, *) {
        switch SystemLanguageModel.default.availability {
        case .available:
            writeString("Apple Intelligence is available", to: reason, length: reasonLen)
            return kAvailable
        case .unavailable(let why):
            switch why {
            case .deviceNotEligible:
                writeString("This Mac does not support Apple Intelligence", to: reason, length: reasonLen)
                return kUnsupported
            case .appleIntelligenceNotEnabled:
                writeString("Apple Intelligence is turned off: enable it in System Settings → Apple Intelligence & Siri", to: reason, length: reasonLen)
                return kNotEnabled
            case .modelNotReady:
                writeString("The Apple Intelligence model is not ready yet (still downloading), try again later", to: reason, length: reasonLen)
                return kModelNotReady
            @unknown default:
                writeString("Apple Intelligence is not available", to: reason, length: reasonLen)
                return kUnknown
            }
        }
    }
    else {
        writeString("Apple Foundation Models need macOS 26 or newer", to: reason, length: reasonLen)
        return kUnsupported
    }
    #else
    writeString("This build was made without the FoundationModels SDK (Xcode 26 is required)", to: reason, length: reasonLen)
    return kUnsupported
    #endif
}

@_cdecl("ct_applefm_generate")
public func ct_applefm_generate(_ instructions: UnsafePointer<CChar>?,
                                _ prompt: UnsafePointer<CChar>?,
                                _ maxTokens: Int32,
                                _ temperature: Double,
                                _ onPiece: CtPieceCallback?,
                                _ shouldCancel: CtCancelCallback?,
                                _ userData: UnsafeMutableRawPointer?,
                                _ error: UnsafeMutablePointer<CChar>?,
                                _ errorLen: Int) -> Int32 {
    #if canImport(FoundationModels)
    guard #available(macOS 26.0, *) else {
        writeString("Apple Foundation Models need macOS 26 or newer", to: error, length: errorLen)
        return kError
    }
    let instructionsText = instructions.map { String(cString: $0) } ?? ""
    let promptText = prompt.map { String(cString: $0) } ?? ""
    if promptText.isEmpty {
        writeString("Empty prompt", to: error, length: errorLen)
        return kError
    }
    // the C caller blocks on its worker thread; the async work runs in a task
    let done = DispatchSemaphore(value: 0)
    let box = GenerationBox(onPiece: onPiece, shouldCancel: shouldCancel, userData: userData)
    Task.detached(priority: .userInitiated) {
        defer { done.signal() }
        do {
            let session = instructionsText.isEmpty
                ? LanguageModelSession()
                : LanguageModelSession(instructions: instructionsText)
            var options = GenerationOptions()
            options.temperature = temperature
            if maxTokens > 0 { options.maximumResponseTokens = Int(maxTokens) }
            var previous = ""
            let stream = session.streamResponse(to: promptText, options: options)
            for try await snapshot in stream {
                if box.cancelled() { box.result = kCancelled; return }
                // every snapshot carries the whole text generated so far
                let full = snapshot.content
                if full.hasPrefix(previous) {
                    box.send(String(full.dropFirst(previous.count)))
                }
                else {
                    box.send(full)
                }
                previous = full
            }
        }
        catch let generationError as LanguageModelSession.GenerationError {
            box.result = kError
            switch generationError {
            case .exceededContextWindowSize:
                box.errorText = "The text is too long for the Apple Intelligence model (4096 tokens): select a shorter part"
            case .guardrailViolation:
                box.errorText = "Apple Intelligence declined this request (content guardrail)"
            case .unsupportedLanguageOrLocale:
                box.errorText = "Apple Intelligence does not support this language yet"
            case .assetsUnavailable:
                box.errorText = "The Apple Intelligence model is not available right now (still downloading?)"
            case .rateLimited:
                box.errorText = "Apple Intelligence is rate limited right now, try again in a moment"
            default:
                box.errorText = "Apple Intelligence: \(generationError.localizedDescription)"
            }
        }
        catch {
            box.result = kError
            box.errorText = "Apple Intelligence: \(error.localizedDescription)"
        }
    }
    done.wait()
    if box.result == kError { writeString(box.errorText, to: error, length: errorLen) }
    return box.result
    #else
    writeString("This build was made without the FoundationModels SDK (Xcode 26 is required)", to: error, length: errorLen)
    return kError
    #endif
}
