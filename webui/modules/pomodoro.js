import * as API from "../api/client.js";

export class PomodoroManager {
    constructor(app) {
        this.app = app;
        const methods = [
            "pomodoroPhaseFromStep",
            "pomodoroStepFromPhase",
            "pomodoroModeFromStep",
            "pomodoroStepFromModePreference",
            "getCurrentPomodoroStepDuration",
            "loadPomodoroStateFromServer",
            "savePomodoroStateToServer",
            "refreshPomodoroTodayStats",
            "incrementPomodoroFocusComplete",
            "initPomodoro",
            "restorePomodoroFromServer",
            "setPomodoroMode",
            "togglePomodoro",
            "startPomodoro",
            "pausePomodoro",
            "resetPomodoro",
            "pomodoroComplete",
            "updatePomodoroDisplay",
            "getCurrentModeDuration",
            "showPomodoroNotification",
        ];
        methods.forEach((name) => {
            app[name] = this[name].bind(app);
        });
    }

    pomodoroPhaseFromStep(step) {
        switch (Number(step) || 0) {
            case 0:
                return "focus-1";
            case 1:
                return "short-break-1";
            case 2:
                return "focus-2";
            case 3:
                return "short-break-2";
            case 4:
                return "long-break";
            default:
                return "focus-1";
        }
    }

    pomodoroStepFromPhase(phase) {
        const p = String(phase || "").toLowerCase();
        if (p === "focus-1") return 0;
        if (p === "short-break-1") return 1;
        if (p === "focus-2") return 2;
        if (p === "short-break-2") return 3;
        if (p === "long-break") return 4;
        if (p === "focus") return 0;
        if (p === "short-break") return 1;
        if (p === "long-break") return 4;
        return 0;
    }

    pomodoroModeFromStep(step) {
        const s = Number(step) || 0;
        if (s === 0 || s === 2) return "focus";
        if (s === 1 || s === 3) return "short-break";
        return "long-break";
    }

    pomodoroStepFromModePreference(mode, currentStep) {
        const m = String(mode || "focus");
        const s = Number(currentStep) || 0;
        if (m === "focus") {
            if (s === 2 || s === 3) return 2;
            return 0;
        }
        if (m === "short-break") {
            if (s === 2 || s === 3) return 3;
            return 1;
        }
        return 4;
    }

    getCurrentPomodoroStepDuration() {
        const step = Number(this.pomodoro.cycleStep) || 0;
        if (step === 0 || step === 2) return this.pomodoro.focusDuration;
        if (step === 1 || step === 3) return this.pomodoro.shortBreakDuration;
        return this.pomodoro.longBreakDuration;
    }

    async loadPomodoroStateFromServer() {
        try {
            return await API.loadPomodoroState();
        } catch (e) {
            console.warn("Failed to load pomodoro state", e);
            return null;
        }
    }

    async savePomodoroStateToServer() {
        try {
            const nowSec = Math.floor(Date.now() / 1000);
            const payload = {
                phase: this.pomodoroPhaseFromStep(this.pomodoro.cycleStep),
                cycle_step: Number(this.pomodoro.cycleStep) || 0,
                is_running: !!this.pomodoro.isRunning,
                is_paused: !!this.pomodoro.isPaused,
                time_left: Math.max(0, Number(this.pomodoro.timeLeft) || 0),
                focus_duration: Math.max(0, Number(this.pomodoro.focusDuration) || 0),
                short_break_duration: Math.max(0, Number(this.pomodoro.shortBreakDuration) || 0),
                long_break_duration: Math.max(0, Number(this.pomodoro.longBreakDuration) || 0),
                auto_start_breaks: !!this.pomodoro.autoStartBreaks,
                updated_at: nowSec,
            };

            await API.savePomodoroState(payload);
            this.pomodoro.lastSavedAt = Date.now();
        } catch (e) {
            console.warn("Failed to save pomodoro state", e);
        }
    }

    async refreshPomodoroTodayStats() {
        const sessionsEl = document.getElementById("pomodoro-sessions-today");
        const totalEl = document.getElementById("pomodoro-total-focus");
        if (!sessionsEl && !totalEl) return;
        try {
            const data = await API.loadPomodoroToday();
            if (!data) return;
            const sessions = Number(data?.focus_sessions ?? 0);
            const minutes = Number(data?.focus_minutes ?? Math.round(Number(data?.focus_seconds ?? 0) / 60));
            if (sessionsEl) sessionsEl.textContent = String(sessions);
            if (totalEl) totalEl.textContent = `${minutes} min`;
        } catch (e) {
            console.warn("Failed to load pomodoro today stats", e);
        }
    }

    async incrementPomodoroFocusComplete(focusSeconds) {
        try {
            const data = await API.incrementPomodoroFocusComplete(focusSeconds);
            if (data) {
                const sessionsEl = document.getElementById("pomodoro-sessions-today");
                const totalEl = document.getElementById("pomodoro-total-focus");
                const sessions = Number(data?.focus_sessions ?? 0);
                const minutes = Number(data?.focus_minutes ?? Math.round(Number(data?.focus_seconds ?? 0) / 60));
                if (sessionsEl) sessionsEl.textContent = String(sessions);
                if (totalEl) totalEl.textContent = `${minutes} min`;
            } else {
                await this.refreshPomodoroTodayStats();
            }
        } catch (e) {
            console.warn("Failed to increment pomodoro focus", e);
        }
    }

    initPomodoro() {
        document.getElementById("pomodoro-focus-mode")?.addEventListener("click", () => this.setPomodoroMode("focus"));
        document
            .getElementById("pomodoro-short-break-mode")
            ?.addEventListener("click", () => this.setPomodoroMode("short-break"));
        document
            .getElementById("pomodoro-long-break-mode")
            ?.addEventListener("click", () => this.setPomodoroMode("long-break"));

        document.getElementById("pomodoro-start")?.addEventListener("click", () => this.togglePomodoro());
        document.getElementById("pomodoro-pause")?.addEventListener("click", () => this.pausePomodoro());
        document.getElementById("pomodoro-reset")?.addEventListener("click", () => this.resetPomodoro());

        document.getElementById("focus-duration-slider")?.addEventListener("input", (e) => {
            const minutes = parseInt(e.target.value);
            document.getElementById("focus-duration-value").textContent = `${minutes} min`;
            this.pomodoro.focusDuration = minutes * 60;
            if (this.pomodoro.mode === "focus" && !this.pomodoro.isRunning) {
                this.pomodoro.timeLeft = minutes * 60;
                this.updatePomodoroDisplay();
            }
        });

        document.getElementById("short-break-slider")?.addEventListener("input", (e) => {
            const minutes = parseInt(e.target.value);
            document.getElementById("short-break-value").textContent = `${minutes} min`;
            this.pomodoro.shortBreakDuration = minutes * 60;
            if (this.pomodoro.mode === "short-break" && !this.pomodoro.isRunning) {
                this.pomodoro.timeLeft = minutes * 60;
                this.updatePomodoroDisplay();
            }
        });

        document.getElementById("long-break-slider")?.addEventListener("input", (e) => {
            const minutes = parseInt(e.target.value);
            document.getElementById("long-break-value").textContent = `${minutes} min`;
            this.pomodoro.longBreakDuration = minutes * 60;
            if (this.pomodoro.mode === "long-break" && !this.pomodoro.isRunning) {
                this.pomodoro.timeLeft = minutes * 60;
                this.updatePomodoroDisplay();
            }
        });

        document.getElementById("auto-start-breaks")?.addEventListener("change", (e) => {
            this.pomodoro.autoStartBreaks = e.target.checked;
            this.savePomodoroStateToServer();
        });

        this.updatePomodoroDisplay();

        setTimeout(async () => {
            await this.restorePomodoroFromServer();
            await this.refreshPomodoroTodayStats();
        }, 0);
    }

    async restorePomodoroFromServer() {
        const state = await this.loadPomodoroStateFromServer();
        if (!state) return;

        const step = Number.isFinite(Number(state.cycle_step))
            ? Number(state.cycle_step)
            : this.pomodoroStepFromPhase(state.phase);
        this.pomodoro.cycleStep = Math.min(4, Math.max(0, step));
        this.pomodoro.mode = this.pomodoroModeFromStep(this.pomodoro.cycleStep);

        if (Number.isFinite(Number(state.focus_duration))) this.pomodoro.focusDuration = Number(state.focus_duration);
        if (Number.isFinite(Number(state.short_break_duration)))
            this.pomodoro.shortBreakDuration = Number(state.short_break_duration);
        if (Number.isFinite(Number(state.long_break_duration)))
            this.pomodoro.longBreakDuration = Number(state.long_break_duration);

        this.pomodoro.autoStartBreaks = !!state.auto_start_breaks;
        const autoEl = document.getElementById("auto-start-breaks");
        if (autoEl) autoEl.checked = !!this.pomodoro.autoStartBreaks;

        const focusSlider = document.getElementById("focus-duration-slider");
        const shortSlider = document.getElementById("short-break-slider");
        const longSlider = document.getElementById("long-break-slider");
        const focusVal = document.getElementById("focus-duration-value");
        const shortVal = document.getElementById("short-break-value");
        const longVal = document.getElementById("long-break-value");
        if (focusSlider) focusSlider.value = String(Math.round(this.pomodoro.focusDuration / 60));
        if (shortSlider) shortSlider.value = String(Math.round(this.pomodoro.shortBreakDuration / 60));
        if (longSlider) longSlider.value = String(Math.round(this.pomodoro.longBreakDuration / 60));
        if (focusVal) focusVal.textContent = `${Math.round(this.pomodoro.focusDuration / 60)} min`;
        if (shortVal) shortVal.textContent = `${Math.round(this.pomodoro.shortBreakDuration / 60)} min`;
        if (longVal) longVal.textContent = `${Math.round(this.pomodoro.longBreakDuration / 60)} min`;

        let timeLeft = Number(state.time_left);
        if (!Number.isFinite(timeLeft)) timeLeft = this.getCurrentPomodoroStepDuration();

        const isRunning = !!state.is_running;
        const isPaused = !!state.is_paused;
        const updatedAt = Number(state.updated_at);

        if (isRunning && !isPaused && Number.isFinite(updatedAt)) {
            const nowSec = Math.floor(Date.now() / 1000);
            const elapsed = Math.max(0, nowSec - Math.floor(updatedAt));
            timeLeft = Math.max(0, timeLeft - elapsed);
        }

        this.pomodoro.timeLeft = Math.max(0, timeLeft);
        this.pomodoro.isRunning = isRunning && this.pomodoro.timeLeft > 0;
        this.pomodoro.isPaused = isPaused;

        const startBtn = document.getElementById("pomodoro-start");
        const pauseBtn = document.getElementById("pomodoro-pause");
        const activelyRunning = this.pomodoro.isRunning && !this.pomodoro.isPaused;

        if (activelyRunning) {
            startBtn?.classList.add("hidden");
            pauseBtn?.classList.remove("hidden");
        } else {
            startBtn?.classList.remove("hidden");
            pauseBtn?.classList.add("hidden");
        }

        if (startBtn) {
            if (this.pomodoro.isRunning && this.pomodoro.isPaused) {
                startBtn.innerHTML = `
            <span class="material-symbols-outlined text-2xl" style="font-variation-settings: 'FILL' 1">
                play_arrow
            </span>
            <span class="text-lg font-bold tracking-tight">Resume</span>
        `;
                // activate the monitoring if we are resuming a focus session
            } else {
                startBtn.innerHTML = `
            <span class="material-symbols-outlined text-2xl" style="font-variation-settings: 'FILL' 1">
                play_arrow
            </span>
            <span class="text-lg font-bold tracking-tight">Start Timer</span>
        `;
            }
        }

        this.updatePomodoroDisplay();

        if (this.pomodoro.isRunning && !this.pomodoro.isPaused && !this.pomodoro.interval) {
            this.pomodoro.interval = setInterval(() => {
                this.pomodoro.timeLeft--;
                if (this.pomodoro.timeLeft <= 0) {
                    this.pomodoroComplete();
                }
                const now = Date.now();
                if (!this.pomodoro.lastSavedAt || now - this.pomodoro.lastSavedAt >= 15000) {
                    this.savePomodoroStateToServer();
                }
                this.updatePomodoroDisplay();
            }, 1000);
        }
    }

    setPomodoroMode(mode) {
        if (this.pomodoro.isRunning) {
            if (!confirm("Timer is running. Switch mode and reset timer?")) return;
            this.resetPomodoro();
        }

        this.pomodoro.cycleStep = this.pomodoroStepFromModePreference(mode, this.pomodoro.cycleStep);
        this.pomodoro.mode = this.pomodoroModeFromStep(this.pomodoro.cycleStep);

        const focusBtn = document.getElementById("pomodoro-focus-mode");
        const shortBreakBtn = document.getElementById("pomodoro-short-break-mode");
        const longBreakBtn = document.getElementById("pomodoro-long-break-mode");

        [focusBtn, shortBreakBtn, longBreakBtn].forEach((btn) => {
            if (btn) {
                btn.classList.remove(
                    "bg-primary",
                    "dark:bg-primary-dark",
                    "text-white",
                    "shadow-lg",
                    "shadow-primary/20",
                    "dark:shadow-primary-dark/20",
                );
                btn.classList.add(
                    "text-gray-600",
                    "dark:text-gray-400",
                    "hover:text-gray-900",
                    "dark:hover:text-white",
                );
            }
        });

        const activeBtn = mode === "focus" ? focusBtn : mode === "short-break" ? shortBreakBtn : longBreakBtn;

        if (activeBtn) {
            activeBtn.classList.add(
                "bg-primary",
                "dark:bg-primary-dark",
                "text-white",
                "shadow-lg",
                "shadow-primary/20",
                "dark:shadow-primary-dark/20",
            );
            activeBtn.classList.remove(
                "text-gray-600",
                "dark:text-gray-400",
                "hover:text-gray-900",
                "dark:hover:text-white",
            );
        }

        this.pomodoro.timeLeft = this.getCurrentPomodoroStepDuration();

        this.updatePomodoroDisplay();
        this.savePomodoroStateToServer();
    }

    togglePomodoro() {
        if (this.pomodoro.isRunning && this.pomodoro.isPaused) {
            this.startPomodoro();
            return;
        }

        if (!this.pomodoro.isRunning) {
            this.startPomodoro();
            return;
        }

        this.pausePomodoro();
    }

    startPomodoro() {
        this.pomodoro.isRunning = true;
        this.pomodoro.isPaused = false;

        if (this.pomodoro.mode === "focus") {
            if (this.pomodoro.disabledMonitoring) {
                this.setMonitoringEnabled(true);
                this.pomodoro.disabledMonitoring = false;
            }
        } else {
            this.setMonitoringEnabled(false);
            this.pomodoro.disabledMonitoring = true;
        }

        document.getElementById("pomodoro-start")?.classList.add("hidden");
        document.getElementById("pomodoro-pause")?.classList.remove("hidden");

        if (this.pomodoro.mode === "focus") {
            this.setMonitoringEnabled(true);
            this.pomodoro.disabledMonitoring = false;
        }

        this.pomodoro.interval = setInterval(() => {
            this.pomodoro.timeLeft--;

            if (this.pomodoro.timeLeft <= 0) {
                this.pomodoroComplete();
            }

            this.updatePomodoroDisplay();

            const now = Date.now();
            if (!this.pomodoro.lastSavedAt || now - this.pomodoro.lastSavedAt >= 15000) {
                this.savePomodoroStateToServer();
            }
        }, 1000);

        this.savePomodoroStateToServer();
    }

    pausePomodoro() {
        this.pomodoro.isPaused = true;
        clearInterval(this.pomodoro.interval);
        this.pomodoro.interval = null;

        document.getElementById("pomodoro-start")?.classList.remove("hidden");
        document.getElementById("pomodoro-pause")?.classList.add("hidden");

        const startBtn = document.getElementById("pomodoro-start");
        if (startBtn) {
            startBtn.innerHTML = `
            <span class="material-symbols-outlined text-2xl" style="font-variation-settings: 'FILL' 1">
                play_arrow
            </span>
            <span class="text-lg font-bold tracking-tight">Resume</span>
        `;
        }

        this.savePomodoroStateToServer();
    }

    resetPomodoro() {
        clearInterval(this.pomodoro.interval);
        this.pomodoro.isRunning = false;
        this.pomodoro.isPaused = false;
        this.pomodoro.interval = null;

        this.pomodoro.timeLeft = this.getCurrentPomodoroStepDuration();

        document.getElementById("pomodoro-start")?.classList.remove("hidden");
        document.getElementById("pomodoro-pause")?.classList.add("hidden");

        const startBtn = document.getElementById("pomodoro-start");
        if (startBtn) {
            startBtn.innerHTML = `
            <span class="material-symbols-outlined text-2xl" style="font-variation-settings: 'FILL' 1">
                play_arrow
            </span>
            <span class="text-lg font-bold tracking-tight">Start Timer</span>
        `;
        }

        this.updatePomodoroDisplay();
        this.savePomodoroStateToServer();
    }

    pomodoroComplete() {
        clearInterval(this.pomodoro.interval);
        this.pomodoro.isRunning = false;
        this.pomodoro.isPaused = false;

        this.playTimerEndSound();
        this.showPomodoroNotification();

        const completedStep = Number(this.pomodoro.cycleStep) || 0;
        const completedWasFocus = completedStep === 0 || completedStep === 2;
        if (completedWasFocus) {
            const focusSeconds = this.pomodoro.focusDuration;
            this.incrementPomodoroFocusComplete(focusSeconds);
        }

        const nextStep = completedStep >= 4 ? 0 : completedStep + 1;
        this.pomodoro.cycleStep = nextStep;
        this.pomodoro.mode = this.pomodoroModeFromStep(nextStep);
        this.pomodoro.timeLeft = this.getCurrentPomodoroStepDuration();

        document.getElementById("pomodoro-start")?.classList.remove("hidden");
        document.getElementById("pomodoro-pause")?.classList.add("hidden");

        this.updatePomodoroDisplay();
        this.savePomodoroStateToServer();

        if (completedWasFocus) {
            this.setMonitoringEnabled(false);
            this.pomodoro.disabledMonitoring = true;
        }

        if (this.pomodoro.autoStartBreaks && this.pomodoro.mode !== "focus") {
            setTimeout(() => {
                this.startPomodoro();
            }, 1000);
        }
    }

    updatePomodoroDisplay() {
        const timerElement = document.getElementById("pomodoro-timer");
        const phaseElement = document.getElementById("pomodoro-phase");
        const progressElement = document.getElementById("pomodoro-progress");

        if (!timerElement || !phaseElement) return;

        const minutes = Math.floor(this.pomodoro.timeLeft / 60);
        const seconds = this.pomodoro.timeLeft % 60;
        timerElement.textContent = `${minutes.toString().padStart(2, "0")}:${seconds.toString().padStart(2, "0")}`;

        const step = Number(this.pomodoro.cycleStep) || 0;
        if (step === 0 || step === 2) {
            phaseElement.textContent = step === 0 ? "Focus 1/2" : "Focus 2/2";
            phaseElement.className =
                "text-gray-600 dark:text-gray-400 uppercase tracking-[0.4em] text-xs mt-4 font-semibold";
        } else if (step === 1 || step === 3) {
            phaseElement.textContent = step === 1 ? "Short Break 1/2" : "Short Break 2/2";
            phaseElement.className =
                "text-emerald-600 dark:text-emerald-400 uppercase tracking-[0.4em] text-xs mt-4 font-semibold";
        } else {
            phaseElement.textContent = "Long Break";
            phaseElement.className =
                "text-blue-600 dark:text-blue-400 uppercase tracking-[0.4em] text-xs mt-4 font-semibold";
        }

        if (progressElement) {
            const totalDuration = this.getCurrentPomodoroStepDuration();
            const progress = 1 - this.pomodoro.timeLeft / totalDuration;
            const circumference = 2 * Math.PI * 48;
            const offset = circumference * progress;
            progressElement.style.strokeDashoffset = offset;
        }
    }

    getCurrentModeDuration() {
        switch (this.pomodoro.mode) {
            case "focus":
                return this.pomodoro.focusDuration;
            case "short-break":
                return this.pomodoro.shortBreakDuration;
            case "long-break":
                return this.pomodoro.longBreakDuration;
            default:
                return 25 * 60;
        }
    }

    showPomodoroNotification() {
        const message =
            this.pomodoro.mode === "focus"
                ? "Focus session complete! Time for a break."
                : "Break time is over! Ready to focus?";

        let notification = document.getElementById("pomodoro-notification");
        if (!notification) {
            notification = document.createElement("div");
            notification.id = "pomodoro-notification";
            notification.className = "fixed bottom-4 right-4 p-4 rounded-lg shadow-xl z-50 hidden";
            document.body.appendChild(notification);
        }

        notification.innerHTML = `
        <div class="flex items-center gap-3">
            <span class="material-symbols-outlined text-2xl">timer</span>
            <div>
                <p class="font-semibold">${message}</p>
                <p class="text-sm opacity-75">Pomodoro Timer</p>
            </div>
            <button onclick="this.parentElement.parentElement.classList.add('hidden')" 
                    class="ml-4 text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200">
                <span class="material-symbols-outlined">close</span>
            </button>
        </div>
    `;

        notification.className = notification.className.replace(/bg-\S+ text-\S+/, "");
        if (document.documentElement.classList.contains("dark")) {
            notification.classList.add("bg-gray-800", "text-white", "border", "border-gray-700");
        } else {
            notification.classList.add("bg-white", "text-gray-900", "border", "border-gray-200");
        }

        notification.classList.remove("hidden");

        setTimeout(() => {
            notification.classList.add("hidden");
        }, 5000);
    }
}
