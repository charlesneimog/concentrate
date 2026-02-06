import * as Utils from "../utils/helpers.js";
import * as API from "../api/client.js";

export class StateManager {
    constructor(app) {
        this.app = app;

        // Expose utils on the app instance so existing method bodies
        // can keep calling `this.escapeHtml(...)`, etc.
        Object.assign(app, Utils);

        const methods = [
            // view + lifecycle
            "setView",
            "isPageActive",
            "refreshFocusOnly",
            "refreshStatsOnly",
            "refreshAll",
            "refreshEverything",
            "handleFocus",
            "handleBlur",
            "handleKeyDown",
            "initHistoryFilters",
            "setupEventListeners",
            "startPolling",
            "updateAppVersion",
            "init",

            // settings/monitoring
            "setMonitoringEnabled",
            "loadSettings",
            "loadEvents",
            "loadHistory",
            "updateServerFocusRules",

            // focus/task state
            "setCurrentTaskId",
            "normalizeAllowList",
            "isFocusAllowed",

            // status rendering
            "renderCurrentStatus",
            "updateFocusWarning",

            // timer + audio
            "setActivityIndicator",
            "ensureAudioContext",
            "playTimerEndSound",
            "parseDurationInput",
            "updateTimerDisplay",
            "stopTimer",
            "startTimer",

            // duration modal
            "openDurationModal",
            "closeDurationModal",
            "saveDurationModal",
        ];

        methods.forEach((name) => {
            app[name] = this[name].bind(app);
        });

        // Bind handlers (same behavior as before)
        app.refreshFocusOnly = app.refreshFocusOnly.bind(app);
        app.refreshAll = app.refreshAll.bind(app);
        app.setView = app.setView.bind(app);
        app.handleKeyDown = app.handleKeyDown.bind(app);
        app.handleFocus = app.handleFocus.bind(app);
        app.handleBlur = app.handleBlur.bind(app);

        app.startTimer = app.startTimer.bind(app);
        app.stopTimer = app.stopTimer.bind(app);
        app.openDurationModal = app.openDurationModal.bind(app);
        app.closeDurationModal = app.closeDurationModal.bind(app);
        app.saveDurationModal = app.saveDurationModal.bind(app);
    }

    // ==================== VIEW MANAGEMENT ====================
    setView(view) {
        this.currentView = view;

        const tasksView = document.getElementById("view-tasks");
        const historyView = document.getElementById("view-history");
        const pomodoroView = document.getElementById("view-pomodoro");

        const tasksAside = document.getElementById("tasks-aside");
        const historyAside = document.getElementById("history-aside");

        if (tasksView) tasksView.classList.toggle("hidden", view !== "tasks");
        if (historyView) historyView.classList.toggle("hidden", view !== "history");
        if (pomodoroView) pomodoroView.classList.toggle("hidden", view !== "pomodoro");

        if (tasksAside) {
            tasksAside.classList.toggle("lg:flex", view === "tasks");
            tasksAside.classList.toggle("hidden", view !== "tasks");
        }
        if (historyAside) {
            historyAside.classList.toggle("lg:flex", view === "history");
            historyAside.classList.toggle("hidden", view !== "history");
        }

        document.querySelectorAll(".nav-link[data-view]").forEach((link) => {
            const isActive = link.dataset.view === view;

            link.classList.remove(
                "bg-primary/10",
                "dark:bg-primary-dark/10",
                "text-primary",
                "dark:text-primary-dark",
                "font-semibold",
                "text-gray-500",
                "dark:text-gray-400",
                "hover:bg-gray-50",
                "dark:hover:bg-gray-700",
                "hover:text-gray-900",
                "dark:hover:text-white",
                "hover:bg-primary/20",
                "dark:hover:bg-primary-dark/20",
                "hover:text-primary",
                "dark:hover:text-primary-dark",
            );

            if (isActive) {
                link.classList.add(
                    "bg-primary/10",
                    "dark:bg-primary-dark/10",
                    "text-primary",
                    "dark:text-primary-dark",
                    "font-semibold",
                    "hover:bg-primary/20",
                    "dark:hover:bg-primary-dark/20",
                    "hover:text-primary",
                    "dark:hover:text-primary-dark",
                );
            } else {
                link.classList.add(
                    "text-gray-500",
                    "dark:text-gray-400",
                    "hover:bg-gray-50",
                    "dark:hover:bg-gray-700",
                    "hover:text-gray-900",
                    "dark:hover:text-white",
                );
            }
        });

        if (view === "tasks") {
            this.refreshAll();
        } else if (view === "history") {
            this.refreshAll();
        } else if (view === "pomodoro") {
            this.updatePomodoroDisplay();
            this.restorePomodoroFromServer();
            this.refreshPomodoroTodayStats();
        }

        let pageTitle = "Focus Dashboard";
        if (view === "tasks") pageTitle = "Focus Dashboard - Tasks";
        else if (view === "pomodoro") pageTitle = "Focus Dashboard - Pomodoro";
        else if (view === "history") pageTitle = "Focus Dashboard - History";

        document.title = pageTitle;
    }

    isPageActive() {
        return document.visibilityState === "visible" && document.hasFocus();
    }

    // ==================== CORE REFRESH ====================
    async refreshFocusOnly() {
        if (!this.isPageActive()) return;
        const current = await API.loadCurrent();
        this.lastCurrentFocus = current;
        this.renderCurrentStatus(current);
        this.updateFocusWarning(current, this.lastTasks);
    }

    async refreshStatsOnly() {
        if (!this.isPageActive()) return;
        if (this.currentView === "tasks" || this.currentView === "history") {
            await this.updateDailyFocus();
            await this.updateMonitoringSummary();
            await this.updateTasksCategories();
        }
    }

    async refreshAll() {
        if (!this.isPageActive()) return;

        if (this.currentView === "history") {
            const [tasks, current] = await Promise.all([this.loadTasks(), API.loadCurrent()]);
            this.lastTasks = Array.isArray(tasks) ? tasks : [];
            this.lastCurrentFocus = current;
            this.renderHistory();
            return;
        }

        const tasks = await this.loadTasks();
        const current = await API.loadCurrent();

        this.lastTasks = Array.isArray(tasks) ? tasks : [];
        this.lastCurrentFocus = current;

        this.renderTasks(tasks);
        this.updateAnytypeWarning();
        this.renderCurrentStatus(current);
        this.renderCurrentTask(tasks);
        this.updateFocusWarning(current, tasks);

        await this.updateDailyFocus();
        await this.updateMonitoringSummary();
        await this.updateDailyActivities();
        await this.updateTasksCategories();
    }

    async refreshEverything() {
        if (!this.isPageActive()) return;
        const tasks = await this.loadTasks();
        const current = await API.loadCurrent();

        this.lastTasks = Array.isArray(tasks) ? tasks : [];
        this.lastCurrentFocus = current;

        this.renderTasks(tasks);
        this.updateAnytypeWarning();
        this.renderCurrentStatus(current);
        this.renderCurrentTask(tasks);
        this.updateFocusWarning(current, tasks);

        await this.updateDailyFocus();
        await this.updateMonitoringSummary();
        this.renderHistory();
        this.updateDailyActivities();
    }

    // ==================== SETTINGS / MONITORING ====================
    async setMonitoringEnabled(enabled) {
        const desired = !!enabled;
        if (this.monitoringEnabled === desired) return;

        try {
            await API.setMonitoringEnabled(desired);

            this.monitoringEnabled = desired;
            const toggle = document.getElementById("monitoring-toggle");
            if (toggle) toggle.checked = this.monitoringEnabled;

            this.renderCurrentStatus(this.lastCurrentFocus);
        } catch (err) {
            console.error("Failed to toggle monitoring", err);
            const toggle = document.getElementById("monitoring-toggle");
            if (toggle) toggle.checked = this.monitoringEnabled;
        }
    }

    async loadSettings() {
        try {
            const data = await API.loadSettings();
            if (!data) return;

            this.monitoringEnabled = data.monitoring_enabled;
            const toggle = document.getElementById("monitoring-toggle");
            if (toggle) {
                toggle.checked = this.monitoringEnabled;
            }
            if (data.current_task_id) {
                this.setCurrentTaskId(data.current_task_id);
            }
        } catch (err) {
            console.error("Failed to load settings", err);
        }
    }

    async loadEvents() {
        return await API.loadEvents();
    }

    async loadHistory() {
        return await API.loadHistoryRaw();
    }

    async updateServerFocusRules(task) {
        try {
            await API.updateServerFocusRules(task);
        } catch (err) {
            console.warn("Failed to update focus rules", err);
        }
    }

    // ==================== TASK / FOCUS STATE ====================
    async setCurrentTaskId(taskId) {
        this.currentTaskId = taskId ? String(taskId) : null;

        if (this.currentTaskId) {
            localStorage.setItem("currentTaskId", this.currentTaskId);
        } else {
            localStorage.removeItem("currentTaskId");
        }

        try {
            if (this.currentTaskId) {
                const response = await API.setCurrentTaskOnServer(this.currentTaskId);
                if (!response.ok) {
                    const text = await response.text();
                    console.error("[CLIENT] Failed to set current task:", text);
                }
            }
        } catch (err) {
            console.error("[CLIENT] Error sending task to server:", err);
        }

        this.renderTasks(this.lastTasks);
        this.renderCurrentTask(this.lastTasks);
        this.updateFocusWarning(null, this.lastTasks);
    }

    normalizeAllowList(list) {
        return (Array.isArray(list) ? list : []).map((item) => String(item || "").trim()).filter(Boolean);
    }

    isFocusAllowed(current, task) {
        if (!current || !task) return true;
        const appId = String(current.app_id || "").trim();
        const title = String(current.title || "").trim();

        const allowedApps = this.normalizeAllowList(task.allowed_app_ids);
        const allowedTitles = this.normalizeAllowList(task.allowed_titles);

        const appMatch =
            allowedApps.length > 0 && allowedApps.some((allowed) => allowed.toLowerCase() === appId.toLowerCase());
        const titleMatch =
            allowedTitles.length > 0 &&
            allowedTitles.some((allowed) => title.toLowerCase().includes(allowed.toLowerCase()));

        if (!allowedApps.length && !allowedTitles.length) return true;
        if (allowedApps.length && allowedTitles.length) return appMatch || titleMatch;
        if (allowedApps.length) return appMatch;
        return titleMatch;
    }

    // ==================== STATUS RENDERING ====================
    renderCurrentStatus(current) {
        const status = document.getElementById("current-status");
        if (!status) return;
        const statusText = document.getElementById("current-status-text");

        const activeDot = document.getElementById("activity-indicator-active");
        const idleDot = document.getElementById("activity-indicator-idle");

        if (!this.monitoringEnabled) {
            if (statusText) {
                statusText.textContent = "Not Monitoring Activities";
            } else {
                status.textContent = "Not Monitoring Activities";
            }
            if (activeDot) activeDot.classList.toggle("hidden", true);
            if (idleDot) idleDot.classList.toggle("hidden", false);
            return;
        }

        if (!current) {
            if (statusText) {
                statusText.textContent = "Idle";
            } else {
                status.textContent = "Idle";
            }
            if (activeDot) activeDot.classList.toggle("hidden", true);
            if (idleDot) idleDot.classList.toggle("hidden", false);
            return;
        }

        if (activeDot) activeDot.classList.toggle("hidden", false);
        if (idleDot) idleDot.classList.toggle("hidden", true);
        const app = current.app_id || "(unknown)";
        const title = current.title || "(untitled)";
        if (statusText) {
            statusText.textContent = `${app} — ${title}`;
        } else {
            status.textContent = `${app} — ${title}`;
        }
    }

    updateFocusWarning(current, tasks) {
        const warning = document.getElementById("focus-warning");
        if (!warning) return;
        const activeDot = document.getElementById("activity-indicator-active");
        const activePing = activeDot?.querySelector(".animate-ping");
        const activeCore = activeDot?.querySelector(".relative.inline-flex");
        const task = Array.isArray(tasks) ? tasks.find((t) => String(t.id) === String(this.currentTaskId)) : null;
        if (!current || !task) {
            warning.classList.add("hidden");
            if (activePing) {
                activePing.classList.remove("bg-red-400");
                activePing.classList.add("bg-emerald-400");
            }
            if (activeCore) {
                activeCore.classList.remove("bg-red-500");
                activeCore.classList.add("bg-emerald-500");
            }
            this.lastFocusWarningKey = "";
            this.amIFocused = true;
            return;
        }

        const allowed = this.isFocusAllowed(current, task);
        if (allowed) {
            warning.textContent = "Focused";
            warning.classList.remove("hidden");
            warning.classList.remove(
                "bg-rose-100",
                "text-rose-700",
                "dark:bg-rose-900/40",
                "dark:text-rose-300",
                "bg-red-100",
                "text-red-700",
                "dark:bg-red-900/40",
                "dark:text-red-300",
            );
            warning.classList.add(
                "bg-emerald-100",
                "text-emerald-700",
                "dark:bg-emerald-900/40",
                "dark:text-emerald-300",
            );
            if (activePing) {
                activePing.classList.remove("bg-red-400");
                activePing.classList.add("bg-emerald-400");
            }
            if (activeCore) {
                activeCore.classList.remove("bg-red-500");
                activeCore.classList.add("bg-emerald-500");
            }
            this.lastFocusWarningKey = "";
            this.amIFocused = true;
            return;
        }

        warning.classList.remove("hidden");
        warning.textContent = "Not focused";
        warning.classList.remove(
            "bg-emerald-100",
            "text-emerald-700",
            "dark:bg-emerald-900/40",
            "dark:text-emerald-300",
        );
        warning.classList.add("bg-red-100", "text-red-700", "dark:bg-red-900/40", "dark:text-red-300");
        if (activePing) {
            activePing.classList.remove("bg-emerald-400");
            activePing.classList.add("bg-red-400");
        }
        if (activeCore) {
            activeCore.classList.remove("bg-emerald-500");
            activeCore.classList.add("bg-red-500");
        }
        this.amIFocused = false;
    }

    // ==================== TIMER METHODS ====================
    setActivityIndicator(isActive) {
        const activeDot = document.getElementById("activity-indicator-active");
        const idleDot = document.getElementById("activity-indicator-idle");
        if (activeDot) activeDot.classList.toggle("hidden", !isActive);
        if (idleDot) idleDot.classList.toggle("hidden", isActive);
    }

    ensureAudioContext() {
        if (!this.audioContext) {
            const Ctx = window.AudioContext || window.webkitAudioContext;
            if (!Ctx) return null;
            this.audioContext = new Ctx();
        }
        if (this.audioContext.state === "suspended") {
            this.audioContext.resume().catch(() => {});
        }
        return this.audioContext;
    }

    playTimerEndSound() {
        const ctx = this.ensureAudioContext();
        if (!ctx) return;

        const now = ctx.currentTime;
        const duration = 0.9;

        const oscillator = ctx.createOscillator();
        const gain = ctx.createGain();
        const compressor = ctx.createDynamicsCompressor();

        oscillator.type = "sine";
        oscillator.frequency.setValueAtTime(880, now);

        gain.gain.setValueAtTime(0.0001, now);
        gain.gain.exponentialRampToValueAtTime(1.2, now + 0.02);
        gain.gain.exponentialRampToValueAtTime(0.0001, now + duration);

        compressor.threshold.setValueAtTime(-24, now);
        compressor.knee.setValueAtTime(30, now);
        compressor.ratio.setValueAtTime(12, now);
        compressor.attack.setValueAtTime(0.003, now);
        compressor.release.setValueAtTime(0.25, now);

        oscillator.connect(gain);
        gain.connect(compressor);
        compressor.connect(ctx.destination);

        oscillator.start(now);
        oscillator.stop(now + duration);
    }

    parseDurationInput(value) {
        const v = String(value || "")
            .trim()
            .toLowerCase();
        if (!v) return 0;
        const match = v.match(/(\d+(?:\.\d+)?)([hms])?/);
        if (!match) return 0;
        const num = parseFloat(match[1]);
        const unit = match[2] || "m";
        if (unit === "h") return Math.round(num * 3600);
        if (unit === "s") return Math.round(num);
        return Math.round(num * 60);
    }

    updateTimerDisplay(seconds) {
        const hrs = document.getElementById("timer-hrs");
        const min = document.getElementById("timer-min");
        const sec = document.getElementById("timer-sec");
        const h = Math.floor(seconds / 3600);
        const m = Math.floor((seconds % 3600) / 60);
        const s = Math.floor(seconds % 60);
        if (hrs) hrs.textContent = String(h).padStart(2, "0");
        if (min) min.textContent = String(m).padStart(2, "0");
        if (sec) sec.textContent = String(s).padStart(2, "0");
    }

    stopTimer() {
        this.timerRunning = false;
        if (this.timerInterval) {
            clearInterval(this.timerInterval);
            this.timerInterval = null;
        }
        this.setActivityIndicator(false);
        const button = document.getElementById("record-toggle");
        if (button) {
            const icon = button.querySelector(".material-symbols-outlined");
            const label = button.querySelector("span:not(.material-symbols-outlined)");
            if (icon) icon.textContent = "play_arrow";
            if (label) label.textContent = "Start Focus";
        }
    }

    startTimer(seconds) {
        this.timerSeconds = Math.max(0, seconds);
        this.updateTimerDisplay(this.timerSeconds);
        this.stopTimer();
        if (this.timerSeconds <= 0) return;
        this.timerRunning = true;
        this.setActivityIndicator(true);
        const button = document.getElementById("record-toggle");
        if (button) {
            const icon = button.querySelector(".material-symbols-outlined");
            const label = button.querySelector("span:not(.material-symbols-outlined)");
            if (icon) icon.textContent = "pause";
            if (label) label.textContent = "Pause";
        }
        this.timerInterval = setInterval(() => {
            if (!this.amIFocused) return;
            if (!this.timerRunning) return;
            this.timerSeconds = Math.max(0, this.timerSeconds - 1);
            this.updateTimerDisplay(this.timerSeconds);
            if (this.timerSeconds <= 0) {
                this.playTimerEndSound();
                this.stopTimer();
                const status = document.getElementById("current-status");
                if (status) status.textContent = "Time's up";
            }
        }, 1000);
    }

    // ==================== DURATION MODAL ====================
    openDurationModal() {
        const modal = document.getElementById("duration-modal");
        const input = document.getElementById("duration-modal-input");
        if (input) input.value = this.currentDurationValue || "25m";
        if (modal) {
            modal.classList.remove("hidden");
            modal.classList.add("flex");
            modal.setAttribute("aria-hidden", "false");
        }
        setTimeout(() => input?.focus(), 0);
    }

    closeDurationModal() {
        const modal = document.getElementById("duration-modal");
        if (modal) {
            modal.classList.add("hidden");
            modal.classList.remove("flex");
            modal.setAttribute("aria-hidden", "true");
        }
    }

    saveDurationModal() {
        const input = document.getElementById("duration-modal-input");
        const value = input?.value || "";
        this.currentDurationValue = value || "25m";
        if (!this.timerRunning) {
            const seconds = this.parseDurationInput(this.currentDurationValue);
            this.updateTimerDisplay(seconds);
        }
        this.closeDurationModal();
    }

    // ==================== EVENT HANDLERS ====================
    handleFocus() {
        if (!this.wasFocused) {
            this.refreshAll();
        }
        this.wasFocused = true;
    }

    handleBlur() {
        this.wasFocused = false;
    }

    handleKeyDown(event) {
        if (event.key === "Enter") {
            event.preventDefault();
            this.submitTask();
        }
    }

    initHistoryFilters() {
        // Delegated to HistoryManager if present
        if (typeof this.historyManagerInitHistoryFilters === "function") {
            return this.historyManagerInitHistoryFilters();
        }
        if (
            typeof this.initHistoryFilters === "function" &&
            this.initHistoryFilters !== StateManager.prototype.initHistoryFilters
        ) {
            // already attached by history module
            return this.initHistoryFilters();
        }

        // fallback: keep same defaults
        this.historyDays = 30;
    }

    // ==================== INITIALIZATION ====================
    setupEventListeners() {
        document.getElementById("task-submit")?.addEventListener("click", this.submitTask);
        document.getElementById("task-name")?.addEventListener("keydown", this.handleKeyDown);

        document.getElementById("tasks-container")?.addEventListener("click", async (event) => {
            const button = event.target.closest("button[data-task-id]");
            if (!button) return;

            event.stopPropagation();
            const taskId = button.dataset.taskId;
            if (!taskId) return;

            const task = Array.isArray(this.lastTasks)
                ? this.lastTasks.find((t) => String(t.id) === String(taskId))
                : null;

            if (!task) return;

            const isCurrentlySelected = this.currentTaskId && String(taskId) === String(this.currentTaskId);

            document.querySelectorAll(".task-markdown-container").forEach((container) => {
                container.remove();
            });

            if (isCurrentlySelected) {
                await this.setCurrentTaskId(null);
                await this.updateServerFocusRules(null);
            } else {
                await this.setCurrentTaskId(taskId);
                await this.updateServerFocusRules(task);
                setTimeout(() => {
                    this.renderTaskMarkdown(task);
                }, 0);
            }

            try {
                const res = await API.setCurrentTaskOnServer(taskId);
                if (!res.ok) {
                    const text = await res.text();
                    console.warn("Failed to update current task on server:", text);
                }
            } catch (error) {
                console.error("Error updating current task on server:", error);
            }
        });

        document.querySelectorAll(".nav-link[data-view]").forEach((link) => {
            link.addEventListener("click", (event) => {
                event.preventDefault();
                const view = link.dataset.view || "tasks";
                this.setView(view);
            });
        });

        document.getElementById("history-filter-month")?.addEventListener("click", () => {
            window.historyFilterMode = "month";
            this.refreshAll();
        });

        document.getElementById("history-export")?.addEventListener("click", async () => {
            if (typeof this.exportHistory === "function") {
                return this.exportHistory();
            }
            const history = await this.loadHistory();
            const blob = new Blob([JSON.stringify(history || [], null, 2)], { type: "application/json" });
            const url = URL.createObjectURL(blob);
            const link = document.createElement("a");
            link.href = url;
            link.download = "focus-history.json";
            document.body.appendChild(link);
            link.click();
            link.remove();
            URL.revokeObjectURL(url);
        });

        document.getElementById("duration-modal-close")?.addEventListener("click", this.closeDurationModal);
        document.getElementById("duration-modal-cancel")?.addEventListener("click", this.closeDurationModal);
        document.getElementById("duration-modal-save")?.addEventListener("click", this.saveDurationModal);

        document.getElementById("duration-modal")?.addEventListener("click", (event) => {
            if (event.target.id === "duration-modal") this.closeDurationModal();
        });

        document.getElementById("duration-modal-input")?.addEventListener("keydown", (event) => {
            if (event.key === "Enter") {
                event.preventDefault();
                this.saveDurationModal();
            }
            if (event.key === "Escape") {
                event.preventDefault();
                this.closeDurationModal();
            }
        });

        document.getElementById("anytype-config-open")?.addEventListener("click", this.openAnytypeConfigModal);
        const anytypeConfigButton = document.querySelector("button[data-anytype-config='open']");
        if (anytypeConfigButton && !document.getElementById("anytype-config-open")) {
            anytypeConfigButton.addEventListener("click", this.openAnytypeConfigModal);
        }

        document.getElementById("anytype-config-close")?.addEventListener("click", this.closeAnytypeConfigModal);
        document.getElementById("anytype-config-cancel")?.addEventListener("click", this.closeAnytypeConfigModal);
        document.getElementById("anytype-refresh-open")?.addEventListener("click", this.refreshAnytypeCache);
        document.getElementById("anytype-config-save")?.addEventListener("click", this.saveAnytypeConfig);
        document.getElementById("anytype-auth-challenge")?.addEventListener("click", this.createAnytypeChallenge);
        document.getElementById("anytype-auth-create")?.addEventListener("click", this.createAnytypeApiKey);
        document.getElementById("anytype-spaces-refresh")?.addEventListener("click", this.loadAnytypeSpaces);
        document.getElementById("anytype-space-save")?.addEventListener("click", this.saveAnytypeSpace);

        document.getElementById("anytype-config-modal")?.addEventListener("click", (event) => {
            if (event.target.id === "anytype-config-modal") this.closeAnytypeConfigModal();
        });

        this.initHistoryFilters();

        const anytypeSpaceIdInput = document.getElementById("anytype-space-id");
        if (anytypeSpaceIdInput) {
            anytypeSpaceIdInput.addEventListener("keydown", (event) => {
                if (event.key === "Enter") {
                    event.preventDefault();
                    this.saveAnytypeConfig();
                }
                if (event.key === "Escape") {
                    event.preventDefault();
                    this.closeAnytypeConfigModal();
                }
            });
        }

        const anytypeApiKeyInput = document.getElementById("anytype-api-key");
        if (anytypeApiKeyInput) {
            anytypeApiKeyInput.addEventListener("keydown", (event) => {
                if (event.key === "Enter") {
                    event.preventDefault();
                    this.saveAnytypeConfig();
                }
                if (event.key === "Escape") {
                    event.preventDefault();
                    this.closeAnytypeConfigModal();
                }
            });
        }

        document.querySelectorAll(".duration-preset").forEach((btn) => {
            btn.addEventListener("click", () => {
                const value = btn.getAttribute("data-value") || "";
                const input = document.getElementById("duration-modal-input");
                if (input) input.value = value;
            });
        });

        document.getElementById("monitoring-toggle")?.addEventListener("change", async (e) => {
            const enabled = e.target.checked;
            try {
                await API.setMonitoringEnabled(enabled);
                this.monitoringEnabled = enabled;
                this.renderCurrentStatus(this.lastCurrentFocus);
                await this.updateMonitoringSummary();
            } catch (err) {
                console.error("Failed to toggle monitoring", err);
                e.target.checked = this.monitoringEnabled;
            }
        });

        window.addEventListener("focus", this.handleFocus);
        window.addEventListener("blur", this.handleBlur);

        const openBtn = document.getElementById("daily-config-open");
        const modal = document.getElementById("daily-config-modal");
        const closeBtn = document.getElementById("daily-config-close");
        const saveBtn = document.getElementById("daily-config-save");
        const cancelBtn = document.getElementById("daily-config-cancel");

        if (openBtn && modal) {
            openBtn.onclick = () => {
                const nameEl = document.getElementById("daily-name");
                const appIdsEl = document.getElementById("daily-appids");
                const appTitlesEl = document.getElementById("daily-apptitles");
                const iconEl = document.getElementById("daily-icon");
                const colorEl = document.getElementById("daily-color");

                if (nameEl) nameEl.value = "";
                if (appIdsEl) appIdsEl.value = "";
                if (appTitlesEl) appTitlesEl.value = "";
                if (iconEl) iconEl.value = "";
                if (colorEl) colorEl.value = "";

                modal.classList.remove("hidden");
            };
        }

        if (closeBtn && modal) closeBtn.onclick = () => modal.classList.add("hidden");
        if (cancelBtn && modal) cancelBtn.onclick = () => modal.classList.add("hidden");

        if (saveBtn && modal) {
            saveBtn.onclick = async () => {
                const data = {
                    name: document.getElementById("daily-name").value.trim(),
                    appIds: document
                        .getElementById("daily-appids")
                        .value.split(";")
                        .map((s) => s.trim())
                        .filter(Boolean),
                    appTitles: document
                        .getElementById("daily-apptitles")
                        .value.split(";")
                        .map((s) => s.trim())
                        .filter(Boolean),
                    icon: document.getElementById("daily-icon").value.trim(),
                    color: document.getElementById("daily-color").value,
                };

                await API.saveRecurringTask(data);
                modal.classList.add("hidden");
            };
        }
    }

    startPolling() {
        setInterval(() => {
            const active = this.isPageActive();
            if (!active) {
                this.lastPageActive = false;
                return;
            }

            if (!this.lastPageActive) {
                this.lastPageActive = true;
                if (this.currentView === "tasks") {
                    this.refreshAll();
                }
                return;
            }

            if (this.currentView === "tasks") {
                this.refreshFocusOnly();
            }
        }, 1000);

        setInterval(() => {
            const active = this.isPageActive();
            if (!active) return;
            this.refreshStatsOnly();
        }, 60000);
    }

    async updateAppVersion() {
        const el = document.getElementById("concentrate-version");
        if (!el) return;

        try {
            const result = await API.loadVersion();
            if (result?.ok && result.version) {
                el.textContent = result.version;
            }
        } catch {
            // Ignore version fetch failures (UI stays blank)
        }
    }

    init() {
        this.setActivityIndicator(true);

        this.updateAppVersion();

        this.setupEventListeners();

        this.setupTokenInput(document.getElementById("meetingAppIds"));
        this.setupTokenInput(document.getElementById("meetingAppTitles"));
        this.setupTokenInput(document.getElementById("noteAppIds"));
        this.setupTokenInput(document.getElementById("noteAppTitles"));
        this.setupTokenInput(document.getElementById("emailAppIds"));
        this.setupTokenInput(document.getElementById("emailAppTitles"));

        this.setView("tasks");

        this.refreshEverything();

        this.loadSettings();

        this.startPolling();

        this.refreshStatsOnly();

        this.refreshAll();
    }
}
