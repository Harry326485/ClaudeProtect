// SPDX-License-Identifier: GPL-3.0-only
import AppKit
import WebKit

final class Controller: NSObject, NSApplicationDelegate, NSWindowDelegate, WKScriptMessageHandler, WKNavigationDelegate {
    private var item: NSStatusItem!
    private var startItem: NSMenuItem!
    private var stopItem: NSMenuItem!
    private var statusItem: NSMenuItem!
    private var window: NSWindow!
    private var webView: WKWebView!
    private var process: Process?
    private var timer: Timer?
    private var output: FileHandle?
    private var launchError: String?
    private var lastDetail = ""
    private var launchedAt = 0.0
    private var quitting = false
    private var quitAfterClose = false
    private var stopping = false
    private var testing = false
    private var dirty = false
    private var ready = false
    private var lastStatus = ""
    private var values: [String: Any] = [:]
    private var sessionValues: [String: Any] = [:]
    private let configurationQueue = DispatchQueue(label: "local.claudeprotect.settings", qos: .userInitiated)
    private var resources: URL { Bundle.main.resourceURL! }
    private var packaged: Bool { FileManager.default.fileExists(atPath: resources.appendingPathComponent("backend/supervisor.py").path) }
    private var build: URL {
        if packaged {
            return FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("ClaudeProtect", isDirectory: true)
        }
        return Bundle.main.bundleURL.deletingLastPathComponent()
    }
    private var backend: URL { packaged ? resources.appendingPathComponent("backend") : build.deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("macos") }
    private var python: URL {
        packaged ? resources.appendingPathComponent("python/bin/python3") : URL(fileURLWithPath: Bundle.main.object(forInfoDictionaryKey: "CPPython") as? String ?? "/usr/bin/python3")
    }
    private func configure(_ task: Process) {
        task.executableURL = python
        task.currentDirectoryURL = build
        var environment = ProcessInfo.processInfo.environment
        for key in environment.keys where key.hasPrefix("PYTHON") || key.hasPrefix("DYLD_") { environment.removeValue(forKey: key) }
        environment["CLAUDEPROTECT_DATA_DIR"] = build.path
        environment["CLAUDEPROTECT_RESOURCE_DIR"] = packaged ? resources.path : build.path
        environment["PYTHONNOUSERSITE"] = "1"
        task.environment = environment
    }
    private var panel: URL { Bundle.main.url(forResource: "panel", withExtension: "html")! }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        do {
            try FileManager.default.createDirectory(at: build, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        } catch {
            let alert = NSAlert(); alert.messageText = "无法创建 ClaudeProtect 资料目录"; alert.informativeText = error.localizedDescription; alert.runModal(); NSApp.terminate(nil); return
        }
        let mainMenu = NSMenu()
        let appItem = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "显示控制面板", action: #selector(showWindow), keyEquivalent: "0").target = self
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(withTitle: "退出 ClaudeProtect", action: #selector(quitApp), keyEquivalent: "q").target = self
        appItem.submenu = appMenu
        mainMenu.addItem(appItem)
        let editItem = NSMenuItem()
        let editMenu = NSMenu(title: "编辑")
        editMenu.addItem(withTitle: "撤销", action: Selector(("undo:")), keyEquivalent: "z")
        editMenu.addItem(NSMenuItem.separator())
        editMenu.addItem(withTitle: "剪切", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        editMenu.addItem(withTitle: "复制", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        editMenu.addItem(withTitle: "粘贴", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        editMenu.addItem(withTitle: "全选", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        editItem.submenu = editMenu
        mainMenu.addItem(editItem)
        NSApp.mainMenu = mainMenu

        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        item.button?.title = "CP"
        let menu = NSMenu()
        statusItem = menu.addItem(withTitle: "会话未运行", action: nil, keyEquivalent: "")
        menu.addItem(NSMenuItem.separator())
        menu.addItem(withTitle: "显示控制面板", action: #selector(showWindow), keyEquivalent: "").target = self
        startItem = menu.addItem(withTitle: "启动 Claude", action: #selector(start), keyEquivalent: "")
        stopItem = menu.addItem(withTitle: "停止会话", action: #selector(stop), keyEquivalent: "")
        let logs = menu.addItem(withTitle: "打开运行记录", action: #selector(openLogs), keyEquivalent: "")
        let docs = menu.addItem(withTitle: "适配说明", action: #selector(openDocs), keyEquivalent: "")
        menu.addItem(NSMenuItem.separator())
        let quit = menu.addItem(withTitle: "退出 ClaudeProtect", action: #selector(quitApp), keyEquivalent: "q")
        for entry in [startItem, stopItem, logs, docs, quit] { entry?.target = self }
        item.menu = menu
        createWindow()
        timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in self?.refresh() }
        refresh()
        showWindow()
    }

    private func createWindow() {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 880, height: 700), styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = "ClaudeProtect"
        window.minSize = NSSize(width: 580, height: 520)
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = .nonPersistent()
        configuration.preferences.javaScriptCanOpenWindowsAutomatically = false
        let bridge = """
        window.CP_PLATFORM = 'macos';
        (() => {
          const listeners = [];
          window.chrome = window.chrome || {};
          window.chrome.webview = {
            postMessage: message => window.webkit.messageHandlers.claudeProtect.postMessage(message),
            addEventListener: (type, listener) => { if (type === 'message') listeners.push(listener); }
          };
          window.__cpDeliver = message => { for (const listener of listeners) listener({data: message}); };
        })();
        """
        configuration.userContentController.addUserScript(WKUserScript(source: bridge, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        configuration.userContentController.add(self, name: "claudeProtect")
        webView = WKWebView(frame: .zero, configuration: configuration)
        webView.navigationDelegate = self
        webView.translatesAutoresizingMaskIntoConstraints = false
        let content = window.contentView!
        content.addSubview(webView)
        NSLayoutConstraint.activate([
            webView.leadingAnchor.constraint(equalTo: content.leadingAnchor), webView.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            webView.topAnchor.constraint(equalTo: content.topAnchor), webView.bottomAnchor.constraint(equalTo: content.bottomAnchor)
        ])
        webView.loadFileURL(panel, allowingReadAccessTo: panel.deletingLastPathComponent())
    }

    func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction, decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        let url = navigationAction.request.url
        decisionHandler(url?.standardizedFileURL == panel.standardizedFileURL ? .allow : .cancel)
    }

    private func deliver(_ message: [String: Any]) {
        guard let data = try? JSONSerialization.data(withJSONObject: message), let text = String(data: data, encoding: .utf8) else { return }
        webView.evaluateJavaScript("window.__cpDeliver(\(text));", completionHandler: nil)
    }

    private func reply(_ id: NSNumber, ok: Bool = true, data: [String: Any] = [:], error: String = "") {
        deliver(["type": "response", "id": id, "ok": ok, "data": data, "error": error])
    }

    func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
        guard message.webView === webView, message.frameInfo.isMainFrame,
              message.frameInfo.request.url?.standardizedFileURL == panel.standardizedFileURL,
              let body = message.body as? [String: Any], let action = body["action"] as? String else { return }
        if action == "dirty" { dirty = body["dirty"] as? Bool ?? false; return }
        if action == "closeCancelled" { quitAfterClose = false; return }
        guard let id = body["id"] as? NSNumber else { return }
        switch action {
        case "load", "save":
            configurationQueue.async { [weak self] in
                guard let self else { return }
                do {
                    var response = try self.configurationRequest(body)
                    DispatchQueue.main.async {
                        if response["ok"] as? Bool == true {
                            var data = response["data"] as? [String: Any] ?? [:]
                            self.values = data["values"] as? [String: Any] ?? self.values
                            self.dirty = false
                            if action == "load" { data["status"] = self.currentStatus(); data["view"] = "overview"; data["page"] = "general" }
                            response["data"] = data
                            self.ready = true
                        }
                        self.reply(id, ok: response["ok"] as? Bool ?? false, data: response["data"] as? [String: Any] ?? [:], error: response["error"] as? String ?? "")
                    }
                } catch {
                    DispatchQueue.main.async { self.reply(id, ok: false, error: error.localizedDescription) }
                }
            }
        case "start", "selftest":
            let accepted = launch(selftest: action == "selftest")
            reply(id, ok: accepted, error: accepted ? "" : launchError ?? "已有操作正在运行。")
        case "stop": stop(); reply(id)
        case "browse":
            guard body["key"] as? String == "executable" else { reply(id, ok: false, error: "无效的路径字段。"); return }
            let picker = NSOpenPanel()
            picker.canChooseFiles = true
            picker.canChooseDirectories = false
            picker.treatsFilePackagesAsDirectories = false
            picker.allowsMultipleSelection = false
            picker.message = "选择 Claude.app 或其中的可执行文件"
            picker.beginSheetModal(for: window) { result in self.reply(id, data: ["path": result == .OK ? picker.url?.path ?? "" : ""]) }
        case "close":
            dirty = false
            window.orderOut(nil)
            reply(id)
            if quitAfterClose { quitAfterClose = false; NSApp.terminate(nil) }
        case "openLogs": openLogs(); reply(id)
        case "openDocs": openDocs(); reply(id)
        default: reply(id, ok: false, error: "无法识别的界面操作。")
        }
    }

    private func configurationRequest(_ request: [String: Any]) throws -> [String: Any] {
        let task = Process()
        configure(task)
        task.arguments = ["-I", "-B", backend.appendingPathComponent("runner.py").path, "config_service"]
        let input = Pipe(), output = Pipe(), errors = Pipe()
        task.standardInput = input; task.standardOutput = output; task.standardError = errors
        try task.run()
        try input.fileHandleForWriting.write(contentsOf: JSONSerialization.data(withJSONObject: request))
        try input.fileHandleForWriting.close()
        let data = output.fileHandleForReading.readDataToEndOfFile()
        task.waitUntilExit()
        guard task.terminationStatus == 0, let value = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw NSError(domain: "ClaudeProtect", code: 2, userInfo: [NSLocalizedDescriptionKey: "配置服务无法读取设置，请检查程序路径或重新构建。"])
        }
        return value
    }

    @objc func showWindow() {
        window.deminiaturize(nil)
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool { showWindow(); return true }
    func windowShouldClose(_ sender: NSWindow) -> Bool {
        if dirty { deliver(["type": "closeRequested"]); return false }
        return true
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if dirty {
            quitAfterClose = true
            showWindow()
            deliver(["type": "closeRequested"])
            return .terminateCancel
        }
        if process != nil { quitting = true; stop(); return .terminateLater }
        return .terminateNow
    }

    @objc func start() {
        if ready { showWindow(); webView.evaluateJavaScript("void run('start');", completionHandler: nil) }
        else { _ = launch(selftest: false) }
    }
    private func launch(selftest: Bool) -> Bool {
        guard process == nil else { return false }
        launchError = nil; lastDetail = ""; launchedAt = Date().timeIntervalSince1970
        testing = selftest; sessionValues = values
        do {
            let log = build.appendingPathComponent(selftest ? "selftest-console.log" : "menu-console.log")
            FileManager.default.createFile(atPath: log.path, contents: nil)
            output = try FileHandle(forWritingTo: log)
            let task = Process()
            configure(task)
            task.arguments = ["-I", "-B", backend.appendingPathComponent("runner.py").path, "supervisor", "--owner-pid", String(ProcessInfo.processInfo.processIdentifier)] + (selftest ? ["--selftest"] : [])
            task.standardOutput = output; task.standardError = output
            task.terminationHandler = { [weak self] task in
                DispatchQueue.main.async {
                    guard let self else { return }
                    _ = self.currentStatus()
                    try? self.output?.close(); self.output = nil
                    self.process = nil; self.stopping = false; self.testing = false
                    if task.terminationStatus != 0 { self.launchError = self.lastDetail.isEmpty ? "启动或验证失败，请打开运行记录。" : self.lastDetail }
                    self.refresh()
                    if selftest { self.deliver(["type": "notice", "error": task.terminationStatus != 0, "text": self.launchError ?? self.lastDetail]) }
                    if self.quitting { NSApp.reply(toApplicationShouldTerminate: true) }
                }
            }
            try task.run(); process = task
        } catch {
            launchError = error.localizedDescription
            try? output?.close(); output = nil; testing = false
            refresh()
            return false
        }
        refresh()
        return true
    }

    @objc func stop() { if process?.isRunning == true { stopping = true; process?.terminate(); refresh() } }
    @objc func openLogs() { NSWorkspace.shared.open(build) }
    @objc func openDocs() { NSWorkspace.shared.open(backend.appendingPathComponent("README.md")) }
    @objc func quitApp() { NSApp.terminate(nil) }

    private func currentStatus() -> [String: Any] {
        let running = process != nil
        var source = running ? sessionValues : values
        var state = launchError == nil ? (running ? "preparing" : "stopped") : "failed"
        var detail = launchError ?? (running ? "正在准备运行环境…" : lastDetail)
        var count = 0
        if launchedAt > 0, let data = try? Data(contentsOf: build.appendingPathComponent("status.json")),
           let status = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
           let timestamp = status["timestamp"] as? Double, timestamp >= launchedAt {
            detail = status["detail"] as? String ?? detail
            lastDetail = detail
            if running { state = status["state"] as? String ?? state; source = status["values"] as? [String: Any] ?? source; count = status["processes"] as? Int ?? 0 }
        }
        if stopping { detail = "正在停止会话…"; state = "preparing" }
        return ["active": running && !testing, "busy": testing, "busyLabel": detail, "processCount": count,
                "timezone": source["timezone"] as? String ?? "America/Los_Angeles", "locale": source["locale"] as? String ?? "en-US",
                "verify": source["verifyinjection"] as? String ?? "strict", "detail": detail, "state": state]
    }

    private func refresh() {
        let status = currentStatus()
        startItem.isEnabled = process == nil
        stopItem.isEnabled = process != nil && !stopping
        statusItem.title = status["busyLabel"] as? String ?? "会话未运行"
        if statusItem.title.isEmpty { statusItem.title = "会话未运行" }
        item.button?.title = process == nil ? "CP" : (status["state"] as? String == "running" ? "CP ●" : "CP …")
        if ready, let data = try? JSONSerialization.data(withJSONObject: status, options: [.sortedKeys]), let key = String(data: data, encoding: .utf8), key != lastStatus {
            lastStatus = key
            deliver(["type": "status", "data": status])
        }
    }
}

let application = NSApplication.shared
let controller = Controller()
application.delegate = controller
application.run()
