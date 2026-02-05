import { StateManager } from "./core/stateManager.js";
import { TaskView } from "./views/taskView.js";
import { HistoryManager } from "./modules/history.js";
import { AnytypeManager } from "./modules/anytype.js";
import { PomodoroManager } from "./modules/pomodoro.js";

class FocusApp {
    constructor() {
        // State variables (kept identical to legacy defaults)
        this.lastCategories = [];
        this.allowed = [];
        this.allowedTitles = [];
        this.lastTasks = [];
        this.lastAnytypeSpaces = [];
        this.anytypeChallengeId = "";
        this.currentTaskId = localStorage.getItem("currentTaskId") || null;
        this.lastFocusWarningKey = "";
        this.lastCurrentFocus = null;
        this.currentDurationValue = "25m";
        this.timerRunning = false;
        this.timerSeconds = 0;
        this.timerInterval = null;
        this.audioContext = null;
        this.currentView = "tasks";
        this.lastPageActive = document.visibilityState === "visible" && document.hasFocus();
        this.anytypeError = null;
        this.amIFocused = false;
        this.monitoringEnabled = true;
        this.categoryColorMap = {};

        this.pomodoro = {
            isRunning: false,
            isPaused: false,
            mode: "focus",
            cycleStep: 0,
            focusDuration: 25 * 60,
            shortBreakDuration: 5 * 60,
            longBreakDuration: 15 * 60,
            timeLeft: 25 * 60,
            interval: null,
            autoStartBreaks: true,
            lastSavedAt: 0,
            disabledMonitoring: false,
        };

        // Keep default history filter mode
        window.historyFilterMode = "month";

        // Compose feature modules (they attach/bind methods onto this app instance)
        this._state = new StateManager(this);
        this._taskView = new TaskView(this);
        this._history = new HistoryManager(this);
        this._anytype = new AnytypeManager(this);
        this._pomodoro = new PomodoroManager(this);

        // Legacy behavior: wire pomodoro listeners immediately
        this.initPomodoro();
    }
}

// Initialize the app when the window loads (matches legacy behavior)
window.onload = function () {
    const app = new FocusApp();
    app.init();
    window.focusApp = app;
};
